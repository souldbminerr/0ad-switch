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

#include "FontManager.h"

#include "graphics/Font.h"
#include "i18n/L10n.h"
#include "lib/file/vfs/vfs_path.h"
#include "lib/posix/posix.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/CStrInternStatic.h"
#include "ps/ConfigDB.h"
#include "ps/Filesystem.h"
#include "ps/Profiler2.h"
#include "ps/strings/StringBuilder.h"
#include "renderer/backend/IDeviceCommandContext.h"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <freetype/fttypes.h>
#include <locale>
#include <map>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unicode/locid.h>
#include <utility>
#include <vector>

namespace
{

struct FontSpec
{
	std::string type;
	bool bold{false};
	bool italic{false};
	bool stroke{false};
	int size{0};
};

FontSpec ParseFontSpec(const std::string& spec)
{
	// Regex breakdown:
	//   ^([^\\-]+)           → capture fontType (one or more non-'-')
	//   (?:-(bold|italic))?  → optional "-bold" or "-italic"
	//   (?:-(stroke))?       → optional "-stroke"
	//   -([0-9]+)$           → "-" then fontSize digits at end
	// examples:
	// "Roboto-italic-stroke-24",
	// "OpenSans-bold-32",
	// "Arial-stroke-16",
	// "Lato-14"
	static const std::regex pattern{R"(^([^\-]+)(?:-(bold|italic))?(?:-(stroke))?-([0-9]+)$)",
		std::regex::icase};

	std::smatch m;
	if (!std::regex_match(spec, m, pattern))
	{
		LOGERROR("Invalid font specification: %s", spec.c_str());
		return {};
	}

	FontSpec fs;
	fs.type = m[1].str();

	if (m[2].matched)
	{
		std::string style = m[2].str();
		if (strcasecmp(style.c_str(), "bold") == 0)
			fs.bold = true;
		else if (strcasecmp(style.c_str(), "italic") == 0)
			fs.italic = true;
	}

	if (m[3].matched)
		fs.stroke = true;

	fs.size = std::stoi(m[4].str());

	return fs;
}
} // namespace

CFontManager::CFontManager()
	: m_GUIScaleHook{std::make_unique<CConfigDBHook>(g_ConfigDB.RegisterHookAndCall(
		"gui.scale", [this]()
		{
			m_GUIScale = g_ConfigDB.Get("gui.scale", 1.0f);
		}))}
{
	FT_Library lib;
	FT_Error error{FT_Init_FreeType(&lib)};
	if (error)
		throw std::runtime_error{"Failed to initialize FreeType " + std::to_string(error)};
	m_FreeType.reset(lib);

	m_GammaCorrectionLUT = std::make_unique<std::array<float, 256>>();

	std::generate(m_GammaCorrectionLUT->begin(), m_GammaCorrectionLUT->end(), [i = 0]() mutable {
		return std::pow((i++) / 255.0f, 1.0f / GAMMA_CORRECTION);
	});
}

CFontManager::~CFontManager() = default;

