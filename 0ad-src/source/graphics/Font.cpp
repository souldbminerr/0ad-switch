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
#include "Font.h"

#include "graphics/TextureManager.h"
#include "lib/debug.h"
#include "lib/file/vfs/vfs.h"
#include "maths/Vector2D.h"
#include "ps/CLogger.h"
#include "ps/Filesystem.h"
#include "ps/Profiler2.h"
#include "ps/strings/StringBuilder.h"
#include "renderer/Renderer.h"
#include "renderer/backend/IDevice.h"
#include "renderer/backend/IDeviceCommandContext.h"
#include "renderer/backend/ITexture.h"
#include "renderer/backend/Sampler.h"

#include FT_ERRORS_H
#include FT_TYPES_H
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
/**
* FreeType represents most of its size and position values in 26.6 fixed-point format — that is,
* 26 bits for the integer part and 6 bits for the fractional part.
* FreeType's metrics such as: ascender, descender, height, advance, etc. are measured in 1/64th of a pixel.
*/
inline FT_F26Dot6 FloatToF26Dot6(float value)
{
    return static_cast<FT_F26Dot6>(std::lround(value * 64.0f));
}

inline float FPosF26Dot6ToFloat(FT_Pos value)
{
    return static_cast<float>(value) / 64.0f;
}

struct FTGlyphDeleter {
	void operator()(FT_Glyph glyph) const
	{
		FT_Done_Glyph(glyph);
	}
};

using UniqueFTGlyph = std::unique_ptr<std::remove_pointer_t<FT_Glyph>, FTGlyphDeleter>;

} // end namespace

const CFont::GlyphData* CFont::GlyphMap::get(u16 codepoint) const
{
	if (!m_Data[codepoint >> 8])
		return nullptr;
	if (!(*m_Data[codepoint >> 8])[codepoint & 0xff].defined)
		return nullptr;
	return &(*m_Data[codepoint >> 8])[codepoint & 0xff];
}

void CFont::GlyphMap::set(u16 codepoint, const GlyphData& glyph)
{
	if (!m_Data[codepoint >> 8])
		m_Data[codepoint >> 8] = std::make_unique<std::array<GlyphData, 256>>();
	(*m_Data[codepoint >> 8])[codepoint & 0xff] = glyph;
	(*m_Data[codepoint >> 8])[codepoint & 0xff].defined = 1;
}

float CFont::GetHeight() const {
	return m_Height / m_Scale;
}

float CFont::GetCapHeight()
{
	const CFont::GlyphData* g{GetGlyph(L'I')};
	return (g ? g->yadvance : 0) + std::abs(FPosF26Dot6ToFloat(m_Faces.front()->size->metrics.descender)) / m_Scale;
}

float CFont::GetCharacterWidth(wchar_t c)
{
	PROFILE2("GetCharacterWidth font texture generate");
	const CFont::GlyphData* g{GetGlyph(c)};

	return g ? g->xadvance : 0;
}

void CFont::CalculateStringSize(const wchar_t* string, float& width, float& height)
{
	PROFILE2("CalculateStringSize font texture generate");
	width = 0;
	height = 0;

	// Compute the width as the width of the longest line.
	std::wstring original{string};
	std::wistringstream stream{string};
	std::wstring line;
	bool firstLine{true};
	while (std::getline(stream, line))
	{
		FT_UInt glyphIndexStorage{0};
		const float lineWidth{std::accumulate(line.begin(), line.end(), 0.0f, [&](float sum, wchar_t c)
			{
				const CFont::GlyphData* g{GetGlyph(c)};

				if (!g)
					return sum;

				if (!FT_HAS_KERNING(g->face))
					return sum + g->xadvance;

				const FT_UInt glyphIndex{FT_Get_Char_Index(g->face, c)};
				if (!glyphIndex)
					return sum + g->xadvance;

				const FT_UInt prevGlyph{std::exchange(glyphIndexStorage, glyphIndex)};
				if (!prevGlyph)
					return sum + g->xadvance;

				// Get the kerning value between the previous and current glyph.
				FT_Vector kerning;
				FT_Get_Kerning(g->face, prevGlyph, glyphIndex, FT_KERNING_DEFAULT, &kerning);
				// Add the kerning distance.
				return sum + g->xadvance + FPosF26Dot6ToFloat(kerning.x);
			})
		};

		width = std::max(width, lineWidth);

		if (!firstLine || !line.empty())
			height += firstLine ? GetCapHeight() : GetHeight();

		firstLine = false;
	}

	if (original.back() == L'\n')
		height += GetHeight();
}

