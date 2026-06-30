/* Copyright (C) 2025 Wildfire Games.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "lib/code_annotation.h"

#include <memory>

class CProfiler2;
class CProfiler2GPUImpl;
namespace Renderer::Backend { class IDeviceCommandContext; }

/**
 * Used by CProfiler2 for GPU profiling support.
 */
class CProfiler2GPU
{
	NONCOPYABLE(CProfiler2GPU);

public:
	CProfiler2GPU(CProfiler2& profiler);
	~CProfiler2GPU();

	void FrameStart(Renderer::Backend::IDeviceCommandContext* deviceCommandContext);
	void FrameEnd(Renderer::Backend::IDeviceCommandContext* deviceCommandContext);
	void RegionEnter(Renderer::Backend::IDeviceCommandContext* deviceCommandContext, const char* id);
	void RegionLeave(Renderer::Backend::IDeviceCommandContext* deviceCommandContext, const char* id);

private:
	CProfiler2& m_Profiler;
	std::unique_ptr<CProfiler2GPUImpl> m_Impl;
};
