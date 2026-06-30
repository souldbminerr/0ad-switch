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

#include "GameSetup.h"

#include "graphics/GameView.h"
#include "gui/CGUI.h"
#include "gui/GUIManager.h"
#include "gui/Scripting/JSInterface_GUIManager.h"
#include "i18n/L10n.h"
#include "lib/app_hooks.h"
#include "lib/code_annotation.h"
#include "lib/code_generation.h"
#include "lib/config2.h"
#include "lib/debug.h"
#include "lib/external_libraries/curl.h"
#include "lib/file/common/file_stats.h"
#include "lib/file/file_system.h"
#include "lib/file/vfs/vfs.h"
#include "lib/file/vfs/vfs_path.h"
#include "lib/file/vfs/vfs_util.h"
#include "lib/input.h"
#include "lib/path.h"
#include "lib/status.h"
#include "lib/sysdep/os.h"
#include "lib/timer.h"
#include "lobby/IXmppClient.h"
#include "network/NetClient.h"
#include "network/NetHost.h"
#include "network/NetMessage.h"
#include "network/NetServer.h"
#include "network/scripting/JSInterface_Network.h"
#include "ps/CConsole.h"
#include "ps/CLogger.h"
#include "ps/ConfigDB.h"
#include "ps/Errors.h"
#include "ps/Filesystem.h"
#include "ps/Game.h"
#include "ps/GameSetup/CmdLineArgs.h"
#include "ps/GameSetup/Config.h"
#include "ps/GameSetup/HWDetect.h"
#include "ps/GameSetup/Paths.h"
#include "ps/Globals.h"
#include "ps/Hotkey.h"
#include "ps/Joystick.h"
#include "ps/Loader.h"
#include "ps/Mod.h"
#include "ps/ModIo.h"
#include "ps/Profile.h"
#include "ps/ProfileViewer.h"
#include "ps/Profiler2.h"
#include "ps/Pyrogenesis.h"	// psSetLogDir
#include "ps/TemplateLoader.h"
#include "ps/ThreadUtil.h"
#include "ps/TouchInput.h"
#include "ps/UserReport.h"
#include "ps/VideoMode.h"
#include "ps/World.h"
#include "ps/XMB/XMBData.h"
#include "ps/XMB/XMBStorage.h"
#include "ps/XML/Xeromyces.h"
#include "ps/scripting/JSInterface_Game.h"
#include "ps/scripting/JSInterface_Main.h"
#include "ps/scripting/JSInterface_VFS.h"
#include "renderer/Renderer.h"
#include "renderer/RenderingOptions.h"
#include "renderer/SceneRenderer.h"
#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/ScriptContext.h"
#include "scriptinterface/ScriptConversions.h"
#include "scriptinterface/ScriptInterface.h"
#include "scriptinterface/ScriptRequest.h"
#include "scriptinterface/ScriptStats.h"
#include "simulation2/Simulation2.h"
#include "simulation2/scripting/JSInterface_Simulation.h"
#include "simulation2/system/Component.h"
#include "soundmanager/ISoundManager.h"

#include <SDL.h>
#include <SDL_error.h>
#include <SDL_hints.h>
#include <SDL_keyboard.h>
#include <SDL_version.h>
#include <algorithm>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/constants.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fmt/format.h>
#include <fstream>
#include <js/CallArgs.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <locale>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>


#if !(OS_WIN || OS_MACOSX || OS_ANDROID || OS_SWITCH) // assume all other platforms use X11 for wxWidgets
#define MUST_INIT_X11 1
#include <X11/Xlib.h>
#else
#define MUST_INIT_X11 0
#endif

extern void RestartEngine();

using namespace std::literals;

ERROR_GROUP(System);
ERROR_TYPE(System, SDLInitFailed);
ERROR_TYPE(System, VmodeFailed);
ERROR_TYPE(System, RequiredExtensionsMissing);

thread_local std::shared_ptr<ScriptContext> g_ScriptContext;

bool g_InDevelopmentCopy;
bool g_CheckedIfInDevelopmentCopy = false;

