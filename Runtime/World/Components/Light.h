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

#pragma once

//= INCLUDES ========================
#include <array>
#include <memory>
#include "IComponent.h"
#include "../../Math/Vector4.h"
#include "../../Math/Vector3.h"
#include "../../Math/Matrix.h"
#include "../../RHI/RHI_Definition.h"
#include "../../Math/Vector2.h"
#include "../../Math/Frustum.h"
//===================================

namespace Spartan
{
	class Camera;
	class Renderable;
	class Renderer;

	enum LightType
	{
		LightType_Directional,
		LightType_Point,
		LightType_Spot
	};

    struct Cascade
    {
        Math::Vector3 min       = Math::Vector3::Zero;
        Math::Vector3 max       = Math::Vector3::Zero;
        Math::Vector3 center    = Math::Vector3::Zero;
        Math::Frustum frustum;
    };

	class SPARTAN_CLASS Light : public IComponent
	{
	public:
		Light(Context* context, Entity* entity, uint32_t id = 0);
        ~Light() = default;

		//= COMPONENT ================================
		void OnInitialize() override;
		void OnStart() override;
		void OnTick(float delta_time) override;
		void Serialize(FileStream* stream) override;
		void Deserialize(FileStream* stream) override;
		//============================================

		auto GetLightType() { return m_lightType; }
		void SetLightType(LightType type);

		void SetColor(float r, float g, float b, float a)	{ m_color = Math::Vector4(r, g, b, a); }
		void SetColor(const Math::Vector4& color)			{ m_color = color; }
		const auto& GetColor()								{ return m_color; }

		void SetIntensity(float value)	{ m_intensity = value; }
		auto GetIntensity()				{ return m_intensity; }

		bool GetCastShadows() const { return m_cast_shadows; }
		void SetCastShadows(bool castShadows);

		void SetRange(float range);
		auto GetRange() { return m_range; }

		void SetAngle(float angle);
		auto GetAngle() { return m_angle_rad; }

		void SetBias(float value)	{ m_bias = value; }
		float GetBias()				{ return m_bias; }

		void SetNormalBias(float value) { m_normal_bias = value; }
		auto GetNormalBias()			{ return m_normal_bias; }

		Math::Vector3 GetDirection() const;

		const Math::Matrix& GetViewMatrix(uint32_t index = 0);
		const Math::Matrix& GetProjectionMatrix(uint32_t index = 0);

		const auto& GetShadowMap() const { return m_shadow_map; }
        void CreateShadowMap(bool force);

        bool IsInViewFrustrum(Renderable* renderable, uint32_t index) const;

	private:
		void ComputeViewMatrix();
		bool ComputeProjectionMatrix(uint32_t index = 0);
        void ComputeCascadeSplits();
		
		LightType m_lightType	= LightType_Directional;
		bool m_cast_shadows		= true;
		float m_range			= 10.0f;
		float m_intensity		= 15.0f;
		float m_angle_rad		= 0.5f; // about 30 degrees
		float m_bias			= 0.0001f;
		float m_normal_bias		= 15.0f;	
		bool m_is_dirty			= true;
		Math::Vector4 m_color   = Math::Vector4(1.0f, 0.76f, 0.57f, 1.0f);
		std::array<Math::Matrix, 6> m_matrix_view;
		std::array<Math::Matrix, 6> m_matrix_projection;
		Math::Quaternion m_lastRotLight;
		Math::Vector3 m_lastPosLight;
		Math::Matrix m_camera_last_view;
        std::vector<Cascade> m_cascades;
		
		// Shadow map
		std::shared_ptr<RHI_Texture> m_shadow_map;	
		Renderer* m_renderer;
	};
}
