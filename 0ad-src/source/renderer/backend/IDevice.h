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

#ifndef INCLUDED_RENDERER_BACKEND_IDEVICE
#define INCLUDED_RENDERER_BACKEND_IDEVICE

#include "graphics/Color.h"
#include "renderer/backend/IBuffer.h"
#include "renderer/backend/IDevice.h"
#include "renderer/backend/ITexture.h"

#include <cstdint>
#include <js/TypeDecls.h>
#include <memory>
#include <span>
#include <string>
#include <vector>

class CShaderDefines;
class CStr;
class ScriptRequest;
namespace Renderer::Backend { class IComputePipelineState; }
namespace Renderer::Backend { class IDeviceCommandContext; }
namespace Renderer::Backend { class IFramebuffer; }
namespace Renderer::Backend { class IGraphicsPipelineState; }
namespace Renderer::Backend { class IShaderProgram; }
namespace Renderer::Backend { class IVertexInputLayout; }
namespace Renderer::Backend { enum class AttachmentLoadOp; }
namespace Renderer::Backend { enum class AttachmentStoreOp; }
namespace Renderer::Backend { enum class Backend; }
namespace Renderer::Backend { enum class Format; }
namespace Renderer::Backend { struct SColorAttachment; }
namespace Renderer::Backend { struct SComputePipelineStateDesc; }
namespace Renderer::Backend { struct SDepthStencilAttachment; }
namespace Renderer::Backend { struct SGraphicsPipelineStateDesc; }
namespace Renderer::Backend { struct SVertexAttributeFormat; }
namespace Renderer::Backend::Sampler { struct Desc; }

namespace Renderer
{

namespace Backend
{

class IDevice
{
public:
	struct Capabilities
	{
		bool S3TC;
		bool ARBShaders;
		bool computeShaders;
		bool debugLabels;
		bool debugScopedLabels;
		bool multisampling;
		bool anisotropicFiltering;
		uint32_t maxSampleCount;
		float maxAnisotropy;
		uint32_t maxTextureSize;
		bool instancing;
		bool storage;
		bool timestamps;
		double timestampMultiplier;
	};

	virtual ~IDevice() {}

	virtual Backend GetBackend() const = 0;

	virtual const std::string& GetName() const = 0;
	virtual const std::string& GetVersion() const = 0;
	virtual const std::string& GetDriverInformation() const = 0;
	virtual const std::vector<std::string>& GetExtensions() const = 0;

	virtual void Report(const ScriptRequest& rq, JS::HandleValue settings) = 0;

	virtual std::unique_ptr<IDeviceCommandContext> CreateCommandContext() = 0;

	/**
	 * Creates a graphics pipeline state. It's a caller responsibility to
	 * guarantee a lifespan of IShaderProgram stored in the description.
	 */
	virtual std::unique_ptr<IGraphicsPipelineState> CreateGraphicsPipelineState(
		const SGraphicsPipelineStateDesc& pipelineStateDesc) = 0;

	/**
	 * Creates a compute pipeline state. It's a caller responsibility to
	 * guarantee a lifespan of IShaderProgram stored in the description.
	 */
	virtual std::unique_ptr<IComputePipelineState> CreateComputePipelineState(
		const SComputePipelineStateDesc& pipelineStateDesc) = 0;

	/**
	 * Creates a vertex input layout. It's recommended to use as few different
	 * layouts as posible.
	 */
	virtual std::unique_ptr<IVertexInputLayout> CreateVertexInputLayout(
		const std::span<const SVertexAttributeFormat> attributes) = 0;

	virtual std::unique_ptr<ITexture> CreateTexture(
		const char* name, const ITexture::Type type, const uint32_t usage,
		const Format format, const uint32_t width, const uint32_t height,
		const Sampler::Desc& defaultSamplerDesc, const uint32_t MIPLevelCount, const uint32_t sampleCount) = 0;