ErrorReactionInternal psDisplayError(const wchar_t* /*text*/, size_t /*flags*/)
{
	// If we're fullscreen, then sometimes (at least on some particular drivers on Linux)
	// displaying the error dialog hangs the desktop since the dialog box is behind the
	// fullscreen window. So we just force the game to windowed mode before displaying the dialog.
	// (But only if we're in the main thread, and not if we're being reentrant.)
	if (Threading::IsMainThread())
	{
		static bool reentering = false;
		if (!reentering)
		{
			reentering = true;
			g_VideoMode.SetFullscreen(false);
			reentering = false;
		}
	}

	// We don't actually implement the error display here, so return appropriately
	return ERI_NOT_IMPLEMENTED;
}

void MountMods(const Paths& paths, const std::vector<CStr>& mods)
{
	OsPath modPath = paths.RData()/"mods";
	OsPath modUserPath = paths.UserData()/"mods";

	{
		std::string list;
		for (const CStr& m : mods) { list += m.c_str(); list += " "; }
		LOGMESSAGE("SWITCHDBG MountMods: rdata-modPath='%s' mods=[ %s]", modPath.string8().c_str(), list.c_str());
	}
	for (const CStr& m : mods)
		LOGMESSAGE("SWITCHDBG   mount '%s': rdataDirExists=%d", m.c_str(), (int)DirectoryExists(modPath / OsPath(m) / ""));

	size_t userFlags = VFS_MOUNT_WATCH|VFS_MOUNT_ARCHIVABLE;
	size_t baseFlags = userFlags|VFS_MOUNT_MUST_EXIST;
	size_t priority = 0;
	for (size_t i = 0; i < mods.size(); ++i)
	{
		priority = i + 1; // Mods are higher priority than regular mountings, which default to priority 0

		OsPath modName(mods[i]);
		// Only mount mods from the user path if they don't exist in the 'rdata' path.
		if (DirectoryExists(modPath / modName / ""))
			g_VFS->Mount(L"", modPath / modName / "", baseFlags, priority);
		else
			g_VFS->Mount(L"", modUserPath / modName / "", userFlags, priority);

		// If mod have a config/<modName>.cfg, load the configuration.
		VfsPath modConfigPath{fmt::format("config/{}.cfg", mods[i].c_str())};
		if (!VfsFileExists(modConfigPath))
			continue;

		g_ConfigDB.SetConfigFile(CFG_MOD, modConfigPath);
		g_ConfigDB.Reload(CFG_MOD);
	}

	// Mount the user mod last. In dev copy, mount it with a low priority. Otherwise, make it writable.
	g_VFS->Mount(L"", modUserPath / "user" / "", userFlags, InDevelopmentCopy() ? 0 : priority + 1);
}

void InitVfs(const CmdLineArgs& args)
{
	PROFILE2("InitVfs");

	const Paths paths(args);

	OsPath logs(paths.Logs());
	CreateDirectories(logs, 0700);

	psSetLogDir(logs);
	// desired location for crashlog is now known. update AppHooks ASAP
	// (particularly before the following error-prone operations):
	app_hooks_update({
		.get_log_dir = psLogDir,
		.bundle_logs = psBundleLogs,
		.display_error = psDisplayError
	});

	g_VFS = CreateVfs();

	const OsPath readonlyConfig = paths.RData()/"config"/"";

	// Mount these dirs with highest priority so that mods can't overwrite them.
	g_VFS->Mount(L"cache/", paths.Cache(), VFS_MOUNT_ARCHIVABLE, VFS_MAX_PRIORITY);	// (adding XMBs to archive speeds up subsequent reads)
	if (readonlyConfig != paths.Config())
		g_VFS->Mount(L"config/", readonlyConfig, 0, VFS_MAX_PRIORITY-1);
	g_VFS->Mount(L"config/", paths.Config(), 0, VFS_MAX_PRIORITY);
	g_VFS->Mount(L"screenshots/", paths.UserData()/"screenshots"/"", 0, VFS_MAX_PRIORITY);
	g_VFS->Mount(L"saves/", paths.UserData()/"saves"/"", VFS_MOUNT_WATCH, VFS_MAX_PRIORITY);

	// Engine localization files (regular priority, these can be overwritten).
	g_VFS->Mount(L"l10n/", paths.RData()/"l10n"/"");

	// Mods will be mounted later.

	// note: don't bother with g_VFS->TextRepresentation - directories
	// haven't yet been populated and are empty.
}


