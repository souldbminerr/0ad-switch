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

#include "ModuleLoader.h"

#include "js/Modules.h"
#include "lib/file/file_system.h"
#include "lib/file/vfs/vfs.h"
#include "lib/os_path.h"
#include "lib/status.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/Errors.h"
#include "ps/Filesystem.h"
#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/ScriptConversions.h"
#include "scriptinterface/ScriptExceptions.h"
#include "scriptinterface/ScriptInterface.h"
#include "scriptinterface/ScriptRequest.h"

#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/CompileOptions.h>
#include <js/Object.h>
#include <js/Promise.h>
#include <js/SourceText.h>
#include <js/Value.h>
#include <jsapi.h>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>

class JSObject;
namespace mozilla { union Utf8Unit; }
struct JSContext;

namespace Script
{
namespace
{
/**
 * When provided with an appendix name (containing a "~" and ending with
 * ".append.js") the name of the base file is returned. When it's not an
 * appendix name an empty string is returned. E.g.
 * "base_file~mod_name.append.js" -> "base_file.js"
 * "base-name~0.append.js" -> "base-name.js"
 * "base_file~mod_name.js" -> ""
 * "base_file_mod_name.append.js" -> ""
 */
VfsPath GetBaseFilename(const VfsPath& filename)
{
	constexpr std::string_view appendixExtension{".append.js"};
	const std::string nameString{filename.string8()};
	if (nameString.size() < appendixExtension.size())
		return {};

	if (nameString.substr(nameString.size() - appendixExtension.size()) != appendixExtension)
		return {};

	const size_t pos{nameString.find('~')};
	if (pos == std::string::npos)
		return {};

	return nameString.substr(0, pos) + ".js";
}

[[nodiscard]] std::vector<VfsPath> GetAppendices(const VfsPath& baseFilepath)
{
	const VfsPath directory{baseFilepath.Parent()};
	CFileInfos fileInfos;
	if (g_VFS->GetDirectoryEntries(baseFilepath, &fileInfos, nullptr) != INFO::OK)
	{
		throw std::runtime_error{fmt::format("Unable to load files in directory: \"{}\"",
			directory.string8())};
	}

	std::vector<VfsPath> filenames;
	std::transform(fileInfos.begin(), fileInfos.end(), std::back_inserter(filenames),
		[](const CFileInfo fileInfo)
		{
			return fileInfo.Name();
		});

	const VfsPath baseFilename{baseFilepath.Filename()};
	const auto endPoint = std::remove_if(filenames.begin(), filenames.end(), [&](const VfsPath& filename)
	{
		const VfsPath base{GetBaseFilename(filename)};
		return base != baseFilename;
	});
	filenames.erase(endPoint, filenames.end());

	for (VfsPath& filename : filenames)
		filename = directory / filename;
	return filenames;
}

[[nodiscard]] std::string GetCode(const ModuleLoader::AllowModuleFunc& allowModule,
	const VfsPath& filePath)
{
	if (!allowModule || !allowModule(filePath))
	{
		throw std::runtime_error{fmt::format("Importing file \"{}\" is disallowed.",
			filePath.string8())};
	}
	if (!VfsFileExists(filePath))
		throw std::runtime_error{fmt::format("The file \"{}\" does not exist.", filePath.string8())};

	if (filePath.Extension() != L".js")
	{
		throw std::runtime_error{fmt::format("The file \"{}\" is not a JavaScript module.",
			filePath.string8())};
	}

	CVFSFile file;
	const PSRETURN ret{file.Load(g_VFS, filePath)};
	if (ret != PSRETURN_OK)
	{
		throw std::runtime_error{fmt::format("Failed to load file \"{}\": {}.", filePath.string8(),
			GetErrorString(ret))};
	}

	return file.DecodeUTF8();
}

template<typename Requester>
[[nodiscard]] JSObject* CompileModule(const ScriptRequest& rq,
	const ModuleLoader::AllowModuleFunc& allowModule, ModuleLoader::RegistryType& registry,
	const VfsPath& filePath, Requester&& requester)
{
	const VfsPath normalizedPath{filePath.fileSystemPath().lexically_normal().generic_string()};
	const auto insertResult = registry.try_emplace(normalizedPath, rq, allowModule, normalizedPath);
	ModuleLoader::CompiledModule& compiledModule{std::get<1>(*std::get<0>(insertResult))};
	compiledModule.AddRequester(std::forward<Requester>(requester));
	return compiledModule.m_ModuleObject;
}
[[nodiscard]] JSObject* Resolve(const ScriptRequest& rq, const ModuleLoader::AllowModuleFunc& allowModule,
	ModuleLoader::RegistryType& registry, JS::HandleValue referencingModule,
	JS::HandleObject moduleRequest)
{
	std::string includeString;
	const JS::RootedValue pathValue{rq.cx,
		JS::StringValue(JS::GetModuleRequestSpecifier(rq.cx, moduleRequest))};
	if (!Script::FromJSVal(rq, pathValue, includeString))
		throw std::logic_error{"The module-name to import isn't a string."};

	std::string includingModule;
	if (!Script::FromJSProperty(rq, referencingModule, "path", includingModule))
		throw std::logic_error{"The importing module doesn't have a \"path\" property."};

	return CompileModule(rq, allowModule, registry, includeString, includingModule);
}

[[nodiscard]] JSObject* Evaluate(const ScriptRequest& rq, JS::HandleObject mod)
{
	if (!JS::ModuleLink(rq.cx, mod))
	{
		ScriptException::CatchPending(rq);
		throw std::invalid_argument{"Unable to link module."};
	}

	JS::RootedValue val{rq.cx};
	if (!JS::ModuleEvaluate(rq.cx, mod, &val) || !val.isObject())
	{
		ScriptException::CatchPending(rq);
		throw std::invalid_argument{"Unable to evaluate module."};
	}

	return &val.toObject();
}

Status FileChangedHook(void* param, const VfsPath& changedFile)
{
	ModuleLoader::RegistryType& registry{*static_cast<ModuleLoader::RegistryType*>(param)};

	const VfsPath proposedBasePath{GetBaseFilename(changedFile)};

	std::vector<VfsPath> modulesToErase{proposedBasePath.empty() ? changedFile : proposedBasePath};
	std::vector<std::reference_wrapper<ModuleLoader::Result>> queries;
	while (!modulesToErase.empty())
	{
		const VfsPath path{modulesToErase.back()};
		modulesToErase.pop_back();
		const VfsPath pathWithExtension{path.ChangeExtension(".js")};
		const auto it = registry.find(pathWithExtension);
		if (it == registry.end())
			continue;

		ModuleLoader::CompiledModule compiledModule{std::move(std::get<1>(*it))};
		registry.erase(it);

		const auto [additionalModules, callbacks] = compiledModule.GetRequesters();
		modulesToErase.insert(modulesToErase.end(),
			additionalModules.begin(), additionalModules.end());

		queries.insert(queries.end(), callbacks.begin(), callbacks.end());
	}

	for (ModuleLoader::Result& result : queries)
		result.Resume();

	return INFO::OK;
}

template<bool reject>
bool Call(JSContext* cx, const unsigned argc, JS::Value* vp)
{
	JS::CallArgs args{JS::CallArgsFromVp(argc, vp)};
	const ScriptRequest rq{cx};

	const auto statusPtr{JS::GetMaybePtrFromReservedSlot<ModuleLoader::Future::Status>(
		&args.callee(), 0)};
	if (!statusPtr)
		return true;

	auto& status = *statusPtr;

	if (reject)
	{
		JS::HandleValue error{args.get(0)};
		std::string asString;
		ScriptFunction::Call(rq, error, "toString", asString);
		std::string stack;
		Script::GetProperty(rq, error, "stack", stack);
		status = ModuleLoader::Future::Rejected{std::make_exception_ptr(std::runtime_error{
			asString + '\n' + stack})};
		return true;
	}

	const auto evaluatingStatus{std::get_if<ModuleLoader::Future::Evaluating>(&status)};
	if (!evaluatingStatus)
	{
		status = ModuleLoader::Future::Rejected{std::make_exception_ptr(std::runtime_error{
			"Future is not Pending."})};
		return true;
	}
	status = ModuleLoader::Future::Fulfilled{evaluatingStatus->moduleNamespace};
	return true;
}

template<bool reject>
constexpr JSClassOps callbackClassOps{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
	/*call =*/Call<reject>, nullptr, nullptr};

template<bool reject>
constexpr JSClass callbackClass{"Callback", JSCLASS_HAS_RESERVED_SLOTS(1), &callbackClassOps<reject>};
} // anonymous namespace

ModuleLoader::CompiledModule::CompiledModule(const ScriptRequest& rq, const AllowModuleFunc& allowModule,
	const VfsPath& filePath):
	m_ModuleObject(rq.cx)
{
	const std::vector<VfsPath> appendices{GetAppendices(filePath)};
	const std::string code{std::accumulate(appendices.begin(), appendices.end(),
		GetCode(allowModule, filePath),
		[&](std::string code, const VfsPath& fileToAppend)
		{
			return std::move(code) + GetCode(allowModule, fileToAppend);
		})};

	JS::CompileOptions options{rq.cx};
	const std::string filePathStr{filePath.string8()};
	options.setFileAndLine(filePathStr.c_str(), 1);

	JS::SourceText<mozilla::Utf8Unit> src;
	if (!src.init(rq.cx, code.c_str(), code.length(), JS::SourceOwnership::Borrowed))
		throw std::invalid_argument{fmt::format("Unable to read code file: \"{}\".", filePathStr)};

	m_ModuleObject = JS::CompileModule(rq.cx, options, src);

	if (!m_ModuleObject)
	{
		ScriptException::CatchPending(rq);
		throw std::invalid_argument{fmt::format("Unable to compile module: \"{}\".",
			filePathStr)};
	}

	JS::RootedValue modInfo{rq.cx};
	Script::CreateObject(rq, &modInfo, "path", filePathStr);
	JS::SetModulePrivate(m_ModuleObject, modInfo);
}

[[nodiscard]] std::tuple<const std::vector<VfsPath>&,
	const std::vector<std::reference_wrapper<ModuleLoader::Result>>&>
	ModuleLoader::CompiledModule::GetRequesters() const
{
	return {m_Importer, m_Callbacks};
}

void ModuleLoader::CompiledModule::AddRequester(VfsPath importer)
{
	m_Importer.push_back(std::move(importer));
}

void ModuleLoader::CompiledModule::AddRequester(Result& callback)
{
	m_Callbacks.push_back(callback);
}

void ModuleLoader::CompiledModule::RemoveRequester(Result* toErase)
{
	m_Callbacks.erase(std::remove_if(m_Callbacks.begin(), m_Callbacks.end(),
		[&](Result& elem)
		{
			return &elem == toErase;
		}), m_Callbacks.end());
}

ModuleLoader::Future::Future(const ScriptRequest& rq, ModuleLoader& loader, Result& result,
	VfsPath modulePath):
	m_Status{Evaluating{{rq.cx, nullptr}, {rq.cx, JS_NewObject(rq.cx, &callbackClass<false>)},
		{rq.cx, JS_NewObject(rq.cx, &callbackClass<true>)}}}
{
	// It's possible to access exported values before the complete module is evaluated (whenever
	// something is `export`-ed before a top-level `await`).
	// Those "partial" module namespaces are not exposed for the following reasons:
	// - The use case for them is too limited.
	// - JS developers are used to getting either a complete namespace or nothing.
	// - Accessing values which are not yet exported results in an error. These errors might implicitly be
	//	dropped.

	JS::RootedObject mod{rq.cx, CompileModule(rq, loader.m_AllowModule, loader.m_Registry, modulePath,
		result)};
	JS::RootedObject promise{rq.cx, Evaluate(rq, mod)};
	Evaluating& evaluatingStatus{std::get<Evaluating>(m_Status)};
	evaluatingStatus.moduleNamespace = JS::GetModuleNamespace(rq.cx, mod);

	SetReservedSlot(JS::PrivateValue(static_cast<void*>(&m_Status)));

	if (!JS::AddPromiseReactions(rq.cx, promise, evaluatingStatus.fulfill, evaluatingStatus.reject))
		throw std::runtime_error{"Failed adding promise reaction."};
}

ModuleLoader::Future::Future(Future&& other) noexcept:
	m_Status{std::exchange(other.m_Status, Invalid{})}
{
	SetReservedSlot(JS::PrivateValue(static_cast<void*>(&m_Status)));
}

ModuleLoader::Future& ModuleLoader::Future::operator=(Future&& other) noexcept
{
	SetReservedSlot(JS::UndefinedValue());
	m_Status = std::exchange(other.m_Status, Invalid{});
	SetReservedSlot(JS::PrivateValue(static_cast<void*>(&m_Status)));

	return *this;
}

ModuleLoader::Future::~Future()
{
	SetReservedSlot(JS::UndefinedValue());
}

[[nodiscard]] bool ModuleLoader::Future::IsDone() const noexcept
{
	return std::holds_alternative<Fulfilled>(m_Status) || std::holds_alternative<Rejected>(m_Status);
}

[[nodiscard]] JSObject* ModuleLoader::Future::Get()
{
	if (std::holds_alternative<Fulfilled>(m_Status))
		return std::get<Fulfilled>(std::exchange(m_Status, Invalid{})).moduleNamespace;
	std::exception_ptr error{std::move(std::get<Rejected>(m_Status).error)};
	m_Status = Invalid{};
	std::rethrow_exception(std::move(error));
}

[[nodiscard]] bool ModuleLoader::Future::IsWaiting() const noexcept
{
	return std::holds_alternative<WaitingForFileChange>(m_Status);
}

void ModuleLoader::Future::SetWaiting() noexcept
{
	m_Status.emplace<WaitingForFileChange>();
}

void ModuleLoader::Future::SetReservedSlot(JS::Value privateValue) noexcept
{
	Evaluating* evaluatingStatus{std::get_if<Evaluating>(&m_Status)};
	if (!evaluatingStatus)
		return;
	if (evaluatingStatus->fulfill)
		JS::SetReservedSlot(evaluatingStatus->fulfill, 0, privateValue);
	if (evaluatingStatus->reject)
		JS::SetReservedSlot(evaluatingStatus->reject, 0, privateValue);
}

ModuleLoader::Result::iterator::iterator(Result& backReference):
	backRef{&backReference}
{}

[[nodiscard]] ModuleLoader::Future& ModuleLoader::Result::iterator::operator*() const
{
	return backRef->m_Storage;
}

[[nodiscard]] ModuleLoader::Future* ModuleLoader::Result::iterator::operator->() const
{
	return &(**this);
}

ModuleLoader::Result::iterator& ModuleLoader::Result::iterator::operator++()
{
	backRef->m_Storage.SetWaiting();
	return *this;
}

ModuleLoader::Result::iterator& ModuleLoader::Result::iterator::operator++(int)
{
	++(*this);
	// All iterator of this `LoadModuleResult` refere to the same `LoadModuleResult`.
	return *this;
}

[[nodiscard]] bool ModuleLoader::Result::iterator::operator==(const iterator&) const
{
	return false;
}

[[nodiscard]] bool ModuleLoader::Result::iterator::operator!=(const iterator&) const
{
	return true;
}

ModuleLoader::Result::Result(const ScriptRequest& rq, const VfsPath& modulePath):
	m_Script{rq.GetScriptInterface()},
	m_ModulePath{modulePath},
	m_Storage{rq, m_Script.GetModuleLoader(), *this, m_ModulePath}
{
}

ModuleLoader::Result::~Result()
{
	ModuleLoader::RegistryType& registry{m_Script.GetModuleLoader().m_Registry};
	const auto modIter = registry.find(m_ModulePath);
	if (modIter == registry.end())
		return;

	std::get<1>(*modIter).RemoveRequester(this);
}

[[nodiscard]] ModuleLoader::Result::iterator ModuleLoader::Result::begin() noexcept
{
	return ModuleLoader::Result::iterator{*this};
}

[[nodiscard]] ModuleLoader::Result::iterator ModuleLoader::Result::end() const noexcept
{
	return ModuleLoader::Result::iterator{};
}

void ModuleLoader::Result::Resume()
{
	if (m_Storage.IsWaiting())
		m_Storage = ModuleLoader::Future{m_Script, m_Script.GetModuleLoader(), *this, m_ModulePath};
}

ModuleLoader::ModuleLoader(ModuleLoader::AllowModuleFunc allowModule):
	m_AllowModule{std::move(allowModule)}
{
	RegisterFileReloadFunc(FileChangedHook, static_cast<void*>(&m_Registry));
}

ModuleLoader::~ModuleLoader()
{
	UnregisterFileReloadFunc(FileChangedHook, static_cast<void*>(&m_Registry));
}

[[nodiscard]] ModuleLoader::Result ModuleLoader::LoadModule(const ScriptRequest& rq,
	const VfsPath& modulePath)
{
	return Result{rq, modulePath};
}

/**
 * This is only executed once per module. Following accesses of `import.meta`
 * evaluate to the same object.
 */
[[nodiscard]] bool ModuleLoader::MetadataHook(JSContext* cx, JS::HandleValue privateValue,
	JS::HandleObject metaObject) noexcept
{
	const ScriptRequest rq{cx};

	JS::RootedValue path{cx};
	if (!Script::GetProperty(rq, privateValue, "path", &path))
		return false;

	JS::RootedValue metaValue{cx, JS::ObjectValue(*metaObject)};
	if (!Script::SetProperty(rq, metaValue, "path", path))
		return false;

	return true;
}

[[nodiscard]] JSObject* ModuleLoader::ResolveHook(JSContext* cx, JS::HandleValue referencingPrivate,
	JS::HandleObject request) noexcept
{
	try
	{
		const ScriptRequest rq{cx};
		ModuleLoader& loader{rq.GetScriptInterface().GetModuleLoader()};
		return Resolve(rq, loader.m_AllowModule, loader.m_Registry, referencingPrivate, request);
	}
	catch (const std::exception& e)
	{
		LOGERROR("%s", e.what());
		return nullptr;
	}
	catch (...)
	{
		LOGERROR("Error compiling module.");
		return nullptr;
	}
}

[[nodiscard]] bool ModuleLoader::DynamicImportHook(JSContext* cx, JS::HandleValue referencingPrivate,
	JS::HandleObject moduleRequest, JS::HandleObject promise) noexcept
{
	const ScriptRequest rq{cx};
	try
	{
		ModuleLoader& loader{rq.GetScriptInterface().GetModuleLoader()};
		JS::RootedObject mod{rq.cx, Resolve(rq, loader.m_AllowModule, loader.m_Registry,
			referencingPrivate, moduleRequest)};
		JS::RootedObject evaluationPromise{rq.cx, Evaluate(rq, mod)};
		return JS::FinishDynamicModuleImport(rq.cx, evaluationPromise, referencingPrivate,
			moduleRequest, promise);
	}
	catch (const std::exception& e)
	{
		LOGERROR("%s", e.what());
		return JS::FinishDynamicModuleImport(rq.cx, nullptr, referencingPrivate, moduleRequest,
			promise);
	}
	catch (...)
	{
		return JS::FinishDynamicModuleImport(rq.cx, nullptr, referencingPrivate, moduleRequest,
			promise);
	}
}
} // namespace Script
