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

#include "TextRenderer.h"

#include "graphics/Font.h"
#include "graphics/FontManager.h"
#include "graphics/TextureManager.h"
#include "lib/code_annotation.h"
#include "lib/debug.h"
#include "lib/types.h"
#include "lib/utf8.h"
#include "ps/CStr.h"
#include "ps/CStrIntern.h"
#include "ps/CStrInternStatic.h"
#include "ps/ConfigDB.h"
#include "renderer/Renderer.h"
#include "renderer/backend/IDeviceCommandContext.h"
#include "renderer/backend/IShaderProgram.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <vector>

namespace
{

// We can't draw chars more than vertices, currently we use 4 vertices per char.
constexpr size_t MAX_CHAR_COUNT_PER_BATCH = 65536 / 4;

} // anonymous namespace

CTextRenderer::CTextRenderer()
	: m_ScopedLinearAllocator{g_Renderer.GetLinearAllocator()}, m_Batches(m_ScopedLinearAllocator)
{
	ResetTranslate();
	SetCurrentColor(CColor(1.0f, 1.0f, 1.0f, 1.0f));
	SetCurrentFont(str_sans_10, CStrIntern{});
}

void CTextRenderer::ResetTranslate(const CVector2D& translate)
{
	m_Translate = translate;
	m_Dirty = true;
}

void CTextRenderer::Translate(float x, float y)
{
	m_Translate += CVector2D{x, y};
	m_Dirty = true;
}

void CTextRenderer::SetClippingRect(const CRect& rect)
{
	m_Clipping = rect;
}

void CTextRenderer::SetCurrentColor(const CColor& color)
{
	if (m_Color != color)
	{
		m_Color = color;
		m_Dirty = true;
	}
}

void CTextRenderer::SetCurrentFont(CStrIntern font, CStrIntern locale)
{
	if (font != m_FontName)
	{
		m_FontName = font;
		m_Font = g_Renderer.GetFontManager().LoadFont(font, locale);
		m_Dirty = true;
	}
}

void CTextRenderer::PrintfAdvance(const wchar_t* fmt, ...)
{
	wchar_t buf[1024] = {0};

	va_list args;
	va_start(args, fmt);
	int ret = vswprintf(buf, ARRAY_SIZE(buf)-1, fmt, args);
	va_end(args);

	if (ret < 0)
		debug_printf("CTextRenderer::Printf vswprintf failed (buffer size exceeded?) - return value %d, errno %d\n", ret, errno);

	PutAdvance(buf);
}


void CTextRenderer::PrintfAt(float x, float y, const wchar_t* fmt, ...)
{
	wchar_t buf[1024] = {0};

	va_list args;
	va_start(args, fmt);
	int ret = vswprintf(buf, ARRAY_SIZE(buf)-1, fmt, args);
	va_end(args);

	if (ret < 0)
		debug_printf("CTextRenderer::PrintfAt vswprintf failed (buffer size exceeded?) - return value %d, errno %d\n", ret, errno);

	Put(x, y, buf);
}

void CTextRenderer::PutAdvance(const wchar_t* buf)
{
	Put(0.0f, 0.0f, buf);

	float w, h;
	m_Font->CalculateStringSize(buf, w, h);
	Translate(w, 0.0f);
}

void CTextRenderer::Put(float x, float y, const wchar_t* buf)
{
	if (buf[0] == 0)
		return; // empty string; don't bother storing

	PutString(x, y, new std::wstring(buf), true);
}

void CTextRenderer::Put(float x, float y, const char* buf)
{
	if (buf[0] == 0)
		return; // empty string; don't bother storing

	PutString(x, y, new std::wstring(wstring_from_utf8(buf)), true);
}

void CTextRenderer::Put(float x, float y, const std::wstring* buf)
{
	if (buf->empty())
		return; // empty string; don't bother storing

	PutString(x, y, buf, false);
}

