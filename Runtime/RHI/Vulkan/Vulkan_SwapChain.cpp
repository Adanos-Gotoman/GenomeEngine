/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#include "../../Math/MathHelper.h"
#ifdef API_GRAPHICS_VULKAN
//================================

//= INCLUDES ==================
#include "../RHI_SwapChain.h"
#include "../RHI_Device.h"
#include "../../Logging/Log.h"
//=============================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace _Vulkan_SwapChain
    {
        inline SwapChainSupportDetails check_surface_compatibility(RHI_Device* rhi_device, const VkSurfaceKHR surface)
        {
            SwapChainSupportDetails details;
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rhi_device->GetContextRhi()->device_physical, surface, &details.capabilities);

            uint32_t format_count;
            vkGetPhysicalDeviceSurfaceFormatsKHR(rhi_device->GetContextRhi()->device_physical, surface, &format_count, nullptr);

            if (format_count != 0)
            {
                details.formats.resize(format_count);
                vkGetPhysicalDeviceSurfaceFormatsKHR(rhi_device->GetContextRhi()->device_physical, surface, &format_count, details.formats.data());
            }

            uint32_t present_mode_count;
            vkGetPhysicalDeviceSurfacePresentModesKHR(rhi_device->GetContextRhi()->device_physical, surface, &present_mode_count, nullptr);

            if (present_mode_count != 0)
            {
                details.present_modes.resize(present_mode_count);
                vkGetPhysicalDeviceSurfacePresentModesKHR(rhi_device->GetContextRhi()->device_physical, surface, &present_mode_count, details.present_modes.data());
            }

            return details;
        }

        inline VkPresentModeKHR choose_present_mode(const VkPresentModeKHR prefered_mode, const std::vector<VkPresentModeKHR>& supported_present_modes)
        {
            // Check if the preferred mode is supported
            for (const auto& supported_present_mode : supported_present_modes)
            {
                if (prefered_mode == supported_present_mode)
                {
                    return prefered_mode;
                }
            }

            // Select a mode from the supported present modes
            VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
            for (const auto& supported_present_mode : supported_present_modes)
            {
                if (supported_present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
                {
                    return supported_present_mode;
                }
                else if (supported_present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
                {
                    present_mode = supported_present_mode;
                }
            }

            return present_mode;
        }

        inline VkSurfaceFormatKHR choose_format(const VkFormat prefered_format, const std::vector<VkSurfaceFormatKHR>& available_formats)
        {
            const VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

            if (available_formats.size() == 1 && available_formats[0].format == VK_FORMAT_UNDEFINED)
            {
                LOG_ERROR("Failed to find format");
                return { prefered_format, color_space };
            }

            for (const auto& available_format : available_formats)
            {
                if (available_format.format == prefered_format && available_format.colorSpace == color_space)
                {
                    return available_format;
                }
            }

            return available_formats.front();
        }

        inline bool create
        (
            const std::shared_ptr<RHI_Device>& rhi_device,
            uint32_t width,
            uint32_t height,
            uint32_t buffer_count,
            RHI_Format format,
            uint32_t flags,
            void* window_handle,
            void* render_pass,
            void*& surface_out,
            void*& swap_chain_view_out,
            vector<void*>& image_views_out,
            vector<void*>& frame_buffers_out,
            vector<void*>& semaphores_image_acquired_out
        )
        {
            auto rhi_context        = rhi_device->GetContextRhi();
            auto device             = rhi_device->GetContextRhi()->device;
            auto device_physical    = rhi_device->GetContextRhi()->device_physical;

            // Create surface
            VkSurfaceKHR surface = nullptr;
            {
                VkWin32SurfaceCreateInfoKHR create_info = {};
                create_info.sType                       = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
                create_info.hwnd                        = static_cast<HWND>(window_handle);
                create_info.hinstance                   = GetModuleHandle(nullptr);

                if (!Vulkan_Common::error::check_result(vkCreateWin32SurfaceKHR(rhi_context->instance, &create_info, nullptr, &surface)))
                    return false;

                VkBool32 present_support = false;
                if (!Vulkan_Common::error::check_result(vkGetPhysicalDeviceSurfaceSupportKHR(device_physical, rhi_context->indices.graphics_family.value(), surface, &present_support)))
                    return false;

                if (!present_support)
                {
                    LOG_ERROR("The device does not support this kind of surface.");
                    return false;
                }
            }

            // Ensure device compatibility
            auto surface_support = check_surface_compatibility(rhi_device.get(), surface);
            if (!surface_support.IsCompatible())
            {
                LOG_ERROR("Device is not surface compatible.");
                return false;
            }

            // Compute extent
            VkExtent2D extent =
            {
                Math::Clamp(width, surface_support.capabilities.minImageExtent.width, surface_support.capabilities.maxImageExtent.width),
                Math::Clamp(height, surface_support.capabilities.minImageExtent.height, surface_support.capabilities.maxImageExtent.height)
            };

            // Choose format
            rhi_context->surface_format = choose_format(vulkan_format[format], surface_support.formats);

            // Swap chain
            VkSwapchainKHR swap_chain;
            {
                VkSwapchainCreateInfoKHR create_info    = {};
                create_info.sType                       = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
                create_info.surface                     = surface;
                create_info.minImageCount               = buffer_count;
                create_info.imageFormat                 = rhi_context->surface_format.format;
                create_info.imageColorSpace             = rhi_context->surface_format.colorSpace;
                create_info.imageExtent                 = extent;
                create_info.imageArrayLayers            = 1;
                create_info.imageUsage                  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

                uint32_t queueFamilyIndices[] = { rhi_context->indices.graphics_family.value(), rhi_context->indices.present_family.value() };
                if (rhi_context->indices.graphics_family != rhi_context->indices.present_family)
                {
                    create_info.imageSharingMode        = VK_SHARING_MODE_CONCURRENT;
                    create_info.queueFamilyIndexCount   = 2;
                    create_info.pQueueFamilyIndices     = queueFamilyIndices;
                }
                else
                {
                    create_info.imageSharingMode        = VK_SHARING_MODE_EXCLUSIVE;
                    create_info.queueFamilyIndexCount   = 0;
                    create_info.pQueueFamilyIndices     = nullptr;
                }

                create_info.preTransform    = surface_support.capabilities.currentTransform;
                create_info.compositeAlpha  = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
                create_info.presentMode     = choose_present_mode(static_cast<VkPresentModeKHR>(flags), surface_support.present_modes);
                create_info.clipped         = VK_TRUE;
                create_info.oldSwapchain    = nullptr;

                if (!Vulkan_Common::error::check_result(vkCreateSwapchainKHR(device, &create_info, nullptr, &swap_chain)))
                    return false;
            }

            // Images
            vector<VkImage> swap_chain_images;
            {
                uint32_t image_count;
                vkGetSwapchainImagesKHR(device, swap_chain, &image_count, nullptr);
                swap_chain_images.resize(image_count);
                vkGetSwapchainImagesKHR(device, swap_chain, &image_count, swap_chain_images.data());
            }

            // Create image view lambda
            auto create_image_view = [](const std::shared_ptr<RHI_Device>& rhi_device, VkImage& _image, VkImageView& image_view, VkFormat format, bool swizzle = false)
            {
                VkImageViewCreateInfo create_info           = {};
                create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                create_info.image                           = _image;
                create_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
                create_info.format                          = format;
                create_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                create_info.subresourceRange.baseMipLevel   = 0;
                create_info.subresourceRange.levelCount     = 1;
                create_info.subresourceRange.baseArrayLayer = 0;
                create_info.subresourceRange.layerCount     = 1;
                if (swizzle)
                {
                    create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
                    create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
                    create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
                    create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
                }

                return vkCreateImageView(rhi_device->GetContextRhi()->device, &create_info, nullptr, &image_view) == VK_SUCCESS;
            };

            // Image views
            auto swizzle = true;
            vector<VkImageView> swap_chain_image_views;
            {
                swap_chain_image_views.resize(swap_chain_images.size());
                for (size_t i = 0; i < swap_chain_image_views.size(); i++)
                {
                    if (!create_image_view(rhi_device, swap_chain_images[i], swap_chain_image_views[i], rhi_context->surface_format.format, swizzle))
                    {
                        LOG_ERROR("Failed to create image view");
                        return false;
                    }
                }
            }

            // Frame buffers
            vector<VkFramebuffer> frame_buffers(swap_chain_image_views.size());
            for (auto i = 0; i < swap_chain_image_views.size(); i++)
            {
                VkImageView attachments[1] = { swap_chain_image_views[i] };

                VkFramebufferCreateInfo framebufferInfo = {};
                framebufferInfo.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                framebufferInfo.renderPass              = static_cast<VkRenderPass>(render_pass);
                framebufferInfo.attachmentCount         = 1;
                framebufferInfo.pAttachments            = attachments;
                framebufferInfo.width                   = extent.width;
                framebufferInfo.height                  = extent.height;
                framebufferInfo.layers                  = 1;

                if (!Vulkan_Common::error::check_result(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &frame_buffers[i])))
                    return false;
            }

            surface_out         = static_cast<void*>(surface);
            swap_chain_view_out = static_cast<void*>(swap_chain);
            image_views_out     = vector<void*>(swap_chain_image_views.begin(), swap_chain_image_views.end());
            frame_buffers_out   = vector<void*>(frame_buffers.begin(), frame_buffers.end());

            for (uint32_t i = 0; i < buffer_count; i++)
            {
                semaphores_image_acquired_out.emplace_back(Vulkan_Common::semaphore::create(rhi_device));
            }

            return true;
        }

        inline void destroy(
            const std::shared_ptr<RHI_Device>& rhi_device,
            void*& surface,
            void*& swap_chain_view,
            vector<void*>& image_views,
            vector<void*>& frame_buffers,
            vector<void*>& semaphores_image_acquired
        )
        {
            for (auto& semaphore : semaphores_image_acquired)
            {
                Vulkan_Common::semaphore::destroy(rhi_device, semaphore);
            }
            semaphores_image_acquired.clear();

            for (auto frame_buffer : frame_buffers) { vkDestroyFramebuffer(rhi_device->GetContextRhi()->device, static_cast<VkFramebuffer>(frame_buffer), nullptr); }
            frame_buffers.clear();

            for (auto& image_view : image_views) { vkDestroyImageView(rhi_device->GetContextRhi()->device, static_cast<VkImageView>(image_view), nullptr); }
            image_views.clear();

            if (swap_chain_view)
            {
                vkDestroySwapchainKHR(rhi_device->GetContextRhi()->device, static_cast<VkSwapchainKHR>(swap_chain_view), nullptr);
                swap_chain_view = nullptr;
            }

            if (surface)
            {
                vkDestroySurfaceKHR(rhi_device->GetContextRhi()->instance, static_cast<VkSurfaceKHR>(surface), nullptr);
                surface = nullptr;
            }
        }
    }

	RHI_SwapChain::RHI_SwapChain(
		void* window_handle,
		const std::shared_ptr<RHI_Device>& rhi_device,
		const uint32_t width,
		const uint32_t height,
        const RHI_Format format		/*= Format_R8G8B8A8_UNORM */,
        const uint32_t buffer_count	/*= 1 */,
        const uint32_t flags		/*= Present_Immediate */
	)
	{
		const auto hwnd = static_cast<HWND>(window_handle);
		if (!hwnd || !rhi_device || !IsWindow(hwnd))
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// Copy parameters
		m_format		= format;
		m_rhi_device	= rhi_device;
		m_buffer_count	= buffer_count;
		m_width			= width;
		m_height		= height;
		m_window_handle	= window_handle;
        m_flags         = flags;

		if (!CreateRenderPass())
			return;

		m_initialized = _Vulkan_SwapChain::create
		(
			m_rhi_device,
			m_width,
			m_height,
			m_buffer_count,
			m_format,
            m_flags,
			m_window_handle,
			m_render_pass,
			m_surface,
			m_swap_chain_view,
			m_image_views,
			m_frame_buffers,
			m_semaphores_image_acquired
		);
	}

	RHI_SwapChain::~RHI_SwapChain()
	{
		_Vulkan_SwapChain::destroy
		(
			m_rhi_device,
			m_surface,
			m_swap_chain_view,
			m_image_views,
			m_frame_buffers,
			m_semaphores_image_acquired
		);

		if (m_render_pass)
		{
			vkDestroyRenderPass(m_rhi_device->GetContextRhi()->device, static_cast<VkRenderPass>(m_render_pass), nullptr);
			m_render_pass = nullptr;
		}
	}

	bool RHI_SwapChain::Resize(const uint32_t width, const uint32_t height)
	{	
		// Only resize if needed
		if (m_width == width && m_height == height)
			return true;

		// Save new dimensions
		m_width		= width;
		m_height	= height;

		// Destroy previous swap chain
		_Vulkan_SwapChain::destroy
		(
			m_rhi_device,
			m_surface,
			m_swap_chain_view,
			m_image_views,
			m_frame_buffers,
			m_semaphores_image_acquired
		);

		// Create the swap chain with the new dimensions
		m_initialized = _Vulkan_SwapChain::create
		(
			m_rhi_device,
			m_width,
			m_height,
			m_buffer_count,
			m_format,
			m_flags,
			m_window_handle,
			m_render_pass,
			m_surface,
			m_swap_chain_view,
			m_image_views,
			m_frame_buffers,
			m_semaphores_image_acquired
		);

		return m_initialized;
	}

	bool RHI_SwapChain::AcquireNextImage()
	{
		// Make index that always matches the m_image_index after vkAcquireNextImageKHR.
		// This is so getting semaphores and fences can be done by using m_image_index.
		const auto index = !image_acquired ? 0 : (m_image_index + 1) % m_buffer_count;

		// Acquire next image
        if (Vulkan_Common::error::check_result
        (
            vkAcquireNextImageKHR
            (
                m_rhi_device->GetContextRhi()->device,
                static_cast<VkSwapchainKHR>(m_swap_chain_view),
                0xFFFFFFFFFFFFFFFF,
                static_cast<VkSemaphore>(m_semaphores_image_acquired[index]),
                nullptr,
                &m_image_index
            )
        ))
        {
            image_acquired = true;
            return true;
        }

        return false;
	}

	bool RHI_SwapChain::Present() const
	{	
		SPARTAN_ASSERT(image_acquired);

		VkSwapchainKHR swap_chains[]	= { static_cast<VkSwapchainKHR>(m_swap_chain_view) };
		VkSemaphore semaphores_wait[]	= { static_cast<VkSemaphore>(m_semaphore_cmd_list_consumed) };

		VkPresentInfoKHR present_info	= {};
		present_info.sType				= VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.waitSemaphoreCount = 1;
		present_info.pWaitSemaphores	= semaphores_wait;
		present_info.swapchainCount		= 1;
		present_info.pSwapchains		= swap_chains;
		present_info.pImageIndices		= &m_image_index;

		return Vulkan_Common::error::check_result(vkQueuePresentKHR(m_rhi_device->GetContextRhi()->queue_present, &present_info));
	}

	bool RHI_SwapChain::CreateRenderPass()
	{
		VkAttachmentDescription color_attachment	= {};
		color_attachment.format						= VK_FORMAT_B8G8R8A8_UNORM; // this has to come from the swapchain
		color_attachment.samples					= VK_SAMPLE_COUNT_1_BIT;
		color_attachment.loadOp						= VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp					= VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.stencilLoadOp				= VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		color_attachment.stencilStoreOp				= VK_ATTACHMENT_STORE_OP_DONT_CARE;
		color_attachment.initialLayout				= VK_IMAGE_LAYOUT_UNDEFINED;
		color_attachment.finalLayout				= VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference color_attachment_ref	= {};
		color_attachment_ref.attachment				= 0;
		color_attachment_ref.layout					= VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass	= {};
		subpass.pipelineBindPoint		= VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount	= 1;
		subpass.pColorAttachments		= &color_attachment_ref;

		// Sub-pass dependencies for layout transitions
		vector<VkSubpassDependency> dependencies
		{
			VkSubpassDependency
			{
				VK_SUBPASS_EXTERNAL,														// uint32_t srcSubpass;
				0,																			// uint32_t dstSubpass;
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,										// PipelineStageFlags srcStageMask;
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,								// PipelineStageFlags dstStageMask;
				VK_ACCESS_MEMORY_READ_BIT,													// AccessFlags srcAccessMask;
				VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,	// AccessFlags dstAccessMask;
				VK_DEPENDENCY_BY_REGION_BIT													// DependencyFlags dependencyFlags;
			},

			VkSubpassDependency
			{
				0,																			// uint32_t srcSubpass;
				VK_SUBPASS_EXTERNAL,														// uint32_t dstSubpass;
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,								// PipelineStageFlags srcStageMask;
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,										// PipelineStageFlags dstStageMask;
				VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,	// AccessFlags srcAccessMask;
				VK_ACCESS_MEMORY_READ_BIT,													// AccessFlags dstAccessMask;
				VK_DEPENDENCY_BY_REGION_BIT													// DependencyFlags dependencyFlags;
			},
		};

		VkRenderPassCreateInfo render_pass_info = {};
		render_pass_info.sType					= VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		render_pass_info.attachmentCount		= 1;
		render_pass_info.pAttachments			= &color_attachment;
		render_pass_info.subpassCount			= 1;
		render_pass_info.pSubpasses				= &subpass;
		render_pass_info.dependencyCount		= static_cast<uint32_t>(dependencies.size());
		render_pass_info.pDependencies			= dependencies.data();

		const auto render_pass = reinterpret_cast<VkRenderPass*>(&m_render_pass);
		if (vkCreateRenderPass(m_rhi_device->GetContextRhi()->device, &render_pass_info, nullptr, render_pass) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create render pass");
			return false;
		}
		return true;
	}
}
#endif
