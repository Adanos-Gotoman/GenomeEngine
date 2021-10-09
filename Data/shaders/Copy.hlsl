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

//= INCLUDES =========
#include "Common.hlsl"
//====================

#if COMPUTE

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    // Out of bounds check
    if (any(int2(thread_id.xy) >= g_resolution_rt.xy))
        return;

#if BILINEAR
    const float2 uv = (thread_id.xy + 0.5f) / g_resolution_rt;
    tex_out_rgb[thread_id.xy] = tex.SampleLevel(sampler_bilinear_clamp, uv, 0).rgb;
#else
    tex_out_rgb[thread_id.xy] = tex[thread_id.xy].rgb;
#endif
}

#elif PIXEL

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
#if BILINEAR
    return tex.Sample(sampler_bilinear_clamp, input.uv);
#else
    return tex.Sample(sampler_point_clamp, input.uv);
#endif
}

#endif