void CTextRenderer::PutString(float x, float y, const std::wstring* buf, bool owned)
{
	if (!m_Font)
		return; // invalid font; can't render

	if (m_Clipping != CRect())
	{
		float x0, y0, x1, y1;
		m_Font->GetGlyphBounds(x0, y0, x1, y1);
		if (y + y1 < m_Clipping.top && y + y0 > m_Clipping.bottom)
			return;
	}

	// If any state has changed since the last batch, start a new batch
	if (m_Dirty)
	{
		SBatch batch{m_ScopedLinearAllocator};
		batch.chars = 0;
		batch.translate = m_Translate;
		batch.color = m_Color;
		batch.font = m_Font;
		m_Batches.emplace_back(batch);
		m_Dirty = false;
	}

	// Push a new run onto the latest batch
	SBatchRun run;
	run.x = x;
	run.y = y;
	m_Batches.back().runs.emplace_back(run);
	m_Batches.back().runs.back().text = buf;
	m_Batches.back().runs.back().owned = owned;
	m_Batches.back().chars += buf->size();
}

struct SBatchCompare
{
	bool operator()(const CTextRenderer::SBatch& a, const CTextRenderer::SBatch& b)
	{
		if (a.font != b.font)
			return a.font < b.font;
		// TODO: is it worth sorting by color/translate too?
		return false;
	}
};

