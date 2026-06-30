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

#ifndef INCLUDED_DAPINTERFACE
#define INCLUDED_DAPINTERFACE

#include "lib/code_annotation.h"

#include <condition_variable>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

class ScriptContext;
class ScriptInterface;

namespace DAP
{
	// DapInterfaceException
	class DapInterfaceException : public std::runtime_error
	{
		public:
		explicit DapInterfaceException(const std::string& message)
			: std::runtime_error(message) {}
	};

	class DapInterfaceNoJSDebuggerException : public DapInterfaceException
	{
		public:
		explicit DapInterfaceNoJSDebuggerException(const std::string& message)
			: DapInterfaceException(message) {}
	};

	class Interface
	{
	public:
		Interface(const std::string server_address, int port, ScriptContext& scriptContext);
		~Interface();
		NONCOPYABLE(Interface);

		void TryHandleMessage();

		void WaitForMessage();
		void EndWaitingForMessage();

	private:
		class SocketHandler;

		bool isJSHandlerDefined();
		std::string SendDapMessage(const std::string& message);
		std::string OnMessage(const std::string& message);
		void SendEventToClient();

		std::unique_ptr<SocketHandler> m_SocketImpl;
		std::unique_ptr<ScriptInterface> m_ScriptInterface;

		std::string m_DapRequest;
		std::string m_DapResponse;

		std::mutex m_MsgLock;
		std::mutex m_WaitingLock;
		std::condition_variable m_MsgApplied;

		bool m_IsWaiting{false};
		JS::RootedValue m_ModuleValue;
	};
}

#endif // !INCLUDED_DAPINTERFACE
