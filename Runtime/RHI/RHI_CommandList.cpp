/*
Copyright(c) 2016-2022 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ============================
#include "Spartan.h"
#include "RHI_CommandList.h"
#include "RHI_Device.h"
#include "RHI_Fence.h"
#include "RHI_Semaphore.h"
#include "RHI_DescriptorSetLayoutCache.h"
//=======================================

namespace Spartan
{
    void RHI_CommandList::Wait()
    {
        // Validate state.
        SP_ASSERT(m_state == RHI_CommandListState::Submitted);

        // Wait for the fence.
        SP_ASSERT(m_processed_fence->Wait() && "Timeout while waiting for command list fence.");

        // Reset the semaphore
        m_processed_semaphore->Reset();

        m_descriptor_set_layout_cache->GrowIfNeeded();
        m_state = RHI_CommandListState::Idle;
    }

    void RHI_CommandList::Discard()
    {
        m_discard = true;
    }
    
    bool RHI_CommandList::Flush(const bool restore_pipeline_state_after_flush)
    {
        if (m_state == RHI_CommandListState::Idle)
            return true;

        // If recording, end
        bool was_recording = false;
        if (m_state == RHI_CommandListState::Recording)
        {
            was_recording = true;

            if (!End())
                return false;
        }

        // If ended, submit
        if (m_state == RHI_CommandListState::Ended)
        {
            if (!Submit(nullptr))
                return false;
        }

        // Flush
        Wait();

        // If idle, restore state (if any)
        if (restore_pipeline_state_after_flush && m_state == RHI_CommandListState::Idle)
        {
            if (was_recording)
            {
                Begin();
            }
        }

        m_flushed = true;

        return true;
    }

    uint32_t RHI_CommandList::Gpu_GetMemory(RHI_Device* rhi_device)
    {
        if (const PhysicalDevice* physical_device = rhi_device->GetPrimaryPhysicalDevice())
        {
            return physical_device->GetMemory();
        }
    
        return 0;
    }
}
