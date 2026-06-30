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

#include "JSInterface_CGUISize.h"

#include "gui/CGUI.h"
#include "gui/CGUISetting.h"
#include "gui/ObjectBases/IGUIObject.h"
#include "gui/Scripting/JSInterface_GUISize.h"
#include "gui/SettingTypes/CGUISize.h"
#include "lib/code_generation.h"
#include "lib/types.h"
#include "maths/Rect.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/ScriptConversions.h"
#include "scriptinterface/ScriptInterface.h"
#include "scriptinterface/ScriptRequest.h"

#include <fmt/format.h>
#include <js/CallArgs.h>
#include <js/Class.h>
#include <js/Conversions.h>
#include <js/Object.h>
#include <js/PropertyDescriptor.h>
#include <js/PropertySpec.h>
#include <js/RootingAPI.h>
#include <js/TypeDecls.h>
#include <js/Value.h>
#include <jsapi.h>
#include <string>
struct JSContext;

namespace
{
template <float CRect::*Member, CRect CGUISize::*RectMember>
bool GetCRectField(JSContext* cx, unsigned argc, JS::Value* vp)
{
	JS::CallArgs args{JS::CallArgsFromVp(argc, vp)};
	JS::RootedObject obj{cx, &args.thisv().toObject()};
	CGUISimpleSetting<CGUISize>* wrapper{JS::GetMaybePtrFromReservedSlot<CGUISimpleSetting<CGUISize>>(obj, ScriptInterface::JSObjectReservedSlots::PRIVATE)};

	args.rval().setDouble(wrapper->GetMutable().*RectMember.*Member);
	return true;
}

template <float CRect::*Member, CRect CGUISize::*RectMember>
bool SetCRectField(JSContext* cx, unsigned argc, JS::Value* vp)
{
	JS::CallArgs args{JS::CallArgsFromVp(argc, vp)};
	JS::RootedObject obj{cx, &args.thisv().toObject()};
	CGUISimpleSetting<CGUISize>* wrapper{JS::GetMaybePtrFromReservedSlot<CGUISimpleSetting<CGUISize>>(obj, ScriptInterface::JSObjectReservedSlots::PRIVATE)};

	double val;
	if (!JS::ToNumber(cx, args.get(0), &val))
		return false;

	wrapper->GetMutable().*RectMember.*Member = static_cast<float>(val);
	args.rval().setUndefined();

	wrapper->DeferSettingChange();
	return true;
}

// Produces "10", "-10", "50%", "50%-10", "50%+10", etc
CStr ToPercentString(double pix, double per)
{
	if (per == 0)
		return CStr::FromDouble(pix);

	if (pix == 0)
		return fmt::format("{}%", per);

	return fmt::format("{}%{:+}", per, pix);
}

bool toString(JSContext* cx, uint argc, JS::Value* vp)
{
	JS::CallArgs args{JS::CallArgsFromVp(argc, vp)};
	JS::RootedObject obj{cx, &args.thisv().toObject()};
	CGUISimpleSetting<CGUISize>* wrapper{JS::GetMaybePtrFromReservedSlot<CGUISimpleSetting<CGUISize>>(obj, ScriptInterface::JSObjectReservedSlots::PRIVATE)};
	CStr buffer;

	buffer += ToPercentString(wrapper->GetMutable().pixel.left, wrapper->GetMutable().percent.left) + " ";
	buffer += ToPercentString(wrapper->GetMutable().pixel.top, wrapper->GetMutable().percent.top) + " ";
	buffer += ToPercentString(wrapper->GetMutable().pixel.right, wrapper->GetMutable().percent.right) + " ";
	buffer += ToPercentString(wrapper->GetMutable().pixel.bottom, wrapper->GetMutable().percent.bottom);

	ScriptRequest rq{cx};
	Script::ToJSVal(rq, args.rval(), buffer);
	return true;
}

JSClass JSI_class = {
	"CGUISize", JSCLASS_HAS_RESERVED_SLOTS(1)
};

JSFunctionSpec JSI_methods[] =
{
	JS_FN("toString", toString, 0, 0),
	JS_FN("toSource", toString, 0, 0),
	JS_FS_END
};

JSPropertySpec JSI_props[] =
{
	JS_PSGS("top", (GetCRectField<&CRect::top, &CGUISize::pixel>), (SetCRectField<&CRect::top, &CGUISize::pixel>), JSPROP_ENUMERATE),
	JS_PSGS("left", (GetCRectField<&CRect::left, &CGUISize::pixel>), (SetCRectField<&CRect::left, &CGUISize::pixel>), JSPROP_ENUMERATE),
	JS_PSGS("bottom", (GetCRectField<&CRect::bottom, &CGUISize::pixel>), (SetCRectField<&CRect::bottom, &CGUISize::pixel>), JSPROP_ENUMERATE),
	JS_PSGS("right", (GetCRectField<&CRect::right, &CGUISize::pixel>), (SetCRectField<&CRect::right, &CGUISize::pixel>), JSPROP_ENUMERATE),

	JS_PSGS("rtop", (GetCRectField<&CRect::top, &CGUISize::percent>), (SetCRectField<&CRect::top, &CGUISize::percent>), JSPROP_ENUMERATE),
	JS_PSGS("rleft", (GetCRectField<&CRect::left, &CGUISize::percent>), (SetCRectField<&CRect::left, &CGUISize::percent>), JSPROP_ENUMERATE),
	JS_PSGS("rbottom", (GetCRectField<&CRect::bottom, &CGUISize::percent>), (SetCRectField<&CRect::bottom, &CGUISize::percent>), JSPROP_ENUMERATE),
	JS_PSGS("rright", (GetCRectField<&CRect::right, &CGUISize::percent>), (SetCRectField<&CRect::right, &CGUISize::percent>), JSPROP_ENUMERATE),
	JS_PS_END
};
} // end anonymous namespace



