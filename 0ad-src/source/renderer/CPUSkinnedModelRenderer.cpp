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

#include "CPUSkinnedModelRenderer.h"

#include "graphics/MeshManager.h"
#include "graphics/Model.h"
#include "graphics/ModelDef.h"
#include "graphics/RenderableObject.h"
#include "lib/debug.h"
#include "ps/containers/StaticVector.h"
#include "renderer/ModelRenderer.h"
#include "renderer/Renderer.h"
#include "renderer/VertexArray.h"
#include "renderer/backend/Format.h"
#include "renderer/backend/IBuffer.h"
#include "renderer/backend/IDeviceCommandContext.h"
#include "renderer/backend/IShaderProgram.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class CVector3D;

namespace
{

constexpr uint32_t MODEL_VERTEX_ATTRIBUTE_STRIDE = 32;
constexpr uint32_t MODEL_VERTEX_ATTRIBUTE_POSITION_OFFSET = 16;
constexpr uint32_t MODEL_VERTEX_ATTRIBUTE_NORMAL_OFFSET = 0;

struct ModelDefRData : public CModelDefRPrivate
{
	/// Indices are the same for all models, so share them
	VertexIndexArray m_IndexArray;

	/// Static per-CModelDef vertex array
	VertexArray m_Array;

	/// The number of UVs is determined by the model
	std::vector<VertexArray::Attribute> m_UVs;

	Renderer::Backend::IVertexInputLayout* m_VertexInputLayout = nullptr;

	ModelDefRData(const CModelDefPtr& mdef);
};

ModelDefRData::ModelDefRData(const CModelDefPtr& mdef)
	: m_IndexArray(Renderer::Backend::IBuffer::Usage::TRANSFER_DST),
	m_Array(Renderer::Backend::IBuffer::Type::VERTEX,
		Renderer::Backend::IBuffer::Usage::TRANSFER_DST)
{
	size_t numVertices = mdef->GetNumVertices();

	m_UVs.resize(mdef->GetNumUVsPerVertex());
	for (size_t i = 0; i < mdef->GetNumUVsPerVertex(); ++i)
	{
		m_UVs[i].format = Renderer::Backend::Format::R32G32_SFLOAT;
		m_Array.AddAttribute(&m_UVs[i]);
	}

	m_Array.SetNumberOfVertices(numVertices);
	m_Array.Layout();

	for (size_t i = 0; i < mdef->GetNumUVsPerVertex(); ++i)
	{
		VertexArrayIterator<float[2]> UVit = m_UVs[i].GetIterator<float[2]>();
		ModelRenderer::BuildUV(mdef, UVit, i);
	}

	m_Array.Upload();
	m_Array.FreeBackingStore();

	m_IndexArray.SetNumberOfVertices(mdef->GetNumFaces()*3);
	m_IndexArray.Layout();
	ModelRenderer::BuildIndices(mdef, m_IndexArray.GetIterator());
	m_IndexArray.Upload();
	m_IndexArray.FreeBackingStore();

	const uint32_t stride = m_Array.GetStride();
	PS::StaticVector<Renderer::Backend::SVertexAttributeFormat, 4> attributes{
		{Renderer::Backend::VertexAttributeStream::UV0,
			m_UVs[0].format, m_UVs[0].offset, stride,
			Renderer::Backend::VertexAttributeRate::PER_VERTEX, 0},
		{Renderer::Backend::VertexAttributeStream::POSITION,
			Renderer::Backend::Format::R32G32B32_SFLOAT,
			MODEL_VERTEX_ATTRIBUTE_POSITION_OFFSET, MODEL_VERTEX_ATTRIBUTE_STRIDE,
			Renderer::Backend::VertexAttributeRate::PER_VERTEX, 1},
		{Renderer::Backend::VertexAttributeStream::NORMAL,
			Renderer::Backend::Format::R32G32B32_SFLOAT,
			MODEL_VERTEX_ATTRIBUTE_NORMAL_OFFSET, MODEL_VERTEX_ATTRIBUTE_STRIDE,
			Renderer::Backend::VertexAttributeRate::PER_VERTEX, 1}
	};

	if (mdef->GetNumUVsPerVertex() >= 2)
	{
		attributes.push_back({
			Renderer::Backend::VertexAttributeStream::UV1,
			m_UVs[1].format, m_UVs[1].offset, stride,
			Renderer::Backend::VertexAttributeRate::PER_VERTEX, 0});
	}

	m_VertexInputLayout = g_Renderer.GetVertexInputLayout({attributes.begin(), attributes.end()});
}

struct ModelRData : public CModelRData
{
	/// Dynamic per-CModel vertex array
	VertexArray m_Array;

	/// Position and normals/lighting are recalculated on CPU every frame
	VertexArray::Attribute m_Position;
	VertexArray::Attribute m_Normal;

	ModelRData(const void* key)
		: CModelRData(key),
		m_Array(Renderer::Backend::IBuffer::Type::VERTEX,
			Renderer::Backend::IBuffer::Usage::DYNAMIC | Renderer::Backend::IBuffer::Usage::TRANSFER_DST)
	{}
};

} // anonymous namespace

struct CPUSkinnedModelVertexRenderer::Internals
{
	// Previously prepared modeldef
	ModelDefRData* modelDefRData{nullptr};
};

// Construction and Destruction
CPUSkinnedModelVertexRenderer::CPUSkinnedModelVertexRenderer()
	: m(std::make_unique<Internals>())
{
}

CPUSkinnedModelVertexRenderer::~CPUSkinnedModelVertexRenderer() = default;

