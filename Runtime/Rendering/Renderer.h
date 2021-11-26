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

#pragma once

//= INCLUDES ========================
#include <unordered_map>
#include <array>
#include <atomic>
#include "Renderer_ConstantBuffers.h"
#include "Renderer_Enums.h"
#include "Material.h"
#include "../Core/ISubsystem.h"
#include "../Math/Rectangle.h"
#include "../RHI/RHI_Definition.h"
#include "../RHI/RHI_Viewport.h"
#include "../RHI/RHI_Vertex.h"
//===================================

namespace Spartan
{
    // Forward declarations
    class Entity;
    class Camera;
    class Light;
    class ResourceCache;
    class Font;
    class Variant;
    class Grid;
    class TransformGizmo;
    class Profiler;

    namespace Math
    {
        class BoundingBox;
        class Frustum;
    }

    class SPARTAN_CLASS Renderer : public ISubsystem
    {
    public:
        // Defines
        #define DEBUG_COLOR            Math::Vector4(0.41f, 0.86f, 1.0f, 1.0f)
        #define RENDER_TARGET(rt_enum) m_render_targets[static_cast<uint8_t>(rt_enum)]

        Renderer(Context* context);
        ~Renderer();

        //= ISubsystem =========================
        bool OnInitialise() override;
        void OnTick(double delta_time) override;
        //======================================

