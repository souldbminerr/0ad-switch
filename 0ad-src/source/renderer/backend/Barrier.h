/* Copyright (C) 2024 Wildfire Games.
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

#ifndef INCLUDED_RENDERER_BACKEND_BARRIER
#define INCLUDED_RENDERER_BACKEND_BARRIER

#include <cstdint>

namespace Renderer
{

namespace Backend
{

// PipelineStageFlags and AccessFlags are mostly taken from the Vulkan
// specification.

namespace PipelineStage
{
static constexpr uint32_t DRAW_INDIRECT{
	1u << 0u};
static constexpr uint32_t VERTEX_INPUT{
	1u << 1u};
static constexpr uint32_t VERTEX_SHADER{
	1u << 2u};
static constexpr uint32_t FRAGMENT_SHADER{
	1u << 3u};
static constexpr uint32_t EARLY_FRAGMENT_TESTS{
	1u << 4u};
static constexpr uint32_t LATE_FRAGMENT_TESTS{
	1u << 5u};
static constexpr uint32_t COLOR_ATTACHMENT_OUTPUT{
	1u << 6u};
static constexpr uint32_t COMPUTE_SHADER{
	1u << 7u};
static constexpr uint32_t TRANSFER{
	1u << 8u};
static constexpr uint32_t HOST{
	1u << 9u};
static constexpr uint32_t ACCELERATION_STRUCTURE_BUILD{
	1u << 10u};
static constexpr uint32_t RAY_TRACING_SHADER{
	1u << 11u};
static constexpr uint32_t TASK_SHADER{
	1u << 12u};
static constexpr uint32_t MESH_SHADER{
	1u << 13u};
} // namespace PipelineStage

namespace Access
{
static constexpr uint32_t INDIRECT_COMMAND_READ{
	1u << 0u};
static constexpr uint32_t INDEX_READ{
	1u << 1u};
static constexpr uint32_t VERTEX_ATTRIBUTE_READ{
	1u << 2u};
static constexpr uint32_t UNIFORM_READ{
	1u << 3u};
static constexpr uint32_t INPUT_ATTACHMENT_READ{
	1u << 4u};
static constexpr uint32_t SHADER_READ{
	1u << 5u};
static constexpr uint32_t SHADER_WRITE{
	1u << 6u};
static constexpr uint32_t COLOR_ATTACHMENT_READ{
	1u << 7u};
static constexpr uint32_t COLOR_ATTACHMENT_WRITE{
	1u << 8u};
static constexpr uint32_t DEPTH_STENCIL_ATTACHMENT_READ{
	1u << 9u};
static constexpr uint32_t DEPTH_STENCIL_ATTACHMENT_WRITE{
	1u << 10u};
static constexpr uint32_t TRANSFER_READ{
	1u << 11u};
static constexpr uint32_t TRANSFER_WRITE{
	1u << 12u};
static constexpr uint32_t HOST_READ{
	1u << 13u};
static constexpr uint32_t HOST_WRITE{
	1u << 14u};
static constexpr uint32_t MEMORY_READ{
	1u << 15u};
static constexpr uint32_t MEMORY_WRITE{
	1u << 16u};
static constexpr uint32_t ACCELERATION_STRUCTURE_READ{
	1u << 17u};
static constexpr uint32_t ACCELERATION_STRUCTURE_WRITE{
	1u << 18u};
} // namespace Access

} // namespace Backend

} // namespace Renderer

#endif // INCLUDED_RENDERER_BACKEND_BARRIER