	virtual std::unique_ptr<ITexture> CreateTexture2D(
		const char* name, const uint32_t usage,
		const Format format, const uint32_t width, const uint32_t height,
		const Sampler::Desc& defaultSamplerDesc, const uint32_t MIPLevelCount = 1, const uint32_t sampleCount = 1) = 0;

	/**
	 * @see IFramebuffer
	 *
	 * The color attachment and the depth-stencil attachment should not be
	 * nullptr at the same time. There should not be many different clear
	 * colors along all color attachments for all framebuffers created for
	 * the device.
	 *
	 * @return A valid framebuffer if it was created successfully else nullptr.
	 */
	virtual std::unique_ptr<IFramebuffer> CreateFramebuffer(
		const char* name, SColorAttachment* colorAttachment,
		SDepthStencilAttachment* depthStencilAttachment) = 0;

	virtual std::unique_ptr<IBuffer> CreateBuffer(
		const char* name, const IBuffer::Type type, const uint32_t size, const uint32_t usage) = 0;

	virtual std::unique_ptr<IShaderProgram> CreateShaderProgram(
		const CStr& name, const CShaderDefines& defines) = 0;

	/**
	 * Acquires a backbuffer for rendering a frame.
	 *
	 * @return True if it was successfully acquired and we can render to it.
	 */
	virtual bool AcquireNextBackbuffer() = 0;

	/**
	 * Returns a framebuffer for the current backbuffer with the required
	 * attachment operations. It should not be called if the last
	 * AcquireNextBackbuffer call returned false.
	 *
	 * It's guaranteed that for the same acquired backbuffer this function returns
	 * a framebuffer with the same attachments and properties except load and
	 * store operations.
	 *
	 * @return The last successfully acquired framebuffer that wasn't
	 * presented.
	 */
	virtual IFramebuffer* GetCurrentBackbuffer(
		const AttachmentLoadOp colorAttachmentLoadOp,
		const AttachmentStoreOp colorAttachmentStoreOp,
		const AttachmentLoadOp depthStencilAttachmentLoadOp,
		const AttachmentStoreOp depthStencilAttachmentStoreOp) = 0;

	/**
	 * Presents the backbuffer to the swapchain queue to be flipped on a
	 * screen. Should be called only if the last AcquireNextBackbuffer call
	 * returned true.
	 */
	virtual void Present() = 0;

	/**
	 * Should be called on window surface resize. It's the device owner
	 * responsibility to call that function. Shouldn't be called during
	 * rendering to an acquired backbuffer.
	 */
	virtual void OnWindowResize(const uint32_t width, const uint32_t height) = 0;

	virtual bool IsTextureFormatSupported(const Format format) const = 0;

	virtual bool IsFramebufferFormatSupported(const Format format) const = 0;

	/**
	 * Returns the most suitable format for the usage. Returns
	 * Format::UNDEFINED if there is no such format.
	 */
	virtual Format GetPreferredDepthStencilFormat(
		const uint32_t usage, const bool depth, const bool stencil) const = 0;

	virtual uint32_t AllocateQuery() = 0;

	virtual void FreeQuery(const uint32_t handle) = 0;

	/**
	 * @see GetQueryResult
	 *
	 * It must be called only if the query was submitted via
	 * IDeviceCommandContext::Flush.
	 *
	 * @param handle Must be a valid handle of a query.
	 *
	 * @return True if a result for the query is available.
	 */
	virtual bool IsQueryResultAvailable(const uint32_t handle) const = 0;

	/**
	 * After a call of the function the query result becomes invalid.
	 *
	 * @param handle Must be a valid handle of a query.
	 *
	 * @return A result for the query. The result is undefined if the query isn't
	 * ready.
	 */
	virtual uint64_t GetQueryResult(const uint32_t handle) = 0;

	virtual const Capabilities& GetCapabilities() const = 0;
};

} // namespace Backend

} // namespace Renderer

#endif // INCLUDED_RENDERER_BACKEND_IDEVICE
