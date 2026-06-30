/* Copyright (C) 2025 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "lib/precompiled.h"

#include "DapInterface.h"

#include "lib/debug.h"
#include "lib/path.h"
#include "lib/posix/posix_types.h"
#include "lib/sysdep/os.h"
#include "ps/CLogger.h"
#include "ps/Filesystem.h"
#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/JSON.h"
#include "scriptinterface/ModuleLoader.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/ScriptContext.h"
#include "scriptinterface/ScriptExceptions.h"
#include "scriptinterface/ScriptInterface.h"
#include "scriptinterface/ScriptRequest.h"

#include <cstddef>
#include <fmt/format.h>
#include <js/Debug.h>
#include <jsapi.h>
#include <memory>
#include <thread>
#include <utility>

#if OS_WIN
#include <WinSock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

namespace DAP
{
	class Interface::SocketHandler {
	public:
		SocketHandler(Interface* callbackData)
			: m_CallbackData(callbackData)
		{
			ENSURE(m_CallbackData && "Callback data must not be null");
		}

		~SocketHandler()
		{
			m_Running = false;
			CloseSocket();
			if (m_ServerThread.joinable())
				m_ServerThread.join();
		}
		NONCOPYABLE(SocketHandler);

		bool BindAndListen(const std::string server_address, int port)
		{
#if OS_WIN
			WSADATA wsaData;
			if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			{
				LOGERROR("Failed to start WSA");
				return false;
			}
#endif

			m_ServerSocket = socket(AF_INET, SOCK_STREAM, 0);
#if OS_WIN
			if (m_ServerSocket == INVALID_SOCKET)
#else
			if (m_ServerSocket < 0)
#endif
			{
				LOGERROR("Failed to create socket");
				return false;
			}

#if OS_WIN
			const int i{1};
			setsockopt(m_ServerSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&i), sizeof(i));
#else
			const int i{1};
			setsockopt(m_ServerSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&i), sizeof(i));
#endif
			sockaddr_in serverAddr{};
			serverAddr.sin_family = AF_INET;
			serverAddr.sin_port = htons(port);

			if (server_address.empty())
				serverAddr.sin_addr.s_addr = INADDR_ANY;
			else
			{
				serverAddr.sin_addr.s_addr = inet_addr(server_address.c_str());
				if (!inet_pton(AF_INET, server_address.c_str(), &serverAddr.sin_addr))
				{
					LOGERROR("Invalid server address: %s", server_address.c_str());
					CloseSocket();
					return false;
				}
			}

			if (bind(m_ServerSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == -1)
			{
				LOGERROR("Failed to bind socket");
				CloseSocket();
				return false;
			}

			if (listen(m_ServerSocket, 1)  == -1)
			{
				LOGERROR("Failed to listen on socket");
				return false;
			}

			return true;
		}

		void StartServerThread()
		{
			m_ServerThread = std::thread(&Interface::SocketHandler::AcceptConnections, this);
		}

		void AcceptConnections()
		{
			while (m_Running)
			{
				sockaddr_in clientAddr{};

#if OS_WIN
				int clientAddrLen{sizeof(clientAddr)};
				m_ClientSocket = accept(m_ServerSocket, reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);
				if (m_ClientSocket == INVALID_SOCKET)
#else
				socklen_t clientAddrLen = sizeof(clientAddr);
				m_ClientSocket = accept(m_ServerSocket, reinterpret_cast<sockaddr*>(&clientAddr), reinterpret_cast<socklen_t*>(&clientAddrLen));
				if (m_ClientSocket < 0)
#endif
				{
					if (m_Running)
						LOGERROR("Failed to accept connection");
					else
						LOGMESSAGE("Server stopped accepting connections");
					continue;
				}

				LOGMESSAGE("Accepted connection from %s", inet_ntoa(clientAddr.sin_addr));
				std::lock_guard<std::mutex> lock{m_ConnectionLock};
				this->HandleClient();
				LOGMESSAGE("Client Disconnected");
			}
		}

		void HandleClient()
		{
			char buffer[1024] = {0};
			std::string message{};
			while (true)
			{
				// TODO: Validation if need more than one recv.
				const int bytesRead{static_cast<int>(recv(m_ClientSocket, buffer, sizeof(buffer), 0))};
				if (bytesRead <= 0)
					break;
				message.append(buffer, static_cast<unsigned int>(bytesRead));
				LOGMESSAGE("Received message: %s", message.c_str());

				while (!message.empty())
				{
					// Parse Content-Length get DAP Body.
					const std::size_t pos{message.find("Content-Length: ")};
					if (pos == std::string::npos)
					{
						LOGERROR("Invalid DAP message");
						break;
					}

					const std::size_t endPos{message.find("\r\n\r\n", pos)};
					if (endPos == std::string::npos)
					{
						LOGERROR("Invalid DAP message");
						break;
					}

					const std::size_t length{static_cast<size_t>(std::stoi(message.substr(pos + 16, endPos - pos - 16)))};

					// Should wait for more data?.
					if (endPos + 4 + length > message.size())
						break;

					const std::string dapMessage{message.substr(endPos + 4, length)};

					const std::string response{this->m_CallbackData->SendDapMessage(dapMessage)};

					if (response.empty())
					{
						LOGERROR("Failed to process message");
						break;
					}

					// Return DAP message with protocol headers.
					const std::string dapResponse{"Content-Length: " + std::to_string(response.size()) + "\r\n\r\n" + response};
					LOGMESSAGE("Sending response to client: %s", dapResponse.c_str());
					send(m_ClientSocket, dapResponse.c_str(), dapResponse.size(), 0);

					// Validate if there more messages.
					message = message.substr(endPos + 4 + length);
				}
			}

	#if OS_WIN
			closesocket(m_ClientSocket);
			m_ClientSocket = INVALID_SOCKET;
	#else
			close(m_ClientSocket);
			m_ClientSocket = -1;
	#endif
		}

		void SendToClient(const std::string& message)
		{
#if OS_WIN
			ENSURE(m_ClientSocket != INVALID_SOCKET && "Client socket is not connected");
#else
			ENSURE(m_ClientSocket != -1 && "Client socket is not connected");
#endif
			send(m_ClientSocket, message.c_str(), message.size(), 0);
		}

		void CloseSocket()
		{
#if OS_WIN
			if (m_ClientSocket != INVALID_SOCKET)
			{
				closesocket(m_ClientSocket);
				m_ClientSocket = INVALID_SOCKET;
			}
			if (m_ServerSocket != INVALID_SOCKET)
			{
				closesocket(m_ServerSocket);
				m_ServerSocket = INVALID_SOCKET;
			}
			WSACleanup();
#else
			if (m_ClientSocket != -1)
			{
				close(m_ClientSocket);
				m_ClientSocket = -1;
			}
			if (m_ServerSocket != -1)
			{
				shutdown(m_ServerSocket, SHUT_RDWR);
				close(m_ServerSocket);
				m_ServerSocket = -1;
			}
#endif
		}

	private:
#if OS_WIN
		SOCKET m_ServerSocket{INVALID_SOCKET};
		SOCKET m_ClientSocket{INVALID_SOCKET};
#else
		int m_ServerSocket{-1};
		int m_ClientSocket{-1};
#endif

		std::thread m_ServerThread;
		std::mutex m_ConnectionLock;
		Interface* m_CallbackData{nullptr};
		bool m_Running{true};
	};

	Interface::Interface(const std::string serverAddress, int port, ScriptContext& scriptContext)
		: m_SocketImpl{std::make_unique<SocketHandler>(this)},
		m_ModuleValue{scriptContext.GetGeneralJSContext()}
	{
		LOGMESSAGERENDER("Starting DAP interface server");

		if (!m_SocketImpl->BindAndListen(serverAddress, port))
			throw DapInterfaceException{fmt::format("Failed to bind and listen on port {}", port)};

		const VfsPath fntPath{L"tools/dap/entry.js"};
		if (!VfsFileExists(fntPath))
			throw DapInterfaceNoJSDebuggerException{ fmt::format("DAP entry script not found at {}", fntPath.string8().c_str())};

		m_ScriptInterface = std::make_unique<ScriptInterface>("Engine", "Debugger", scriptContext, [](const VfsPath& path) {
			return path.string8().find("tools/dap/") == 0;
		});
		m_ScriptInterface->SetCallbackData(this);

		ScriptRequest rq(m_ScriptInterface.get());
		if (!JS_DefineDebuggerObject(rq.cx, rq.glob))
		{
			ScriptException::CatchPending(rq);
			throw DapInterfaceNoJSDebuggerException{"Failed to define debugger object"};
		}

		// Register methods.
		constexpr ScriptFunction::ObjectGetter<DAP::Interface> Getter{&ScriptInterface::ObjectFromCBData<DAP::Interface>};
		ScriptFunction::Register<&DAP::Interface::WaitForMessage, Getter>(rq, "WaitForMessage");
		ScriptFunction::Register<&DAP::Interface::EndWaitingForMessage, Getter>(rq, "EndWaitingForMessage");

		auto result{m_ScriptInterface->GetModuleLoader().LoadModule(rq, fntPath)};

		scriptContext.RunJobs();

		auto& future{*result.begin()};
		JS::RootedObject ns{rq.cx, future.Get()};
		m_ModuleValue = {rq.cx, JS::ObjectValue(*ns)};

		// Validate that message handler function is defined in the script.
		if (!this->isJSHandlerDefined())
			throw DapInterfaceNoJSDebuggerException{"Message handler function not defined in the script"};

		// Now its time to start the server thread.
		m_SocketImpl->StartServerThread();
	}

	Interface::~Interface() = default;

	bool Interface::isJSHandlerDefined()
	{
		ScriptRequest rq{m_ScriptInterface.get()};
		JS::RootedValue handler{rq.cx};

		if (!Script::GetProperty(rq, m_ModuleValue, "handleMessage", &handler))
		{
			ScriptException::CatchPending(rq);
			return false;
		}

		if (!handler.isObject())
			return false;

		JS::RootedObject obj{rq.cx, &handler.toObject()};
		return JS_ObjectIsFunction(obj);
	}

	std::string Interface::SendDapMessage(const std::string& message)
	{
		std::unique_lock<std::mutex> msgLock{m_MsgLock};
		ENSURE(m_DapRequest.empty());
		m_DapRequest = std::move(message);
		m_MsgApplied.notify_all();

		m_MsgApplied.wait(msgLock, [this] { return m_DapRequest.empty(); });
		return m_DapResponse;
	}

	std::string Interface::OnMessage(const std::string& message)
	{
		ScriptRequest rq{m_ScriptInterface.get()};

		JS::RootedValue msg{rq.cx};
		if (!Script::ParseJSON(rq, message, &msg))
		{
			LOGERROR("Failed to parse JSON message");
			return "";
		}

		JS::RootedValue rval{rq.cx};
		if (!ScriptFunction::Call(rq, m_ModuleValue, "handleMessage", &rval, msg))
		{
			LOGERROR("Failed to call message handler");
			return "";
		}

		return Script::StringifyJSON(rq, &rval, false);
	}

	void Interface::SendEventToClient()
	{
		ScriptRequest rq{m_ScriptInterface.get()};
		JS::RootedValue global{rq.cx, rq.globalValue()};
		JS::RootedValue rval{rq.cx};

		while (true)
		{
			if (!ScriptFunction::Call(rq, m_ModuleValue, "sendEventToClient", &rval))
			{
				LOGERROR("Failed to call sendEventToClient");
				return;
			}

			// Nothing to send.
			if (rval.isUndefined() || rval.isNull())
				return;

			const std::string ret{Script::StringifyJSON(rq, &rval, false)};

			// Send to socket client.
			const std::string dapResponse{"Content-Length: " + std::to_string(ret.size()) + "\r\n\r\n" + ret};
			LOGMESSAGE("Sending event to client: %s", ret.c_str());
			m_SocketImpl->SendToClient(dapResponse);
		}
	}

	void Interface::TryHandleMessage()
	{
		std::lock_guard<std::mutex> waitingLock{m_WaitingLock};
		if (!m_DapRequest.empty())
		{
			m_DapResponse = OnMessage(m_DapRequest);
			m_DapRequest.clear();
			m_MsgApplied.notify_all();
		}

		// Handle Events from the script.
		SendEventToClient();
	}

	void Interface::WaitForMessage()
	{
		std::unique_lock<std::mutex> waitingLock{m_MsgLock};
		m_IsWaiting = true;
		// Handle Events from the script.
		do
		{
			SendEventToClient();

			m_MsgApplied.wait(waitingLock, [this] { return !m_DapRequest.empty(); });

			m_DapResponse = OnMessage(m_DapRequest);
			m_DapRequest.clear();
			m_MsgApplied.notify_all();
		} while (m_IsWaiting);
	}

	void Interface::EndWaitingForMessage()
	{
		if (!m_IsWaiting)
			return;
		std::lock_guard<std::mutex> waitingLock{m_WaitingLock};
		m_IsWaiting = false;
	}
}
