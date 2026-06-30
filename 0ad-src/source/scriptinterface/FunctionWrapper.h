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

#ifndef INCLUDED_FUNCTIONWRAPPER
#define INCLUDED_FUNCTIONWRAPPER

#include "lib/types.h"
#include "scriptinterface/ScriptConversions.h"
#include "scriptinterface/ScriptExceptions.h"
#include "scriptinterface/ScriptRequest.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <fmt/format.h>
#include <js/CallAndConstruct.h>
#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/GCVector.h>
#include <js/Object.h>
#include <js/PropertyAndElement.h>
#include <js/PropertyDescriptor.h>
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <jsapi.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

class JSFunction;
class JSObject;
class ScriptInterface;
namespace JS { class GCContext; }
struct JSContext;

/**
 * This class introduces templates to conveniently wrap C++ functions in JSNative functions.
 * This _is_ rather template heavy, so compilation times beware.
 * The C++ code can have arbitrary arguments and arbitrary return types, so long
 * as they can be converted to/from JS using Script::ToJSVal (FromJSVal respectively),
 * and they are default-constructible (TODO: that can probably changed).
 * (This could be a namespace, but I like being able to specify public/private).
 */
class ScriptFunction
{
private:
	ScriptFunction() = delete;
	ScriptFunction(const ScriptFunction&) = delete;
	ScriptFunction(ScriptFunction&&) = delete;

	/**
	 * In JS->C++ calls, types are converted using FromJSVal,
	 * and this requires them to be default-constructible (as that function takes an out parameter)
	 * thus constref needs to be removed when defining the tuple.
	 * Exceptions are:
	 *  - const ScriptRequest& (as the first argument only, for implementation simplicity).
	 *  - const ScriptInterface& (as the first argument only, for implementation simplicity).
	 *  - JS::HandleValue
	 */
	template<typename T>
	using type_transform = std::conditional_t<
		std::is_same_v<const ScriptRequest&, T> || std::is_same_v<const ScriptInterface&, T>,
		T,
		std::remove_const_t<typename std::remove_reference_t<T>>
	>;

	/**
	 * Convenient struct to get info on a [class] [const] function pointer.
	 */
	template <class T> struct args_info_t;
	template <auto T> using args_info = args_info_t<decltype(T)>;

	template<typename R, typename ...Types>
	struct args_info_t<R(*)(Types ...)>
	{
		static constexpr const size_t nb_args = sizeof...(Types);
		using return_type = R;
		using object_type = void;
		using arg_types = std::tuple<type_transform<Types>...>;
	};

	template<typename C, typename R, typename ...Types>
	struct args_info_t<R(C::*)(Types ...)> : public args_info_t<R(*)(Types ...)> { using object_type = C; };
	template<typename C, typename R, typename ...Types>
	struct args_info_t<R(C::*)(Types ...) const> : public args_info_t<R(C::*)(Types ...)> {};

	struct IteratorResultError : std::runtime_error
	{
		IteratorResultError(const std::string& property) :
			IteratorResultError{property.c_str()}
		{}
		IteratorResultError(const char* property) :
			std::runtime_error{fmt::format("Failed to get `{}` from an `IteratorResult`.", property)}
		{}
		using std::runtime_error::runtime_error;
	};

	///////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////

	/**
	 * DoConvertFromJS takes a type, a JS argument, and converts.
	 * The type T must be default constructible (except for HandleValue, which is handled specially).
	 * (possible) TODO: this could probably be changed if FromJSVal had a different signature.
	 * @param wentOk - true if the conversion succeeded and wentOk was true before, false otherwise.
	 */
	template<size_t idx, typename T>
	static T DoConvertFromJS([[maybe_unused]] const ScriptRequest& rq,
		[[maybe_unused]] JS::CallArgs& args, [[maybe_unused]] bool& wentOk)
	{
		// No need to convert JS values.
		if constexpr (std::is_same_v<T, JS::HandleValue>)
		{
			// Default-construct values that aren't passed by JS.
			// TODO: this should perhaps be removed, as it's distinct from C++ default values and kind of tricky.
			if (idx >= args.length())
				return JS::UndefinedHandleValue;
			else
				return args[idx]; // This passes the null handle value if idx is beyond the length of args.
		}
		else
		{
			// Default-construct values that aren't passed by JS.
			// TODO: this should perhaps be removed, as it's distinct from C++ default values and kind of tricky.
			if (idx >= args.length())
				return {};
			else
			{
				T ret;
				wentOk &= Script::FromJSVal<T>(rq, args[idx], ret);
				return ret;
			}
		}
	}