bool CFont::SetFontParams(const std::string& fontName, float size, float strokeWidth, float scale)
{
	ENSURE(m_FontSize == 0 && size > 0);

	// TODO: expose the Stroke Width outside class.
	m_Scale = scale;
	m_StrokeWidth = strokeWidth * scale;
	m_FontSize = size * scale;
	m_FontName = fontName;

	if (m_StrokeWidth)
	{
		FT_Stroker stroker;
		if (FT_Error error{FT_Stroker_New(m_FreeType, &stroker)})
		{
			LOGERROR("Failed to create stroker: %d", error);
			return false;
		}
		m_Stroker.reset(stroker);
		FT_Stroker_Set(m_Stroker.get(), FloatToF26Dot6(m_StrokeWidth), FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);
	}

	if (!ConstructAtlasTexture())
	{
		LOGERROR("Failed to create font texture atlas %s", fontName);
		return false;
	}

	return true;
}

bool CFont::AddFontFromPath(const OsPath& fontPath)
{
	ENSURE(m_FontSize > 0);

	if (!VfsFileExists(fontPath))
	{
		LOGERROR("Font file does not exist: %s", fontPath.string8());
		return false;
	}

	std::shared_ptr<u8> fontData;
	size_t fontDataSize;
	if (g_VFS->LoadFile(fontPath, fontData, fontDataSize) != 0)
	{
		LOGERROR("Failed to load font file: %s", fontPath.string8());
		return false;
	}

	FT_Face face;
	if (FT_Error error{FT_New_Memory_Face(m_FreeType, fontData.get(), static_cast<FT_Long>(fontDataSize), 0, &face)}; error == FT_Err_Unknown_File_Format)
	{
		LOGERROR("Font file format is not supported: %s", fontPath.string8());
		return false;
	}
	else if (error)
	{
		LOGERROR("Failed to load font %s: %d", fontPath.string8(), error);
		return false;
	}

	// Keep the font data alive.
	m_FontsData.push_back(fontData);

	// Set the font size.
	if (FT_Error error{FT_Set_Char_Size(face, 0, FloatToF26Dot6(m_FontSize), 0 , 0)})
	{
		LOGERROR("Failed to set font size %d: %d", m_FontSize, error);
		return false;
	}

	// Get the height of the font.
	if(m_Faces.empty())
		m_Height = FPosF26Dot6ToFloat(face->size->metrics.height);

	// Add the fallback font to the list.
	m_Faces.push_back({face, &ftFaceDeleter});

	return true;
}

Renderer::Backend::Sampler::Desc CFont::ChooseTextureFormatAndSampler()
{
	Renderer::Backend::Sampler::Desc defaultSamplerDesc{
		Renderer::Backend::Sampler::MakeDefaultSampler(
			Renderer::Backend::Sampler::Filter::LINEAR,
			Renderer::Backend::Sampler::AddressMode::CLAMP_TO_EDGE)
	};

	if (m_StrokeWidth > 0)
		return defaultSamplerDesc;

	// TODO: Add Support for R8_UNORM.
	// for R8 we will use texture swizzling to convert to RGBA.
	// and sampler will be changed

	// Legacy Format
	m_TextureFormat = Renderer::Backend::Format::A8_UNORM;
	m_TextureFormatStride = 1;
	m_HasRGB = false;

	return defaultSamplerDesc;
}

