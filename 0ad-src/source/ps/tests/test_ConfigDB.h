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

#include "lib/file/file_system.h"
#include "lib/file/vfs/vfs.h"
#include "lib/path.h"
#include "ps/CStr.h"
#include "ps/ConfigDB.h"

#include <memory>
#include <string>
#include <vector>

extern PIVFS g_VFS;

class TestConfigDB : public CxxTest::TestSuite
{
	std::unique_ptr<CConfigDB> configDB;
public:

	void setUp()
	{
		g_VFS = CreateVfs();

		TS_ASSERT_OK(g_VFS->Mount(L"", DataDir() / "mods" / "_test.mods" / "", VFS_MOUNT_MUST_EXIST));
		TS_ASSERT_OK(g_VFS->Mount(L"config", DataDir() / "_testconfig" / "", 0, VFS_MAX_PRIORITY));
		configDB = std::make_unique<CConfigDB>();
	}

	void tearDown()
	{
		DeleteDirectory(DataDir()/"_testconfig");
		g_VFS.reset();
		configDB.reset();
	}

	void test_setting_int()
	{
		configDB->SetConfigFile(CFG_SYSTEM, "config/file.cfg");
		configDB->WriteFile(CFG_SYSTEM);
		configDB->Reload(CFG_SYSTEM);
		configDB->SetValueString(CFG_SYSTEM, "test_setting", "5");
		configDB->WriteFile(CFG_SYSTEM);
		configDB->Reload(CFG_SYSTEM);
		{
			std::string res;
			configDB->GetValue(CFG_SYSTEM, "test_setting", res);
			TS_ASSERT_EQUALS(res, "5");
		}
		{
			int res;
			configDB->GetValue(CFG_SYSTEM, "test_setting", res);
			TS_ASSERT_EQUALS(res, 5);
		}
	}

	void CheckFloat(const std::string& value, const float expectedValue)
	{
		configDB->SetValueString(CFG_SYSTEM, "test_setting", value);
		configDB->WriteFile(CFG_SYSTEM);
		configDB->Reload(CFG_SYSTEM);
		{
			std::string res;
			configDB->GetValue(CFG_SYSTEM, "test_setting", res);
			TS_ASSERT_EQUALS(res, value);
		}
		{
			float res;
			configDB->GetValue(CFG_SYSTEM, "test_setting", res);
			TS_ASSERT_EQUALS(res, expectedValue);
		}
	}

	void test_setting_float()
	{
		configDB->SetConfigFile(CFG_SYSTEM, "config/file.cfg");
		configDB->WriteFile(CFG_SYSTEM);
		configDB->Reload(CFG_SYSTEM);

		const char* oldLocale{setlocale(LC_NUMERIC, "")};
		for (const char* locale : {oldLocale, "fr_FR.UTF-8", "de_DE.UTF-8", "ja_JP.UTF-8"})
		{
			setlocale(LC_NUMERIC, locale);

			CheckFloat("1", 1.0f);
			CheckFloat("1.0", 1.0f);
			CheckFloat("1e-3", 1e-3f);
			CheckFloat("-1e-3", -1e-3f);
			CheckFloat("1234.567", 1234.567f);
			CheckFloat("-1234.567", -1234.567f);
			CheckFloat("1.0suffix", 1.0f);
		}

		setlocale(LC_NUMERIC, oldLocale);
	}

	void test_setting_empty()
	{
		configDB->SetConfigFile(CFG_SYSTEM, "config/file.cfg");
		configDB->WriteFile(CFG_SYSTEM);
		configDB->Reload(CFG_SYSTEM);
		configDB->SetValueList(CFG_SYSTEM, "test_setting", {});
		configDB->WriteFile(CFG_SYSTEM);
		configDB->Reload(CFG_SYSTEM);
		{
			std::string res = "toto";
			configDB->GetValue(CFG_SYSTEM, "test_setting", res);
			// Empty config values don't overwrite
			TS_ASSERT_EQUALS(res, "toto");
		}
		{
			int res = 3;
			configDB->GetValue(CFG_SYSTEM, "test_setting", res);
			// Empty config values don't overwrite
			TS_ASSERT_EQUALS(res, 3);
		}
	}