static void InitPs(bool setup_gui, const CStrW& gui_page, ScriptInterface* srcScriptInterface, JS::HandleValue initData)
{
	g_Console->Init();
	LoadHotkeys(g_ConfigDB);

	if (!setup_gui)
	{
		// We do actually need *some* kind of GUI loaded, so use the
		// (currently empty) Atlas one
		g_GUI->SwitchPage(L"page_atlas.xml", srcScriptInterface, initData);
		return;
	}

	// GUI uses VFS, so this must come after VFS init.
	g_GUI->SwitchPage(gui_page, srcScriptInterface, initData);
}

void InitInput()
{
	g_Joystick.Initialise();

	// register input handlers
	// This stack is constructed so the first added, will be the last
	//  one called. This is important, because each of the handlers
	//  has the potential to block events to go further down
	//  in the chain. I.e. the last one in the list added, is the
	//  only handler that can block all messages before they are
	//  processed.
	in_add_handler(game_view_handler);

	in_add_handler(CProfileViewer::InputThunk);

	in_add_handler(HotkeyInputActualHandler);

	// gui_handler needs to be registered after (i.e. called before!) the
	// hotkey handler so that input boxes can be typed in without
	// setting off hotkeys.
	in_add_handler(gui_handler);
	// Likewise for the console.
	in_add_handler(conInputHandler);

	in_add_handler(touch_input_handler);

	// Should be called after scancode map update (i.e. after the global input, but before UI).
	// This never blocks the event, but it does some processing necessary for hotkeys,
	// which are triggered later down the input chain.
	// (by calling this before the UI, we can use 'EventWouldTriggerHotkey' in the UI).
	in_add_handler(HotkeyInputPrepHandler);

	// These two must be called first (i.e. pushed last)
	// GlobalsInputHandler deals with some important global state,
	// such as which scancodes are being pressed, mouse buttons pressed, etc.
	// while HotkeyStateChange updates the map of active hotkeys.
	in_add_handler(GlobalsInputHandler);
	in_add_handler(HotkeyStateChange);
}


static void ShutdownPs()
{
	SAFE_DELETE(g_GUI);

	UnloadHotkeys();
}

static void InitSDL()
{
#if OS_LINUX
	// In fullscreen mode when SDL is compiled with DGA support, the mouse
	// sensitivity often appears to be unusably wrong (typically too low).
	// (This seems to be reported almost exclusively on Ubuntu, but can be
	// reproduced on Gentoo after explicitly enabling DGA.)
	// Disabling the DGA mouse appears to fix that problem, and doesn't
	// have any obvious negative effects.
	setenv("SDL_VIDEO_X11_DGAMOUSE", "0", 0);
#endif

	Uint32 sdlInitFlags = SDL_INIT_VIDEO|SDL_INIT_TIMER|SDL_INIT_NOPARACHUTE;
#if OS_SWITCH
	// The Switch has no mouse. Drive 0 A.D.'s mouse-based cursor from the
	// touchscreen by having SDL synthesize mouse events from touch, and bring up
	// the controller subsystem for gamepad navigation.
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
	sdlInitFlags |= SDL_INIT_GAMECONTROLLER|SDL_INIT_JOYSTICK;
#endif
	if(SDL_Init(sdlInitFlags) < 0)
	{
		LOGERROR("SDL library initialization failed: %s", SDL_GetError());
		throw PSERROR_System_SDLInitFailed();
	}
	atexit(SDL_Quit);

	// Text input is active by default, disable it until it is actually needed.
	SDL_StopTextInput();

#if SDL_VERSION_ATLEAST(2, 0, 9)
	// SDL2 >= 2.0.9 defaults to 32 pixels (to support touch screens) but that can break our double-clicking.
	SDL_SetHint(SDL_HINT_MOUSE_DOUBLE_CLICK_RADIUS, "1");
#endif

#if SDL_VERSION_ATLEAST(2, 0, 14) && OS_WIN
	// SDL2 >= 2.0.14 Before SDL 2.0.14, this defaulted to true. In 2.0.14 they switched to false
	// breaking the behavior on Windows.
	// https://github.com/libsdl-org/SDL/commit/1947ca7028ab165cc3e6cbdb0b4b7c4db68d1710
	// https://github.com/libsdl-org/SDL/issues/5033
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "1");
#endif