bool CFont::ConstructAtlasTexture()
{
	Renderer::Backend::IDevice* backendDevice = g_Renderer.GetDeviceCommandContext()->GetDevice();

	// Make backend texture ahead of time.
	// TODO: calculate based on device support.
	const int textureSize{1024};
	m_AtlasWidth = textureSize;
	m_AtlasHeight = textureSize;
	m_HasRGB = true;
	m_AtlasPadding = 4 + m_StrokeWidth * 2;
	m_AtlasX = m_AtlasY = 0;

	m_Bounds.right = textureSize;
	m_Bounds.bottom = textureSize;

	// TODO: preload from cache?.

	const Renderer::Backend::Sampler::Desc defaultSamplerDesc{ChooseTextureFormatAndSampler()};

	m_AtlasSize = m_AtlasWidth * m_AtlasHeight * m_TextureFormatStride;

	char buffer[128];
	PS::StringBuilder fontTextureNameBuilder{{std::begin(buffer), std::end(buffer)}};
	fontTextureNameBuilder.Append("Font Texture ");
	fontTextureNameBuilder.Append(m_FontName);
	m_Texture = g_Renderer.GetTextureManager().WrapBackendTexture(backendDevice->CreateTexture2D(
		fontTextureNameBuilder.Str().data(),
		Renderer::Backend::ITexture::Usage::TRANSFER_DST |
			Renderer::Backend::ITexture::Usage::SAMPLED,
		m_TextureFormat,
		textureSize, textureSize, defaultSamplerDesc
	));

	if (!m_Texture)
	{
		LOGERROR("Failed to create font texture %s", m_FontName);
		return false;
	}

	// Initialise texture with transparency, for the areas we don't
	// overwrite with uploading later.
	m_TexData = std::make_unique<u8[]>(m_AtlasSize);
	std::fill_n(m_TexData.get(), m_AtlasSize, 0x00);

	m_IsTextureInitialized = false;

	return true;
}

void CFont::InitalizeAtlasTextureIfNeeded(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext)
{
	if (m_IsTextureInitialized)
		return;

	deviceCommandContext->UploadTexture(
		m_Texture->GetBackendTexture(), m_TextureFormat,
		m_TexData.get(), m_AtlasSize);

	m_IsTextureInitialized = true;
}

const CFont::GlyphData* CFont::GetGlyph(u16 codepoint)
{
	const CFont::GlyphData* g{m_Glyphs.get(codepoint)};
	return (g && g->defined) ? g : ExtractAndGenerateGlyph(codepoint);
}

const CFont::GlyphData* CFont::ExtractAndGenerateGlyph(u16 codepoint)
{
	ENSURE(!m_Faces.empty());
	PROFILE2("Glyph font texture generate");

	const auto [faceToUse, glyphIndex]{[&]()->std::pair<FT_Face, FT_UInt>
		{
			FT_UInt index{0};
			std::vector<UniqueFTFace>::iterator it{std::find_if(m_Faces.begin(), m_Faces.end(), [&](const UniqueFTFace& face)
				{
					index = FT_Get_Char_Index(face.get(), codepoint);
					return index != 0;
				}
			)};

			return {it != m_Faces.end() ? it->get() : m_Faces.front().get(), index};
		}()
	};
	const FT_Int32 loadFlags{FT_LOAD_DEFAULT | (m_FontSize <= MINIMAL_FONT_SIZE_ANTIALIASING ? FT_LOAD_TARGET_MONO : 0)};

	if (FT_Error error{FT_Load_Glyph(faceToUse, glyphIndex, loadFlags)})
	{
		LOGERROR("Failed to load glyph %u: %d", codepoint, error);
		return nullptr;
	}

	const FT_GlyphSlot slot{faceToUse->glyph};
	FT_Glyph glyph;
	if (FT_Error error{FT_Get_Glyph(slot, &glyph)})
	{
		LOGERROR("Failed to get glyph %u: %d", codepoint, error);
		return nullptr;
	}
	UniqueFTGlyph glyphPtr(glyph);

	const float baselineInAtlas{FPosF26Dot6ToFloat(faceToUse->size->metrics.ascender)};
	const float glyphW{FPosF26Dot6ToFloat(slot->advance.x)};

	if (m_AtlasX + glyphW + m_StrokeWidth + m_AtlasPadding > m_AtlasWidth)
	{
		m_AtlasX = 0;
		m_AtlasY += std::ceil(m_Height + m_StrokeWidth + m_AtlasPadding);
	}

	if (m_AtlasY + m_Height + m_StrokeWidth + m_AtlasPadding > m_AtlasHeight)
	{
		LOGERROR("Font texture atlas is full, cannot load more glyphs");
		return nullptr;
	}

	m_IsDirty = true;
	CVector2D offset{0.0f, 0.0f};

	const FT_Render_Mode renderMode{FT_RENDER_MODE_NORMAL};

	if (m_StrokeWidth)
	{
		std::optional<CVector2D> offsetStroke{GenerateStrokeGlyphBitmap(glyph, codepoint, renderMode, baselineInAtlas)};
		if (!offsetStroke.has_value())
		{
			LOGERROR("Failed to generate stroke glyph %u", codepoint);
			return nullptr;
		}

		offset = offsetStroke.value();
	}

	std::optional<CVector2D> offsetGlyph{GenerateGlyphBitmap(glyph, codepoint, renderMode, offset, baselineInAtlas)};
	if (!offsetGlyph.has_value())
	{
		LOGERROR("Failed to generate glyph %u", codepoint);
		return nullptr;
	}
	offset = offsetGlyph.value();

	CFont::GlyphData gd;
	gd.u0 = static_cast<float>(m_AtlasX) / m_AtlasWidth;
	gd.v0 = static_cast<float>(m_AtlasY) / m_AtlasHeight;
	gd.u1 = static_cast<float>(m_AtlasX - offset.X + glyphW + m_StrokeWidth * 2) / m_AtlasWidth;
	gd.v1 = static_cast<float>(m_AtlasY + offset.Y + m_Height + m_StrokeWidth * 2) / m_AtlasHeight;

	gd.x0 = (offset.X - m_StrokeWidth) / m_Scale;
	gd.y0 = (-(m_Height + offset.Y + m_StrokeWidth)) / m_Scale;
	gd.x1 = (glyphW + m_StrokeWidth) / m_Scale;
	gd.y1 = m_StrokeWidth / m_Scale;

	gd.xadvance = glyphW / m_Scale;
	gd.yadvance = FPosF26Dot6ToFloat(slot->metrics.height) / m_Scale;
	gd.defined = 1;
	gd.face = faceToUse;

	m_Glyphs.set(codepoint, gd);

	// Update positions for next glyph.
	m_AtlasX += std::ceil(glyphW + m_StrokeWidth + m_AtlasPadding);

	return m_Glyphs.get(codepoint);
}

