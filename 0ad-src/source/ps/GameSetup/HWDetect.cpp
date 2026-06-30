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

#include "lib/build_version.h"
#include "lib/code_annotation.h"
#include "lib/config2.h"
#include "lib/debug.h"
#include "lib/external_libraries/curl.h"
#include "lib/external_libraries/enet.h"
#if CONFIG2_LOBBY
#include "lib/external_libraries/gloox.h"
#endif
#include "lib/external_libraries/png.h"
#include "lib/external_libraries/libsdl.h"
#include "lib/path.h"
#include "lib/posix/posix_utsname.h"
#include "lib/sysdep/arch.h"
#include "lib/sysdep/compiler.h"
#include "lib/sysdep/cpu.h"
#include "lib/sysdep/numa.h"
#include "lib/sysdep/os.h"
#include "lib/sysdep/os_cpu.h"
#include "lib/sysdep/smbios.h"
#include "lib/sysdep/sysdep.h"	// sys_OpenFile
#include "lib/timer.h"
#include "lib/types.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/Errors.h"
#include "ps/Filesystem.h"
#include "ps/GameSetup/Config.h"
#include "ps/Profiler2.h"
#include "ps/Pyrogenesis.h"
#include "ps/UserReport.h"
#include "ps/VideoMode.h"
#include "ps/scripting/JSInterface_ConfigDB.h"
#include "ps/scripting/JSInterface_Debug.h"
#include "renderer/backend/IDevice.h"
#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/JSON.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/ScriptInterface.h"
#include "scriptinterface/ScriptRequest.h"
#include "scriptinterface/StructuredClone.h"
#include "soundmanager/ISoundManager.h"

#include <SDL_cpuinfo.h>
#include <SDL_version.h>
#include <SDL_video.h>
#include <boost/version.hpp>
#include <cstdio>
#include <ctime>
#include <cwchar>
#include <fmt/format.h>
#include <freetype/fttypes.h>
#include <iterator>
#include <js/PropertyAndElement.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <libxml/xmlversion.h>
#include <random>
#include <sodium.h>
#include <sstream>
#include <string>
#include <thread>
#include <unicode/uvernum.h>
#include <vector>
#include <zlib.h>

// FreeType headers might have an include order.
#include <ft2build.h>
#include FT_FREETYPE_H

#if ARCH_AMD64
#include "lib/sysdep/arch/x86_x64/x86_x64.h"
#endif

#if OS_LINUX
#include <fstream>
#endif

#if CONFIG2_NVTT
#include "nvtt/nvtt.h"
#endif

#if CONFIG2_MINIUPNPC
#include <miniupnpc/miniupnpc.h>
#endif

#if CONFIG2_AUDIO
#include <vorbis/codec.h>
#endif

namespace
{

class Reporter
{
public:
	Reporter(const ScriptRequest& rq)
		: m_Rq(rq), m_LibrarySettings(rq.cx)
	{
		Script::CreateObject(m_Rq, &m_LibrarySettings);
	}

	template<typename PropertyType>
	Reporter& Add(const char* propertyName, const PropertyType& propertyValue)
	{
		Script::SetProperty(m_Rq, m_LibrarySettings, propertyName, propertyValue);
		return *this;
	}

