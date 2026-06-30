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

#include "graphics/MapGenerator.h"
#include "lib/debug.h"
#include "lib/file/vfs/vfs.h"
#include "lib/file/vfs/vfs_path.h"
#include "lib/file/vfs/vfs_util.h"
#include "lib/path.h"
#include "ps/CLogger.h"
#include "ps/Filesystem.h"
#include "ps/Future.h"
#include "ps/XML/Xeromyces.h"
#include "scriptinterface/ScriptInterface.h"
#include "scriptinterface/StructuredClone.h"

#include <atomic>
#include <functional>
#include <js/PropertyDescriptor.h>
#include <string>
#include <vector>

class TestMapGenerator : public CxxTest::TestSuite
{
public:
	void setUp()
	{
		g_VFS = CreateVfs();
		g_VFS->Mount(L"", DataDir() / "mods" / "mod" / "", VFS_MOUNT_MUST_EXIST);
		g_VFS->Mount(L"", DataDir() / "mods" / "public" / "", VFS_MOUNT_MUST_EXIST, 1); // ignore directory-not-found errors
	}

	void tearDown()
	{
		g_VFS.reset();
	}

	void test_mapgen_scripts()
	{
		CXeromycesEngine xeromycesEngine;
		if (!VfsDirectoryExists(L"maps/random/tests/"))
		{
			debug_printf("Skipping map generator tests (can't find binaries/data/mods/public/maps/random/tests/)\n");
			return;
		}

		VfsPaths paths;
		TS_ASSERT_OK(vfs::GetPathnames(g_VFS, L"maps/random/tests/", L"test_*.js", paths));

		for (const VfsPath& path : paths)
		{
			TestLogger logger;
			ScriptInterface scriptInterface{"Engine", "MapGenerator", g_ScriptContext,
				[](const VfsPath& path){
					return path.string().find(RANDOM_MAP_PREFIX) == 0;
				}};
			ScriptTestSetup(scriptInterface);

			std::atomic<int> progress{1};
			std::atomic<bool> stopRequest{false};
			const Script::StructuredClone result{RunMapGenerationScript(StopToken{stopRequest},
				progress, scriptInterface, path, "{\"Seed\": 0}",
				JSPROP_ENUMERATE | JSPROP_PERMANENT)};

			TS_ASSERT_DIFFERS(result, nullptr);

			if (path == "maps/random/tests/test_Generator.js")
				TS_ASSERT_EQUALS(progress.load(), 50);
		}
	}
};