#if OS_MACOSX
	// Some Mac mice only have one button, so they can't right-click
	// but SDL2 can emulate that with Ctrl+Click
	SDL_SetHint(SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK,
		g_ConfigDB.Get("macmouse", false) ? "1" : "0");
#endif
}

static void ShutdownSDL()
{
	PROFILE2("ShutdownSDL");
	SDL_Quit();
}


void EndGame()
{
	SAFE_DELETE(g_NetClient);
	SAFE_DELETE(g_NetServer);
	SAFE_DELETE(g_Game);

	if (CRenderer::IsInitialised())
	{
		ISoundManager::CloseGame();
		g_Renderer.GetSceneRenderer().ResetState();
	}
}

void ShutdownNetworkAndUI()
{
	const bool hasRenderer = CRenderer::IsInitialised();

	EndGame();

	SAFE_DELETE(g_XmppClient);

	SAFE_DELETE(g_ModIo);

	ShutdownPs();

	if (hasRenderer)
		delete &g_Renderer;

	g_RenderingOptions.ClearHooks();

	g_Profiler2.ShutdownGPU();

	if (hasRenderer)
		g_VideoMode.Shutdown();

	ShutdownSDL();
	g_UserReporter.Deinitialize();

	// Cleanup curl now that g_ModIo and g_UserReporter have been shutdown.
	curl_global_cleanup();

	delete &g_L10n;
}

void ShutdownConfigAndSubsequent()
{
	CConfigDB::Shutdown();

	SAFE_DELETE(g_Console);

	// This is needed to ensure that no callbacks from the JSAPI try to use
	// the profiler when it's already destructed
	g_ScriptContext.reset();

	// resource
	// first shut down all resource owners, and then the handle manager.
	{
		PROFILE2("resource modules");

		ISoundManager::SetEnabled(false);

		g_VFS.reset();

		file_stats_dump();
	}

	PROFILE2("shutdown misc");
	timer_DisplayClientTotals();

	CNetHost::Deinitialize();

	// Should be destroyed last, since the above uses them.
	delete &g_Profiler;
	delete &g_ProfileViewer;

	SAFE_DELETE(g_ScriptStatsTable);
}

#if OS_UNIX
static void FixLocales()
{
#if OS_MACOSX || OS_BSD
	// OS X requires a UTF-8 locale in LC_CTYPE so that *wprintf can handle
	// wide characters. Peculiarly the string "UTF-8" seems to be acceptable
	// despite not being a real locale, and it's conveniently language-agnostic,
	// so use that.
	setlocale(LC_CTYPE, "UTF-8");
#endif


	// On misconfigured systems with incorrect locale settings, we'll die
	// with a C++ exception when some code (e.g. Boost) tries to use locales.
	// To avoid death, we'll detect the problem here and warn the user and
	// reset to the default C locale.


	// For informing the user of the problem, use the list of env vars that
	// glibc setlocale looks at. (LC_ALL is checked first, and LANG last.)
	const char* const LocaleEnvVars[] = {
		"LC_ALL",
		"LC_COLLATE",
		"LC_CTYPE",
		"LC_MONETARY",
		"LC_NUMERIC",
		"LC_TIME",
		"LC_MESSAGES",
		"LANG"
	};

	try
	{
		// this constructor is similar to setlocale(LC_ALL, ""),
		// but instead of returning NULL, it throws runtime_error
		// when the first locale env variable found contains an invalid value
		std::locale("");
	}
	catch (std::runtime_error&)
	{
		LOGWARNING("Invalid locale settings");

		for (size_t i = 0; i < ARRAY_SIZE(LocaleEnvVars); i++)
		{
			if (char* envval = getenv(LocaleEnvVars[i]))
				LOGWARNING("  %s=\"%s\"", LocaleEnvVars[i], envval);
			else
				LOGWARNING("  %s=\"(unset)\"", LocaleEnvVars[i]);
		}

		// We should set LC_ALL since it overrides LANG
		if (setenv("LC_ALL", std::locale::classic().name().c_str(), 1))
			debug_warn(L"Invalid locale settings, and unable to set LC_ALL env variable.");
		else
			LOGWARNING("Setting LC_ALL env variable to: %s", getenv("LC_ALL"));
	}
}
#else
static void FixLocales()
{
	// Do nothing on Windows
}
#endif