	/**
	 * Wrapper: calls DoConvertFromJS for each element in T.
	 */
	template<typename... T, size_t... idx>
	static std::tuple<T...> DoConvertFromJS(std::index_sequence<idx...>, const ScriptRequest& rq,
		JS::CallArgs& args, bool& wentOk)
	{
		return {DoConvertFromJS<idx, T>(rq, args, wentOk)...};
	}

	/**
	 * ConvertFromJS is a wrapper around DoConvertFromJS, and handles specific cases for the
	 * first argument (ScriptRequest, ...).
	 *
	 * Trick: to unpack the types of the tuple as a parameter pack, we deduce them from the function signature.
	 * To do that, we want the tuple in the arguments, but we don't want to actually have to default-instantiate,
	 * so we'll pass a nullptr that's static_cast to what we want.
	 */
	template<typename ...Types>
	static std::tuple<Types...> ConvertFromJS(const ScriptRequest& rq, JS::CallArgs& args, bool& wentOk,
		std::tuple<Types...>*)
	{
		return DoConvertFromJS<Types...>(std::index_sequence_for<Types...>(), rq, args, wentOk);
	}

	// Overloads for ScriptRequest& first argument.
	template<typename ...Types>
	static std::tuple<const ScriptRequest&, Types...> ConvertFromJS(const ScriptRequest& rq,
		JS::CallArgs& args, bool& wentOk, std::tuple<const ScriptRequest&, Types...>*)
	{
		return std::tuple_cat(std::tie(rq), DoConvertFromJS<Types...>(
			std::index_sequence_for<Types...>(), rq, args, wentOk));
	}

	// Overloads for ScriptInterface& first argument.
	template<typename ...Types>
	static std::tuple<const ScriptInterface&, Types...> ConvertFromJS(const ScriptRequest& rq,
		JS::CallArgs& args, bool& wentOk, std::tuple<const ScriptInterface&, Types...>*)
	{
		return std::tuple_cat(std::tie(rq.GetScriptInterface()),
			DoConvertFromJS<Types...>(std::index_sequence_for<Types...>(), rq, args, wentOk));
	}

	///////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////

	/**
	 * Wrap std::apply for the case where we have an object method or a regular function.
	 */
	template <auto callable, typename T, typename tuple>
	static typename args_info<callable>::return_type call([[maybe_unused]] T* object, tuple& args)
	{
		if constexpr(std::is_same_v<T, void>)
			return std::apply(callable, args);
		else
			return std::apply(callable, std::tuple_cat(std::forward_as_tuple(*object), args));
	}

	///////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////

	struct IgnoreResult_t {};
	static inline IgnoreResult_t IgnoreResult;

	/**
	 * Converts any number of arguments to a `JS::MutableHandleValueVector`.
	 * If `idx` is empty this function does nothing. For that case there is a
	 * `[[maybe_unused]]` on `argv`. GCC would issue a
	 * "-Wunused-but-set-parameter" warning.
	 * For references like `rq` this warning isn't issued.
	 */
	template<typename... Types, size_t... idx>
	static void ToJSValVector(std::index_sequence<idx...>, const ScriptRequest& rq,
		[[maybe_unused]] JS::MutableHandleValueVector argv, const Types&... params)
	{
		(Script::ToJSVal(rq, argv[idx], params), ...);
	}

