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

#ifndef INCLUDED_RENDERER_BACKEND_VULKAN_MAPPING
#define INCLUDED_RENDERER_BACKEND_VULKAN_MAPPING

#include <cstdint>
#include <glad/vulkan.h>

namespace Renderer::Backend { enum class AttachmentLoadOp; }
namespace Renderer::Backend { enum class AttachmentStoreOp; }
namespace Renderer::Backend { enum class BlendFactor; }
namespace Renderer::Backend { enum class BlendOp; }
namespace Renderer::Backend { enum class CompareOp; }
namespace Renderer::Backend { enum class CullMode; }
namespace Renderer::Backend { enum class Format; }
namespace Renderer::Backend { enum class PolygonMode; }
namespace Renderer::Backend { enum class StencilOp; }
namespace Renderer::Backend::Sampler { enum class AddressMode; }

namespace Renderer
{

namespace Backend
{

namespace Vulkan
{

namespace Mapping
{

VkCompareOp FromCompareOp(const CompareOp compareOp);

VkStencilOp FromStencilOp(const StencilOp stencilOp);

VkBlendFactor FromBlendFactor(const BlendFactor blendFactor);

VkBlendOp FromBlendOp(const BlendOp blendOp);

VkColorComponentFlags FromColorWriteMask(const uint32_t colorWriteMask);

VkPolygonMode FromPolygonMode(const PolygonMode polygonMode);

VkCullModeFlags FromCullMode(const CullMode cullMode);

VkFormat FromFormat(const Format format);

VkSampleCountFlagBits FromSampleCount(const uint32_t sampleCount);

VkSamplerAddressMode FromAddressMode(const Sampler::AddressMode addressMode);

VkAttachmentLoadOp FromAttachmentLoadOp(const AttachmentLoadOp loadOp);

VkAttachmentStoreOp FromAttachmentStoreOp(const AttachmentStoreOp storeOp);

VkPipelineStageFlags FromPipelineStageMask(const uint32_t mask);

VkAccessFlags FromAccessMask(const uint32_t mask);

} // namespace Mapping

} // namespace Vulkan

} // namespace Backend

} // namespace Renderer

#endif // INCLUDED_RENDERER_BACKEND_VULKAN_MAPPING