void JSI_CGUISize::RegisterScriptClass(ScriptInterface& scriptInterface)
{
	scriptInterface.DefineCustomObjectType(&JSI_class, nullptr, 0, JSI_props, JSI_methods, nullptr, nullptr);
}

template class CGUISimpleSetting<CGUISize>;
template<>
void CGUISimpleSetting<CGUISize>::ToJSVal(const ScriptRequest& rq, JS::MutableHandleValue ret)
{
	const ScriptInterface& scriptInterface = rq.GetScriptInterface();
	JS::RootedObject obj{rq.cx, scriptInterface.CreateCustomObject("CGUISize")};

	JS::SetReservedSlot(obj, ScriptInterface::JSObjectReservedSlots::PRIVATE, JS::PrivateValue(this));
	ret.setObject(*obj);
};

template<>
bool CGUISimpleSetting<CGUISize>::DoFromString(const CStrW& value)
{
	return CGUI::ParseString<CGUISize>(&m_Object.GetGUI(), value, m_Setting);
};

template<>
bool CGUISimpleSetting<CGUISize>::DoFromJSVal(const ScriptRequest& rq, JS::HandleValue value)
{
	if (value.isString())
	{
		CStrW str;
		if (!Script::FromJSVal(rq, value, str))
		{
			LOGERROR("CGUISize could not read JS string");
			return false;
		}

		if (!m_Setting.FromString(str.ToUTF8()))
		{
			LOGERROR("CGUISize could not parse JS string");
			return false;
		}

		return true;
	}

	if (!value.isObject())
	{
		LOGERROR("CGUISize value is not an String, nor Object");
		return false;
	}

	JS::RootedObject obj{rq.cx, &value.toObject()};
	if (JS_InstanceOf(rq.cx, obj, &JSI_class, nullptr))
	{
		CGUISimpleSetting<CGUISize>* wrapper = JS::GetMaybePtrFromReservedSlot<CGUISimpleSetting<CGUISize>>(obj, ScriptInterface::JSObjectReservedSlots::PRIVATE);
		if (this != wrapper)
			this->Set(wrapper->m_Setting, false);
		return true;
	}

	if (JS_InstanceOf(rq.cx, obj, &JSI_GUISize::JSI_class, nullptr))
		ONCE(LOGWARNING("Assigning an GUISize to CGUISize is deprecated. Please use the object.size = {left:number, top:number, right:number, bottom:number} format instead. This support will be removed in a future version."));

	bool atLeastOnePropertySet{false};
	CRect pixel{};
	CRect percent{};
	auto tryGet = [&](const char* name, float& field)
	{
		if (!Script::HasProperty(rq, value, name) || !Script::GetProperty(rq, value, name, field))
			return;

		atLeastOnePropertySet = true;
	};

	tryGet("left", pixel.left);
	tryGet("top", pixel.top);
	tryGet("right", pixel.right);
	tryGet("bottom", pixel.bottom);

	tryGet("rtop", percent.top);
	tryGet("rleft", percent.left);
	tryGet("rbottom", percent.bottom);
	tryGet("rright", percent.right);

	// If no properties were set, return false to indicate failure.
	if (!atLeastOnePropertySet)
	{
		LOGERROR("CGUISize could not read JS object");
		return false;
	}

	m_Setting.pixel = pixel;
	m_Setting.percent = percent;

	return true;
};
