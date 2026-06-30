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

#include "dapinterface/DapInterface.h"
#include "lib/file/vfs/vfs.h"
#include "lib/path.h"
#include "ps/CLogger.h"
#include "ps/Filesystem.h"
#include "scriptinterface/ScriptInterface.h"

#include <fmt/format.h>
#include <memory>
#include <string>

class TestDapInterface : public CxxTest::TestSuite
{
public:
	void setUp()
	{
		g_VFS = CreateVfs();
	}

	void tearDown()
	{
		g_VFS.reset();
	}

	void test_dap_interface()
	{
		TestLogger logger;
		// Test invalid address
		TS_ASSERT_THROWS_EQUALS((DAP::Interface{"invalid_address", 1234, *g_ScriptContext}), const DAP::DapInterfaceException& e, e.what(), fmt::format("Failed to bind and listen on port 1234"));

		const std::string address{"127.0.0.1"};
		const int port{1234};
		// Test no tools/dap/ directory
		TS_ASSERT_THROWS_EQUALS((DAP::Interface{address, port, *g_ScriptContext}), const DAP::DapInterfaceNoJSDebuggerException& e, e.what(), "DAP entry script not found at tools/dap/entry.js");

		TS_ASSERT_OK(g_VFS->Mount(L"", DataDir() / "mods" / "mod" / "", VFS_MOUNT_MUST_EXIST))
		// Test localhost address
		TS_ASSERT_THROWS_NOTHING((DAP::Interface{address, port, *g_ScriptContext }));

		// Test two instances of DAP::Interface
		DAP::Interface dap1{address, port, *g_ScriptContext};
		TS_ASSERT_THROWS_EQUALS((DAP::Interface{address, port, *g_ScriptContext}), const DAP::DapInterfaceException& e, e.what(), fmt::format("Failed to bind and listen on port {}", port));

		// Test send a message without clients
		TS_ASSERT_THROWS_NOTHING(dap1.TryHandleMessage());
	}
};