	JS::Value MakeReport()
	{
		return Script::DeepCopy(m_Rq, m_LibrarySettings);
	}

private:
	const ScriptRequest& m_Rq;
	JS::RootedValue m_LibrarySettings;
};

class LibraryReporter : public Reporter
{
public:
	LibraryReporter(const ScriptRequest& rq, const char* name)
		: Reporter(rq)
	{
		Add("name", name);
	}
};

JS::Value MakeSDLReport(const ScriptRequest& rq)
{
	LibraryReporter reporter{rq, "sdl"};

	SDL_version build, runtime;
	SDL_VERSION(&build);

	char version[16];
	snprintf(version, ARRAY_SIZE(version), "%d.%d.%d", build.major, build.minor, build.patch);
	reporter.Add("build_version", version);

	SDL_GetVersion(&runtime);
	snprintf(version, ARRAY_SIZE(version), "%d.%d.%d", runtime.major, runtime.minor, runtime.patch);
	reporter.Add("runtime_version", version);

	// This is null in atlas (and further the call triggers an assertion).
	const char* backend = g_VideoMode.GetWindow() ? GetSDLSubsystem(g_VideoMode.GetWindow()) : "none";
	reporter.Add("video_backend", backend ? backend : "unknown");

	reporter.Add("display_count", SDL_GetNumVideoDisplays());

	reporter.Add("cpu_count", SDL_GetCPUCount());
	reporter.Add("system_ram", SDL_GetSystemRAM());

	return reporter.MakeReport();
}

JS::Value MakeFreeTypeReport(const ScriptRequest& rq)
{
	FT_Library FTLibrary;

	LibraryReporter libraryReporter{rq, "freetype"};
	if (!FT_Init_FreeType(&FTLibrary))
	{
		FT_Int major, minor, patch;
		FT_Library_Version(FTLibrary, &major, &minor, &patch);
		FT_Done_FreeType(FTLibrary);
		std::stringstream version;
		version << major << "." << minor << "." << patch;
		libraryReporter.Add("version", version.str());
	}
	else
		libraryReporter.Add("version", "unavailable");
	return libraryReporter.MakeReport();
}

void ReportLibraries(const ScriptRequest& rq, JS::HandleValue settings)
{
	JS::RootedValue librariesSettings(rq.cx);
	Script::CreateArray(rq, &librariesSettings);
	int libraryCount = 0;

	auto appendLibrary = [&rq, &librariesSettings, &libraryCount](const JS::Value& librarySettings)
	{
		JS::RootedValue value(rq.cx, librarySettings);
		Script::SetPropertyInt(rq, librariesSettings, libraryCount++, value);
	};

	appendLibrary(MakeSDLReport(rq));
	appendLibrary(MakeFreeTypeReport(rq));

	appendLibrary(LibraryReporter{rq, "boost"}.Add("version", BOOST_VERSION).MakeReport());
	appendLibrary(LibraryReporter{rq, "enet"}.Add("version", std::to_string(ENET_VERSION)).MakeReport());
	appendLibrary(LibraryReporter{rq, "fmt"}.Add("version", FMT_VERSION).MakeReport());
#if CONFIG2_LOBBY
	appendLibrary(LibraryReporter{rq, "gloox"}.Add("version", gloox_version()).MakeReport());
#endif
	appendLibrary(LibraryReporter{rq, "libicu"}.Add("version", U_ICU_VERSION).MakeReport());
	appendLibrary(LibraryReporter{rq, "libcurl"}.Add("version", std::string(curl_version())).MakeReport());
#if CONFIG2_AUDIO
	appendLibrary(LibraryReporter{rq, "libvorbis"}.Add("version", std::string(vorbis_version_string())).MakeReport());
#endif
	appendLibrary(LibraryReporter{rq, "libpng"}.Add("version", std::string(png_get_libpng_ver(nullptr))).MakeReport());
	appendLibrary(LibraryReporter{rq, "libsodium"}.Add("version", std::string(sodium_version_string())).MakeReport());
	appendLibrary(LibraryReporter{rq, "libxml2"}.Add("version", LIBXML_DOTTED_VERSION).MakeReport());
#if CONFIG2_MINIUPNPC
	appendLibrary(LibraryReporter{rq, "miniunpnpc"}.Add("version", MINIUPNPC_VERSION).MakeReport());
#endif
	appendLibrary(LibraryReporter{rq, "zlib"}.Add("version", ZLIB_VERSION).MakeReport());

#if CONFIG2_NVTT
	appendLibrary(LibraryReporter{rq, "nvtt"}
		.Add("build_version", NVTT_VERSION)
		.Add("runtime_version", nvtt::version())
		.MakeReport());
#endif

	Script::SetProperty(rq, settings, "libraries", librariesSettings);
}

void WriteSystemInfo(Renderer::Backend::IDevice* device, const utsname& un)
{
	PROFILE2("WriteSystemInfo");

	OsPath pathname = psLogDir() / "system_info.txt";
	FILE* f = sys_OpenFile(pathname, "w");
	if(!f)
		return;

	// current timestamp (redundant WRT OS timestamp, but that is not
	// visible when people are posting this file's contents online)
	{
	wchar_t timestampBuf[100] = {'\0'};
	time_t seconds;
	time(&seconds);
	struct tm* t = gmtime(&seconds);
	const size_t charsWritten = wcsftime(timestampBuf, ARRAY_SIZE(timestampBuf), L"(generated %Y-%m-%d %H:%M:%S UTC)", t);
	ENSURE(charsWritten != 0);
	fprintf(f, "%ls\n\n", timestampBuf);
	}

	// OS
	fprintf(f, "OS             : %s %s (%s)\n", un.sysname, un.release, un.version);

	// CPU
	fprintf(f, "CPU            : %s, %s", un.machine, cpu_IdentifierString());
	double cpuClock = os_cpu_ClockFrequency();	// query OS (may fail)
#if ARCH_X86_X64
	if(cpuClock <= 0.0)
		cpuClock = x86_x64::ClockFrequency();	// measure (takes a few ms)
#endif
	if(cpuClock > 0.0)
	{
		if(cpuClock < 1e9)
			fprintf(f, ", %.2f MHz\n", cpuClock*1e-6);
		else
			fprintf(f, ", %.2f GHz\n", cpuClock*1e-9);
	}
	else
		fprintf(f, "\n");

	// memory
	fprintf(f, "Memory         : %u MiB; %u MiB free\n", (unsigned)os_cpu_MemorySize(), (unsigned)os_cpu_MemoryAvailable());

	// graphics
	fprintf(f, "Video Card     : %s\n", device->GetName().c_str());
	fprintf(f, "Video Driver   : %s\n", device->GetDriverInformation().c_str());
	fprintf(f, "Video Mode     : %dx%d:%d\n", g_VideoMode.GetXRes(), g_VideoMode.GetYRes(), g_VideoMode.GetBPP());

#if CONFIG2_AUDIO
	if (g_SoundManager)
	{
		fprintf(f, "Sound Card     : %s\n", g_SoundManager->GetSoundCardNames().c_str());
		fprintf(f, "Sound Drivers  : %s\n", g_SoundManager->GetOpenALVersion().c_str());
	}
	else if(g_DisableAudio)
		fprintf(f, "Sound          : Game was ran without audio\n");
	else
		fprintf(f, "Sound          : No audio device was found\n");
#else
	fprintf(f, "Sound          : Game was compiled without audio\n");
#endif

	// OpenGL extensions (write them last, since it's a lot of text)
	fprintf(f, "\nBackend Extensions:\n");
	if (device->GetExtensions().empty())
		fprintf(f, "{unknown}\n");
	else
		for (const std::string& extension : device->GetExtensions())
			fprintf(f, "%s\n", extension.c_str());

	// System Management BIOS (even more text than OpenGL extensions)
	std::string smbios = SMBIOS::StringizeStructures(SMBIOS::GetStructures());
	fprintf(f, "\nSMBIOS: \n%s\n", smbios.c_str());

	fclose(f);
	f = 0;

	debug_printf("FILES| Hardware details written to '%s'\n", pathname.string8().c_str());
}

} // anonymous namespace

