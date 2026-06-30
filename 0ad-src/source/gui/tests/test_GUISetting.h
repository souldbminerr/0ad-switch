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

#include "gui/CGUI.h"
#include "gui/CGUISetting.h"
#include "gui/ObjectBases/IGUIObject.h"
#include "gui/SettingTypes/CGUISize.h"
#include "i18n/L10n.h"
#include "lib/file/file_system.h"
#include "lib/file/vfs/vfs.h"
#include "lib/path.h"
#include "maths/Rect.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/ConfigDB.h"
#include "ps/Filesystem.h"
#include "ps/ProfileViewer.h"
#include "ps/VideoMode.h"
#include "ps/XML/Xeromyces.h"
#include "renderer/Renderer.h"
#include "scriptinterface/ScriptInterface.h"
#include "scriptinterface/ScriptRequest.h"

#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

class TestGUISetting : public CxxTest::TestSuite
{
	std::optional<CXeromycesEngine> m_XeromycesEngine;
	std::unique_ptr<CProfileViewer> m_Viewer;
	std::unique_ptr<CRenderer> m_Renderer;
	std::unique_ptr<L10n> m_L10n;

public:
	class TestGUIObject : public IGUIObject
	{
	public:
		TestGUIObject(CGUI& gui) : IGUIObject(gui) {}

		void Draw(CCanvas2D&) {}
		void UpdateCachedSize() {
			haveUpdateCachedSize = true;
			IGUIObject::UpdateCachedSize();
		}

		CGUISimpleSetting<CGUISize>* GetSizeSetting() const
		{
			return static_cast<CGUISimpleSetting<CGUISize>*>(m_Settings.at("size"));
		}

		bool haveUpdateCachedSize{false};
	};

	void setUp()
	{
		g_VFS = CreateVfs();
		TS_ASSERT_OK(g_VFS->Mount(L"", DataDir() / "mods" / "_test.gui" / "", VFS_MOUNT_MUST_EXIST));
		TS_ASSERT_OK(g_VFS->Mount(L"", DataDir() / "mods" / "_test.minimal" / "", VFS_MOUNT_MUST_EXIST));
		TS_ASSERT_OK(g_VFS->Mount(L"cache", DataDir() / "_testcache" / "", 0, VFS_MAX_PRIORITY));

		m_XeromycesEngine.emplace();

		// The renderer spews messages.
		TestLogger logger;

		// We need to initialise the renderer to initialise the font manager.
		// TODO: decouple this.
		CConfigDB::Initialise();
		CConfigDB::Instance()->SetValueString(CFG_SYSTEM, "rendererbackend", "dummy");
		g_VideoMode.InitNonSDL();
		g_VideoMode.CreateBackendDevice(false);
		m_Viewer = std::make_unique<CProfileViewer>();
		m_Renderer = std::make_unique<CRenderer>(g_VideoMode.GetBackendDevice());
		m_L10n = std::make_unique<L10n>();
	}

	void tearDown()
	{
		m_L10n.reset();
		m_Renderer.reset();
		m_Viewer.reset();
		g_VideoMode.Shutdown();
		CConfigDB::Shutdown();
		m_XeromycesEngine.reset();
		g_VFS.reset();
		DeleteDirectory(DataDir() / "_testcache");
	}

	void test_movability()
	{
		CGUI gui{*g_ScriptContext};
		TestGUIObject object(gui);

		static_assert(std::is_move_constructible_v<CGUISimpleSetting<CStr>>);
		static_assert(!std::is_move_assignable_v<CGUISimpleSetting<CStr>>);

		CGUISimpleSetting<CStr> settingA(&object, "A");
		TS_ASSERT(settingA->empty());
		TS_ASSERT(object.SettingExists("A"));
		object.SetSettingFromString("A", L"ValueA", false);
		TS_ASSERT_EQUALS(*settingA, "ValueA");

		CGUISimpleSetting<CStr> settingB(std::move(settingA));
		TS_ASSERT(object.SettingExists("A"));
		object.SetSettingFromString("A", L"ValueB", false);
		TS_ASSERT_EQUALS(*settingB, "ValueB");
	}

	void test_setting_cguisize()
	{
		CGUI gui{*g_ScriptContext};
		gui.AddObjectTypes();
		TestGUIObject object{gui};

		CGUISimpleSetting<CGUISize>* setting{object.GetSizeSetting()};
		object.SetSettingFromString("size", L"2 2 20 20", false);
		object.haveUpdateCachedSize = false;

		ScriptRequest rq{gui.GetScriptInterface()};
		JS::RootedValue val(rq.cx);
		val.setObject(*object.GetJSObject());
		JS::RootedObject global(rq.cx, rq.glob);
		JS_DefineProperty(rq.cx, global, "testObject", val, JSPROP_ENUMERATE);

		// Lazy assigment.
		TS_ASSERT(gui.GetScriptInterface()->LoadGlobalScriptFile(L"gui/settings/cguisize/lazyassign.js"));
		TS_ASSERT_EQUALS(setting->GetMutable().pixel, (CRect{5, 2, 20, 20}));
		TS_ASSERT_EQUALS(setting->GetMutable().percent, (CRect{0, 0, 0, 0}));
		TS_ASSERT_EQUALS(object.haveUpdateCachedSize, false);

		// Force update of cached size.
		object.GetComputedSize();
		object.haveUpdateCachedSize = false;

		// Compound assignment operator.
		TS_ASSERT(gui.GetScriptInterface()->LoadGlobalScriptFile(L"gui/settings/cguisize/compoundassignmentoperator.js"));
		TS_ASSERT_EQUALS(setting->GetMutable().pixel, (CRect{10, 2, 20, 20}));
		TS_ASSERT_EQUALS(setting->GetMutable().percent, (CRect{0, 0, 0, 0}));
		TS_ASSERT_EQUALS(object.haveUpdateCachedSize, false);

		// Force update of cached size.
		object.GetComputedSize();
		object.haveUpdateCachedSize = false;

		// Object assignment.
		TS_ASSERT(gui.GetScriptInterface()->LoadGlobalScriptFile(L"gui/settings/cguisize/objectassign.js"));
		TS_ASSERT_EQUALS(setting->GetMutable().pixel, (CRect{10, 2, 20, 20}));
		TS_ASSERT_EQUALS(setting->GetMutable().percent, (CRect{4, 0, 0, 20}));
		TS_ASSERT_EQUALS(object.haveUpdateCachedSize, false);

		// Force update of cached size.
		object.GetComputedSize();
		object.haveUpdateCachedSize = false;

		// assign
		TS_ASSERT(gui.GetScriptInterface()->LoadGlobalScriptFile(L"gui/settings/cguisize/assign.js"));
		TS_ASSERT_EQUALS(setting->GetMutable().pixel, (CRect{3, 0, 0, 2}));
		TS_ASSERT_EQUALS(setting->GetMutable().percent, (CRect{0, 0, 0, 0}));
		TS_ASSERT_EQUALS(object.haveUpdateCachedSize, true);
	}
};