	void test_setting_mods()
	{
		configDB->SetConfigFile(CFG_DEFAULT, "config/start.cfg");
		configDB->Reload(CFG_DEFAULT);
		TS_ASSERT_EQUALS(configDB->Get("a", 0, CFG_MOD), 1);
		TS_ASSERT_EQUALS(configDB->Get("b", 0, CFG_MOD), 2);
		TS_ASSERT_EQUALS(configDB->Get("c", 0, CFG_MOD), 3);
		TS_ASSERT_EQUALS(configDB->Get("d", 0, CFG_MOD), 4);
		TS_ASSERT_EQUALS(configDB->Get("scoped.e", 0, CFG_MOD), 5);
		TS_ASSERT_EQUALS(configDB->Get("scoped.f", 0, CFG_MOD), 6);

		configDB->SetConfigFile(CFG_MOD, "config/moda.cfg");
		configDB->Reload(CFG_MOD);
		TS_ASSERT_EQUALS(configDB->Get("a", 0, CFG_MOD), 10);
		TS_ASSERT_EQUALS(configDB->Get("b", 0, CFG_MOD), 2);
		TS_ASSERT_EQUALS(configDB->Get("c", 0, CFG_MOD), 3);
		TS_ASSERT_EQUALS(configDB->Get("d", 0, CFG_MOD), 4);
		TS_ASSERT_EQUALS(configDB->Get("scoped.e", 0, CFG_MOD), 5);
		TS_ASSERT_EQUALS(configDB->Get("scoped.f", 0, CFG_MOD), 6);
		TS_ASSERT_EQUALS(configDB->Get("atext", std::string{}, CFG_MOD), "dummy");

		configDB->SetConfigFile(CFG_MOD, "config/modb.cfg");
		configDB->Reload(CFG_MOD);
		TS_ASSERT_EQUALS(configDB->Get("a", 0, CFG_MOD), 10);
		TS_ASSERT_EQUALS(configDB->Get("b", 0, CFG_MOD), 10);
		TS_ASSERT_EQUALS(configDB->Get("c", 0, CFG_MOD), 3);
		TS_ASSERT_EQUALS(configDB->Get("d", 0, CFG_MOD), 4);
		TS_ASSERT_EQUALS(configDB->Get("scoped.e", 0, CFG_MOD), 5);
		TS_ASSERT_EQUALS(configDB->Get("scoped.f", 0, CFG_MOD), 6);
		TS_ASSERT_EQUALS(configDB->Get("atext", std::string{}, CFG_MOD), "dummy");

		configDB->SetConfigFile(CFG_MOD, "config/modscoped.cfg");
		configDB->Reload(CFG_MOD);
		TS_ASSERT_EQUALS(configDB->Get("a", 0, CFG_MOD), 10);
		TS_ASSERT_EQUALS(configDB->Get("b", 0, CFG_MOD), 10);
		TS_ASSERT_EQUALS(configDB->Get("c", 0, CFG_MOD), 3);
		TS_ASSERT_EQUALS(configDB->Get("d", 0, CFG_MOD), 4);
		TS_ASSERT_EQUALS(configDB->Get("scoped.e", 0, CFG_MOD), 10);
		TS_ASSERT_EQUALS(configDB->Get("scoped.f", 0, CFG_MOD), 6);
		TS_ASSERT_EQUALS(configDB->Get("atext", std::string{}, CFG_MOD), "dummy");

		configDB->SetConfigFile(CFG_MOD, "config/modreplace.cfg");
		configDB->Reload(CFG_MOD);
		TS_ASSERT_EQUALS(configDB->Get("a", 0, CFG_MOD), 8);
		TS_ASSERT_EQUALS(configDB->Get("b", 0, CFG_MOD), 8);
		TS_ASSERT_EQUALS(configDB->Get("c", 0, CFG_MOD), 8);
		TS_ASSERT_EQUALS(configDB->Get("d", 0, CFG_MOD), 4);
		TS_ASSERT_EQUALS(configDB->Get("scoped.e", 0, CFG_MOD), 8);
		TS_ASSERT_EQUALS(configDB->Get("scoped.f", 0, CFG_MOD), 6);
		TS_ASSERT_EQUALS(configDB->Get("atext", std::string{}, CFG_MOD), "dummyreplaced");
	}
};
