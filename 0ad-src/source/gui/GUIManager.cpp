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

#include "precompiled.h"

#include "GUIManager.h"

#include "graphics/Canvas2D.h"
#include "gui/CGUI.h"
#include "gui/SGUIMessage.h"
#include "lib/debug.h"
#include "ps/ConfigDB.h"
#include "ps/GameSetup/Config.h"
#include "lib/file/vfs/vfs_path.h"
#include "lib/file/vfs/vfs_util.h"
#include "lib/utf8.h"
#include "ps/CLogger.h"
#include "ps/Errors.h"
#include "ps/Filesystem.h"
#include "ps/Profile.h"
#include "ps/Profiler2.h"
#include "ps/VideoMode.h"
#include "ps/XMB/XMBData.h"
#include "ps/XMB/XMBStorage.h"
#include "ps/XML/Xeromyces.h"
#include "ps/containers/StaticVector.h"
#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/ScriptContext.h"
#include "scriptinterface/ScriptConversions.h"
#include "scriptinterface/ScriptInterface.h"
#include "scriptinterface/ScriptRequest.h"
#include "scriptinterface/StructuredClone.h"
#include "simulation2/system/Component.h"

#include <algorithm>
#include <iterator>
#include <js/Equality.h>
#include <js/GCVector.h>
#include <js/Promise.h>
#include <js/RootingAPI.h>
#include <js/String.h>
#include <js/Symbol.h>
#include <js/Value.h>
#include <js/ValueArray.h>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace
{

const CStr EVENT_NAME_GAME_LOAD_PROGRESS = "GameLoadProgress";
const CStr EVENT_NAME_WINDOW_RESIZED = "WindowResized";
constexpr const char* START_ATLAS{"startAtlas"};

} // anonymous namespace

CGUIManager* g_GUI = nullptr;

// General TODOs:
//
// A lot of the CGUI data could (and should) be shared between
// multiple pages, instead of treating them as completely independent, to save
// memory and loading time.


// called from main loop when (input) events are received.
// event is passed to other handlers if false is returned.
// trampoline: we don't want to make the HandleEvent implementation static
InReaction gui_handler(const SDL_Event_* ev)
{
	if (!g_GUI)
		return IN_PASS;

	PROFILE("GUI event handler");
	return g_GUI->HandleEvent(ev);
}

static Status ReloadChangedFileCB(void* param, const VfsPath& path)
{
	return static_cast<CGUIManager*>(param)->ReloadChangedFile(path);
}

CGUIManager::CGUIManager(ScriptContext& scriptContext, ScriptInterface& scriptInterface) :
	m_ScriptContext{scriptContext},
	m_ScriptInterface{scriptInterface}
{
	m_ScriptInterface.SetCallbackData(this);
	m_ScriptInterface.LoadGlobalScripts();

	if (!g_Xeromyces.AddValidator(g_VFS, "gui_page", "gui/gui_page.rng"))
		LOGERROR("CGUIManager: failed to load GUI page grammar file 'gui/gui_page.rng'");
	if (!g_Xeromyces.AddValidator(g_VFS, "gui", "gui/gui.rng"))
		LOGERROR("CGUIManager: failed to load GUI XML grammar file 'gui/gui.rng'");

	RegisterFileReloadFunc(ReloadChangedFileCB, this);
}

CGUIManager::~CGUIManager()
{
	UnregisterFileReloadFunc(ReloadChangedFileCB, this);
}

size_t CGUIManager::GetPageCount() const
{
	return m_PageStack.size();
}

void CGUIManager::SwitchPage(const CStrW& pageName, const ScriptInterface* srcScriptInterface, JS::HandleValue initData)
{
	// The page stack is cleared (including the script context where initData came from),
	// therefore we have to clone initData.

	Script::StructuredClone initDataClone;
	if (!initData.isUndefined())
	{
		ScriptRequest rq(srcScriptInterface);
		initDataClone = Script::WriteStructuredClone(rq, initData);
	}

	if (!m_PageStack.empty())
	{
		// Make sure we unfocus anything on the current page.
		m_PageStack.back().gui->SendFocusMessage(GUIM_LOST_FOCUS);
		m_PageStack.clear();
	}

	OpenChildPage(pageName, initDataClone);
}

JS::Value CGUIManager::OpenChildPage(const CStrW& pageName, Script::StructuredClone initData)
{
	// Store the callback handler in the current GUI page before opening the new one
	JS::RootedValue promise{m_ScriptInterface.GetGeneralJSContext(), [&]
		{
			if (m_PageStack.empty())
				return JS::UndefinedValue();

			CGUI& currentPage = *m_PageStack.back().gui;
			// Make sure we unfocus anything on the current page.
			currentPage.SendFocusMessage(GUIM_LOST_FOCUS);
			return m_PageStack.back().ReplacePromise(*currentPage.GetScriptInterface());
		}()};

	// Emplace the page prior to loading its contents, because that may open
	// another GUI page on init which should be emplaced on top of this new page.
	m_PageStack.emplace_back(pageName, initData);
	m_PageStack.back().LoadPage(m_ScriptContext);

	return promise;
}