	/**
	 * Wrapper around calling a JS function from C++.
	 * Arguments are const& to avoid lvalue/rvalue issues, and so can't be used as out-parameters.
	 * In particular, the problem is that Rooted are deduced as Rooted, not Handle, and so can't be copied.
	 * This could be worked around with more templates, but it doesn't seem particularly worth doing.
	 */
	template<typename R, typename ...Args>
	static bool Call_(const ScriptRequest& rq, JS::HandleValue val, const char* name,
		[[maybe_unused]] R& ret, const Args&... args)
	{
		JS::RootedObject obj(rq.cx);
		if (!JS_ValueToObject(rq.cx, val, &obj) || !obj)
			return false;

		// Fetch the property explicitly - this avoids converting the arguments if it doesn't exist.
		JS::RootedValue func(rq.cx);
		if (!JS_GetProperty(rq.cx, obj, name, &func) || func.isUndefined())
			return false;

		JS::RootedValueVector argv(rq.cx);
		std::ignore = argv.resize(sizeof...(Args));
		ToJSValVector(std::index_sequence_for<Args...>{}, rq, &argv, args...);

		bool success;
		if constexpr (std::is_same_v<R, JS::MutableHandleValue>)
			success = JS_CallFunctionValue(rq.cx, obj, func, argv, ret);
		else
		{
			JS::RootedValue jsRet(rq.cx);
			success = JS_CallFunctionValue(rq.cx, obj, func, argv, &jsRet);
			if constexpr (!std::is_same_v<R, IgnoreResult_t>)
			{
				if (success)
					success = Script::FromJSVal(rq, jsRet, ret);
			}
		}
		// Even if everything succeeded, there could be pending exceptions
		return !ScriptException::CatchPending(rq) && success;
	}

	struct StatefullCallbackPrivateSlot
	{
		static constexpr size_t callback{0};
		static constexpr size_t classInfo{1};
		static constexpr uint32_t count{2};
	};

	///////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////////////////////////////////////////
public:
	template <typename T>
	using ObjectGetter = T*(*)(const ScriptRequest&, JS::CallArgs&);

	template <auto callable>
	using GetterFor = ObjectGetter<typename args_info<callable>::object_type>;

	/**
	 * The meat of this file. This wraps a C++ function into a JSNative,
	 * so that it can be called from JS and manipulated in Spidermonkey.
	 * Most C++ functions can be directly wrapped, so long as their arguments are
	 * convertible from JS::Value and their return value is convertible to JS::Value (or void)
	 * The C++ function may optionally take const ScriptRequest& or ScriptInterface& as its first argument.
	 * The function may be an object method, in which case you need to pass an appropriate getter
	 *
	 * Optimisation note: the ScriptRequest object is created even without arguments,
	 * as it's necessary for IsExceptionPending.
	 *
	 * @param thisGetter to get the object, if necessary.
	 */
	template <auto callable, GetterFor<callable> thisGetter = nullptr>
	static bool ToJSNative(JSContext* cx, unsigned argc, JS::Value* vp)
	{
		using ObjType = typename args_info<callable>::object_type;

		JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
		ScriptRequest rq(cx);

		// If the callable is an object method, we must specify how to fetch the object.
		static_assert(std::is_same_v<typename args_info<callable>::object_type, void> || thisGetter != nullptr,
					  "ScriptFunction::Register - No getter specified for object method");

// GCC 7 triggers spurious warnings
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress"
#endif
		ObjType* obj = nullptr;
		if constexpr (thisGetter != nullptr)
		{
			obj = thisGetter(rq, args);
			if (!obj)
				return false;
		}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

		bool wentOk = true;
		typename args_info<callable>::arg_types outs = ConvertFromJS(rq, args, wentOk,
			static_cast<typename args_info<callable>::arg_types*>(nullptr));
		if (!wentOk)
			return false;

		try
		{
			if constexpr (std::is_same_v<void, typename args_info<callable>::return_type>)
				call<callable>(obj, outs);
			else if constexpr (std::is_same_v<JS::Value, typename args_info<callable>::return_type>)
				args.rval().set(call<callable>(obj, outs));
			else
				Script::ToJSVal(rq, args.rval(), call<callable>(obj, outs));

			return !ScriptException::IsPending(rq);
		}
		catch (const std::exception& e)
		{
			ScriptException::Raise(rq, "%s", e.what());
		}
		catch (...)
		{
			ScriptException::Raise(rq, "Unknown error occured in an Engine callback.");
		}
		return false;
	}

