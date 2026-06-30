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

#ifndef INCLUDED_SCRIPTMODULELOADER
#define INCLUDED_SCRIPTMODULELOADER

#include "lib/file/vfs/vfs_path.h"
#include "lib/path.h"

#include <cstddef>
#include <exception>
#include <functional>
#include <iterator>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

class JSObject;
class ScriptContext;
class ScriptInterface;
class ScriptRequest;
namespace JS { class Value; }
struct JSContext;

namespace Script
{
class ModuleLoader
{
public:
	friend ScriptContext;

	class CompiledModule;
	class Future;
	class Result;

	using AllowModuleFunc = std::function<bool(const VfsPath&)>;
	using RegistryType = std::unordered_map<VfsPath, CompiledModule>;

	ModuleLoader(AllowModuleFunc allowModule);
	ModuleLoader(const ModuleLoader&) = delete;
	ModuleLoader& operator=(const ModuleLoader&) = delete;
	ModuleLoader(ModuleLoader&&) = delete;
	ModuleLoader& operator=(ModuleLoader&&) = delete;
	~ModuleLoader();

	/**
	 * Load the specified module and all module it imports recursively.
	 *
	 * @param rq @c globalThis is taken from this @c ScriptRequest.
	 * @param modulePath The path to the file which should be loaded as a
	 *	module.
	 * @return A range of futures. The compilation of the first future is
	 *	already started. The evaluation of the subsequent futures start once
	 *	the module file is edited.
	 */
	[[nodiscard]] Result LoadModule(const ScriptRequest& rq, const VfsPath& modulePath);

private:
	// Functions used by the `ScriptContext`.
	[[nodiscard]] static bool MetadataHook(JSContext* cx, JS::HandleValue privateValue,
		JS::HandleObject metaObject) noexcept;
	[[nodiscard]] static JSObject* ResolveHook(JSContext* cx, JS::HandleValue referencingPrivate,
		JS::HandleObject moduleRequest) noexcept;
	[[nodiscard]] static bool DynamicImportHook(JSContext* cx, JS::HandleValue referencingPrivate,
		JS::HandleObject moduleRequest, JS::HandleObject promise) noexcept;

	AllowModuleFunc m_AllowModule;
	RegistryType m_Registry;
};

class ModuleLoader::CompiledModule
{
public:
	CompiledModule(const ScriptRequest& rq, const AllowModuleFunc& allowModule, const VfsPath& filePath);

	std::tuple<const std::vector<VfsPath>&,
		const std::vector<std::reference_wrapper<Result>>&> GetRequesters() const;

	void AddRequester(VfsPath importer);
	void AddRequester(Result& callback);
	void RemoveRequester(Result* toErase);

	JS::PersistentRootedObject m_ModuleObject;
private:
	std::vector<VfsPath> m_Importer;
	std::vector<std::reference_wrapper<Result>> m_Callbacks;
};

/**
 * The future is fulfilled once the evaluation of the module
 * completes.
 * Note: The evaluation might not be started yet.
 */
class ModuleLoader::Future
{
	friend Result;
public:
	struct Evaluating
	{
		JS::PersistentRootedObject moduleNamespace;
		JS::PersistentRootedObject fulfill;
		JS::PersistentRootedObject reject;
	};
	struct Fulfilled
	{
		JS::PersistentRootedObject moduleNamespace;
	};
	struct Rejected
	{
		std::exception_ptr error;
	};
	struct WaitingForFileChange {};
	struct Invalid {};
	using Status = std::variant<Evaluating, Fulfilled, Rejected, WaitingForFileChange, Invalid>;

	explicit Future(const ScriptRequest& rq, ModuleLoader& reqistry, Result& result, VfsPath modulePath);
	Future() = default;
	Future(const Future&) = delete;
	Future& operator=(const Future&) = delete;
	Future(Future&& other) noexcept;
	Future& operator=(Future&& other) noexcept;
	~Future();

	[[nodiscard]] bool IsDone() const noexcept;

	/**
	 * Throws if the evaluation of the module failed.
	 * @return The module namespace. All exported values are a property
	 *	of this object. @c default is a property with name "default".
	 */
	[[nodiscard]] JSObject* Get();

private:
	[[nodiscard]] bool IsWaiting() const noexcept;
	void SetWaiting() noexcept;

	// It's save to not require a `JS::HandleValue` here.
	void SetReservedSlot(JS::Value privateValue) noexcept;

	Status m_Status{Invalid{}};
};

class ModuleLoader::Result
{
public:
	class iterator;

	explicit Result(const ScriptRequest& rq, const VfsPath& modulePath);
	Result(const Result&) = delete;
	Result& operator=(const Result&) = delete;
	Result(Result&&) = delete;
	Result& operator=(Result&&) = delete;
	~Result();

	[[nodiscard]] iterator begin() noexcept;
	[[nodiscard]] iterator end() const noexcept;

	void Resume();

private:
	const ScriptInterface& m_Script;
	VfsPath m_ModulePath;
	Future m_Storage;
};

class ModuleLoader::Result::iterator
{
public:
	using difference_type = std::ptrdiff_t;
	using value_type = Future;
	using pointer = value_type*;
	using reference = value_type&;
	using iterator_category = std::input_iterator_tag;

	explicit iterator() = default;
	explicit iterator(Result& backReference);

	[[nodiscard]] reference operator*() const;
	[[nodiscard]] pointer operator->() const;

	iterator& operator++();
	iterator& operator++(int);

	[[nodiscard]] bool operator==(const iterator&) const;
	[[nodiscard]] bool operator!=(const iterator&) const;

private:
	Result* backRef{nullptr};
};
} // namespace Script

#endif // INCLUDED_SCRIPTMODULELOADER
