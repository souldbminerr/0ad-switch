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

#ifndef INCLUDED_FONT
#define INCLUDED_FONT

#include "graphics/Texture.h"
#include "lib/code_annotation.h"
#include "lib/os_path.h"
#include "lib/types.h"
#include "maths/Rect.h"
#include "renderer/backend/Format.h"

#include <array>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_IMAGE_H
#include FT_STROKER_H
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

class CVector2D;
namespace Renderer::Backend { class IDeviceCommandContext; }
namespace Renderer::Backend::Sampler { struct Desc; }

/**
 * Storage for a bitmap font. Loaded by CFontManager.
 */
class CFont
{
public:
	CFont(FT_Library library, const std::array<float, 256>& gammaCorrection) : m_FreeType{library}, m_GammaCorrectionLUT{gammaCorrection} {}
	~CFont() = default;
	MOVABLE(CFont);
	NONCOPYABLE(CFont);

	struct GlyphData
	{
		float u0, v0, u1, v1;
		float x0, y0, x1, y1;
		float xadvance;
		float yadvance;
		u8 defined{0};
		FT_Face face;
	};

	/**
	 * Relatively efficient lookup of GlyphData from 16-bit Unicode codepoint.
	 * This is stored as a sparse 2D array, exploiting the knowledge that a font
	 * typically only supports a small number of 256-codepoint blocks, so most
	 * elements of m_Data will be NULL.
	 * This implementation expand the store the GlyphData for a given Unicode codepoint (0 ≤ cp ≤ 0x10FFFF)
	 */
	class GlyphMap
	{
	public:
		GlyphMap() = default;
		~GlyphMap() = default;
		MOVABLE(GlyphMap);
		NONCOPYABLE(GlyphMap);

		/**
		* @brief Store the GlyphData for a given Unicode codepoint
		*
		* @param codepoint The unicode codepoint (0 ≤ cp ≤ 0x10FFFF)
		* @param glyph The glyphData data to store
		*/
		void set(u16 codepoint, const GlyphData& glyph);

		const GlyphData* get(u16 codepoint) const;
	private:
		std::unique_ptr<std::array<GlyphData, 256>> m_Data[256];
	};

	bool HasRGB() const { return m_HasRGB; }
	float GetHeight() const;

	/**
	* In typography, cap height is the height of a capital letter above the baseline for a particular typeface.
	* It specifically is the height of capital letters that are flat—such as H or I—as opposed to round letters such as O, or pointed letters like A,
	* both of which may display overshoot. The height of the small letters is the x-height.
	* REf: https://en.wikipedia.org/wiki/Cap_height
	*/
	float GetCapHeight();

	float GetCharacterWidth(wchar_t c);
	float GetStrokeWidth() const { return m_StrokeWidth; }
	void CalculateStringSize(const wchar_t* string, float& w, float& h);
	void GetGlyphBounds(float& x0, float& y0, float& x1, float& y1) const
	{
		x0 = m_Bounds.left;
		y0 = m_Bounds.top;
		x1 = m_Bounds.right;
		y1 = m_Bounds.bottom;
	}

	CTexturePtr GetTexture() const { return m_Texture; }
	void InitalizeAtlasTextureIfNeeded(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext);
	void UploadAtlasTextureToGPU(
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext);
	const GlyphData* GetGlyph(u16 i);

private:
	static void ftFaceDeleter(FT_Face face)
	{
		FT_Done_Face(face);
	}

	static void ftStrokerDeleter(FT_Stroker stroker)
	{
		FT_Stroker_Done(stroker);
	}

	using UniqueFTFace = std::unique_ptr<std::remove_pointer_t<FT_Face>, decltype(&ftFaceDeleter)>;
	using UniqueFTStroker = std::unique_ptr<std::remove_pointer_t<FT_Stroker>, decltype(&ftStrokerDeleter)>;

	friend class CFontManager;

	bool AddFontFromPath(const OsPath& fontPath);
	bool SetFontParams(const std::string& fontName, float size, float strokeWidth, float scale);

	void BlendGlyphBitmapToTexture(const FT_Bitmap& bitmap, int targetX, int targetY, u8 r, u8 g, u8 b);
	void BlendGlyphBitmapToTextureRGBA(const FT_Bitmap& bitmap, int targetX, int targetY, u8 r, u8 g, u8 b);
	void BlendGlyphBitmapToTextureR8(const FT_Bitmap& bitmap, int targetX, int targetY);

	std::optional<CVector2D> GenerateStrokeGlyphBitmap(const FT_Glyph& glyph, u16 codepoint, FT_Render_Mode renderMode, const float baselineInAtlas);
	std::optional<CVector2D> GenerateGlyphBitmap(FT_Glyph& glyph, u16 codepoint, FT_Render_Mode renderMode, CVector2D offset, const float baselineInAtlas);

	const GlyphData* ExtractAndGenerateGlyph(u16 codepoint);
	bool ConstructAtlasTexture();
	Renderer::Backend::Sampler::Desc ChooseTextureFormatAndSampler();

	CTexturePtr m_Texture;

	// True if RGBA, false if ALPHA.
	bool m_HasRGB{true};

	GlyphMap m_Glyphs;

	// Height of a capital letter, roughly.
	float m_Height;

	// Bounding box of all glyphs
	CRect m_Bounds;

	// The position for the next glyph in the texture atlas.
	int m_AtlasX;
	int m_AtlasY;

	// Size of the texture atlas.
	int m_AtlasWidth;
	int m_AtlasHeight;

	int m_AtlasPadding;
	bool m_IsDirty{false};
	bool m_IsTextureInitialized{false};
	float m_StrokeWidth{0.0f};
	float m_Scale{1.0f};

	std::unique_ptr<u8[]> m_TexData;
	Renderer::Backend::Format m_TextureFormat{Renderer::Backend::Format::R8G8B8A8_UNORM};
	int m_TextureFormatStride{4};
	int m_AtlasSize{0};

	float m_FontSize{0.0f};
	std::string m_FontName;
	std::reference_wrapper<const std::array<float, 256>> m_GammaCorrectionLUT;

	FT_Library m_FreeType;
	std::vector<std::shared_ptr<u8>> m_FontsData;
	std::vector<UniqueFTFace> m_Faces;
	UniqueFTStroker m_Stroker{nullptr, &ftStrokerDeleter};

	/**
	* Some fonts are not rendered well at small sizes, so we set a minimum size.
	* Because we are using a bitmap blending mode, when a font is using small size,
	* We need to use a different render mode, one with less antialiasing.
	*/
	static constexpr float MINIMAL_FONT_SIZE_ANTIALIASING{12.0f};
};

#endif // INCLUDED_FONT