	/**
	 * Call a JS function @a name, property of object @a val, with the arguments @a args.
	 * @a ret will be updated with the return value, if any.
	 * @return the success (or failure) thereof.
	 */
	template<typename R, typename ...Args>
	static bool Call(const ScriptRequest& rq, JS::HandleValue val, const char* name, R& ret, const Args&... args)
	{
		return Call_(rq, val, name, ret, std::forward<const Args>(args)...);
	}

	// Specialisation for MutableHandleValue return.
	template<typename ...Args>
	static bool Call(const ScriptRequest& rq, JS::HandleValue val, const char* name, JS::MutableHandleValue ret, const Args&... args)
	{
		return Call_(rq, val, name, ret, std::forward<const Args>(args)...);
	}

	/**
	 * Call a JS function @a name, property of object @a val, with the arguments @a args.
	 * @return the success (or failure) thereof.
	 */
	template<typename ...Args>
	static bool CallVoid(const ScriptRequest& rq, JS::HandleValue val, const char* name, const Args&... args)
	{
		return Call(rq, val, name, IgnoreResult, std::forward<const Args>(args)...);
	}

	/**
	 * Call a JS function @a name, property of object @a val, with the argument @a args. Repeatetly
	 * invokes @a yieldCallback with the yielded value.
	 * @return the final value of the generator.
	 */
	template<typename Callback>
	static JS::Value RunGenerator(const ScriptRequest& rq, JS::HandleValue val, const char* name,
		JS::HandleValue arg, Callback yieldCallback)
	{
		JS::RootedValue generator{rq.cx};
		if (!ScriptFunction::Call(rq, val, name, &generator, arg))
			throw std::runtime_error{fmt::format("Failed to call the generator `{}`.", name)};

		const auto continueGenerator = [&](const char* property, auto... args) -> JS::Value
			{
				JS::RootedValue iteratorResult{rq.cx};
				if (!ScriptFunction::Call(rq, generator, property, &iteratorResult, args...))
					throw std::runtime_error{fmt::format("Failed to call `{}`.", name)};
				return iteratorResult;
			};

		JS::PersistentRootedValue error{rq.cx, JS::UndefinedValue()};
		while (true)
		{
			JS::RootedValue iteratorResult{rq.cx, error.isUndefined() ? continueGenerator("next") :
				continueGenerator("throw", std::exchange(error, JS::UndefinedValue()))};

			try
			{
				JS::RootedObject iteratorResultObject{rq.cx, &iteratorResult.toObject()};

				bool done;
				if (!Script::FromJSProperty(rq, iteratorResult, "done", done, true))
					throw IteratorResultError{"done"};

				JS::RootedValue value{rq.cx};
				if (!JS_GetProperty(rq.cx, iteratorResultObject, "value", &value))
					throw IteratorResultError{"value"};

				if (done)
					return value;

				yieldCallback(value);
			}
			catch (const std::exception& e)
			{
				JS::RootedValue global{rq.cx, rq.globalValue()};
				if (!ScriptFunction::Call(rq, global, "Error", &error, e.what()))
					throw std::runtime_error{"Failed to construct `Error`."};
			}
		}
	}

	/**
	 * Return a function spec from a C++ function.
	 */
	template <auto callable, GetterFor<callable> thisGetter = nullptr>
	static JSFunctionSpec Wrap(const char* name,
		const u16 flags = JSPROP_ENUMERATE | JSPROP_READONLY | JSPROP_PERMANENT)
	{
		return JS_FN(name, (&ToJSNative<callable, thisGetter>), args_info<callable>::nb_args, flags);
	}

	/**
	 * Return a JSFunction from a C++ function.
	 */
	template <auto callable, GetterFor<callable> thisGetter = nullptr>
	static JSFunction* Create(const ScriptRequest& rq, const char* name,
		const u16 flags = JSPROP_ENUMERATE | JSPROP_READONLY | JSPROP_PERMANENT)
	{
		return JS_NewFunction(rq.cx, &ToJSNative<callable, thisGetter>, args_info<callable>::nb_args, flags, name);
	}

	/**
	 * Register a function on the native scope (usually 'Engine').
	 */
	template <auto callable, GetterFor<callable> thisGetter = nullptr>
	static void Register(const ScriptRequest& rq, const char* name,
		const u16 flags = JSPROP_ENUMERATE | JSPROP_READONLY | JSPROP_PERMANENT)
	{
		JS_DefineFunction(rq.cx, rq.nativeScope, name, &ToJSNative<callable, thisGetter>, args_info<callable>::nb_args, flags);
	}