CFont* CFontManager::LoadFont(CStrIntern fontName, CStrIntern locale)
{
	const std::string localeToUse{[&]
		{
			if (!locale.empty())
				return locale.string();

			if (g_L10n.GetCurrentLocale() == icu::Locale::getUS())
				return std::string{};

			// Use the current locale, but not US English.
			return g_L10n.GetCurrentLocaleString();
		} ()
	};
	// fmt::format_to_n is expensive for frequent LoadFont calls, parsing the
	// format string takes a noticeable amount of time.
	char buffer[128];
	PS::StringBuilder fontNameBuilder{{std::begin(buffer), std::end(buffer)}};
	fontNameBuilder.Append(localeToUse);
	fontNameBuilder.Append(fontName.string());
	fontNameBuilder.Append('-');
	fontNameBuilder.Append(m_GUIScale);
	CStrIntern localeFontName{fontNameBuilder.Str()};

	FontsMap::iterator it{m_Fonts.find(localeFontName)};
	if (it != m_Fonts.end())
		return &it->second;

	// TODO: use hooks or something to hotrealoding default font.
	const std::string defaultFont{g_ConfigDB.Get("fonts.default", std::string{})};

	if (defaultFont.empty())
	{
		LOGERROR("Default font not set in config");
		return nullptr;
	}

	// FontName contain the format fontType(-fontBold|fontItalic)(-fontStroke)-fontSize.
	// We are going to split it to get the fontType and fontSize.
	FontSpec fontSpec{ParseFontSpec(fontName.string())};

	if (fontSpec.type.empty())
	{
		LOGERROR("Failed to parse font specification: %s, using default font", fontName.string().c_str());
		fontSpec = ParseFontSpec(str_sans_10.string());
	}

	// Check for font configuration or fallback.
	const std::map<CStr, CConfigValueSet> fontToSearch{[&]
		{
			std::vector<std::string> candidateFonts;
			// 3 types * 2 (bold, italic).
			candidateFonts.reserve(6);

			// TODO: explicit Locale like RTL or Arabic fonts.
			// 1. Locale-specific fonts first
			if (!localeToUse.empty())
			{
				if (fontSpec.bold)
					candidateFonts.push_back(fmt::format("fonts.{}.{}.bold", localeToUse, fontSpec.type));
				if (fontSpec.italic)
					candidateFonts.push_back(fmt::format("fonts.{}.{}.italic", localeToUse, fontSpec.type));
				candidateFonts.push_back(fmt::format("fonts.{}.{}.regular", localeToUse, fontSpec.type));
			}

			// 2. Then global fonts
			if (fontSpec.bold)
				candidateFonts.push_back(fmt::format("fonts.{}.bold", fontSpec.type));
			if (fontSpec.italic)
				candidateFonts.push_back(fmt::format("fonts.{}.italic", fontSpec.type));
			candidateFonts.push_back(fmt::format("fonts.{}.regular", fontSpec.type));

			for (const std::string& key : candidateFonts)
			{
				std::map<CStr, CConfigValueSet> value{g_ConfigDB.GetValuesWithPrefix(CFG_COMMAND, key)};
				std::map<CStr, CConfigValueSet>::iterator item{value.find(key)};

				if (item != value.end() && !item->second.empty())
					return value;
			}

			// Fallback to default.
			return g_ConfigDB.GetValuesWithPrefix(CFG_COMMAND, defaultFont);
		}()
	};

	CFont font{this->m_FreeType.get(), *m_GammaCorrectionLUT};

	if (!font.SetFontParams(localeFontName.string(), fontSpec.size, fontSpec.stroke ? 1.0f : 0.0f, m_GUIScale))
	{
		LOGERROR("Failed to set font params for %s", localeFontName.string().c_str());
		return nullptr;
	}

	const VfsPath path(L"fonts/");
	for (const std::pair<const CStr, CConfigValueSet>& configPair : fontToSearch)
	{
		for (const CStr& fontPath : configPair.second)
		{
			const VfsPath fntPath{path / fontPath};
			if (!VfsFileExists(fntPath))
			{
				LOGERROR("Font file %s not found", fontPath.c_str());
				return nullptr;
			}

			if (!font.AddFontFromPath(fntPath))
			{
				LOGERROR("Failed to load font %s", fntPath.string8());
				return nullptr;
			}
		}
	}

	// Preload the common characters for visual quality.
	// Common characters are: Latin, numbers, punctuation.
	std::string_view glypshSet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,;:!?\"'()[]{}<>-+=_@#$%^&*`~\\|/";
	for (const char c : glypshSet)
		font.GetGlyph(c);

	return &m_Fonts.insert_or_assign(localeFontName, std::move(font)).first->second;
}

void CFontManager::UploadAtlasTexturesToGPU(Renderer::Backend::IDeviceCommandContext* deviceCommandContext)
{
	PROFILE2("Loading font textures");
	GPU_SCOPED_LABEL(deviceCommandContext, "Loading font textures");

	for (auto& [fontName, font] : m_Fonts)
		font.UploadAtlasTextureToGPU(deviceCommandContext);
}
