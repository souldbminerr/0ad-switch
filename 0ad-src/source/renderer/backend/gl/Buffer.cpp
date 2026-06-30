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

#include "Buffer.h"

#include "lib/config2.h"
#include "lib/debug.h"
#include "renderer/backend/gl/Device.h"

namespace Renderer
{

namespace Backend
{

namespace GL
{

namespace
{

GLenum GetTargetFromBufferType(const IBuffer::Type type)
{
	GLenum target{GL_ARRAY_BUFFER};
	switch (type)
	{
	case IBuffer::Type::INDEX:
		target = GL_ELEMENT_ARRAY_BUFFER;
		break;
#if !CONFIG2_GLES
	case IBuffer::Type::UNIFORM:
		target = GL_UNIFORM_BUFFER;
		break;
#endif
	default:
		target = GL_ARRAY_BUFFER;
		break;
	}
	return target;
}

} // anonymous namespace

// static
std::unique_ptr<CBuffer> CBuffer::Create(
	CDevice* device, [[maybe_unused]] const char* name,
	const Type type, const uint32_t size, const uint32_t usage)
{
	ENSURE(type == Type::VERTEX || type == Type::INDEX || type == Type::UNIFORM);
	std::unique_ptr<CBuffer> buffer(new CBuffer());
	buffer->m_Device = device;
	buffer->m_Type = type;
	buffer->m_Size = size;
	buffer->m_Usage = usage;
	glGenBuffersARB(1, &buffer->m_Handle);
	const GLenum target{GetTargetFromBufferType(type)};
	glBindBufferARB(target, buffer->m_Handle);
	glBufferDataARB(target, size, nullptr, (usage & IBuffer::Usage::DYNAMIC) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
#if !CONFIG2_GLES
	if (buffer->m_Device->GetCapabilities().debugLabels)
	{
		glObjectLabel(GL_BUFFER, buffer->m_Handle, -1, name);
	}
#endif
	glBindBufferARB(target, 0);
	return buffer;
}

CBuffer::CBuffer() = default;

CBuffer::~CBuffer()
{
	if (m_Handle)
		glDeleteBuffersARB(1, &m_Handle);
}

IDevice* CBuffer::GetDevice()
{
	return m_Device;
}

} // namespace GL

} // namespace Backend

} // namespace Renderer
