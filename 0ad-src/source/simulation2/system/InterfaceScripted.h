/* Copyright (C) 2024 Wildfire Games.
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

#ifndef INCLUDED_INTERFACE_SCRIPTED
#define INCLUDED_INTERFACE_SCRIPTED

#include "scriptinterface/FunctionWrapper.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/ScriptConversions.h"
#include "scriptinterface/ScriptInterface.h"
#include "simulation2/system/ComponentManager.h"

#define BEGIN_INTERFACE_WRAPPER(iname) \
	JSClass class_ICmp##iname = { \
		"ICmp" #iname, JSCLASS_HAS_RESERVED_SLOTS( ScriptInterface::JSObjectReservedSlots::PRIVATE+1 ) \
	}; \
	static JSFunctionSpec methods_ICmp##iname[] = {

#define END_INTERFACE_WRAPPER(iname) \
		JS_FS_END \
	}; \
	void ICmp##iname::InterfaceInit(ScriptInterface& scriptInterface) { \
		scriptInterface.DefineCustomObjectType(&class_ICmp##iname, NULL, 0, NULL, methods_ICmp##iname, NULL, NULL); \
	} \
	bool ICmp##iname::NewJSObject(const ScriptInterface& scriptInterface, JS::MutableHandleObject out) const\
	{ \
		out.set(scriptInterface.CreateCustomObject("ICmp" #iname)); \
		IComponent* comp = const_cast<IComponent*>(static_cast<const IComponent*>(this)); \
		JS::SetReservedSlot(out, ScriptInterface::JSObjectReservedSlots::PRIVATE, JS::PrivateValue(comp)); \
		return true; \
	} \
	JS::HandleValue ICmp##iname::GetJSInstance() const \
	{ \
		if (m_CachedInstance) \
			return JS::HandleValue::fromMarkedLocation(m_CachedInstance.address()); \
		\
		const ScriptInterface& si = GetSimContext().GetScriptInterface(); \
		ScriptRequest rq(si); \
		JS::RootedObject obj(rq.cx); \
		NewJSObject(GetSimContext().GetScriptInterface(), &obj); \
		m_CachedInstance.setObject(*obj); \
		\
		GetSimContext().GetComponentManager().RegisterTrace(GetEntityId(), m_CachedInstance); \
		return JS::HandleValue::fromMarkedLocation(m_CachedInstance.address()); \
	} \
	void RegisterComponentInterface_##iname(ScriptInterface& scriptInterface) { \
		ICmp##iname::InterfaceInit(scriptInterface); \
	}

template <typename T>
inline T* ComponentGetter(const ScriptRequest& rq, JS::CallArgs& args)
{
	return ScriptInterface::GetPrivate<T>(rq, args);
}

#define DEFINE_INTERFACE_METHOD(scriptname, classname, methodname) \
	ScriptFunction::Wrap<&classname::methodname, ComponentGetter<classname>>(scriptname),

#endif // INCLUDED_INTERFACE_SCRIPTED