void EarlyInit()
{
	// If you ever want to catch a particular allocation:
	//_CrtSetBreakAlloc(232647);

	Threading::SetMainThread();

	debug_SetThreadName("main");
	// add all debug_printf "tags" that we are interested in:
	debug_filter_add("FILES");

	timer_Init();

	// initialise profiler early so it can profile startup,
	// but only after LatchStartTime
	g_Profiler2.Initialise();

	FixLocales();

	// Because we do GL calls from a secondary thread, Xlib needs to
	// be told to support multiple threads safely.
	// This is needed for Atlas, but we have to call it before any other
	// Xlib functions (e.g. the ones used when drawing the main menu
	// before launching Atlas)
#if MUST_INIT_X11
	int status = XInitThreads();
	if (status == 0)
		debug_printf("Error enabling thread-safety via XInitThreads\n");
#endif

	// Initialise the low-quality rand function
	srand(time(NULL));	// NOTE: this rand should *not* be used for simulation!
}

bool Autostart(const CmdLineArgs& args);

/**
 * Returns true if the user has intended to start a visual replay from command line.
 */
bool AutostartVisualReplay(const std::string& replayFile);

bool Init(const CmdLineArgs& args, int flags)
{
	new CProfileViewer;
	new CProfileManager;	// before any script code

	g_ScriptStatsTable = new CScriptStatsTable;
	g_ProfileViewer.AddRootTable(g_ScriptStatsTable);

	// Set up the console early, so that debugging
	// messages can be logged to it. (The console's size
	// and fonts are set later in InitPs())
	g_Console = new CConsole();

	// g_ConfigDB, command line args, globals
	CONFIG_Init(args);

	// Using a global object for the context is a workaround until Simulation and AI use
	// their own threads and also their own contexts.
	const int contextSize = 384 * 1024 * 1024;
	const int heapGrowthBytesGCTrigger = 12 * 1024 * 1024;
	g_ScriptContext = ScriptContext::CreateContext(contextSize, heapGrowthBytesGCTrigger);

	// On the first Init (INIT_MODS), check for command-line arguments
	// or use the default mods from the config and enable those.
	// On later engine restarts (e.g. the mod selector), we will skip this path,
	// to avoid overwriting the newly selected mods.
	if (flags & INIT_MODS)
	{
		ScriptInterface modInterface("Engine", "Mod", g_ScriptContext);
		g_Mods.UpdateAvailableMods(modInterface);
		std::vector<CStr> mods;
		if (args.Has("mod"))
			mods = args.GetMultiple("mod");
		else
		{
			// Note: boost::split on an empty string yields a single empty-string
			// element (not an empty list), which would then be treated as an
			// incompatible mod and force the {"mod"}-only fallback below. Guard it.
			const std::string enabledMods = g_ConfigDB.Get("mod.enabledmods", std::string{});
			if (!enabledMods.empty())
				boost::split(mods, enabledMods,
					boost::algorithm::is_space(), boost::token_compress_on);
		}

		if (!g_Mods.EnableMods(mods, flags & INIT_MODS_PUBLIC))
		{
			// In non-visual mode, fail entirely.
			if (args.Has("autostart-nonvisual"))
			{
				LOGERROR("Trying to start with incompatible mods: %s.", boost::algorithm::join(g_Mods.GetIncompatibleMods(), ", "));
				return false;
			}
		}
	}
	// If there are incompatible mods, switch to the mod selector so players can resolve the problem.
	if (g_Mods.GetIncompatibleMods().empty())
		MountMods(Paths(args), g_Mods.GetEnabledMods());
	else
		MountMods(Paths(args), { "mod" });

	// Special command-line mode to dump the entity schemas instead of running the game.
	// (This must be done after loading VFS etc, but should be done before wasting time
	// on anything else.)
	if (args.Has("dumpSchema"))
	{
		try
		{
			CSimulation2 sim{NULL, *g_ScriptContext, NULL};
			sim.LoadDefaultScripts();
			std::ofstream f("entity.rng", std::ios_base::out | std::ios_base::trunc);
			f << sim.GenerateSchema();
			debug_printf("Generated entity.rng\n");
		}
		catch (const CSimulation2::LoadScriptError& e)
		{
			LOGERROR("%s", e.what());
		}
		return false;
	}

	CNetHost::Initialize();

#if CONFIG2_AUDIO
	if (!args.Has("autostart-nonvisual") && !g_DisableAudio)
		ISoundManager::CreateSoundManager();
#endif

	new L10n;

	// Optionally start profiler HTTP output automatically
	// (By default it's only enabled by a hotkey, for security/performance)
	if (g_ConfigDB.Get("profiler2.autoenable", false))
		g_Profiler2.EnableHTTP();

	// Initialise everything except Win32 sockets (because our networking
	// system already inits those)
	curl_global_init(CURL_GLOBAL_ALL & ~CURL_GLOBAL_WIN32);

	if (!g_Quickstart)
		g_UserReporter.Initialize(); // after config

	PROFILE2_EVENT("Init finished");
	return true;
}

