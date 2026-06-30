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

#ifndef INCLUDED_FONTMANAGER
#define INCLUDED_FONTMANAGER

#include "lib/code_annotation.h"
#include "ps/CStrIntern.h"

#include <array>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <memory>
#include <unordered_map>

class CConfigDBHook;
class CFont;
struct FT_LibraryRec_;

namespace Renderer::Backend { class IDeviceCommandContext; }

/**
 * Font manager: loads and caches bitmap fonts.
 */
class CFontManager
{
public:
	CFontManager();
	~CFontManager();
	NONCOPYABLE(CFontManager);

	CFont* LoadFont(CStrIntern fontName, CStrIntern locale);
	void UploadAtlasTexturesToGPU(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext);

private:
	static void ftLibraryDeleter(FT_Library library)
	{
		FT_Done_FreeType(library);
	}

	std::unique_ptr<FT_LibraryRec_, decltype(&ftLibraryDeleter)> m_FreeType{nullptr, &ftLibraryDeleter};

	std::unique_ptr<std::array<float, 256>> m_GammaCorrectionLUT;

	using FontsMap = std::unordered_map<CStrIntern, CFont>;
	FontsMap m_Fonts;

	float m_GUIScale{1.0f};
	std::unique_ptr<CConfigDBHook> m_GUIScaleHook;

	/*
	* Most monitors today use 2.2 as the standard gamma.
	* MacOS may use 2.2 or 1.8 in some cases.
	* This method assumes your OS or GPU didn’t override the gamma ramp.
	* Unless we need super-accurate gamma (e.g., for print preview or color grading), this is usually acceptable.
	*/
	static constexpr float GAMMA_CORRECTION = 2.2f;
};

#endif // INCLUDED_FONTMANAGER