CGUIManager::SGUIPage::SGUIPage(const CStrW& pageName, const Script::StructuredClone initData)
	: m_Name(pageName), initData(initData)
{
}

void CGUIManager::SGUIPage::LoadPage(ScriptContext& scriptContext)
{
	// If we're hotloading then try to grab some data from the previous page
	Script::StructuredClone hotloadData;
	if (gui)
	{
		std::shared_ptr<ScriptInterface> scriptInterface = gui->GetScriptInterface();
		ScriptRequest rq(scriptInterface);
		JS::RootedValue hotloadDataVal(rq.cx, gui->GetHotloadData(rq));
		hotloadData = Script::WriteStructuredClone(rq, hotloadDataVal);
	}

	g_VideoMode.ResetCursor();
	inputs.clear();
	gui.reset(new CGUI(scriptContext));
	const ScriptRequest rq{gui->GetScriptInterface()};

	{
		JS::RootedString jsName{rq.cx, JS_NewStringCopyZ(rq.cx, START_ATLAS)};
		JS::RootedValue symbol{rq.cx, JS::SymbolValue(JS::NewSymbol(rq.cx, jsName))};
		JS::RootedValue nativeScope{rq.cx, JS::ObjectValue(*rq.nativeScope)};
		Script::SetProperty(rq, nativeScope, START_ATLAS, symbol, true);
	}
	gui->AddObjectTypes();

	VfsPath path = VfsPath("gui") / m_Name;
	inputs.insert(path);

	CXeromyces xero;
	if (xero.Load(g_VFS, path, "gui_page") != PSRETURN_OK)
		// Fail silently (Xeromyces reported the error)
		return;

	int elmt_page = xero.GetElementID("page");
	int elmt_include = xero.GetElementID("include");

	XMBElement root = xero.GetRoot();

	if (root.GetNodeName() != elmt_page)
	{
		LOGERROR("GUI page '%s' must have root element <page>", utf8_from_wstring(m_Name));
		return;
	}

	VfsPath rootModule;
	XERO_ITER_EL(root, node)
	{
		if (node.GetNodeName() != elmt_include)
		{
			LOGERROR("GUI page '%s' must only have <include> elements inside <page>", utf8_from_wstring(m_Name));
			continue;
		}

		CStr8 name = node.GetText();
		CStrW nameW = node.GetText().FromUTF8();

		PROFILE2("load gui xml");
		PROFILE2_ATTR("name: %s", name.c_str());

		if (name.back() == '/')
		{
			VfsPath currentDirectory = VfsPath("gui") / nameW;
			VfsPaths directories;
			vfs::GetPathnames(g_VFS, currentDirectory, L"*.xml", directories);
			for (const VfsPath& directory : directories)
				gui->LoadXmlFile(directory, inputs);
		}
		else
		{
			VfsPath directory = VfsPath("gui") / nameW;
			gui->LoadXmlFile(directory, inputs);
		}
	}

	gui->LoadedXmlFiles();

	scriptContext.RunJobs();
	if (gui->m_LoadModuleResult.has_value())
	{
		gui->m_LoadModuleResult->moduleNamespace = gui->m_LoadModuleResult->iterator->Get();
		++gui->m_LoadModuleResult->iterator;
	}

	JS::RootedValue hotloadDataVal(rq.cx);

	if (hotloadData)
		Script::ReadStructuredClone(rq, hotloadData, &hotloadDataVal);

	sendingPromise = std::make_shared<JS::PersistentRootedObject>(rq.cx);
	// Assigning to `sendingPromise` isn't possible after `init` has been called because `init` might
	// replace this page. So a local copy has to be made.
	const std::shared_ptr<JS::PersistentRootedObject> localPromise{sendingPromise};

	JS::RootedObject returnObject{rq.cx, gui->CallPageInit(rq, initData, hotloadDataVal,
		utf8_from_wstring(m_Name))};

	*localPromise = returnObject ? returnObject : JS::NewPromiseObject(rq.cx, nullptr);
}

JS::Value CGUIManager::SGUIPage::ReplacePromise(ScriptInterface& scriptInterface)
{
	const ScriptRequest rq{scriptInterface};
	receivingPromise = std::make_shared<JS::PersistentRootedObject>(rq.cx,
			JS::NewPromiseObject(rq.cx, nullptr));

	return JS::ObjectValue(**receivingPromise);
}