void InitGraphics(const CmdLineArgs& args, int flags, const std::vector<CStr>& installedMods,
	ScriptContext& scriptContext, ScriptInterface& scriptInterface)
{
	const bool setup_vmode = (flags & INIT_HAVE_VMODE) == 0;

	if(setup_vmode)
	{
		InitSDL();

		if (!g_VideoMode.InitSDL())
			throw PSERROR_System_VmodeFailed(); // abort startup
	}

	RunHardwareDetection(!g_Quickstart, g_VideoMode.GetBackendDevice());

	// Optionally start profiler GPU timings automatically
	// (By default it's only enabled by a hotkey, for performance/compatibility)
	if (g_ConfigDB.Get("profiler2.autoenable", false))
		g_Profiler2.EnableGPU();

	if(g_DisableAudio)
		ISoundManager::SetEnabled(false);

	g_GUI = new CGUIManager{scriptContext, scriptInterface};


	if (RenderPathEnum::FromString(g_ConfigDB.Get("renderpath", "default"s)) == FIXED)
	{
		// It doesn't make sense to continue working here, because we're not
		// able to display anything.
		DEBUG_DISPLAY_FATAL_ERROR(
			L"Your graphics card doesn't appear to be fully compatible with OpenGL shaders."
			L" The game does not support pre-shader graphics cards."
			L" You are advised to try installing newer drivers and/or upgrade your graphics card."
			L" For more information, please see http://www.wildfiregames.com/forum/index.php?showtopic=16734"
		);
	}

	g_RenderingOptions.ReadConfigAndSetupHooks();

	// create renderer
	new CRenderer(g_VideoMode.GetBackendDevice());

	InitInput();

	// TODO: Is this the best place for this?
	if (VfsDirectoryExists(L"maps/"))
		g_Xeromyces.AddValidator(g_VFS, "map", "maps/scenario.rng");

	try
	{
		if (!AutostartVisualReplay(args.Get("replay-visual")) && !Autostart(args))
		{
			const bool setup_gui = ((flags & INIT_NO_GUI) == 0);

			ScriptRequest rq{g_GUI->GetScriptInterface()};
			JS::RootedValue data(rq.cx);
			Script::CreateObject(rq, &data, "isStartup", true);
			if (!installedMods.empty())
				Script::SetProperty(rq, data, "installedMods", installedMods);
			InitPs(setup_gui, installedMods.empty() ? L"page_pregame.xml" : L"page_modmod.xml", &g_GUI->GetScriptInterface(), data);
		}
	}
	catch (PSERROR_Game_World_MapLoadFailed& e)
	{
		// Map Loading failed

		// Start the engine so we have a GUI
		InitPs(true, L"page_pregame.xml", NULL, JS::UndefinedHandleValue);

		// Call script function to do the actual work
		//	(delete game data, switch GUI page, show error, etc.)
		CancelLoad(CStr(e.what()).FromUTF8());
	}
}

bool InitNonVisual(const CmdLineArgs& args)
{
	return Autostart(args);
}