void SetDisableAudio(bool disabled)
{
	g_DisableAudio = disabled;
}

void RunHardwareDetection(bool writeSystemInfoBeforeDetection, Renderer::Backend::IDevice* device)
{
	utsname un;
	uname(&un);

	if (writeSystemInfoBeforeDetection)
		WriteSystemInfo(device, un);

	PROFILE2("RunHardwareDetection");

	ScriptInterface scriptInterface("Engine", "HWDetect", g_ScriptContext);

	ScriptRequest rq(scriptInterface);

	JSI_Debug::RegisterScriptFunctions(scriptInterface); // Engine.DisplayErrorDialog
	JSI_ConfigDB::RegisterScriptFunctions(scriptInterface);

	ScriptFunction::Register<SetDisableAudio>(rq, "SetDisableAudio");

	// Load the detection script:

	const wchar_t* scriptName = L"hwdetect/hwdetect.js";
	CVFSFile file;
	if (file.Load(g_VFS, scriptName) != PSRETURN_OK)
	{
		LOGERROR("Failed to load hardware detection script");
		return;
	}

	std::string code = file.DecodeUTF8(); // assume it's UTF-8
	scriptInterface.LoadScript(scriptName, code);

	// Collect all the settings we'll pass to the script:
	// (We'll use this same data for the opt-in online reporting system, so it
	// includes some fields that aren't directly useful for the hwdetect script)

	JS::RootedValue settings(rq.cx);
	Script::CreateObject(rq, &settings);

	Script::SetProperty(rq, settings, "os_unix", OS_UNIX);
	Script::SetProperty(rq, settings, "os_bsd", OS_BSD);
	Script::SetProperty(rq, settings, "os_linux", OS_LINUX);
	Script::SetProperty(rq, settings, "os_android", OS_ANDROID);
	Script::SetProperty(rq, settings, "os_macosx", OS_MACOSX);
	Script::SetProperty(rq, settings, "os_win", OS_WIN);

	Script::SetProperty(rq, settings, "arch_ia32", ARCH_IA32);
	Script::SetProperty(rq, settings, "arch_amd64", ARCH_AMD64);
	Script::SetProperty(rq, settings, "arch_arm", ARCH_ARM);
	Script::SetProperty(rq, settings, "arch_aarch64", ARCH_AARCH64);
	Script::SetProperty(rq, settings, "arch_e2k", ARCH_E2K);
	Script::SetProperty(rq, settings, "arch_ppc64", ARCH_PPC64);

#ifdef NDEBUG
	Script::SetProperty(rq, settings, "build_debug", 0);
#else
	Script::SetProperty(rq, settings, "build_debug", 1);
#endif
	Script::SetProperty(rq, settings, "build_opengles", CONFIG2_GLES);

	Script::SetProperty(rq, settings, "build_datetime", std::string(__DATE__ " " __TIME__));
	Script::SetProperty(rq, settings, "build_version", std::wstring(build_version));

	Script::SetProperty(rq, settings, "build_msc", (int)MSC_VERSION);
	Script::SetProperty(rq, settings, "build_gcc", (int)GCC_VERSION);
	Script::SetProperty(rq, settings, "build_clang", (int)CLANG_VERSION);

	Script::SetProperty(rq, settings, "gfx_card", device->GetName());
	Script::SetProperty(rq, settings, "gfx_drv_ver", device->GetDriverInformation());
#if CONFIG2_AUDIO
	if (g_SoundManager)
	{
		Script::SetProperty(rq, settings, "snd_card", g_SoundManager->GetSoundCardNames());
		Script::SetProperty(rq, settings, "snd_drv_ver", g_SoundManager->GetOpenALVersion());
	}
#endif

	ReportLibraries(rq, settings);

	JS::RootedValue backendDeviceSettings(rq.cx);
	Script::CreateObject(rq, &backendDeviceSettings);

	device->Report(rq, backendDeviceSettings);
	Script::SetProperty(rq, settings, "renderer_backend", backendDeviceSettings);

	Script::SetProperty(rq, settings, "video_desktop_xres", g_VideoMode.GetDesktopXRes());
	Script::SetProperty(rq, settings, "video_desktop_yres", g_VideoMode.GetDesktopYRes());
	Script::SetProperty(rq, settings, "video_desktop_bpp", g_VideoMode.GetDesktopBPP());
	Script::SetProperty(rq, settings, "video_desktop_freq", g_VideoMode.GetDesktopFreq());

	Script::SetProperty(rq, settings, "uname_sysname", std::string(un.sysname));
	Script::SetProperty(rq, settings, "uname_release", std::string(un.release));
	Script::SetProperty(rq, settings, "uname_version", std::string(un.version));
	Script::SetProperty(rq, settings, "uname_machine", std::string(un.machine));

#if OS_LINUX
	{
		std::ifstream ifs("/etc/lsb-release");
		if (ifs.good())
		{
			std::string str((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
			Script::SetProperty(rq, settings, "linux_release", str);
		}
	}
#endif

	Script::SetProperty(rq, settings, "cpu_identifier", std::string(cpu_IdentifierString()));
	Script::SetProperty(rq, settings, "cpu_frequency", os_cpu_ClockFrequency());
	Script::SetProperty(rq, settings, "cpu_pagesize", (u32)os_cpu_PageSize());
	Script::SetProperty(rq, settings, "cpu_largepagesize", (u32)os_cpu_LargePageSize());
	Script::SetProperty(rq, settings, "cpu_numprocs", (u32)os_cpu_NumProcessors());

	Script::SetProperty(rq, settings, "numa_numnodes", (u32)numa_NumNodes());
	Script::SetProperty(rq, settings, "numa_factor", numa_Factor());
	Script::SetProperty(rq, settings, "numa_interleaved", numa_IsMemoryInterleaved());

	Script::SetProperty(rq, settings, "ram_total", (u32)os_cpu_MemorySize());
	Script::SetProperty(rq, settings, "ram_total_os", (u32)os_cpu_QueryMemorySize());

#if ARCH_X86_X64
	Script::SetProperty(rq, settings, "x86_vendor", (u32)x86_x64::Vendor());
	Script::SetProperty(rq, settings, "x86_model", (u32)x86_x64::Model());
	Script::SetProperty(rq, settings, "x86_family", (u32)x86_x64::Family());

	u32 caps0, caps1, caps2, caps3;
	x86_x64::GetCapBits(&caps0, &caps1, &caps2, &caps3);
	Script::SetProperty(rq, settings, "x86_caps[0]", caps0);
	Script::SetProperty(rq, settings, "x86_caps[1]", caps1);
	Script::SetProperty(rq, settings, "x86_caps[2]", caps2);
	Script::SetProperty(rq, settings, "x86_caps[3]", caps3);
#endif

	Script::SetProperty(rq, settings, "timer_resolution", timer_Resolution());

	Script::SetProperty(rq, settings, "hardware_concurrency", std::thread::hardware_concurrency());
	Script::SetProperty(rq, settings, "random_device_entropy", std::random_device{}.entropy());
	Script::SetProperty(rq, settings, "neon", static_cast<int>(SDL_HasNEON()));

	// The version should be increased for every meaningful change.
	const int reportVersion = 23;

	// Send the same data to the reporting system
	g_UserReporter.SubmitReport(
		"hwdetect",
		reportVersion,
		Script::StringifyJSON(rq, &settings, false),
		Script::StringifyJSON(rq, &settings, true));

	// Run the detection script:
	JS::RootedValue global(rq.cx, rq.globalValue());
	ScriptFunction::CallVoid(rq, global, "RunHardwareDetection", settings);
}