std::optional<CGUIManager::SGUIPage::CloseResult> CGUIManager::SGUIPage::MaybeClose(const bool topmostPage)
{
	if (JS::GetPromiseState(*sendingPromise) == JS::PromiseState::Pending)
		return std::nullopt;

	// Make sure we unfocus anything on the current page.
	gui->SendFocusMessage(GUIM_LOST_FOCUS);

	const ScriptRequest rq{gui->GetScriptInterface()};
	JS::RootedValue arg{rq.cx, JS::GetPromiseResult(*sendingPromise)};
	const bool rejected{JS::GetPromiseState(*sendingPromise) == JS::PromiseState::Rejected};
	if (topmostPage)
	{
		JS::RootedValue nativeScope{rq.cx, JS::ObjectValue(*rq.nativeScope)};
		JS::RootedValue symbol{rq.cx};
		Script::GetProperty(rq, nativeScope, START_ATLAS, &symbol);
		bool equals;
		if (!JS::StrictlyEqual(rq.cx, arg, symbol, &equals))
			throw std::runtime_error{"Error while comparing return value to a symbol."};

		if (equals)
			return CGUIManager::SGUIPage::CloseResult{nullptr, rejected};
	}
	return CGUIManager::SGUIPage::CloseResult{Script::WriteStructuredClone(rq, arg), rejected};
}

void CGUIManager::SGUIPage::Refocus(const CloseResult& result)
{
	ENSURE(receivingPromise);

	std::shared_ptr<ScriptInterface> scriptInterface = gui->GetScriptInterface();
	ScriptRequest rq(scriptInterface);

	JS::RootedObject globalObj(rq.cx, rq.glob);

	JS::RootedObject recv(rq.cx, *std::exchange(receivingPromise, nullptr));

	JS::RootedValue argVal(rq.cx);
	Script::ReadStructuredClone(rq, result.arg, &argVal);

	// This only resolves the promise, it doesn't call the continuation.
	(result.rejected ? JS::RejectPromise : JS::ResolvePromise)(rq.cx, recv, argVal);

	// We return to a page where some object might have been focused.
	gui->SendFocusMessage(GUIM_GOT_FOCUS);
}

Status CGUIManager::ReloadChangedFile(const VfsPath& path)
{
	for (SGUIPage& p : m_PageStack)
		if (p.inputs.find(path) != p.inputs.end())
		{
			LOGMESSAGE("GUI file '%s' changed - reloading page '%s'", path.string8(), utf8_from_wstring(p.m_Name));
			p.LoadPage(m_ScriptContext);
			// TODO: this can crash if LoadPage runs an init script which modifies the page stack and breaks our iterators
		}

	return INFO::OK;
}

Status CGUIManager::ReloadAllPages()
{
	// TODO: this can crash if LoadPage runs an init script which modifies the page stack and breaks our iterators
	for (SGUIPage& p : m_PageStack)
		p.LoadPage(m_ScriptContext);

	return INFO::OK;
}

InReaction CGUIManager::HandleEvent(const SDL_Event_* ev)
{
	// We want scripts to have access to the raw input events, so they can do complex
	// processing when necessary (e.g. for unit selection and camera movement).
	// Sometimes they'll want to be layered behind the GUI widgets (e.g. to detect mousedowns on the
	// visible game area), sometimes they'll want to intercepts events before the GUI (e.g.
	// to capture all mouse events until a mouseup after dragging).
	// So we call two separate handler functions:

	bool handled = false;

	{
		PROFILE("handleInputBeforeGui");
		ScriptRequest rq(*top()->GetScriptInterface());

		JS::RootedValue global(rq.cx, rq.globalValue());
		if (ScriptFunction::Call(rq, global, "handleInputBeforeGui", handled, *ev, top()->FindObjectUnderMouse()))
			if (handled)
				return IN_HANDLED;
	}

	{
		PROFILE("handle event in native GUI");
		InReaction r = top()->HandleEvent(ev);
		if (r != IN_PASS)
			return r;
	}

	{
		// We can't take the following lines out of this scope because top() may be another gui page than it was when calling handleInputBeforeGui!
		ScriptRequest rq(*top()->GetScriptInterface());
		JS::RootedValue global(rq.cx, rq.globalValue());

		PROFILE("handleInputAfterGui");
		if (ScriptFunction::Call(rq, global, "handleInputAfterGui", handled, *ev))
			if (handled)
				return IN_HANDLED;
	}

	return IN_PASS;
}

void CGUIManager::SendEventToAll(const CStr& eventName) const
{
	const auto pageStack = GetCopyOfFrozenStack();

	for (const SGUIPage& p : pageStack)
		p.gui->SendEventToAll(eventName);

}

void CGUIManager::SendEventToAll(const CStr& eventName, JS::HandleValueArray paramData) const
{
	const auto pageStack = GetCopyOfFrozenStack();

	for (const SGUIPage& p : pageStack)
		p.gui->SendEventToAll(eventName, paramData);
}