	/**
	 * Register a function on @param scope.
	 * Prefer the version taking ScriptRequest unless you have a good reason not to.
	 * @see Register
	 */
	template <auto callable, GetterFor<callable> thisGetter = nullptr>
	static void Register(JSContext* cx, JS::HandleObject scope, const char* name,
		const u16 flags = JSPROP_ENUMERATE | JSPROP_READONLY | JSPROP_PERMANENT)
	{
		JS_DefineFunction(cx, scope, name, &ToJSNative<callable, thisGetter>, args_info<callable>::nb_args, flags);
	}

	template<typename Callable>
	class StatefulCallback
	{
		class ClassInfo
		{
		public:
			ClassInfo(std::string className) :
				name{std::move(className)}
			{}

			JSClassOps classOps{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
				/*.finalize = */finalize, /*.call = */&ToJSNative<&Callable::operator(), getter>,
				nullptr, nullptr};
			std::string name;
			JSClass jsClass{name.c_str(),
				JSCLASS_HAS_RESERVED_SLOTS(StatefullCallbackPrivateSlot::count) |
					JSCLASS_BACKGROUND_FINALIZE,
				&classOps};
		};
	public:
		explicit StatefulCallback(const ScriptRequest& rq, std::string name, Callable callable) :
			StatefulCallback{rq, std::move(name),
				JSPROP_ENUMERATE | JSPROP_READONLY | JSPROP_PERMANENT, std::move(callable)}
		{}

		explicit StatefulCallback(const ScriptRequest& rq, std::string name, const unsigned flags,
			Callable callable) :
			callback{std::move(callable)},
			functionObject{rq.cx}
		{
			auto classInfo = std::make_unique<ClassInfo>(std::move(name));
			functionObject.set(JS_NewObject(rq.cx, &classInfo->jsClass));
			JS::RootedValue functionValue{rq.cx, JS::ObjectValue(*functionObject)};
			if (!JS_DefineProperty(rq.cx, rq.nativeScope, classInfo->name.c_str(), functionValue,
				flags))
			{
				throw std::runtime_error{fmt::format(
					"Failed defining function {:?} on the native scope.", classInfo->name)};
			}
			JS::SetReservedSlot(functionObject, StatefullCallbackPrivateSlot::classInfo,
				JS::PrivateValue(classInfo.release()));
			JS::SetReservedSlot(functionObject, StatefullCallbackPrivateSlot::callback,
				JS::PrivateValue(&callback));
		}
		StatefulCallback(const StatefulCallback&) = delete;
		StatefulCallback& operator=(const StatefulCallback&) = delete;
		StatefulCallback(StatefulCallback&&) = delete;
		StatefulCallback& operator=(StatefulCallback&&) = delete;

		~StatefulCallback()
		{
			JS::SetReservedSlot(functionObject, StatefullCallbackPrivateSlot::callback,
				JS::UndefinedValue());
		}

	private:
		static Callable* getter(const ScriptRequest&, JS::CallArgs& args)
		{
			return JS::GetMaybePtrFromReservedSlot<Callable>(&args.callee(),
				StatefullCallbackPrivateSlot::callback);
		}

		static void finalize(JS::GCContext*, JSObject* obj)
		{
			delete JS::GetMaybePtrFromReservedSlot<ClassInfo>(obj,
				StatefullCallbackPrivateSlot::classInfo);
		}

		Callable callback;
		JS::RootedObject functionObject;
	};

	template<typename Callable>
	static StatefulCallback<Callable> Register(const ScriptRequest& rq, std::string name,
		const unsigned flags, Callable callable)
	{
		return StatefulCallback{rq, std::move(name), flags, std::move(callable)};
	}

	template<typename Callable>
	static StatefulCallback<Callable> Register(const ScriptRequest& rq, std::string name,
		Callable callable)
	{
		return StatefulCallback{rq, std::move(name), std::move(callable)};
	}
};

#endif // INCLUDED_FUNCTIONWRAPPER
