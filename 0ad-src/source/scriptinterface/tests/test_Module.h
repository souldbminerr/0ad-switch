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

#include "lib/file/vfs/vfs.h"
#include "lib/os_path.h"
#include "lib/path.h"
#include "lib/status.h"
#include "lib/sysdep/dir_watch.h"
#include "lib/sysdep/os.h"
#include "ps/CLogger.h"
#include "ps/Filesystem.h"
#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/ModuleLoader.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/ScriptContext.h"
#include "scriptinterface/ScriptConversions.h"
#include "scriptinterface/ScriptInterface.h"
#include "scriptinterface/ScriptRequest.h"

#include <exception>
#include <functional>
#include <js/Promise.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

#if OS_MACOSX
#include <CoreFoundation/CoreFoundation.h>
#endif
#if OS_LINUX
#include <cstdlib>
#endif
#if OS_WIN || OS_WIN64 || OS_MACOSX
#include <filesystem>
#endif

Status wdir_watch_Init();
Status wdir_watch_Shutdown();

namespace
{
void ClearFromCache(const VfsPath& path)
{
#if OS_BSD && !OS_MACOSX
	TS_SKIP("On BSD hotload isn't implemented.");
#endif

	OsPath file;
	if (g_VFS->GetRealPath(path, file) != INFO::OK)
		throw std::exception{};
	PDirWatch dirWatch;
	dir_watch_Add((file.Parent() / "").string8(), dirWatch);

#if OS_WIN || OS_WIN64 || OS_MACOSX
	std::filesystem::last_write_time(file.string8(), std::filesystem::file_time_type::clock::now());
#endif

#if OS_LINUX
	// On Linux only this aproach seems to trigger a file reload.
	if (std::system(("touch " + file.string8()).c_str()) != 0)
		throw std::runtime_error{"`touch` didn't work."};
#endif

	Status status{INFO::SKIPPED};
	while (status == INFO::SKIPPED)
	{
		status = ReloadChangedFiles();
#if OS_MACOSX
		// Console apps don't have a run loop, so we need to wait
		// a bit for the file watcher to catch up.
		CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
#endif
	}

	TS_ASSERT_OK(status);
}

bool AllowAllPredicate(const VfsPath&)
{
	return true;
}

bool DisallowedfilePredicate(const VfsPath& path)
{
	return path != "restriction/disallowedfile.js";
}
}

class TestScriptModule : public CxxTest::TestSuite
{
public:
	void setUp()
	{
		if constexpr (OS_WIN)
			wdir_watch_Init();

		g_VFS = CreateVfs();
		TS_ASSERT_OK(g_VFS->Mount(L"", DataDir() / "mods" / "_test.scriptinterface" / "module" / "",
			VFS_MOUNT_MUST_EXIST));
	}

	void tearDown()
	{
		g_VFS.reset();

		if constexpr (OS_WIN)
			wdir_watch_Shutdown();
	}

	void test_StaticImport()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		TestLogger logger;
		std::ignore = script.GetModuleLoader().LoadModule(rq, "include/entry.js");