        // Primitive rendering
        void DrawLine(const Math::Vector3& from, const Math::Vector3& to, const Math::Vector4& color_from = DEBUG_COLOR, const Math::Vector4& color_to = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        void DrawTriangle(const Math::Vector3& v0, const Math::Vector3& v1, const Math::Vector3& v2, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        void DrawRectangle(const Math::Rectangle& rectangle, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        void DrawBox(const Math::BoundingBox& box, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        void DrawCircle(const Math::Vector3& center, const Math::Vector3& axis, const float radius, const uint32_t segment_count, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);

        // Viewport
        const RHI_Viewport& GetViewport() const { return m_viewport; }
        void SetViewport(float width, float height);

        // Resolution render
        const Math::Vector2& GetResolutionRender() const { return m_resolution_render; }
        void SetResolutionRender(uint32_t width, uint32_t height, bool recreate_resources = true);

        // Resolution output
        const Math::Vector2& GetResolutionOutput() const { return m_resolution_output; }
        void SetResolutionOutput (uint32_t width, uint32_t height, bool recreate_resources = true);

        // Transform handle
        std::weak_ptr<Entity> SnapTransformHandleToEntity(const std::shared_ptr<Entity>& entity) const;
        bool IsTransformHandleEditing() const;
        Entity* GetTransformHandleEntity();

        // Debug/Visualise a render targets
        const auto& GetRenderTargets()                                  { return m_render_targets; }
        void SetRenderTargetDebug(const RendererRt render_target_debug) { m_render_target_debug = render_target_debug; }
        RendererRt GetRenderTargetDebug() const                         { return m_render_target_debug; }

        // Depth
        float GetClearDepth() { return GetOption(Render_ReverseZ) ? m_viewport.depth_min : m_viewport.depth_max; }

        // Environment
        const std::shared_ptr<RHI_Texture>& GetEnvironmentTexture();
        void SetEnvironmentTexture(const std::shared_ptr<RHI_Texture> texture);

        // Options
        uint64_t GetOptions()                        const { return m_options; }
        void SetOptions(const uint64_t options)            { m_options = options; }
        bool GetOption(const Renderer_Option option) const { return m_options & option; }
        void SetOption(Renderer_Option option, bool enable);
        
        // Options values
        template<typename T>
        T GetOptionValue(const Renderer_Option_Value option) { return static_cast<T>(m_option_values[option]); }
        void SetOptionValue(Renderer_Option_Value option, float value);

        // Swapchain
        RHI_SwapChain* GetSwapChain() const { return m_swap_chain.get(); }
        bool Present(RHI_CommandList* cmd_list);

        // Sync
        void Flush();

        // Default textures
        RHI_Texture* GetDefaultTextureWhite()       const { return m_tex_default_white.get(); }
        RHI_Texture* GetDefaultTextureBlack()       const { return m_tex_default_black.get(); }
        RHI_Texture* GetDefaultTextureTransparent() const { return m_tex_default_transparent.get(); }

        // Global uber constant buffer calls
        void SetCbUberTransform(RHI_CommandList* cmd_list, const Math::Matrix& transform);
        void SetCbUberColor(RHI_CommandList* cmd_list, const Math::Vector4& color);

        // Rendering
        bool IsRenderingAllowed() const { return m_is_rendering_allowed; }

        // Misc
        void SetGlobalShaderResources(RHI_CommandList* cmd_list) const;
        void RequestTextureMipGeneration(RHI_Texture* texture);
        const std::shared_ptr<RHI_Device>& GetRhiDevice()           const { return m_rhi_device; }
        RHI_PipelineCache* GetPipelineCache()                       const { return m_pipeline_cache.get(); }
        RHI_DescriptorSetLayoutCache* GetDescriptorLayoutSetCache() const { return m_descriptor_set_layout_cache.get(); }
        RHI_Texture* GetFrameTexture()                                    { return RENDER_TARGET(RendererRt::Frame_Output).get(); }
        auto GetFrameNum()                                          const { return m_frame_num; }
        std::shared_ptr<Camera> GetCamera()                         const { return m_camera; }
        auto IsInitialised()                                        const { return m_initialised; }
        auto GetShaders()                                           const { return m_shaders; }
        RHI_CommandList* GetCmdList()                               const { return m_cmd_current; }
        uint32_t GetCmdIndex()                                      const { return m_cmd_index;  }

        // Passes
        void Pass_CopyToBackbuffer(RHI_CommandList* cmd_list);

        // Adjustable parameters
        float m_gizmo_transform_size  = 0.015f;
        float m_gizmo_transform_speed = 12.0f;

    private:
        // Resource creation
        void CreateConstantBuffers();
        void CreateStructuredBuffers();
        void CreateDepthStencilStates();
        void CreateRasterizerStates();
        void CreateBlendStates();
        void CreateFonts();
        void CreateMeshes();
        void CreateTextures();
        void CreateShaders();
        void CreateSamplers(const bool create_only_anisotropic = false);
        void CreateRenderTextures(const bool create_render, const bool create_output, const bool create_fixed, const bool create_dynamic);

        // Passes
        void Pass_Main(RHI_CommandList* cmd_list);
        void Pass_UpdateFrameBuffer(RHI_CommandList* cmd_list);
        void Pass_ShadowMaps(RHI_CommandList* cmd_list, const bool is_transparent_pass);
        void Pass_ReflectionProbes(RHI_CommandList* cmd_list);
        void Pass_Depth_Prepass(RHI_CommandList* cmd_list);
        void Pass_GBuffer(RHI_CommandList* cmd_list, const bool is_transparent_pass);
        void Pass_Ssao(RHI_CommandList* cmd_list);
        void Pass_Ssr(RHI_CommandList* cmd_list, RHI_Texture* tex_in);
        void Pass_Light(RHI_CommandList* cmd_list, const bool is_transparent_pass);
        void Pass_Light_Composition(RHI_CommandList* cmd_list, RHI_Texture* tex_out, const bool is_transparent_pass);
        void Pass_Light_ImageBased(RHI_CommandList* cmd_list, RHI_Texture* tex_out, const bool is_transparent_pass);
        void Pass_PostProcess(RHI_CommandList* cmd_list);
        void Pass_PostProcess_TAA(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_PostProcess_ToneMapping(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_PostProcess_Fxaa(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_PostProcess_FilmGrain(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out); 
        void Pass_PostProcess_ChromaticAberration(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_PostProcess_MotionBlur(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_PostProcess_DepthOfField(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_PostProcess_Debanding(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_PostProcess_Bloom(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_Blur_Gaussian(RHI_CommandList* cmd_list, RHI_Texture* tex_in, const bool depth_aware, const float sigma, const float pixel_stride, const int mip = -1);
        void Pass_AMD_FidelityFX_ContrastAdaptiveSharpening(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_AMD_FidelityFX_SinglePassDowsnampler(RHI_CommandList* cmd_list, RHI_Texture* tex, const bool luminance_antiflicker);
        void Pass_AMD_FidelityFX_SuperResolution(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out, RHI_Texture* tex_out_scratch);
        bool Pass_DebugBuffer(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_Lines(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_DebugMeshes(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_Outline(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_Icons(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_TransformHandle(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_PeformanceMetrics(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_BrdfSpecularLut(RHI_CommandList* cmd_list);
        void Pass_Copy(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out, const bool bilinear);
        void Pass_Generate_Mips();

        // Constant buffers
        bool Update_Cb_Frame(RHI_CommandList* cmd_list);
        bool Update_Cb_Uber(RHI_CommandList* cmd_list);
        bool Update_Cb_Light(RHI_CommandList* cmd_list, const Light* light);
        bool Update_Cb_Material(RHI_CommandList* cmd_list);

        // Event handlers
        void OnRenderablesAcquire(const Variant& renderables);
        void OnClear();
        void OnWorldLoaded();
        void OnFullScreenToggled();

        // Misc
        void SortRenderables(std::vector<Entity*>* renderables);

        // Lines
        void Lines_PreMain();
        void Lines_PostMain(const double delta_time);

        // Render targets
        std::array<std::shared_ptr<RHI_Texture>, 23> m_render_targets;

        // Standard textures
        std::shared_ptr<RHI_Texture> m_tex_environment;
        std::shared_ptr<RHI_Texture> m_tex_default_noise_normal;
        std::shared_ptr<RHI_Texture> m_tex_default_noise_blue;
        std::shared_ptr<RHI_Texture> m_tex_default_white;
        std::shared_ptr<RHI_Texture> m_tex_default_black;
        std::shared_ptr<RHI_Texture> m_tex_default_transparent;
        std::shared_ptr<RHI_Texture> m_tex_gizmo_light_directional;
        std::shared_ptr<RHI_Texture> m_tex_gizmo_light_point;
        std::shared_ptr<RHI_Texture> m_tex_gizmo_light_spot;

        // Shaders
        std::unordered_map<RendererShader, std::shared_ptr<RHI_Shader>> m_shaders;

        // Depth-stencil states
        std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_off_off;
        std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_off_r;
        std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_rw_off;
        std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_r_off;
        std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_rw_w;

        // Blend states 
        std::shared_ptr<RHI_BlendState> m_blend_disabled;
        std::shared_ptr<RHI_BlendState> m_blend_alpha;
        std::shared_ptr<RHI_BlendState> m_blend_additive;

        // Rasterizer states
        std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_back_solid;
        std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_back_wireframe;
        std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_none_solid;
        std::shared_ptr<RHI_RasterizerState> m_rasterizer_light_point_spot;
        std::shared_ptr<RHI_RasterizerState> m_rasterizer_light_directional;

        // Samplers
        std::shared_ptr<RHI_Sampler> m_sampler_compare_depth;
        std::shared_ptr<RHI_Sampler> m_sampler_point_clamp;
        std::shared_ptr<RHI_Sampler> m_sampler_point_wrap;
        std::shared_ptr<RHI_Sampler> m_sampler_bilinear_clamp;
        std::shared_ptr<RHI_Sampler> m_sampler_bilinear_wrap;
        std::shared_ptr<RHI_Sampler> m_sampler_trilinear_clamp;
        std::shared_ptr<RHI_Sampler> m_sampler_anisotropic_wrap;

        //= CONSTANT BUFFERS =================================
        Cb_Frame m_cb_frame_cpu;
        Cb_Frame m_cb_frame_cpu_previous;
        std::shared_ptr<RHI_ConstantBuffer> m_cb_frame_gpu;
        uint32_t m_cb_frame_offset_index = 0;

        Cb_Uber m_cb_uber_cpu;
        Cb_Uber m_cb_uber_cpu_previous;
        std::shared_ptr<RHI_ConstantBuffer> m_cb_uber_gpu;
        uint32_t m_cb_uber_offset_index = 0;

        Cb_Light m_cb_light_cpu;
        Cb_Light m_cb_light_cpu_previous;
        std::shared_ptr<RHI_ConstantBuffer> m_cb_light_gpu;
        uint32_t m_cb_light_offset_index = 0;

        Cb_Material m_cb_material_cpu;
        Cb_Material m_cb_material_cpu_previous;
        std::shared_ptr<RHI_ConstantBuffer> m_cb_material_gpu;
        uint32_t m_cb_material_offset_index = 0;
        //====================================================

        // Structured buffers
        std::shared_ptr<RHI_StructuredBuffer> m_sb_counter;

        // Line rendering
        std::shared_ptr<RHI_VertexBuffer> m_vertex_buffer_lines;
        std::vector<RHI_Vertex_PosCol> m_line_vertices;
        std::vector<float> m_lines_duration;
        uint32_t m_lines_index_depth_off = 0;
        uint32_t m_lines_index_depth_on  = 0;

        // Gizmos
        std::unique_ptr<TransformGizmo> m_transform_handle;
        std::unique_ptr<Grid> m_gizmo_grid;
        Math::Rectangle m_gizmo_light_rect;
        std::shared_ptr<RHI_VertexBuffer> m_sphere_vertex_buffer;
        std::shared_ptr<RHI_IndexBuffer> m_sphere_index_buffer;

        // Resolution & Viewport
        Math::Vector2 m_resolution_render          = Math::Vector2::Zero;
        Math::Vector2 m_resolution_output          = Math::Vector2::Zero;
        RHI_Viewport m_viewport                    = RHI_Viewport(0, 0, 0, 0);
        Math::Rectangle m_viewport_quad            = Math::Rectangle(0, 0, 0, 0);
        Math::Vector2 m_resolution_output_previous = Math::Vector2::Zero;
        RHI_Viewport m_viewport_previous           = RHI_Viewport(0, 0, 0, 0);

        // Options
        uint64_t m_options = 0;
        std::unordered_map<Renderer_Option_Value, float> m_option_values;

        // Misc
        std::unique_ptr<Font> m_font;
        Math::Vector2 m_taa_jitter               = Math::Vector2::Zero;
        Math::Vector2 m_taa_jitter_previous      = Math::Vector2::Zero;
        RendererRt m_render_target_debug         = RendererRt::Undefined;
        bool m_initialised                       = false;
        float m_near_plane                       = 0.0f;
        float m_far_plane                        = 0.0f;
        uint64_t m_frame_num                     = 0;
        bool m_is_odd_frame                      = false;
        bool m_update_ortho_proj                 = true;
        std::atomic<bool> m_is_rendering_allowed = true;
        std::atomic<bool> m_flush_requested      = false;
        bool m_brdf_specular_lut_rendered        = false;
        uint32_t m_cmd_index                     = std::numeric_limits<uint32_t>::max();
        std::thread::id m_render_thread_id;

        // Constants
        const uint32_t m_resolution_shadow_min = 128;
        const float m_gizmo_size_max           = 2.0f;
        const float m_gizmo_size_min           = 0.1f;
        const float m_thread_group_count       = 8.0f;
        const float m_depth_bias               = 0.004f; // bias that's applied directly into the depth buffer
        const float m_depth_bias_clamp         = 0.0f;
        const float m_depth_bias_slope_scaled  = 2.0f;

        // Requests for mip generation
        std::vector<RHI_Texture*> m_textures_mip_generation;
        std::atomic<bool> m_is_generating_mips = false;

        // RHI Core
        std::shared_ptr<RHI_Device> m_rhi_device;
        std::shared_ptr<RHI_PipelineCache> m_pipeline_cache;
        std::shared_ptr<RHI_DescriptorSetLayoutCache> m_descriptor_set_layout_cache;
        std::vector<std::shared_ptr<RHI_CommandList>> m_cmd_lists;
        RHI_CommandList* m_cmd_current = nullptr;

        // Swapchain
        static const uint8_t m_swap_chain_buffer_count = 3;
        std::shared_ptr<RHI_SwapChain> m_swap_chain;

        // Entity references
        std::unordered_map<Renderer_ObjectType, std::vector<Entity*>> m_entities;
        std::array<Material*, m_max_material_instances> m_material_instances;
        std::shared_ptr<Camera> m_camera;

        // Dependencies
        Profiler* m_profiler            = nullptr;
        ResourceCache* m_resource_cache = nullptr;
    };
}