// Build model data (and modeldef data if necessary)
CModelRData* CPUSkinnedModelVertexRenderer::CreateModelData(const void* key, CModel* model)
{
	CModelDefPtr mdef = model->GetModelDef();
	ModelDefRData* modelDefRData = static_cast<ModelDefRData*>(mdef->GetRenderData(m.get()));

	if (!modelDefRData)
	{
		modelDefRData = new ModelDefRData(mdef);
		mdef->SetRenderData(m.get(), modelDefRData);
	}

	// Build the per-model data
	ModelRData* modelRData = new ModelRData(key);

	// Positions and normals must be 16-byte aligned for SSE writes.

	modelRData->m_Position.format = Renderer::Backend::Format::R32G32B32A32_SFLOAT;
	modelRData->m_Array.AddAttribute(&modelRData->m_Position);

	modelRData->m_Normal.format = Renderer::Backend::Format::R32G32B32A32_SFLOAT;
	modelRData->m_Array.AddAttribute(&modelRData->m_Normal);

	modelRData->m_Array.SetNumberOfVertices(mdef->GetNumVertices());
	modelRData->m_Array.Layout();

	// Verify alignment
	ENSURE(modelRData->m_Position.offset % 16 == 0);
	ENSURE(modelRData->m_Normal.offset % 16 == 0);
	ENSURE(modelRData->m_Array.GetStride() % 16 == 0);

	// We assume that the vertex input layout is the same for all models with the
	// same ModelDefRData.
	// TODO: we need a more strict way to guarantee that.
	ENSURE(modelRData->m_Array.GetStride() == MODEL_VERTEX_ATTRIBUTE_STRIDE);
	ENSURE(modelRData->m_Position.offset == MODEL_VERTEX_ATTRIBUTE_POSITION_OFFSET);
	ENSURE(modelRData->m_Normal.offset == MODEL_VERTEX_ATTRIBUTE_NORMAL_OFFSET);

	return modelRData;
}

void CPUSkinnedModelVertexRenderer::UpdateModelsData(Renderer::Backend::IDeviceCommandContext*,
	std::span<CModel*> models)
{
	for (CModel* model : models)
	{
		CModelRData* rdata = static_cast<CModelRData*>(model->GetRenderData());
		UpdateModelData(model, rdata, rdata->m_UpdateFlags);
	}
}

// Fill in and upload dynamic vertex array
void CPUSkinnedModelVertexRenderer::UpdateModelData(CModel* model, CModelRData* data, int updateflags)
{
	ModelRData* modelRData = static_cast<ModelRData*>(data);

	if (updateflags & RENDERDATA_UPDATE_VERTICES)
	{
		// build vertices
		VertexArrayIterator<CVector3D> Position = modelRData->m_Position.GetIterator<CVector3D>();
		VertexArrayIterator<CVector3D> Normal = modelRData->m_Normal.GetIterator<CVector3D>();

		ModelRenderer::BuildPositionAndNormals(model, Position, Normal);

		// upload everything to vertex buffer
		modelRData->m_Array.Upload();
	}

	modelRData->m_Array.PrepareForRendering();
}

void CPUSkinnedModelVertexRenderer::UploadModelsData(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
	std::span<CModel*> models)
{
	for (CModel* model : models)
	{
		CModelRData* rdata = static_cast<CModelRData*>(model->GetRenderData());
		UploadModelData(deviceCommandContext, model, rdata);
	}
}

void CPUSkinnedModelVertexRenderer::UploadModelData(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
	CModel* model, CModelRData* data)
{
	ModelDefRData* modelDefRData = static_cast<ModelDefRData*>(model->GetModelDef()->GetRenderData(m.get()));
	ENSURE(modelDefRData);

	modelDefRData->m_Array.UploadIfNeeded(deviceCommandContext);
	modelDefRData->m_IndexArray.UploadIfNeeded(deviceCommandContext);

	ModelRData* modelRData = static_cast<ModelRData*>(data);

	modelRData->m_Array.UploadIfNeeded(deviceCommandContext);
}

// Prepare UV coordinates for this modeldef
void CPUSkinnedModelVertexRenderer::PrepareModelDef(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext,
	const CModelDef& def)
{
	m->modelDefRData = static_cast<ModelDefRData*>(def.GetRenderData(m.get()));
	ENSURE(m->modelDefRData);

	deviceCommandContext->SetVertexInputLayout(m->modelDefRData->m_VertexInputLayout);

	const uint32_t stride = m->modelDefRData->m_Array.GetStride();
	const uint32_t firstVertexOffset = m->modelDefRData->m_Array.GetOffset() * stride;

	deviceCommandContext->SetVertexBuffer(
		0, m->modelDefRData->m_Array.GetBuffer(), firstVertexOffset);
}

// Render one model
void CPUSkinnedModelVertexRenderer::RenderModel(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext, Renderer::Backend::IShaderProgram*,
	CModel* model, CModelRData* data)
{
	const CModelDefPtr& mdldef = model->GetModelDef();
	ModelRData* modelRData = static_cast<ModelRData*>(data);

	const uint32_t stride = modelRData->m_Array.GetStride();
	const uint32_t firstVertexOffset = modelRData->m_Array.GetOffset() * stride;

	deviceCommandContext->SetVertexBuffer(
		1, modelRData->m_Array.GetBuffer(), firstVertexOffset);
	deviceCommandContext->SetIndexBuffer(m->modelDefRData->m_IndexArray.GetBuffer());

	// Render the lot.
	const size_t numberOfFaces = mdldef->GetNumFaces();

	deviceCommandContext->DrawIndexedInRange(
		m->modelDefRData->m_IndexArray.GetOffset(), numberOfFaces * 3, 0, mdldef->GetNumVertices() - 1);

	// Bump stats.
	g_Renderer.m_Stats.m_DrawCalls++;
	g_Renderer.m_Stats.m_ModelTris += numberOfFaces;
}