		// This test does not rely on export to the engine. So use the logger to check if it succeeded.
		TS_ASSERT_STR_CONTAINS(logger.GetOutput(),"Test succeeded");
	}

	void test_Sequential()
	{
		{
			ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
			const ScriptRequest rq{script};
			std::ignore = script.GetModuleLoader().LoadModule(rq, "empty.js");
		}
		{
			ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
			const ScriptRequest rq{script};
			std::ignore = script.GetModuleLoader().LoadModule(rq, "empty.js");
		}
	}

	void test_Stacked()
	{
		ScriptInterface scriptOuter{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rqOuter{scriptOuter};
		{
			ScriptInterface scriptInner{"Test", "Test", g_ScriptContext, AllowAllPredicate};
			const ScriptRequest rqInner{scriptInner};
			std::ignore = scriptInner.GetModuleLoader().LoadModule(rqInner, "empty.js");
		}
		std::ignore = scriptOuter.GetModuleLoader().LoadModule(rqOuter, "empty.js");
	}

	void test_ImportInFunction()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		TestLogger logger;
		TS_ASSERT_THROWS(std::ignore = script.GetModuleLoader().LoadModule(rq,
			"import_inside_function.js"), const std::invalid_argument&);
		const std::string log{logger.GetOutput()};
		TS_ASSERT_STR_CONTAINS(log, "import_inside_function.js line 3");
		TS_ASSERT_STR_CONTAINS(log, "import declarations may only appear at top level of a module");
	}

	void test_NonExistent()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		const TestLogger _;
		TS_ASSERT_THROWS(std::ignore = script.GetModuleLoader().LoadModule(rq, "nonexistent.js"),
			const std::runtime_error&);
	}

	void test_EvaluateOnce()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		{
			TestLogger logger;
			std::ignore = script.GetModuleLoader().LoadModule(rq, "blabbermouth.js");
			TS_ASSERT_STR_CONTAINS(logger.GetOutput(), "blah blah blah");
		}
		{
			TestLogger logger;
			std::ignore = script.GetModuleLoader().LoadModule(rq, "include/../blabbermouth.js");
			TS_ASSERT_STR_NOT_CONTAINS(logger.GetOutput(), "blah blah blah");
		}
	}

	void test_TopLevelAwaitFinite()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};
		auto result = script.GetModuleLoader().LoadModule(rq, "top_level_await_finite.js");

		auto& future = *result.begin();
		TS_ASSERT(!future.IsDone());
		g_ScriptContext->RunJobs();
		TS_ASSERT(future.IsDone());
		std::ignore = future.Get();
	}

	void test_TopLevelAwaitInfinite()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		auto result = script.GetModuleLoader().LoadModule(rq, "top_level_await_infinite.js");

		auto& future = *result.begin();
		g_ScriptContext->RunJobs();
		TS_ASSERT(!future.IsDone());
		TS_ASSERT_THROWS_ANYTHING(std::ignore = future.Get());
	}

	void test_MoveFulfilledFuture()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		auto result{script.GetModuleLoader().LoadModule(rq, "empty.js")};
		Script::ModuleLoader::Future& future0{*result.begin()};

		g_ScriptContext->RunJobs();
		TS_ASSERT(future0.IsDone());

		Script::ModuleLoader::Future future1{std::move(future0)};
		Script::ModuleLoader::Future future2;
		future2 = std::move(future1);

		TS_ASSERT(!future0.IsDone());
		TS_ASSERT(!future1.IsDone());
		TS_ASSERT(future2.IsDone());
	}

	void test_MoveEvaluatingFuture()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		auto result{script.GetModuleLoader().LoadModule(rq, "top_level_await_finite.js")};
		Script::ModuleLoader::Future& future0{*result.begin()};

		Script::ModuleLoader::Future future1{std::move(future0)};
		Script::ModuleLoader::Future future2;
		future2 = std::move(future1);

		TS_ASSERT(!future0.IsDone());
		TS_ASSERT(!future1.IsDone());
		TS_ASSERT(!future2.IsDone());
		g_ScriptContext->RunJobs();
		TS_ASSERT(!future0.IsDone());
		TS_ASSERT(!future1.IsDone());
		TS_ASSERT(future2.IsDone());
	}

	void test_EvaluateReplacedFuture()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		TestLogger logger;
		auto blabbermouthResult{script.GetModuleLoader().LoadModule(rq, "delayed_blabbermouth.js")};
		TS_ASSERT_STR_NOT_CONTAINS(logger.GetOutput(), "blah blah blah");
		auto future = std::move(*blabbermouthResult.begin());
		TS_ASSERT(!future.IsDone());

		auto emptyResult{script.GetModuleLoader().LoadModule(rq, "empty.js")};
		future = std::move(*emptyResult.begin());
		TS_ASSERT(!future.IsDone());

		g_ScriptContext->RunJobs();
		TS_ASSERT(future.IsDone());
		TS_ASSERT_STR_CONTAINS(logger.GetOutput(), "blah blah blah");
	}

	void test_TopLevelThrow()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		// To silence the error.
		const TestLogger _;
		auto result = script.GetModuleLoader().LoadModule(rq, "top_level_throw.js");

		auto& future = *result.begin();
		g_ScriptContext->RunJobs();
		TS_ASSERT(future.IsDone());
		TS_ASSERT_THROWS_EQUALS(std::ignore = future.Get(), const std::runtime_error& e, e.what(),
			"Error: Test reason\n@top_level_throw.js:1:7\n");
	}

	void test_Export()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		auto result = script.GetModuleLoader().LoadModule(rq, "export.js");
		g_ScriptContext->RunJobs();

		auto& future = *result.begin();
		JS::RootedObject ns{rq.cx, future.Get()};
		JS::RootedValue moduleValue{rq.cx, JS::ObjectValue(*ns)};

		{
			int value{0};
			TS_ASSERT(Script::GetProperty(rq, moduleValue, "value", value));
			TS_ASSERT_EQUALS(value, 6);
		}

		TS_ASSERT(ScriptFunction::CallVoid(rq, moduleValue, "mutate", 12));

		{
			int value{0};
			TS_ASSERT(Script::GetProperty(rq, moduleValue, "value", value));
			TS_ASSERT_EQUALS(value, 12);
		}
	}

	void test_ExportSame()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		{
			auto result = script.GetModuleLoader().LoadModule(rq, "export.js");
			g_ScriptContext->RunJobs();
			JS::RootedObject ns{rq.cx, result.begin()->Get()};
			JS::RootedValue moduleValue{rq.cx, JS::ObjectValue(*ns)};
			TS_ASSERT(ScriptFunction::CallVoid(rq, moduleValue, "mutate", 12));
		}

		{
			auto result = script.GetModuleLoader().LoadModule(rq, "include/../export.js");
			g_ScriptContext->RunJobs();
			JS::RootedObject ns{rq.cx, result.begin()->Get()};
			JS::RootedValue moduleValue{rq.cx, JS::ObjectValue(*ns)};
			int value{0};
			TS_ASSERT(Script::GetProperty(rq, moduleValue, "value", value));
			TS_ASSERT_EQUALS(value, 12);
		}
	}

	void test_ExportIndirect()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		{
			auto result = script.GetModuleLoader().LoadModule(rq, "export.js");
			g_ScriptContext->RunJobs();
			JS::RootedObject ns{rq.cx, result.begin()->Get()};
			JS::RootedValue moduleValue{rq.cx, JS::ObjectValue(*ns)};
			TS_ASSERT(ScriptFunction::CallVoid(rq, moduleValue, "mutate", 12));
		}

		{
			auto result = script.GetModuleLoader().LoadModule(rq, "indirect.js");
			g_ScriptContext->RunJobs();
			JS::RootedObject ns{rq.cx, result.begin()->Get()};
			JS::RootedValue moduleValue{rq.cx, JS::ObjectValue(*ns)};
			int value{0};
			TS_ASSERT(Script::GetProperty(rq, moduleValue, "value", value));
			TS_ASSERT_EQUALS(value, 12);
		}
	}

	void test_ExportDefaultImmutable()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		auto result = script.GetModuleLoader().LoadModule(rq, "export_default/immutable.js");
		g_ScriptContext->RunJobs();

		auto& future = *result.begin();
		JS::RootedObject ns{rq.cx, future.Get()};
		JS::RootedValue moduleValue{rq.cx, JS::ObjectValue(*ns)};

		int value{0};
		TS_ASSERT(Script::GetProperty(rq, moduleValue, "default", value));
		TS_ASSERT_EQUALS(value, 36);
	}

	void test_ExportDefaultInvalid()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		TestLogger logger;
		TS_ASSERT_THROWS(std::ignore = script.GetModuleLoader().LoadModule(rq,
			"export_default/invalid.js"), const std::invalid_argument&);
		const std::string log{logger.GetOutput()};
		TS_ASSERT_STR_CONTAINS(log, "export_default/invalid.js line 1");
	}

	void test_ExportDefaultDoesNotWorkAround()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		auto result = script.GetModuleLoader().LoadModule(rq, "export_default/does_not_work_around.js");

		g_ScriptContext->RunJobs();

		auto& future = *result.begin();
		JS::RootedObject ns{rq.cx, future.Get()};
		JS::RootedValue moduleValue{rq.cx, JS::ObjectValue(*ns)};

		int value{0};
		TS_ASSERT(Script::GetProperty(rq, moduleValue, "default", value));
		TS_ASSERT_DIFFERS(value, 36);
		TS_ASSERT_EQUALS(value, 6);
	}

	void test_ExportDefaultWorksAround()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		auto result = script.GetModuleLoader().LoadModule(rq, "export_default/works_around.js");
		g_ScriptContext->RunJobs();

		auto& future = *result.begin();
		JS::RootedObject ns{rq.cx, future.Get()};
		JS::RootedValue moduleValue{rq.cx, JS::ObjectValue(*ns)};

		int value{0};
		TS_ASSERT(Script::GetProperty(rq, moduleValue, "default", value));
		TS_ASSERT_EQUALS(value, 36);
	}

	void test_ReplaceEvaluatingFuture()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		auto awaitResult = script.GetModuleLoader().LoadModule(rq, "top_level_await_finite.js");
		auto future = std::move(*awaitResult.begin());
		auto exportResult = script.GetModuleLoader().LoadModule(rq, "export.js");
		future = std::move(*exportResult.begin());

		g_ScriptContext->RunJobs();
		JS::RootedObject ns{rq.cx, future.Get()};
		JS::RootedValue moduleValue{rq.cx, JS::ObjectValue(*ns)};

		int value{0};
		TS_ASSERT(Script::GetProperty(rq, moduleValue, "value", value));
		TS_ASSERT_EQUALS(value, 6);
	}

	void test_DynamicImport()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		auto result = script.GetModuleLoader().LoadModule(rq, "dynamic_import.js");
		g_ScriptContext->RunJobs();

		auto& future = *result.begin();
		JS::RootedObject ns{rq.cx, future.Get()};
		JS::RootedValue moduleValue{rq.cx, JS::ObjectValue(*ns)};

		JS::RootedValue promise{rq.cx};
		TS_ASSERT(ScriptFunction::Call(rq, moduleValue, "default", &promise));
		TS_ASSERT(promise.isObject());
		JS::RootedObject promiseObject{rq.cx, &promise.toObject()};
		TS_ASSERT(JS::IsPromiseObject(promiseObject));

		TS_ASSERT_EQUALS(JS::GetPromiseState(promiseObject), JS::PromiseState::Pending);
		g_ScriptContext->RunJobs();
		TS_ASSERT_EQUALS(JS::GetPromiseState(promiseObject), JS::PromiseState::Fulfilled);

		JS::RootedValue piModule{rq.cx, JS::GetPromiseResult(promiseObject)};

		double pi{0.0};
		TS_ASSERT(Script::FromJSProperty(rq, piModule, "default", pi));

		TS_ASSERT_LESS_THAN(pi, 3.1416);
		TS_ASSERT_LESS_THAN(3.1415, pi);
	}

	void test_Meta()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		auto result = script.GetModuleLoader().LoadModule(rq, "meta.js");
		g_ScriptContext->RunJobs();

		auto& future = *result.begin();
		JS::RootedObject ns{rq.cx, future.Get()};
		const JS::RootedValue modNamespace{rq.cx, JS::ObjectValue(*ns)};

		JS::RootedValue meta{rq.cx};
		TS_ASSERT(ScriptFunction::Call(rq, modNamespace, "getMeta", &meta));

		std::string path;
		TS_ASSERT(Script::GetProperty(rq, meta, "path", path));
		TS_ASSERT_STR_EQUALS(path, "meta.js");
	}

	void test_Modified()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		auto result = script.GetModuleLoader().LoadModule(rq, "modified/base.js");
		g_ScriptContext->RunJobs();

		auto& future = *result.begin();
		JS::RootedObject ns{rq.cx, future.Get()};
		JS::RootedValue moduleValue{rq.cx, JS::ObjectValue(*ns)};

		std::string returnValue;
		TS_ASSERT(ScriptFunction::Call(rq, moduleValue, "fn", returnValue));
		TS_ASSERT_STR_EQUALS(returnValue, "Base01");
	}

	void test_Hotload()
	{
		constexpr int goal{2};

		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		int counter{0};
		for (auto& future : script.GetModuleLoader().LoadModule(rq, "empty.js"))
		{
			TS_ASSERT(!future.IsDone());

			if (counter != 0)
				ClearFromCache("empty.js");

			g_ScriptContext->RunJobs();
			TS_ASSERT(future.IsDone());

			if (counter == goal)
				break;

			++counter;
		}

		TS_ASSERT_EQUALS(counter, goal);
	}

	void test_HotloadWithoutIncrement()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		auto result = script.GetModuleLoader().LoadModule(rq, "top_level_await_finite.js");
		g_ScriptContext->RunJobs();
		TS_ASSERT(result.begin()->IsDone());
		ClearFromCache("top_level_await_finite.js");
		TS_ASSERT(result.begin()->IsDone());
	}

	void test_HotloadIndipendence()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		// It's intended to be used as in the test above but it's easier to test when it's unrolled.
		auto result = script.GetModuleLoader().LoadModule(rq, "export.js");
		auto iter = result.begin();
		{
			auto& future = *iter;
			g_ScriptContext->RunJobs();
			JS::RootedObject ns{rq.cx, future.Get()};
			const JS::RootedValue moduleValue{rq.cx, JS::ObjectValue(*ns)};

			TS_ASSERT(ScriptFunction::CallVoid(rq, moduleValue, "mutate", 12));

			int value{0};
			TS_ASSERT(Script::GetProperty(rq, moduleValue, "value", value));
			TS_ASSERT_EQUALS(value, 12);
		}
		++iter;
		{
			auto& future = *iter;
			g_ScriptContext->RunJobs();
			TS_ASSERT(!future.IsDone());
			ClearFromCache("export.js");
			g_ScriptContext->RunJobs();
			TS_ASSERT(future.IsDone());

			JS::RootedObject ns{rq.cx, future.Get()};
			JS::RootedValue moduleValue{rq.cx, JS::ObjectValue(*ns)};

			int value{0};
			TS_ASSERT(Script::GetProperty(rq, moduleValue, "value", value));
			TS_ASSERT_DIFFERS(value, 12);
			TS_ASSERT_EQUALS(value, 6);
		}
	}

	void test_HotloadModified()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		auto result = script.GetModuleLoader().LoadModule(rq, "empty.js");
		auto iter = result.begin();
		g_ScriptContext->RunJobs();
		TS_ASSERT(iter->IsDone());

		++iter;
		TS_ASSERT(!iter->IsDone());

		ClearFromCache("empty~trigger.append.js");

		g_ScriptContext->RunJobs();
		TS_ASSERT(iter->IsDone());
	}

	void test_HotloadIndirect()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		const ScriptRequest rq{script};

		auto result = script.GetModuleLoader().LoadModule(rq, "indirect.js");
		auto iter = result.begin();
		g_ScriptContext->RunJobs();
		TS_ASSERT(iter->IsDone());

		++iter;
		ClearFromCache("export.js");

		g_ScriptContext->RunJobs();
		TS_ASSERT(iter->IsDone());
	}

	void test_HotloadUnobserved()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		{
			const ScriptRequest rq{script};

			TestLogger logger;
			auto result = script.GetModuleLoader().LoadModule(rq, "blabbermouth.js");
			g_ScriptContext->RunJobs();
			TS_ASSERT_STR_CONTAINS(logger.GetOutput(), "blah blah blah");
		}

		{
			TestLogger logger;
			ClearFromCache("blabbermouth.js");
			g_ScriptContext->RunJobs();
			TS_ASSERT_STR_NOT_CONTAINS(logger.GetOutput(), "blah blah blah");
		}

		{
			const ScriptRequest rq{script};

			TestLogger logger;
			auto result = script.GetModuleLoader().LoadModule(rq, "blabbermouth.js");
			g_ScriptContext->RunJobs();
			TS_ASSERT_STR_CONTAINS(logger.GetOutput(), "blah blah blah");
		}
	}

	void test_HotloadAfterResultDestruction()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		{
			const ScriptRequest rq{script};

			TestLogger logger;
			auto result = script.GetModuleLoader().LoadModule(rq, "blabbermouth.js");
			g_ScriptContext->RunJobs();
			TS_ASSERT_STR_CONTAINS(logger.GetOutput(), "blah blah blah");

			auto iter = result.begin();
			TS_ASSERT(iter->IsDone());
			++iter;
		}

		TestLogger logger;
		ClearFromCache("blabbermouth.js");
		g_ScriptContext->RunJobs();
		TS_ASSERT_STR_NOT_CONTAINS(logger.GetOutput(), "blah blah blah");
	}

	void test_HotloadAfterScriptRequestDestruction()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, AllowAllPredicate};
		auto result = script.GetModuleLoader().LoadModule(ScriptRequest{script}, "empty.js");
		g_ScriptContext->RunJobs();
		auto iter = result.begin();
		TS_ASSERT(iter->IsDone());

		++iter;
		ClearFromCache("empty.js");
		g_ScriptContext->RunJobs();
		TS_ASSERT(iter->IsDone());
	}

	void test_RestrictionNoPredicate()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext};
		const ScriptRequest rq{script};

		TS_ASSERT_THROWS_EQUALS(std::ignore = script.GetModuleLoader().LoadModule(rq,
			"empty.js"), const std::runtime_error& e, e.what(),
			"Importing file \"empty.js\" is disallowed.");
	}

	void test_RestrictionDirect()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, DisallowedfilePredicate};
		const ScriptRequest rq{script};

		TS_ASSERT_THROWS_EQUALS(std::ignore = script.GetModuleLoader().LoadModule(rq,
			"restriction/disallowedfile.js"), const std::runtime_error& e, e.what(),
			"Importing file \"restriction/disallowedfile.js\" is disallowed.");
	}

	void test_RestrictionIndirect()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, DisallowedfilePredicate};
		const ScriptRequest rq{script};
		TestLogger logger;
		TS_ASSERT_THROWS_EQUALS(std::ignore = script.GetModuleLoader().LoadModule(rq,
			"restriction/import.js"), const std::invalid_argument& e, e.what(),
			"Unable to link module.");
		TS_ASSERT_STR_CONTAINS(logger.GetOutput(),
			"Importing file \"restriction/disallowedfile.js\" is disallowed.");
	}

	void test_RestrictionFancy()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, DisallowedfilePredicate};
		const ScriptRequest rq{script};
		TestLogger logger;
		TS_ASSERT_THROWS_EQUALS(std::ignore = script.GetModuleLoader().LoadModule(rq,
			"restriction/fancy_import.js"), const std::invalid_argument& e, e.what(),
			"Unable to link module.");
		TS_ASSERT_STR_CONTAINS(logger.GetOutput(),
			"Importing file \"restriction/disallowedfile.js\" is disallowed.");
	}

	void test_RestrictionDynamic()
	{
		ScriptInterface script{"Test", "Test", g_ScriptContext, DisallowedfilePredicate};
		const ScriptRequest rq{script};

		TestLogger logger;
		auto result = script.GetModuleLoader().LoadModule(rq, "restriction/dynamic_import.js");
		TS_ASSERT_STR_CONTAINS(logger.GetOutput(),
			"Importing file \"restriction/disallowedfile.js\" is disallowed.");

		g_ScriptContext->RunJobs();
		TS_ASSERT_THROWS_ANYTHING(std::ignore = result.begin()->Get());
	}
};
