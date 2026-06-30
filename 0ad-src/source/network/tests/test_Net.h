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

#include "lib/self_test.h"

#include "lib/debug.h"
#include "lib/external_libraries/enet.h"
#include "lib/file/file_system.h"
#include "lib/file/vfs/vfs.h"
#include "lib/path.h"
#include "lib/posix/posix_types.h"
#include "network/NetClient.h"
#include "network/NetMessage.h"
#include "network/NetServer.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/Filesystem.h"
#include "ps/Game.h"
#include "ps/Loader.h"
#include "ps/XML/Xeromyces.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/ScriptInterface.h"
#include "scriptinterface/ScriptRequest.h"
#include "simulation2/system/TurnManager.h"

#include <SDL_timer.h>
#include <cstddef>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <optional>
#include <string>
#include <vector>

class TestNetComms : public CxxTest::TestSuite
{
	std::optional<CXeromycesEngine> xeromycesEngine;

public:
	void setUp()
	{
		g_VFS = CreateVfs();
		TS_ASSERT_OK(g_VFS->Mount(L"", DataDir() / "mods" / "public" / "", VFS_MOUNT_MUST_EXIST));
		TS_ASSERT_OK(g_VFS->Mount(L"cache", DataDir() / "_testcache" / "", 0, VFS_MAX_PRIORITY));
		xeromycesEngine.emplace();

		enet_initialize();
	}

	void tearDown()
	{
		enet_deinitialize();

		xeromycesEngine.reset();
		g_VFS.reset();
		DeleteDirectory(DataDir()/"_testcache");
	}

	bool clients_are_all(const std::vector<CNetClient*>& clients, uint state)
	{
		for (size_t j = 0; j < clients.size(); ++j)
			if (clients[j]->GetCurrState() != state)
				return false;
		return true;
	}

	void connect(CNetServer& server, const std::vector<CNetClient*>& clients)
	{
		TS_ASSERT(server.SetupConnection(PS_DEFAULT_PORT));
		for (CNetClient* client: clients)
		{
			client->SetupServerData("127.0.0.1", PS_DEFAULT_PORT);
			TS_ASSERT(client->SetupConnection(nullptr));
		}

		for (size_t i = 0; ; ++i)
		{
//			debug_printf(".");
			for (size_t j = 0; j < clients.size(); ++j)
				clients[j]->Poll();

			if (clients_are_all(clients, NCS_PREGAME))
				break;

			if (i > 20)
			{
				TS_FAIL("connection timeout");
				break;
			}

			SDL_Delay(100);
		}
	}

#if 0
	void disconnect(CNetServer& server, const std::vector<CNetClient*>& clients)
	{
		for (size_t i = 0; ; ++i)
		{
//			debug_printf(".");
			server.Poll();
			for (size_t j = 0; j < clients.size(); ++j)
				clients[j]->Poll();

			if (server.GetState() == SERVER_STATE_UNCONNECTED && clients_are_all(clients, NCS_UNCONNECTED))
				break;

			if (i > 20)
			{
				TS_FAIL("disconnection timeout");
				break;
			}

			SDL_Delay(100);
		}
	}
#endif

	void wait(const std::vector<CNetClient*>& clients, size_t msecs)
	{
		for (size_t i = 0; i < msecs/10; ++i)
		{
			for (size_t j = 0; j < clients.size(); ++j)
				clients[j]->Poll();

			SDL_Delay(10);
		}
	}

	// just so that cxxtestgen doesn't complain "No tests defined" if all are disabled
	void test_dummy() {}

	void DISABLED_test_basic()
	{
		// This doesn't actually test much, it just runs a very quick multiplayer game
		// and prints a load of debug output so you can see if anything funny's going on

		ScriptInterface scriptInterface("Engine", "Test", g_ScriptContext);
		ScriptRequest rq(scriptInterface);

		TestStdoutLogger logger;

		std::vector<CNetClient*> clients;

		CGame client1Game(false);
		CGame client2Game(false);
		CGame client3Game(false);

		CNetServer server{false};

		JS::RootedValue attrs(rq.cx);
		Script::CreateObject(
			rq,
			&attrs,
			"mapType", "scenario",
			"map", "maps/scenarios/Saharan Oases",
			"mapPath", "maps/scenarios/",
			"thing", "example");

		server.UpdateInitAttributes(&attrs, scriptInterface);

		CNetClient client1(&client1Game);
		CNetClient client2(&client2Game);
		CNetClient client3(&client3Game);

		clients.push_back(&client1);
		clients.push_back(&client2);
		clients.push_back(&client3);

		connect(server, clients);
		debug_printf("%s", client1.TestReadGuiMessages().c_str());

		server.StartGame();
		SDL_Delay(100);
		for (size_t j = 0; j < clients.size(); ++j)
		{
			clients[j]->Poll();
			TS_ASSERT_OK(LDR_NonprogressiveLoad());
			clients[j]->LoadFinished();
		}

		wait(clients, 100);

		{
			JS::RootedValue cmd(rq.cx);
			Script::CreateObject(
				rq,
				&cmd,
				"type", "debug-print",
				"message", "[>>> client1 test sim command]\\n");
			client1Game.GetTurnManager()->PostCommand(cmd);
		}

		{
			JS::RootedValue cmd(rq.cx);
			Script::CreateObject(
				rq,
				&cmd,
				"type", "debug-print",
				"message", "[>>> client2 test sim command]\\n");
			client2Game.GetTurnManager()->PostCommand(cmd);
		}

		wait(clients, 100);
		client1Game.GetTurnManager()->Update(1.0f, 1);
		client2Game.GetTurnManager()->Update(1.0f, 1);
		client3Game.GetTurnManager()->Update(1.0f, 1);
		wait(clients, 100);
		client1Game.GetTurnManager()->Update(1.0f, 1);
		client2Game.GetTurnManager()->Update(1.0f, 1);
		client3Game.GetTurnManager()->Update(1.0f, 1);
		wait(clients, 100);
	}