std::optional<CVector2D> CFont::GenerateStrokeGlyphBitmap(const FT_Glyph& glyph, u16 codepoint, FT_Render_Mode renderMode, const float baselineInAtlas)
{
	FT_Glyph strokedGlyph;
	if (FT_Error error{FT_Glyph_Copy(glyph, &strokedGlyph)})
	{
		LOGERROR("Failed to copy glyph %u: %d", codepoint, error);
		FT_Done_Glyph(strokedGlyph);
		return std::nullopt;
	}

	if (FT_Error error{FT_Glyph_StrokeBorder(&strokedGlyph, m_Stroker.get(), 0, 1)})
	{
		LOGERROR("Failed to stroke glyph %u: %d", codepoint, error);
		FT_Done_Glyph(strokedGlyph);
		return std::nullopt;
	}

	if (FT_Error error{FT_Glyph_To_Bitmap(&strokedGlyph, renderMode, nullptr, 1)})
	{
		LOGERROR("Failed to render glyph %u: %d", codepoint, error);
		FT_Done_Glyph(strokedGlyph);
		return std::nullopt;
	}

	FT_BitmapGlyph bitmapGlyph{reinterpret_cast<FT_BitmapGlyph>(strokedGlyph)};
	FT_Bitmap& bitmapStroke{bitmapGlyph->bitmap};

	CVector2D offset{0.0f, 0.0f};
	int targetStrokeY{static_cast<int>(std::ceil(m_AtlasY + m_StrokeWidth + baselineInAtlas - bitmapGlyph->top))};
	int targetStrokeX{static_cast<int>(std::ceil(m_AtlasX + m_StrokeWidth + bitmapGlyph->left))};
	if (targetStrokeX < m_AtlasX)
	{
		offset.X = bitmapGlyph->left + m_StrokeWidth;
		targetStrokeX = m_AtlasX;
	}
	if (targetStrokeY < m_AtlasY)
	{
		offset.Y = bitmapGlyph->top - baselineInAtlas - m_StrokeWidth;
		targetStrokeY = m_AtlasY;
	}
	BlendGlyphBitmapToTexture(bitmapStroke, targetStrokeX, targetStrokeY, 0, 0, 0);
	FT_Done_Glyph(strokedGlyph);
	return offset;
}

