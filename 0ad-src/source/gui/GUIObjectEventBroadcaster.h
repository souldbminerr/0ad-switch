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

#ifndef GUIOBJECTEVENTBROADCASTER
#define GUIOBJECTEVENTBROADCASTER

#include "gui/ObjectBases/IGUIObject.h"
#include "lib/alignment.h"
#include "lib/allocators/DynamicArena.h"
#include "lib/allocators/STLAllocators.h"
#include "lib/types.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace
{
	struct VisibleObject
	{
		IGUIObject* object;
		// Index of the object in a depth-first search inside GUI tree.
		u32 index;
		// Cached value of GetBufferedZ to avoid recursive calls in a deep hierarchy.
		float bufferedZ;
	};

	template<class Container>
	void CollectVisibleObjectsRecursively(const std::vector<IGUIObject*>& objects, Container* visibleObjects)
	{
		for (IGUIObject* const& object : objects)
			if (!object->IsHidden())
			{
				visibleObjects->emplace_back(VisibleObject{ object, static_cast<u32>(visibleObjects->size()), 0.0f });
				CollectVisibleObjectsRecursively(object->GetVisibleChildren(), visibleObjects);
			}
	}
}

class CGUIObjectEventBroadcaster
{
public:
	template<typename... Args>
	static void RecurseVisibleObject(IGUIObject* object, void(IGUIObject::* callbackFunction)(Args... args), Args&&... args)
	{
		using Arena = Allocators::DynamicArena<128 * KiB>;
		using ObjectListAllocator = ProxyAllocator<VisibleObject, Arena>;
		Arena arena;

		std::vector<VisibleObject, ObjectListAllocator> visibleObjects((ObjectListAllocator(arena)));
		CollectVisibleObjectsRecursively(object->GetVisibleChildren(), &visibleObjects);
		for (VisibleObject& visibleObject : visibleObjects)
			visibleObject.bufferedZ = visibleObject.object->GetBufferedZ();

		std::sort(visibleObjects.begin(), visibleObjects.end(), [](const VisibleObject& visibleObject1, const VisibleObject& visibleObject2) -> bool {
			if (visibleObject1.bufferedZ != visibleObject2.bufferedZ)
				return visibleObject1.bufferedZ < visibleObject2.bufferedZ;
			return visibleObject1.index < visibleObject2.index;
			});

		for (const VisibleObject& visibleObject : visibleObjects)
			(visibleObject.object->*callbackFunction)(std::forward<Args>(args)...);
	}
};

#endif // !GUIOBJECTEVENTBROADCASTER