void CTextRenderer::Render(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
	Renderer::Backend::IShaderProgram* shader,
	const CVector2D& transformScale, const CVector2D& translation,
	const bool debugFontBox, const CColor& debugBoxColor)
{
	std::vector<u16, ProxyAllocator<u16, PS::Memory::ScopedLinearAllocator>> indices{m_ScopedLinearAllocator};
	std::vector<CVector2D, ProxyAllocator<CVector2D, PS::Memory::ScopedLinearAllocator>> positions{m_ScopedLinearAllocator};
	std::vector<CVector2D, ProxyAllocator<CVector2D, PS::Memory::ScopedLinearAllocator>> uvs{m_ScopedLinearAllocator};

	// Try to merge non-consecutive batches that share the same font/color/translate:
	// sort the batch list by font, then merge the runs of adjacent compatible batches
	m_Batches.sort(SBatchCompare());
	for (SBatchList::iterator it = m_Batches.begin(); it != m_Batches.end(); )
	{
		SBatchList::iterator next = std::next(it);
		if (next != m_Batches.end() && it->chars + next->chars <= MAX_CHAR_COUNT_PER_BATCH && it->font == next->font && it->color == next->color && it->translate == next->translate)
		{
			it->chars += next->chars;
			it->runs.splice(it->runs.end(), next->runs);
			m_Batches.erase(next);
		}
		else
			++it;
	}

	const int32_t texBindingSlot = shader->GetBindingSlot(str_tex);
	const int32_t translationBindingSlot = shader->GetBindingSlot(str_translation);
	const int32_t colorAddBindingSlot = shader->GetBindingSlot(str_colorAdd);
	const int32_t colorMulBindingSlot = shader->GetBindingSlot(str_colorMul);

	bool translationChanged = false;

	CTexture* lastTexture = nullptr;
	for (SBatch& batch : m_Batches)
	{
		if (lastTexture != batch.font->GetTexture().get())
		{
			batch.font->InitalizeAtlasTextureIfNeeded(deviceCommandContext);
			lastTexture = batch.font->GetTexture().get();
			lastTexture->UploadBackendTextureIfNeeded(deviceCommandContext);
			deviceCommandContext->SetTexture(texBindingSlot, lastTexture->GetBackendTexture());
		}

		if (batch.translate.X != 0.0f || batch.translate.Y != 0.0f)
		{
			const CVector2D localTranslation =
				translation + CVector2D(batch.translate.X * transformScale.X, batch.translate.Y * transformScale.Y);
			deviceCommandContext->SetUniform(translationBindingSlot, localTranslation.AsFloatArray());
			translationChanged = true;
		}

		CColor boxColor;

		// ALPHA-only textures will have .rgb sampled as 0, so we need to
		// replace it with white (but not affect RGBA textures)
		if (!debugFontBox && batch.font->HasRGB())
			boxColor = CColor(0.0f, 0.0f, 0.0f, 0.0f);
		else if (debugFontBox && batch.font->HasRGB())
			boxColor = debugBoxColor;
		else
			boxColor = CColor(batch.color.r, batch.color.g, batch.color.b, debugFontBox ? debugBoxColor.r : 0);

		deviceCommandContext->SetUniform(colorAddBindingSlot, boxColor.AsFloatArray());
		deviceCommandContext->SetUniform(colorMulBindingSlot, batch.color.AsFloatArray());

		positions.resize(std::min(MAX_CHAR_COUNT_PER_BATCH, batch.chars) * 4);
		uvs.resize(std::min(MAX_CHAR_COUNT_PER_BATCH, batch.chars) * 4);
		indices.resize(std::min(MAX_CHAR_COUNT_PER_BATCH, batch.chars) * 6);

		size_t idx = 0;

		auto flush = [deviceCommandContext, &idx, &positions, &uvs, &indices]() -> void
		{
			if (idx == 0)
				return;

			deviceCommandContext->SetVertexBufferData(
				0, positions.data(), positions.size() * sizeof(positions[0]));
			deviceCommandContext->SetVertexBufferData(
				1, uvs.data(), uvs.size() * sizeof(uvs[0]));
			deviceCommandContext->SetIndexBufferData(
				indices.data(), indices.size() * sizeof(indices[0]));

			deviceCommandContext->DrawIndexed(0, idx * 6, 0);
			idx = 0;
		};

		for (SBatchRun& run : batch.runs)
		{
			float x{std::ceil(run.x)};
			float y{std::ceil(run.y)};
			for (size_t i = 0; i < run.text->size(); ++i)
			{
				const CFont::GlyphData* g = batch.font->GetGlyph((*run.text)[i]);

				// Use the missing glyph symbol.
				if (!g)
					g = batch.font->GetGlyph(0xFFFD);

				// Missing the missing glyph symbol - give up.
				if (!g)
					continue;

				uvs[idx*4].X = g->u1;
				uvs[idx*4].Y = g->v0;
				positions[idx*4].X = g->x1 + x;
				positions[idx*4].Y = g->y0 + y;

				uvs[idx*4+1].X = g->u0;
				uvs[idx*4+1].Y = g->v0;
				positions[idx*4+1].X = g->x0 + x;
				positions[idx*4+1].Y = g->y0 + y;

				uvs[idx*4+2].X = g->u0;
				uvs[idx*4+2].Y = g->v1;
				positions[idx*4+2].X = g->x0 + x;
				positions[idx*4+2].Y = g->y1 + y;

				uvs[idx*4+3].X = g->u1;
				uvs[idx*4+3].Y = g->v1;
				positions[idx*4+3].X = g->x1 + x;
				positions[idx*4+3].Y = g->y1 + y;

				indices[idx*6+0] = static_cast<u16>(idx*4+0);
				indices[idx*6+1] = static_cast<u16>(idx*4+1);
				indices[idx*6+2] = static_cast<u16>(idx*4+2);
				indices[idx*6+3] = static_cast<u16>(idx*4+2);
				indices[idx*6+4] = static_cast<u16>(idx*4+3);
				indices[idx*6+5] = static_cast<u16>(idx*4+0);

				x += g->xadvance;

				++idx;
				if (idx == MAX_CHAR_COUNT_PER_BATCH)
					flush();
			}
		}

		flush();
	}

	m_Batches.clear();

	if (translationChanged)
		deviceCommandContext->SetUniform(translationBindingSlot, translation.AsFloatArray());
}