/**
 * Temporarily loads a scenario map and retrieves the "ScriptSettings" JSON
 * data from it.
 * The scenario map format is used for scenario and skirmish map types (random
 * games do not use a "map" (format) but a small JavaScript program which
 * creates a map on the fly). It contains a section to initialize the game
 * setup screen.
 * @param mapPath Absolute path (from VFS root) to the map file to peek in.
 * @return ScriptSettings in JSON format extracted from the map.
 */
CStr8 LoadSettingsOfScenarioMap(const VfsPath &mapPath)
{
	CXeromyces mapFile;
	const char *pathToSettings[] =
	{
		"Scenario", "ScriptSettings", ""	// Path to JSON data in map
	};

	Status loadResult = mapFile.Load(g_VFS, mapPath);

	if (INFO::OK != loadResult)
	{
		LOGERROR("LoadSettingsOfScenarioMap: Unable to load map file '%s'", mapPath.string8());
		throw PSERROR_Game_World_MapLoadFailed("Unable to load map file, check the path for typos.");
	}
	XMBElement mapElement = mapFile.GetRoot();

	// Select the ScriptSettings node in the map file...
	for (int i = 0; pathToSettings[i][0]; ++i)
	{
		int childId = mapFile.GetElementID(pathToSettings[i]);

		XMBElementList nodes = mapElement.GetChildNodes();
		auto it = std::find_if(nodes.begin(), nodes.end(), [&childId](const XMBElement& child) {
			return child.GetNodeName() == childId;
		});

		if (it != nodes.end())
			mapElement = *it;
	}
	// ... they contain a JSON document to initialize the game setup
	// screen
	return mapElement.GetText();
}

/**
 * Autostart arguments are parsed in javascript for convenience and moddability.
 * This C++ part only handles the following arguments:
 * -autostart=MAP		to enable regular autostart / host mode autostart
 * -autostart-client=IP	to enable 'MP client' autostart.
 * -autostart-host		to enable 'MP host' autostart.
 * -autostart-nonvisual	to start in non-visual mode.
 *  TODO: it might be nice to move these to JS too.
 */
bool Autostart(const CmdLineArgs& args)
{
	if (!args.Has("autostart-client") && !args.Has("autostart"))
		return false;

	// Create some scriptinterface to store the js values for the settings.
	ScriptInterface scriptInterface("Engine", "Game Setup", g_ScriptContext);
	ScriptRequest rq(scriptInterface);

	// We use the javascript gameSettings to handle options, but that requires running JS.
	// Since we don't want to use the full Gui manager, we load an entrypoint script
	// that can run the priviledged "LoadScript" function, and then call the appropriate function.

	// TODO: this essentially duplicates the CGUI logic to load directory or scripts.
	std::unordered_set<VfsPath> templateCache;
	const auto autostartLoadScript = [&templateCache](const ScriptInterface& scriptInterface,
		const VfsPath& path)
	{
		if (!std::get<1>(templateCache.insert(path)))
			return;

		if (path.IsDirectory())
		{
			VfsPaths pathnames;
			vfs::GetPathnames(g_VFS, path, L"*.js", pathnames);
			for (const VfsPath& file : pathnames)
				scriptInterface.LoadGlobalScriptFile(file);
		}
		else
			scriptInterface.LoadGlobalScriptFile(path);
	};

	const auto loadScriptCallback = ScriptFunction::Register(rq, "LoadScript", autostartLoadScript);
	// Load the entire folder to allow mods to extend the entrypoint without copying the whole file.
	autostartLoadScript(scriptInterface, VfsPath(L"autostart/"));

	// Provide some required functions to the script.

	struct GetTemplate
	{
		CTemplateLoader templateLoader;

		CParamNode operator()(const std::string& templateName){
			// TODO: this essentially duplicates the CGUI function
			const CParamNode& templateRoot{
				templateLoader.GetTemplateFileData(templateName).GetOnlyChild()};
			if (!templateRoot.IsOk())
				LOGERROR("Invalid template found for '%s'", templateName.c_str());

			return templateRoot;
		}
	};

	std::optional<ScriptFunction::StatefulCallback<GetTemplate>> getTemplateCallback;
	if (args.Has("autostart-nonvisual"))
		getTemplateCallback.emplace(rq, "GetTemplate", GetTemplate{});
	else
	{
		JSI_GUIManager::RegisterScriptFunctions(rq);
		// TODO: this loads pregame, which is hardcoded to exist by various code paths. That ought be changed.
		InitPs(false, L"page_pregame.xml", &g_GUI->GetScriptInterface(), JS::UndefinedHandleValue);
	}

	JSI_Game::RegisterScriptFunctions(rq);
	JSI_Main::RegisterScriptFunctions(rq);
	JSI_Simulation::RegisterScriptFunctions(rq);
	JSI_VFS::RegisterScriptFunctions_ReadWriteAnywhere(rq);
	JSI_Network::RegisterScriptFunctions(rq);

	JS::RootedValue cmdLineArgs(rq.cx);
	Script::ToJSVal(rq, &cmdLineArgs, args);

	if (args.Has("autostart-client") || args.Has("autostart-host"))
	{
		// Pass the default port if undefined, to avoid duplicating it in JS.
		if (!Script::HasProperty(rq, cmdLineArgs, "autostart-port"))
			Script::SetProperty(rq, cmdLineArgs, "autostart-port", PS_DEFAULT_PORT);

		JS::RootedValue global(rq.cx, rq.globalValue());
		if (!ScriptFunction::CallVoid(rq, global, args.Has("autostart-client") ? "autostartClient" : "autostartHost", cmdLineArgs, true))
			return false;

		bool shouldQuit = false;
		while (!shouldQuit)
		{
			g_NetClient->Poll();
			if (!ScriptFunction::Call(rq, global, "onTick", shouldQuit))
				return false;
			std::this_thread::sleep_for(std::chrono::microseconds(200));
		}
	}
	else
	{
		JS::RootedValue global(rq.cx, rq.globalValue());
		if (!ScriptFunction::CallVoid(rq, global, "autostartHost", cmdLineArgs, false))
			return false;
	}

	if (args.Has("autostart-nonvisual"))
	{
		LDR_NonprogressiveLoad();
		g_Game->ReallyStartGame();
	}

	return true;
}