std::optional<bool> CGUIManager::TickObjects()
{
	PROFILE3("gui tick");

	// We share the script context with everything else that runs in the same thread.
	// This call makes sure we trigger GC regularly even if the simulation is not running.
	m_ScriptContext.MaybeIncrementalGC();

	const auto pageStack = GetCopyOfFrozenStack();

	for (const SGUIPage& p : pageStack)
	{
		const ScriptRequest rq{p.gui->GetScriptInterface()};
		JS::RootedObject newSendingPromise{rq.cx, p.gui->TickObjects(rq, p.initData,
			utf8_from_wstring(p.m_Name))};
		if (newSendingPromise)
			(*p.sendingPromise) = newSendingPromise;
	}

	m_ScriptContext.RunJobs();

	while (!m_PageStack.empty())
	{
		const size_t stackSize{m_PageStack.size()};
		const std::optional<SGUIPage::CloseResult> result{
			m_PageStack.back().MaybeClose(stackSize == 1)};
		if (!result.has_value())
			break;
		ENSURE(m_PageStack.size() == stackSize);
		m_PageStack.pop_back();
		if (m_PageStack.empty())
			return !result.value().arg;
		else
			m_PageStack.back().Refocus(result.value());

		m_ScriptContext.RunJobs();
	}
	return std::nullopt;
}

void CGUIManager::Draw(Renderer::Backend::IDeviceCommandContext* deviceCommandContext) const
{
	PROFILE3("gui");

	// The bottom (main) page uses the global gui.scale so the primary UI can be
	// scaled up for readability. Pages stacked above it are popups/overlays
	// (dialogs, message boxes, menus); they use a smaller "popup" scale so they
	// stay small and fit on screen. A smaller scale means a larger logical canvas,
	// so a fixed-size dialog occupies less of the screen.
	const float uiScale = g_VideoMode.GetScale();
#if OS_SWITCH
	const float defaultPopupScale = 1.0f;
#else
	const float defaultPopupScale = uiScale;
#endif
	const float popupScale = g_ConfigDB.Get("gui.popupscale", defaultPopupScale);

	std::size_t index = 0;
	for (const SGUIPage& p : m_PageStack)
	{
		const float scale = index == 0 ? uiScale : popupScale;
		// Changing scale re-lays-out the page (cached object sizes depend on the
		// logical window size, which is resolution / scale).
		if (p.gui->GetScale() != scale)
		{
			p.gui->SetScale(scale);
			p.gui->UpdateResolution();
		}
		CCanvas2D canvas{static_cast<uint32_t>(g_xres), static_cast<uint32_t>(g_yres),
			scale, deviceCommandContext};
		p.gui->Draw(canvas);
		++index;
	}
}

void CGUIManager::UpdateResolution()
{
	const auto pageStack = GetCopyOfFrozenStack();

	for (const SGUIPage& p : pageStack)
	{
		p.gui->UpdateResolution();
		p.gui->SendEventToAll(EVENT_NAME_WINDOW_RESIZED);
	}
}

bool CGUIManager::TemplateExists(const std::string& templateName) const
{
	return m_TemplateLoader.TemplateExists(templateName);
}

const CParamNode& CGUIManager::GetTemplate(const std::string& templateName)
{
	const CParamNode& templateRoot = m_TemplateLoader.GetTemplateFileData(templateName).GetOnlyChild();
	if (!templateRoot.IsOk())
		LOGERROR("Invalid template found for '%s'", templateName.c_str());

	return templateRoot;
}

void CGUIManager::DisplayLoadProgress(int percent, const wchar_t* pending_task)
{
	const ScriptInterface& scriptInterface = *(GetActiveGUI()->GetScriptInterface());
	ScriptRequest rq(scriptInterface);

	JS::RootedValueVector paramData(rq.cx);

	std::ignore = paramData.append(JS::NumberValue(percent));

	JS::RootedValue valPendingTask(rq.cx);
	Script::ToJSVal(rq, &valPendingTask, pending_task);
	std::ignore = paramData.append(valPendingTask);

	SendEventToAll(EVENT_NAME_GAME_LOAD_PROGRESS, paramData);
}

// This returns a shared_ptr to make sure the CGUI doesn't get deallocated
// while we're in the middle of calling a function on it (e.g. if a GUI script
// calls SwitchPage)
std::shared_ptr<CGUI> CGUIManager::top() const
{
	ENSURE(m_PageStack.size());
	return m_PageStack.back().gui;
}

PS::StaticVector<CGUIManager::SGUIPage, 16> CGUIManager::GetCopyOfFrozenStack() const
{
	PS::StaticVector<CGUIManager::SGUIPage, 16> stack;
	std::copy(m_PageStack.begin(), m_PageStack.end(), std::back_inserter(stack));
	return stack;
}
