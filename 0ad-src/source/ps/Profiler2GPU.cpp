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

#include "precompiled.h"

#include "Profiler2GPU.h"

#include "lib/debug.h"
#include "lib/types.h"
#include "ps/ConfigDB.h"
#include "ps/Profiler2.h"
#include "ps/VideoMode.h"
#include "renderer/backend/IDevice.h"
#include "renderer/backend/IDeviceCommandContext.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

/**
 * At each enter/leave-region event, we do an async GPU timestamp query.
 * When all the queries for a frame have their results available,
 * we convert their GPU timestamps into durations and record the data.
 */
class CProfiler2GPUImpl
{
	NONCOPYABLE(CProfiler2GPUImpl);

	struct SEvent
	{
		const char* id;
		uint32_t queryHandle;
		bool isEnter; // true if entering region; false if leaving
	};

	struct SFrame
	{
		u32 num;

		double syncTimeStart; // CPU time at start of this frame.

		std::vector<SEvent> events;
	};

	std::deque<SFrame> m_Frames;

public:
	CProfiler2GPUImpl(CProfiler2& profiler)
		: m_Profiler(profiler), m_Storage(*new CProfiler2::ThreadStorage(profiler, "gpu"))
	{
		m_Storage.RecordSyncMarker(m_Profiler.GetTime());
		m_Storage.Record(CProfiler2::ITEM_EVENT, m_Profiler.GetTime(), "thread start");

		m_Profiler.AddThreadStorage(&m_Storage);
	}

	~CProfiler2GPUImpl()
	{
		while (!m_Frames.empty())
			PopFrontFrame();

		for (const uint32_t queryHandle : m_FreeQueries)
			g_VideoMode.GetBackendDevice()->FreeQuery(queryHandle);
		m_FreeQueries.clear();

		m_Profiler.RemoveThreadStorage(&m_Storage);
	}

	void FrameStart(Renderer::Backend::IDeviceCommandContext* deviceCommandContext)
	{
		ProcessFrames();

		SFrame frame;
		frame.num = m_Profiler.GetFrameNumber();

		// GL backend:
		// On (at least) some NVIDIA Windows drivers, when GPU-bound, or when
		// vsync enabled and not CPU-bound, the first glGet* call at the start
		// of a frame appears to trigger a wait (to stop the GPU getting too
		// far behind, or to wait for the vsync period).
		// That will be this GL_TIMESTAMP get, which potentially distorts the
		// reported results. So we'll only do it fairly rarely, and for most
		// frames we'll just assume the clocks don't drift much

		// Timestamps might shift and overflow for all backends. So for
		// simplicity we don't synchronize the frame start on CPU and GPU. As
		// we only need durations. We just roughly assume that the first
		// timestamp on GPU matches the CPU frame start. For real timestamps
		// it's better to use GPU Trace instruments.

		frame.syncTimeStart = m_Profiler.GetTime();
		m_Frames.push_back(frame);

		RegionEnter(deviceCommandContext, "frame");
	}

	void FrameEnd(Renderer::Backend::IDeviceCommandContext* deviceCommandContext)
	{
		RegionLeave(deviceCommandContext, "frame");
	}

	void RecordRegion(Renderer::Backend::IDeviceCommandContext* deviceCommandContext, const char* id, bool isEnter)
	{
		ENSURE(!m_Frames.empty());
		SFrame& frame = m_Frames.back();

		SEvent event;
		event.id = id;
		event.queryHandle = NewQuery();
		event.isEnter = isEnter;

		deviceCommandContext->InsertTimestampQuery(event.queryHandle, isEnter);

		frame.events.push_back(event);
	}

	void RegionEnter(Renderer::Backend::IDeviceCommandContext* deviceCommandContext, const char* id)
	{
		RecordRegion(deviceCommandContext, id, true);
	}

	void RegionLeave(Renderer::Backend::IDeviceCommandContext* deviceCommandContext, const char* id)
	{
		RecordRegion(deviceCommandContext, id, false);
	}

private:

	void ProcessFrames()
	{
		Renderer::Backend::IDevice* device{g_VideoMode.GetBackendDevice()};
		while (!m_Frames.empty())
		{
			SFrame& frame = m_Frames.front();

			// We assume queries become available in order so we only need to
			// check the last one.
			if (!device->IsQueryResultAvailable(frame.events.back().queryHandle))
				break;

			// We use the first event GPU timestamp as a frame start.
			const uint64_t firstFrameTimestamp{!frame.events.empty()
				? device->GetQueryResult(frame.events[0].queryHandle) : 0u};

			const double timestampMultiplier{
				device->GetCapabilities().timestampMultiplier};

			std::vector<std::pair<int, uint64_t>> stack;

			// The frame's queries are now available, so retrieve and record all their results:
			for (size_t i = 0; i < frame.events.size(); ++i)
			{
				const uint64_t queryTimestamp{
					i == 0 ? firstFrameTimestamp : device->GetQueryResult(frame.events[i].queryHandle)};
				ENSURE(queryTimestamp >= firstFrameTimestamp);

				// Convert to absolute CPU-clock time
				const double t{
					frame.syncTimeStart + static_cast<double>(queryTimestamp - firstFrameTimestamp) * timestampMultiplier};

				// Record a frame-start for syncing
				if (i == 0)
					m_Storage.RecordFrameStart(t);

				if (frame.events[i].isEnter)
					m_Storage.Record(CProfiler2::ITEM_ENTER, t, frame.events[i].id);
				else
					m_Storage.RecordLeave(t);

				// Associate the frame number with the "frame" region
				if (i == 0)
					m_Storage.RecordAttributePrintf("%u", frame.num);
			}

			PopFrontFrame();
		}
	}

	void PopFrontFrame()
	{
		ENSURE(!m_Frames.empty());
		SFrame& frame = m_Frames.front();
		for (size_t i = 0; i < frame.events.size(); ++i)
			m_FreeQueries.push_back(frame.events[i].queryHandle);
		m_Frames.pop_front();
	}

	// Returns a new backend query handle (or a recycled old one).
	uint32_t NewQuery()
	{
		if (m_FreeQueries.empty())
			return g_VideoMode.GetBackendDevice()->AllocateQuery();

		const uint32_t queryHandle{m_FreeQueries.back()};
		m_FreeQueries.pop_back();
		return queryHandle;
	}

	CProfiler2& m_Profiler;
	CProfiler2::ThreadStorage& m_Storage;

	std::vector<uint32_t> m_FreeQueries; // query objects that are allocated but not currently in used
};

CProfiler2GPU::CProfiler2GPU(CProfiler2& profiler) :
	m_Profiler(profiler)
{
	if (g_ConfigDB.Get("profiler2.gpu.enable", false) && g_VideoMode.GetBackendDevice()->GetCapabilities().timestamps)
	{
		m_Impl = std::make_unique<CProfiler2GPUImpl>(m_Profiler);
	}
}

CProfiler2GPU::~CProfiler2GPU() = default;

void CProfiler2GPU::FrameStart(Renderer::Backend::IDeviceCommandContext* deviceCommandContext)
{
	if (m_Impl)
		m_Impl->FrameStart(deviceCommandContext);
}

void CProfiler2GPU::FrameEnd(Renderer::Backend::IDeviceCommandContext* deviceCommandContext)
{
	if (m_Impl)
		m_Impl->FrameEnd(deviceCommandContext);
}

void CProfiler2GPU::RegionEnter(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext, const char* id)
{
	if (m_Impl)
		m_Impl->RegionEnter(deviceCommandContext, id);
}

void CProfiler2GPU::RegionLeave(
	Renderer::Backend::IDeviceCommandContext* deviceCommandContext, const char* id)
{
	if (m_Impl)
		m_Impl->RegionLeave(deviceCommandContext, id);
}
