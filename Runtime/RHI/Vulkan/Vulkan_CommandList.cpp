/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES ===============================
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_CommandList.h"
#include "../RHI_Pipeline.h"
#include "../RHI_VertexBuffer.h"
#include "../RHI_IndexBuffer.h"
#include "../RHI_ConstantBuffer.h"
#include "../RHI_StructuredBuffer.h"
#include "../RHI_Sampler.h"
#include "../RHI_DescriptorSet.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../RHI_DescriptorSetLayoutCache.h"
#include "../RHI_PipelineCache.h"
#include "../RHI_Semaphore.h"
#include "../RHI_Fence.h"
#include "../../Profiling/Profiler.h"
#include "../../Rendering/Renderer.h"
//==========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    RHI_CommandList::RHI_CommandList(Context* context)
    {
        m_renderer                    = context->GetSubsystem<Renderer>();
        m_profiler                    = context->GetSubsystem<Profiler>();
        m_rhi_device                  = m_renderer->GetRhiDevice().get();
        m_pipeline_cache              = m_renderer->GetPipelineCache();
        m_descriptor_set_layout_cache = m_renderer->GetDescriptorLayoutSetCache();

        RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        // Command buffer
        vulkan_utility::command_buffer::create(m_rhi_device->GetCmdPoolGraphics(), m_resource, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        vulkan_utility::debug::set_name(static_cast<VkCommandBuffer>(m_resource), "cmd_list");

        // Sync - Fence
        m_processed_fence = make_shared<RHI_Fence>(m_rhi_device, "cmd_buffer_processed");

        // Sync - Semaphore
        m_processed_semaphore = make_shared<RHI_Semaphore>(m_rhi_device, false, "cmd_buffer_processed");

        // Query pool
        if (rhi_context->profiler)
        {
            VkQueryPoolCreateInfo query_pool_create_info = {};
            query_pool_create_info.sType                 = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            query_pool_create_info.queryType             = VK_QUERY_TYPE_TIMESTAMP;
            query_pool_create_info.queryCount            = m_max_timestamps;

            auto query_pool = reinterpret_cast<VkQueryPool*>(&m_query_pool);
            vulkan_utility::error::check(vkCreateQueryPool(rhi_context->device, &query_pool_create_info, nullptr, query_pool));

            m_timestamps.fill(0);
        }
    }

    RHI_CommandList::~RHI_CommandList()
    {
        // Wait in case it's still in use by the GPU
        m_rhi_device->QueueWaitAll();

        // Command buffer
        vulkan_utility::command_buffer::destroy(m_rhi_device->GetCmdPoolGraphics(), m_resource);

        // Query pool
        if (m_query_pool)
        {
            vkDestroyQueryPool(m_rhi_device->GetContextRhi()->device, static_cast<VkQueryPool>(m_query_pool), nullptr);
            m_query_pool = nullptr;
        }
    }

    bool RHI_CommandList::Begin()
    {
        // If the command list is in use, wait for it
        if (m_state == RHI_CommandListState::Submitted)
        {
            if (!Wait())
            {
                LOG_ERROR("Failed to wait");
                return false;
            }
        }

        // Verify a few things
        SP_ASSERT(m_resource != nullptr);
        SP_ASSERT(m_state == RHI_CommandListState::Idle);

        // Get queries
        {
            if (m_rhi_device->GetContextRhi()->profiler)
            {
                if (m_query_pool)
                {
                    if (m_timestamp_index != 0)
                    {
                        const uint32_t query_count     = m_timestamp_index * 2;
                        const size_t stride            = sizeof(uint64_t);
                        const VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT;

                        vkGetQueryPoolResults(
                            m_rhi_device->GetContextRhi()->device,  // device
                            static_cast<VkQueryPool>(m_query_pool), // queryPool
                            0,                                      // firstQuery
                            query_count,                            // queryCount
                            query_count * stride,                   // dataSize
                            m_timestamps.data(),                    // pData
                            stride,                                 // stride
                            flags                                   // flags
                        );
                    }
                }
            }

            m_timestamp_index = 0;
        }

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!vulkan_utility::error::check(vkBeginCommandBuffer(static_cast<VkCommandBuffer>(m_resource), &begin_info)))
            return false;

        vkCmdResetQueryPool(static_cast<VkCommandBuffer>(m_resource), static_cast<VkQueryPool>(m_query_pool), 0, m_max_timestamps);

        m_state   = RHI_CommandListState::Recording;
        m_flushed = false;

        return true;
    }

    bool RHI_CommandList::End()
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!vulkan_utility::error::check(vkEndCommandBuffer(static_cast<VkCommandBuffer>(m_resource))))
            return false;

        m_state = RHI_CommandListState::Ended;
        return true;
    }

    bool RHI_CommandList::Submit(RHI_Semaphore* wait_semaphore)
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Ended);

        // Get signal semaphore
        RHI_Semaphore* signal_semaphore = nullptr;
        if (m_pipeline)
        {
            if (RHI_PipelineState* state = m_pipeline->GetPipelineState())
            {
                if (state->render_target_swapchain)
                {
                    // If the swapchain is not presenting (e.g. minimised window), don't submit any work
                    if (!state->render_target_swapchain->PresentEnabled())
                    {
                        m_state = RHI_CommandListState::Submitted;
                        return true;
                    }

                    // Ensure the processed semaphore can be used
                    SP_ASSERT(m_processed_semaphore->GetState() == RHI_Semaphore_State::Idle);

                    // Swapchain waits for this when presenting
                    signal_semaphore = m_processed_semaphore.get(); 
                }
            }
        }

        // Reset fence if it wasn't waited for
        if (m_processed_fence->IsSignaled())
        {
            m_processed_fence->Reset();
        }

        if (!m_rhi_device->QueueSubmit(
            RHI_Queue_Type::Graphics,                      // queue
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // wait flags
            static_cast<VkCommandBuffer>(m_resource),      // cmd buffer
            wait_semaphore,                                // wait semaphore
            signal_semaphore,                              // signal semaphore
            m_processed_fence.get()                        // signal fence
            ))
        {
            LOG_ERROR("Failed to submit the command list.");
            return false;
        }

        m_state = RHI_CommandListState::Submitted;
        return true;
    }

    bool RHI_CommandList::Reset()
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        lock_guard<mutex> guard(m_mutex_reset);

        if (!vulkan_utility::error::check(vkResetCommandBuffer(static_cast<VkCommandBuffer>(m_resource), VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT)))
            return false;

        m_state = RHI_CommandListState::Idle;
        return true;
    }

    bool RHI_CommandList::BeginRenderPass(RHI_PipelineState& pipeline_state)
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Get pipeline
        {
            m_pipeline_active = false;

            // Update the descriptor cache with the pipeline state
            m_descriptor_set_layout_cache->SetPipelineState(pipeline_state);

            // Get (or create) a pipeline which matches the pipeline state
            m_pipeline = m_pipeline_cache->GetPipeline(this, pipeline_state, m_descriptor_set_layout_cache->GetCurrentDescriptorSetLayout());
            if (!m_pipeline)
            {
                LOG_ERROR("Failed to acquire appropriate pipeline");
                return false;
            }

            // Keep a local pointer for convenience
            m_pipeline_state = &pipeline_state;
        }

        // Start marker and profiler (if used)
        Timeblock_Start(m_pipeline_state);

        // Shader resources
        {
            // If the pipeline changed, resources have to be set again
            m_vertex_buffer_id = 0;
            m_index_buffer_id  = 0;

            // Vulkan doesn't have a persistent state so global resources have to be set
            m_renderer->SetGlobalShaderResources(this);
        }

        return true;
    }

    bool RHI_CommandList::EndRenderPass()
    {
        // If the render pass is about to end, there are clear values, but there have been no draw calls,
        // then Deferred_BeginRenderPass() was never called, and any render targets were never cleared.
        // In this case we manually clear it them
        if (m_pipeline_state->HasClearValues() && !m_render_pass_active)
        {
            Deferred_BeginRenderPass();
            ClearPipelineStateRenderTargets(*m_pipeline_state);
        }

        if (m_render_pass_active)
        {
            // Render pass
            vkCmdEndRenderPass(static_cast<VkCommandBuffer>(m_resource));
            m_render_pass_active = false;
        }

        // Profiling
        Timeblock_End(m_pipeline_state);
       
        return true;
    }

    void RHI_CommandList::ClearPipelineStateRenderTargets(RHI_PipelineState& pipeline_state)
    {
        // Validate state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(m_render_pass_active == true);

        uint32_t attachment_count = 0;
        array<VkClearAttachment, rhi_max_render_target_count + 1> attachments; // +1 for depth-stencil

        for (uint8_t i = 0; i < rhi_max_render_target_count; i++)
        { 
            if (pipeline_state.clear_color[i] != rhi_color_load)
            {
                VkClearAttachment& attachment = attachments[attachment_count++];

                attachment.aspectMask                   = VK_IMAGE_ASPECT_COLOR_BIT;
                attachment.colorAttachment              = 0;
                attachment.clearValue.color.float32[0]  = pipeline_state.clear_color[i].x;
                attachment.clearValue.color.float32[1]  = pipeline_state.clear_color[i].y;
                attachment.clearValue.color.float32[2]  = pipeline_state.clear_color[i].z;
                attachment.clearValue.color.float32[3]  = pipeline_state.clear_color[i].w;
            }
        }

        bool clear_depth    = pipeline_state.clear_depth    != rhi_depth_load   && pipeline_state.clear_depth   != rhi_depth_dont_care;
        bool clear_stencil  = pipeline_state.clear_stencil  != rhi_stencil_load && pipeline_state.clear_stencil != rhi_stencil_dont_care;

        if (clear_depth || clear_stencil)
        {
            VkClearAttachment& attachment = attachments[attachment_count++];

            attachment.aspectMask = 0;

            if (clear_depth)
            {
                attachment.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
            }

            if (clear_stencil)
            {
                attachment.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
        
            attachment.clearValue.depthStencil.depth    = pipeline_state.clear_depth;
            attachment.clearValue.depthStencil.stencil  = pipeline_state.clear_stencil;
        }

        VkClearRect clear_rect           = {};
        clear_rect.baseArrayLayer        = 0;
        clear_rect.layerCount            = 1;
        clear_rect.rect.extent.width     = pipeline_state.GetWidth();
        clear_rect.rect.extent.height    = pipeline_state.GetHeight();

        if (attachment_count == 0)
            return;

        vkCmdClearAttachments(static_cast<VkCommandBuffer>(m_resource), attachment_count, attachments.data(), 1, &clear_rect);
    }

    void RHI_CommandList::ClearRenderTarget(RHI_Texture* texture,
        const uint32_t color_index          /*= 0*/,
        const uint32_t depth_stencil_index  /*= 0*/,
        const bool storage                  /*= false*/,
        const Math::Vector4& clear_color    /*= rhi_color_load*/,
        const float clear_depth             /*= rhi_depth_load*/,
        const uint32_t clear_stencil        /*= rhi_stencil_load*/
    )
    {
        SP_ASSERT(m_state == RHI_CommandListState::Recording);
        SP_ASSERT(texture->CanBeCleared());

        if (m_render_pass_active)
        {
            LOG_ERROR("Must only be called outside of a render pass instance");
            return;
        }

        if (!texture || !texture->GetResource_View_Srv())
        {
            LOG_ERROR("Texture is null.");
            return;
        }

        // One of the required layouts for clear functions
        texture->SetLayout(RHI_Image_Layout::Transfer_Dst_Optimal, this);

        VkImageSubresourceRange image_subresource_range = {};
        image_subresource_range.baseMipLevel            = 0;
        image_subresource_range.levelCount              = 1;
        image_subresource_range.baseArrayLayer          = 0;
        image_subresource_range.layerCount              = 1;

        if (texture->IsColorFormat())
        {
            VkClearColorValue _clear_color = { clear_color.x, clear_color.y, clear_color.z, clear_color.w };

            image_subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

            vkCmdClearColorImage(static_cast<VkCommandBuffer>(m_resource), static_cast<VkImage>(texture->GetResource()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &_clear_color, 1, &image_subresource_range);
        }
        else if (texture->IsDepthStencilFormat())
        {
            VkClearDepthStencilValue clear_depth_stencil = { clear_depth, clear_stencil };

            if (texture->IsDepthFormat())
            {
                image_subresource_range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            }

            if (texture->IsStencilFormat())
            {
                image_subresource_range.aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
            }

            vkCmdClearDepthStencilImage(static_cast<VkCommandBuffer>(m_resource), static_cast<VkImage>(texture->GetResource()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_depth_stencil, 1, &image_subresource_range);
        }
    }

    bool RHI_CommandList::Draw(const uint32_t vertex_count)
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Ensure correct state before attempting to draw
        if (!OnDraw())
            return false;

        // Draw
        vkCmdDraw(
            static_cast<VkCommandBuffer>(m_resource), // commandBuffer
            vertex_count,                               // vertexCount
            1,                                          // instanceCount
            0,                                          // firstVertex
            0                                           // firstInstance
        );

        // Profiler
        m_profiler->m_rhi_draw++;

        return true;
    }

    bool RHI_CommandList::DrawIndexed(const uint32_t index_count, const uint32_t index_offset, const uint32_t vertex_offset)
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Ensure correct state before attempting to draw
        if (!OnDraw())
            return false;

        // Draw
        vkCmdDrawIndexed(
            static_cast<VkCommandBuffer>(m_resource), // commandBuffer
            index_count,                                // indexCount
            1,                                          // instanceCount
            index_offset,                               // firstIndex
            vertex_offset,                              // vertexOffset
            0                                           // firstInstance
        );

        // Profile
        m_profiler->m_rhi_draw++;

        return true;
    }

    bool RHI_CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z, bool async /*= false*/)
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Ensure correct state before attempting to draw
        if (!OnDraw())
            return false;

        // Dispatch
        vkCmdDispatch(static_cast<VkCommandBuffer>(m_resource), x, y, z);

        // Profiler
        m_profiler->m_rhi_dispatch++;

        return true;
    }

    void RHI_CommandList::Blit(RHI_Texture* source, RHI_Texture* destination)
    {
        // D3D11 baggage: https://docs.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource
        SP_ASSERT(source != nullptr);
        SP_ASSERT(destination != nullptr);
        SP_ASSERT(source->GetResource() != nullptr);
        SP_ASSERT(destination->GetResource() != nullptr);
        SP_ASSERT(source->GetObjectId() != destination->GetObjectId());
        SP_ASSERT(source->GetFormat() == destination->GetFormat());
        SP_ASSERT(source->GetWidth() == destination->GetWidth());
        SP_ASSERT(source->GetHeight() == destination->GetHeight());
        SP_ASSERT(source->GetArrayLength() == destination->GetArrayLength());
        SP_ASSERT(source->GetMipCount() == destination->GetMipCount());

        VkOffset3D blit_size = {};
        blit_size.x          = source->GetWidth();
        blit_size.y          = source->GetHeight();
        blit_size.z          = 1;

        VkImageBlit blit_region               = {};
        blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit_region.srcSubresource.layerCount = source->GetMipCount();
        blit_region.srcOffsets[1]             = blit_size;
        blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit_region.dstSubresource.layerCount = destination->GetMipCount();
        blit_region.dstOffsets[1]             = blit_size;

        // Save the initial layouts
        std::array<RHI_Image_Layout, 12> layouts_initial_source      = source->GetLayouts();
        std::array<RHI_Image_Layout, 12> layouts_initial_destination = destination->GetLayouts();

        // Transition to blit appropriate layouts
        source->SetLayout(RHI_Image_Layout::Transfer_Src_Optimal, this);
        destination->SetLayout(RHI_Image_Layout::Transfer_Dst_Optimal, this);

        // Blit
        vkCmdBlitImage(
            static_cast<VkCommandBuffer>(m_resource),
            static_cast<VkImage>(source->GetResource()),      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            static_cast<VkImage>(destination->GetResource()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &blit_region,
            VK_FILTER_NEAREST
        );

        // Transition to the initial layouts
        for (uint32_t i = 0; i < source->GetMipCount(); i++)
        {
            source->SetLayout(layouts_initial_source[i], this, i);
            destination->SetLayout(layouts_initial_destination[i], this, i);
        }
    }

    void RHI_CommandList::SetViewport(const RHI_Viewport& viewport) const
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        VkViewport vk_viewport = {};
        vk_viewport.x          = viewport.x;
        vk_viewport.y          = viewport.y;
        vk_viewport.width      = viewport.width;
        vk_viewport.height     = viewport.height;
        vk_viewport.minDepth   = viewport.depth_min;
        vk_viewport.maxDepth   = viewport.depth_max;

        vkCmdSetViewport(
            static_cast<VkCommandBuffer>(m_resource), // commandBuffer
            0,                                          // firstViewport
            1,                                          // viewportCount
            &vk_viewport                                // pViewports
        );
    }

    void RHI_CommandList::SetScissorRectangle(const Math::Rectangle& scissor_rectangle) const
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        VkRect2D vk_scissor;
        vk_scissor.offset.x      = static_cast<int32_t>(scissor_rectangle.left);
        vk_scissor.offset.y      = static_cast<int32_t>(scissor_rectangle.top);
        vk_scissor.extent.width  = static_cast<uint32_t>(scissor_rectangle.Width());
        vk_scissor.extent.height = static_cast<uint32_t>(scissor_rectangle.Height());

        vkCmdSetScissor(
            static_cast<VkCommandBuffer>(m_resource), // commandBuffer
            0,          // firstScissor
            1,          // scissorCount
            &vk_scissor // pScissors
        );
    }

    void RHI_CommandList::SetBufferVertex(const RHI_VertexBuffer* buffer, const uint64_t offset /*= 0*/)
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (m_vertex_buffer_id == buffer->GetObjectId() && m_vertex_buffer_offset == offset)
            return;

        VkBuffer vertex_buffers[]    = { static_cast<VkBuffer>(buffer->GetResource()) };
        VkDeviceSize offsets[]        = { offset };

        vkCmdBindVertexBuffers(
            static_cast<VkCommandBuffer>(m_resource), // commandBuffer
            0,                                          // firstBinding
            1,                                          // bindingCount
            vertex_buffers,                             // pBuffers
            offsets                                     // pOffsets
        );

        m_profiler->m_rhi_bindings_buffer_vertex++;
        m_vertex_buffer_id      = buffer->GetObjectId();
        m_vertex_buffer_offset  = offset;
    }

    void RHI_CommandList::SetBufferIndex(const RHI_IndexBuffer* buffer, const uint64_t offset /*= 0*/)
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (m_index_buffer_id == buffer->GetObjectId() && m_index_buffer_offset == offset)
            return;

        vkCmdBindIndexBuffer(
            static_cast<VkCommandBuffer>(m_resource),                     // commandBuffer
            static_cast<VkBuffer>(buffer->GetResource()),                   // buffer
            offset,                                                         // offset
            buffer->Is16Bit() ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32 // indexType
        );

        m_profiler->m_rhi_bindings_buffer_index++;
        m_index_buffer_id       = buffer->GetObjectId();
        m_index_buffer_offset   = offset;
    }

    void RHI_CommandList::SetConstantBuffer(const uint32_t slot, const uint8_t scope, RHI_ConstantBuffer* constant_buffer) const
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_descriptor_set_layout_cache->GetCurrentDescriptorSetLayout())
        {
            LOG_WARNING("Descriptor layout not set, try setting constant buffer \"%s\" within a render pass", constant_buffer->GetObjectName().c_str());
            return;
        }

        // Set (will only happen if it's not already set)
        m_descriptor_set_layout_cache->SetConstantBuffer(slot, constant_buffer);
    }

    void RHI_CommandList::SetSampler(const uint32_t slot, RHI_Sampler* sampler) const
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_descriptor_set_layout_cache->GetCurrentDescriptorSetLayout())
        {
            LOG_WARNING("Descriptor layout not set, try setting sampler \"%s\" within a render pass", sampler->GetObjectName().c_str());
            return;
        }

        // Set (will only happen if it's not already set)
        m_descriptor_set_layout_cache->SetSampler(slot, sampler);
    }

    void RHI_CommandList::SetTexture(const uint32_t slot, RHI_Texture* texture, const int mip /*= -1*/, const bool ranged /*= false*/, const bool uav /*= false*/)
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Validate texture
        if (texture)
        {
            if (uav)
            {
                SP_ASSERT(texture->IsUav());
            }
            else
            {
                SP_ASSERT(texture->IsSrv());
            }
        }

        if (!m_descriptor_set_layout_cache->GetCurrentDescriptorSetLayout())
        {
            LOG_WARNING("Descriptor layout not set, try setting texture \"%s\" within a render pass", texture->GetObjectName().c_str());
            return;
        }

        // Null textures are allowed, and get replaced with a default texture here
        if (!texture || !texture->GetResource_View_Srv())
        {
            texture = m_renderer->GetDefaultTextureTransparent();
        }

        const bool individual_mip = mip != -1;
        RHI_Image_Layout current_layout = texture->GetLayout(individual_mip ? mip : 0);

        // If the image has an invalid layout (can happen for a few frames during staging), replace with a default texture
        if (current_layout == RHI_Image_Layout::Undefined || current_layout == RHI_Image_Layout::Preinitialized)
        {
            LOG_ERROR("Can't set texture without a layout, replacing with a default texture");
            texture = m_renderer->GetDefaultTextureTransparent();
            current_layout = texture->GetLayout(0);
        }

        // Transition to appropriate layout (if needed)
        {
            RHI_Image_Layout target_layout = RHI_Image_Layout::Undefined;

            if (uav)
            {
                // According to section 13.1 of the Vulkan spec, storage textures have to be in a general layout.
                // https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#descriptorsets-storageimage
                if (current_layout != RHI_Image_Layout::General)
                {
                    target_layout = RHI_Image_Layout::General;
                }
            }
            else
            {
                // Color
                if (texture->IsColorFormat() && current_layout != RHI_Image_Layout::Shader_Read_Only_Optimal)
                {
                    target_layout = RHI_Image_Layout::Shader_Read_Only_Optimal;
                }

                // Depth
                if (texture->IsDepthFormat() && current_layout != RHI_Image_Layout::Depth_Stencil_Read_Only_Optimal)
                {
                    target_layout = RHI_Image_Layout::Depth_Stencil_Read_Only_Optimal;
                }
            }

            bool transition_required = target_layout != RHI_Image_Layout::Undefined;

            // Transition
            if (transition_required && !m_render_pass_active)
            {
                texture->SetLayout(target_layout, this, mip, ranged);
            }
            else if (transition_required && m_render_pass_active)
            {
                LOG_ERROR("Can't transition texture to target layout while a render pass is active, replacing with a default texture");
                texture = m_renderer->GetDefaultTextureTransparent();
            }
        }

        // Set (will only happen if it's not already set)
        m_descriptor_set_layout_cache->SetTexture(slot, texture, mip, ranged);
    }

    void RHI_CommandList::SetStructuredBuffer(const uint32_t slot, RHI_StructuredBuffer* structured_buffer) const
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_descriptor_set_layout_cache->GetCurrentDescriptorSetLayout())
        {
            LOG_WARNING("Descriptor layout not set, try setting structured buffer \"%s\" within a render pass", structured_buffer->GetObjectName().c_str());
            return;
        }

        m_descriptor_set_layout_cache->SetStructuredBuffer(slot, structured_buffer);
    }

    uint32_t RHI_CommandList::Gpu_GetMemoryUsed(RHI_Device* rhi_device)
    {
        if (!rhi_device || !rhi_device->GetContextRhi() || !vulkan_utility::functions::get_physical_device_memory_properties_2)
            return 0;

        VkPhysicalDeviceMemoryBudgetPropertiesEXT device_memory_budget_properties = {};
        device_memory_budget_properties.sType                                     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
        device_memory_budget_properties.pNext                                     = nullptr;

        VkPhysicalDeviceMemoryProperties2 device_memory_properties = {};
        device_memory_properties.sType                             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        device_memory_properties.pNext                             = &device_memory_budget_properties;

        vulkan_utility::functions::get_physical_device_memory_properties_2(static_cast<VkPhysicalDevice>(rhi_device->GetContextRhi()->device_physical), &device_memory_properties);

        return static_cast<uint32_t>(device_memory_budget_properties.heapUsage[0] / 1024 / 1024); // MBs
    }

    bool RHI_CommandList::Timestamp_Start(void* query_disjoint /*= nullptr*/, void* query_start /*= nullptr*/)
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_rhi_device->GetContextRhi()->profiler)
            return true;

        if (!m_query_pool)
            return false;

        vkCmdWriteTimestamp(static_cast<VkCommandBuffer>(m_resource), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, static_cast<VkQueryPool>(m_query_pool), m_timestamp_index++);

        return true;
    }

    bool RHI_CommandList::Timestamp_End(void* query_disjoint /*= nullptr*/, void* query_end /*= nullptr*/)
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        if (!m_rhi_device->GetContextRhi()->profiler)
            return true;

        if (!m_query_pool)
            return false;

        vkCmdWriteTimestamp(static_cast<VkCommandBuffer>(m_resource), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, static_cast<VkQueryPool>(m_query_pool), m_timestamp_index++);

        return true;
    }

    float RHI_CommandList::Timestamp_GetDuration(void* query_disjoint, void* query_start, void* query_end, const uint32_t pass_index)
    {
        if (pass_index + 1 >= m_timestamps.size())
        {
            LOG_ERROR("Pass index out of timestamp array range");
            return 0.0f;
        }

        uint64_t start  = m_timestamps[pass_index];
        uint64_t end    = m_timestamps[pass_index + 1];

        // If end has not been acquired yet (zero), early exit
        if (end < start)
            return 0.0f;

        uint64_t duration   = Math::Helper::Clamp<uint64_t>(end - start, 0, std::numeric_limits<uint64_t>::max());
        float duration_ms   = static_cast<float>(duration * m_rhi_device->GetContextRhi()->device_properties.limits.timestampPeriod * 1e-6f);

        return duration_ms;
    }

    bool RHI_CommandList::Gpu_QueryCreate(RHI_Device* rhi_device, void** query /*= nullptr*/, RHI_Query_Type type /*= RHI_Query_Timestamp*/)
    {
        // Not needed
        return true;
    }

    void RHI_CommandList::Gpu_QueryRelease(void*& query_object)
    {
        // Not needed
    }

    void RHI_CommandList::ResetDescriptorCache()
    {
        if (m_descriptor_set_layout_cache)
        {
            m_descriptor_set_layout_cache->Reset();
        }
    }

    void RHI_CommandList::Timeblock_Start(const RHI_PipelineState* pipeline_state)
    {
        if (!pipeline_state || !pipeline_state->pass_name)
            return;

        // Allowed profiler ?
        if (m_rhi_device->GetContextRhi()->profiler)
        {
            if (m_profiler && pipeline_state->profile)
            {
                m_profiler->TimeBlockStart(pipeline_state->pass_name, TimeBlockType::Cpu, this);
                m_profiler->TimeBlockStart(pipeline_state->pass_name, TimeBlockType::Gpu, this);
            }
        }

        // Allowed to markers ?
        if (m_rhi_device->GetContextRhi()->markers && pipeline_state->mark)
        {
            vulkan_utility::debug::marker_begin(static_cast<VkCommandBuffer>(m_resource), pipeline_state->pass_name, Vector4::Zero);
        }
    }

    void RHI_CommandList::Timeblock_End(const RHI_PipelineState* pipeline_state)
    {
        if (!pipeline_state)
            return;

        // Allowed markers ?
        if (m_rhi_device->GetContextRhi()->markers && pipeline_state->mark)
        {
            vulkan_utility::debug::marker_end(static_cast<VkCommandBuffer>(m_resource));
        }

        // Allowed profiler ?
        if (m_rhi_device->GetContextRhi()->profiler && pipeline_state->profile)
        {
            if (m_profiler)
            {
                m_profiler->TimeBlockEnd(); // cpu
                m_profiler->TimeBlockEnd(); // gpu
            }
        }
    }

    bool RHI_CommandList::Deferred_BeginRenderPass()
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Get pipeline state
        RHI_PipelineState* pipeline_state = m_pipeline->GetPipelineState();

        // Validate pipeline state
        SP_ASSERT(pipeline_state != nullptr);
        SP_ASSERT(pipeline_state->GetRenderPass() != nullptr);
        SP_ASSERT(pipeline_state->GetFrameBuffer() != nullptr);

        // Clear values
        array<VkClearValue, rhi_max_render_target_count + 1> clear_values; // +1 for depth-stencil
        uint32_t clear_value_count = 0;
        {
            // Color
            for (uint8_t i = 0; i < rhi_max_render_target_count; i++)
            {
                if (m_pipeline_state->render_target_color_textures[i] != nullptr)
                {
                    Vector4& color = m_pipeline_state->clear_color[i];
                    clear_values[clear_value_count++].color = { color.x, color.y, color.z, color.w };
                }
            }

            // Depth-stencil
            if (m_pipeline_state->render_target_depth_texture != nullptr)
            {
                clear_values[clear_value_count++].depthStencil = VkClearDepthStencilValue{ m_pipeline_state->clear_depth, m_pipeline_state->clear_stencil };
            }

            // Swapchain
            if (m_pipeline_state->render_target_swapchain != nullptr)
            {
                Vector4& color = m_pipeline_state->clear_color[0];
                clear_values[clear_value_count++].color = { color.x, color.y, color.z, color.w };
            }
        }

        // Begin render pass
        VkRenderPassBeginInfo render_pass_info    = {};
        render_pass_info.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_info.renderPass               = static_cast<VkRenderPass>(pipeline_state->GetRenderPass());
        render_pass_info.framebuffer              = static_cast<VkFramebuffer>(pipeline_state->GetFrameBuffer());
        render_pass_info.renderArea.offset        = { 0, 0 };
        render_pass_info.renderArea.extent.width  = pipeline_state->GetWidth();
        render_pass_info.renderArea.extent.height = pipeline_state->GetHeight();
        render_pass_info.clearValueCount          = clear_value_count;
        render_pass_info.pClearValues             = clear_values.data();
        vkCmdBeginRenderPass(static_cast<VkCommandBuffer>(m_resource), &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

        m_render_pass_active = true;

        return true;
    }

    bool RHI_CommandList::Deferred_BindDescriptorSet()
    {
        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Descriptor set != null, result = true    -> a descriptor set must be bound
        // Descriptor set == null, result = true    -> a descriptor set is already bound
        // Descriptor set == null, result = false   -> a new descriptor was needed but we are out of memory (allocates next frame)

        RHI_DescriptorSet* descriptor_set = nullptr;
        bool result = m_descriptor_set_layout_cache->GetDescriptorSet(descriptor_set);

        if (result && descriptor_set != nullptr)
        {
            // Bind point
            VkPipelineBindPoint pipeline_bind_point = m_pipeline_state->IsCompute() ? VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_COMPUTE : VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS;

            // Dynamic offsets
            RHI_DescriptorSetLayout* descriptor_set_layout = m_descriptor_set_layout_cache->GetCurrentDescriptorSetLayout();
            const array<uint32_t, rhi_max_constant_buffer_count> dynamic_offsets = descriptor_set_layout->GetDynamicOffsets();
            uint32_t dynamic_offset_count = descriptor_set_layout->GetDynamicOffsetCount();

            // Validate descriptor sets
            array<void*, 1> descriptor_sets = { descriptor_set->GetResource() };
            for (uint32_t i = 0; i < static_cast<uint32_t>(descriptor_sets.size()); i++)
            {
                SP_ASSERT(descriptor_sets[i] != nullptr);
            }

            // Bind descriptor set
            vkCmdBindDescriptorSets
            (
                static_cast<VkCommandBuffer>(m_resource),                     // commandBuffer
                pipeline_bind_point,                                            // pipelineBindPoint
                static_cast<VkPipelineLayout>(m_pipeline->GetPipelineLayout()), // layout
                0,                                                              // firstSet
                static_cast<uint32_t>(descriptor_sets.size()),                  // descriptorSetCount
                reinterpret_cast<VkDescriptorSet*>(descriptor_sets.data()),     // pDescriptorSets
                dynamic_offset_count,                                           // dynamicOffsetCount
                !dynamic_offsets.empty() ? dynamic_offsets.data() : nullptr     // pDynamicOffsets
            );

            m_profiler->m_rhi_bindings_descriptor_set++;
        }

        return result;
    }

    bool RHI_CommandList::Deferred_BindPipeline()
    {
        if (VkPipeline vk_pipeline = static_cast<VkPipeline>(m_pipeline->GetPipeline()))
        {
            // Bind point
            VkPipelineBindPoint pipeline_bind_point = m_pipeline_state->IsCompute() ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;

            vkCmdBindPipeline(static_cast<VkCommandBuffer>(m_resource), pipeline_bind_point, vk_pipeline);
            m_profiler->m_rhi_bindings_pipeline++;
            m_pipeline_active = true;
        }
        else
        {
            LOG_ERROR("Invalid pipeline");
            return false;
        }

        return true;
    }

    bool RHI_CommandList::OnDraw()
    {
        if (m_flushed)
            return false;

        // Validate command list state
        SP_ASSERT(m_state == RHI_CommandListState::Recording);

        // Begin render pass
        if (!m_render_pass_active && !m_pipeline_state->IsCompute())
        {
            if (!Deferred_BeginRenderPass())
            {
                LOG_ERROR("Failed to begin render pass");
                return false;
            }
        }

        // Set pipeline
        if (!m_pipeline_active)
        {
            if (!Deferred_BindPipeline())
            {
                LOG_ERROR("Failed to begin render pass");
                return false;
            }
        }

        // Bind descriptor set
        return Deferred_BindDescriptorSet();
    }

    void RHI_CommandList::UnbindOutputTextures()
    {

    }
}
