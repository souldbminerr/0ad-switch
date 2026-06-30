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

#ifndef INCLUDED_RENDERER_BACKEND_GL_SHADERPROGRAM
#define INCLUDED_RENDERER_BACKEND_GL_SHADERPROGRAM

#include "lib/code_annotation.h"
#include "lib/debug.h"
#include "lib/ogl.h"
#include "ps/CStr.h"
#include "renderer/backend/Format.h"
#include "renderer/backend/IShaderProgram.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

class CShaderDefines;
class CStrIntern;
namespace Renderer::Backend::GL { class CDevice; }

namespace Renderer
{

namespace Backend
{

namespace GL
{

class CVertexInputLayout : public IVertexInputLayout
{
public:
	CVertexInputLayout(CDevice* device, const std::span<const SVertexAttributeFormat> attributes)
		: m_Device(device), m_Attributes(attributes.begin(), attributes.end())
	{
		for (const SVertexAttributeFormat& attribute : m_Attributes)
		{
			ENSURE(attribute.format != Format::UNDEFINED);
			ENSURE(attribute.stride > 0);
		}
	}

	~CVertexInputLayout() override = default;

	IDevice* GetDevice() override;

	const std::vector<SVertexAttributeFormat>& GetAttributes() const noexcept { return m_Attributes; }

private:
	CDevice* m_Device = nullptr;

	std::vector<SVertexAttributeFormat> m_Attributes;
};

/**
 * A compiled vertex+fragment shader program.
 * The implementation may use GL_ARB_{vertex,fragment}_program (ARB assembly syntax)
 * or GL_ARB_{vertex,fragment}_shader (GLSL), or may use hard-coded fixed-function
 * multitexturing setup code; the difference is hidden from the caller.
 *
 * Texture/uniform IDs are typically strings, corresponding to the names defined in
 * the shader .xml file. Alternatively (and more efficiently, if used very frequently),
 * call GetBindingSlot and pass its return value as the ID.
 * Setting uniforms that the shader .xml doesn't support is harmless.
 *
 * For a high-level overview of shaders and materials, see
 * https://gitea.wildfiregames.com/0ad/0ad/wiki/MaterialSystem
 */
class CShaderProgram : public IShaderProgram
{
	NONCOPYABLE(CShaderProgram);

public:
	typedef CStrIntern attrib_id_t;

	static std::unique_ptr<CShaderProgram> Create(
		CDevice* device, const CStr& name, const CShaderDefines& baseDefines);

	~CShaderProgram() override;

	/**
	 * Binds the shader into the GL context. Call this before calling Uniform()
	 * or trying to render with it.
	 */
	virtual void Bind(CShaderProgram* previousShaderProgram) = 0;

	/**
	 * Unbinds the shader from the GL context. Call this after rendering with it.
	 */
	virtual void Unbind() = 0;

	struct TextureUnit
	{
		GLenum type;
		GLenum target;
		GLint unit;
	};
	virtual TextureUnit GetTextureUnit(const int32_t bindingSlot) = 0;

	virtual GLuint GetStorageBuffer(const int32_t bindingSlot) = 0;

	virtual void SetUniform(
		const int32_t bindingSlot,
		const float value) = 0;
	virtual void SetUniform(
		const int32_t bindingSlot,
		const float valueX, const float valueY) = 0;
	virtual void SetUniform(
		const int32_t bindingSlot,
		const float valueX, const float valueY,
		const float valueZ) = 0;
	virtual void SetUniform(
		const int32_t bindingSlot,
		const float valueX, const float valueY,
		const float valueZ, const float valueW) = 0;
	virtual void SetUniform(
		const int32_t bindingSlot, std::span<const float> values) = 0;

	// Vertex attribute pointers (equivalent to glVertexPointer etc).
	virtual void VertexAttribPointer(
		const VertexAttributeStream stream, const Format format,
		const uint32_t offset, const uint32_t stride,
		const VertexAttributeRate rate, const void* data);

	bool IsStreamActive(const VertexAttributeStream stream) const;

	bool HasImageUniforms() const { return m_HasImageUniforms; }

	/**
	 * Checks that all the required vertex attributes have been set.
	 * Call this before calling Draw/DrawIndexed etc to avoid potential crashes.
	 */
	void AssertPointersBound();

protected:
	CShaderProgram(int streamflags);

	void VertexPointer(const Renderer::Backend::Format format, GLsizei stride, const void* pointer);
	void NormalPointer(const Renderer::Backend::Format format, GLsizei stride, const void* pointer);
	void ColorPointer(const Renderer::Backend::Format format, GLsizei stride, const void* pointer);
	void TexCoordPointer(GLenum texture, const Renderer::Backend::Format format, GLsizei stride, const void* pointer);

	int m_StreamFlags;

	// Non-GLSL client state handling:
	void BindClientStates();
	void UnbindClientStates();
	int m_ValidStreams; // which streams have been specified via VertexPointer etc since the last Bind

	bool m_HasImageUniforms{false};
};

} // namespace GL

} // namespace Backend

} // namespace Renderer

#endif // INCLUDED_RENDERER_BACKEND_GL_SHADERPROGRAM