	void DISABLED_test_rejoin()
	{
		ScriptInterface scriptInterface("Engine", "Test", g_ScriptContext);
		ScriptRequest rq(scriptInterface);

		TestStdoutLogger logger;

		std::vector<CNetClient*> clients;

		CGame client1Game(false);
		CGame client2Game(false);
		CGame client3Game(false);

		CNetServer server{false};

		JS::RootedValue attrs(rq.cx);
		Script::CreateObject(
			rq,
			&attrs,
			"mapType", "scenario",
			"map", "maps/scenarios/Saharan Oases",
			"mapPath", "maps/scenarios/",
			"thing", "example");

		server.UpdateInitAttributes(&attrs, scriptInterface);

		CNetClient client1(&client1Game);
		CNetClient client2(&client2Game);
		CNetClient client3(&client3Game);

		client1.SetUserName(L"alice");
		client2.SetUserName(L"bob");
		client3.SetUserName(L"charlie");

		clients.push_back(&client1);
		clients.push_back(&client2);
		clients.push_back(&client3);

		connect(server, clients);
		debug_printf("%s", client1.TestReadGuiMessages().c_str());

		server.StartGame();
		SDL_Delay(100);
		for (size_t j = 0; j < clients.size(); ++j)
		{
			clients[j]->Poll();
			TS_ASSERT_OK(LDR_NonprogressiveLoad());
			clients[j]->LoadFinished();
		}

		wait(clients, 100);

		{
			JS::RootedValue cmd(rq.cx);
			Script::CreateObject(
				rq,
				&cmd,
				"type", "debug-print",
				"message", "[>>> client1 test sim command 1]\\n");

			client1Game.GetTurnManager()->PostCommand(cmd);
		}

		wait(clients, 100);
		client1Game.GetTurnManager()->Update(1.0f, 1);
		client2Game.GetTurnManager()->Update(1.0f, 1);
		client3Game.GetTurnManager()->Update(1.0f, 1);
		wait(clients, 100);

		{
			JS::RootedValue cmd(rq.cx);
			Script::CreateObject(
				rq,
				&cmd,
				"type", "debug-print",
				"message", "[>>> client1 test sim command 2]\\n");
			client1Game.GetTurnManager()->PostCommand(cmd);
		}

		debug_printf("==== Disconnecting client 2\n");

		client2.DestroyConnection();
		clients.erase(clients.begin()+1);

		debug_printf("==== Connecting client 2B\n");

		CGame client2BGame(false);
		CNetClient client2B(&client2BGame);
		client2B.SetUserName(L"bob");
		clients.push_back(&client2B);

		client2B.SetupServerData("127.0.0.1", PS_DEFAULT_PORT);
		TS_ASSERT(client2B.SetupConnection(nullptr));

		for (size_t i = 0; ; ++i)
		{
			debug_printf("[%u]\n", client2B.GetCurrState());
			client2B.Poll();
			if (client2B.GetCurrState() == NCS_PREGAME)
				break;

			if (client2B.GetCurrState() == NCS_UNCONNECTED)
			{
				TS_FAIL("connection rejected");
				return;
			}

			if (i > 20)
			{
				TS_FAIL("connection timeout");
				return;
			}

			SDL_Delay(100);
		}

		wait(clients, 100);

		client1Game.GetTurnManager()->Update(1.0f, 1);
		client3Game.GetTurnManager()->Update(1.0f, 1);
		wait(clients, 100);
		server.SetTurnLength(100);
		client1Game.GetTurnManager()->Update(1.0f, 1);
		client3Game.GetTurnManager()->Update(1.0f, 1);
		wait(clients, 100);

		// (This SetTurnLength thing doesn't actually detect errors unless you change
		// CTurnManager::TurnNeedsFullHash to always return true)

		{
			JS::RootedValue cmd(rq.cx);
			Script::CreateObject(
				rq,
				&cmd,
				"type", "debug-print",
				"message", "[>>> client1 test sim command 3]\\n");
			client1Game.GetTurnManager()->PostCommand(cmd);
		}


		clients[2]->Poll();
		TS_ASSERT_OK(LDR_NonprogressiveLoad());
		clients[2]->LoadFinished();

		wait(clients, 100);

		{
			JS::RootedValue cmd(rq.cx);
			Script::CreateObject(
				rq,
				&cmd,
				"type", "debug-print",
				"message", "[>>> client1 test sim command 4]\\n");

			client1Game.GetTurnManager()->PostCommand(cmd);
		}

		for (size_t i = 0; i < 3; ++i)
		{
			client1Game.GetTurnManager()->Update(1.0f, 1);
			client2BGame.GetTurnManager()->Update(1.0f, 1);
			client3Game.GetTurnManager()->Update(1.0f, 1);
			wait(clients, 100);
		}
	}
};