std::optional<CVector2D> CFont::GenerateGlyphBitmap(FT_Glyph& glyph, u16 codepoint, FT_Render_Mode renderMode, CVector2D offset, const float baselineInAtlas)
{
	if (FT_Error error{FT_Glyph_To_Bitmap(&glyph, renderMode, nullptr, 0)})
	{
		LOGERROR("Failed to render glyph %u: %d", codepoint, error);
		return std::nullopt;
	}
	FT_BitmapGlyph bitmapGlyph{reinterpret_cast<FT_BitmapGlyph>(glyph)};
	FT_Bitmap& bitmap{bitmapGlyph->bitmap};

	int targetY{static_cast<int>(std::ceil(m_AtlasY + offset.Y + m_StrokeWidth + baselineInAtlas - bitmapGlyph->top))};
	int targetX{static_cast<int>(std::ceil(m_AtlasX - offset.X + m_StrokeWidth + bitmapGlyph->left))};
	CVector2D newOffset{0.0f, 0.0f};
	if (targetX < m_AtlasX)
	{
		newOffset.X = bitmapGlyph->left + m_StrokeWidth;
		targetX = m_AtlasX;
	}

	if (targetY < m_AtlasY)
	{
		newOffset.Y = bitmapGlyph->top - baselineInAtlas - m_StrokeWidth;
		targetY = m_AtlasY;
	}

	BlendGlyphBitmapToTexture(bitmap, targetX, targetY, 255, 255, 255);
	FT_Done_Glyph(glyph);
	return newOffset;
}

void CFont::UploadAtlasTextureToGPU(Renderer::Backend::IDeviceCommandContext* deviceCommandContext)
{
	if (!m_IsDirty)
		return;

	deviceCommandContext->UploadTexture(
		m_Texture->GetBackendTexture(),
		m_TextureFormat,
		m_TexData.get(),
		m_AtlasSize
	);

	m_IsDirty = false;
}

void CFont::BlendGlyphBitmapToTexture(const FT_Bitmap& bitmap, int targetX, int targetY, u8 r, u8 g, u8 b)
{
	PROFILE2("BlendGlyphBitmapToTexture font texture generate");
	if (m_TextureFormat == Renderer::Backend::Format::R8G8B8A8_UNORM)
		BlendGlyphBitmapToTextureRGBA(bitmap, targetX, targetY, r, g, b);
	else
		BlendGlyphBitmapToTextureR8(bitmap, targetX, targetY);
}

void CFont::BlendGlyphBitmapToTextureRGBA(const FT_Bitmap& bitmap, int targetX, int targetY, u8 r, u8 g, u8 b)
{
	for (uint y{0}; y != bitmap.rows; ++y)
	{
		const u8* srcRow{bitmap.buffer + y * bitmap.pitch};
		u8* dstRow{m_TexData.get() + ((targetY + y) * m_AtlasWidth + targetX) * m_TextureFormatStride};

		for (uint x{0}; x != bitmap.width; ++x)
		{
			u8* tempDstRow{dstRow + x * m_TextureFormatStride};
			u8 alpha{srcRow[x]};

			const float srcAlpha{m_StrokeWidth > 0 ? m_GammaCorrectionLUT.get()[alpha] : alpha / 255.0f};
			const float dstAlpha{tempDstRow[3] / 255.0f};
			const float outAlpha{srcAlpha + dstAlpha * (1.0f - srcAlpha)};

			if (outAlpha == 0.0f)
				continue;

			tempDstRow[0] = static_cast<u8>(std::round(((r * srcAlpha + tempDstRow[0] * dstAlpha * (1.0f - srcAlpha)) / outAlpha)));
			tempDstRow[1] = static_cast<u8>(std::round(((g * srcAlpha + tempDstRow[1] * dstAlpha * (1.0f - srcAlpha)) / outAlpha)));
			tempDstRow[2] = static_cast<u8>(std::round(((b * srcAlpha + tempDstRow[2] * dstAlpha * (1.0f - srcAlpha)) / outAlpha)));
			tempDstRow[3] = static_cast<u8>(std::round(outAlpha * 255.0f));
		}
	}
}

void  CFont::BlendGlyphBitmapToTextureR8(const FT_Bitmap& bitmap, int targetX, int targetY)
{
	for (uint y{0}; y != bitmap.rows; ++y)
	{
		const u8* srcRow{bitmap.buffer + y * bitmap.pitch};
		u8* dstRow{m_TexData.get() + ((targetY + y) * m_AtlasWidth + targetX)};

		std::memcpy(dstRow, srcRow, bitmap.width);
	}
}
