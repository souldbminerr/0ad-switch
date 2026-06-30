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

#ifndef INCLUDED_JSI_GUISIZE
#define INCLUDED_JSI_GUISIZE


#include "lib/posix/posix_types.h"
#include "lib/types.h"
#include "ps/CStr.h"

class ScriptInterface;
namespace JS { class Value; }
struct JSClass;
struct JSClassOps;
struct JSContext;
struct JSFunctionSpec;
struct JSPropertySpec;

namespace JSI_GUISize
{
	extern JSClass JSI_class;
	extern JSClassOps JSI_classops;
	extern JSPropertySpec JSI_props[];
	extern JSFunctionSpec JSI_methods[];

	void RegisterScriptClass(ScriptInterface& scriptInterface);

	bool construct(JSContext* cx, uint argc, JS::Value* vp);
	bool toString(JSContext* cx, uint argc, JS::Value* vp);

	CStr ToPercentString(double pix, double per);
}

#endif // INCLUDED_JSI_GUISIZE