bool AutostartVisualReplay(const std::string& replayFile)
{
	// No replay requested -> don't autostart. (Guard the empty path explicitly:
	// on Switch FileExists("") wrongly returns true, which would otherwise spin up
	// a CGame on an empty "replay" with empty attributes and break startup.)
	if (replayFile.empty() || !FileExists(OsPath(replayFile)))
		return false;

	g_Game = new CGame(false);
	g_Game->SetPlayerID(-1);
	g_Game->StartVisualReplay(replayFile);

	ScriptInterface& scriptInterface = g_Game->GetSimulation2()->GetScriptInterface();
	ScriptRequest rq(scriptInterface);
	JS::RootedValue attrs(rq.cx, g_Game->GetSimulation2()->GetInitAttributes());

	JS::RootedValue playerAssignments(rq.cx);
	Script::CreateObject(rq, &playerAssignments);
	JS::RootedValue localPlayer(rq.cx);
	Script::CreateObject(rq, &localPlayer, "player", g_Game->GetPlayerID());
	Script::SetProperty(rq, playerAssignments, "local", localPlayer);

	JS::RootedValue sessionInitData(rq.cx);

	Script::CreateObject(
		rq,
		&sessionInitData,
		"attribs", attrs,
		"playerAssignments", playerAssignments);

	InitPs(true, L"page_loading.xml", &scriptInterface, sessionInitData);

	return true;
}

void CancelLoad(const CStrW& message)
{
	std::shared_ptr<ScriptInterface> pScriptInterface = g_GUI->GetActiveGUI()->GetScriptInterface();
	ScriptRequest rq(pScriptInterface);

	JS::RootedValue global(rq.cx, rq.globalValue());

	LDR_Cancel();

	if (g_GUI &&
	    g_GUI->GetPageCount() &&
		Script::HasProperty(rq, global, "cancelOnLoadGameError"))
		ScriptFunction::CallVoid(rq, global, "cancelOnLoadGameError", message);
}

bool InDevelopmentCopy()
{
	if (!g_CheckedIfInDevelopmentCopy)
	{
		g_InDevelopmentCopy = (g_VFS->GetFileInfo(L"config/dev.cfg", NULL) == INFO::OK);
		g_CheckedIfInDevelopmentCopy = true;
	}
	return g_InDevelopmentCopy;
}
