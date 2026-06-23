#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/SystemSamplers.hlsli"

cbuffer ScopeLensCB : register(b2)
{
    float Radius;
    float Feather;
    float OuterBlurRadius;
    float EdgeBlurRadius;
    float Intensity;
    float AspectRatio;
    float CenterX;
    float CenterY;
    float2 CenterOffset;
};

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float2 GetSceneTexel(float radius)
{
    uint w;
    uint h;
    SceneColorTexture.GetDimensions(w, h);
    return float2(1.0f / max((float)w, 1.0f), 1.0f / max((float)h, 1.0f)) * radius;
}

float2 GetScopeTexel(float radius)
{
    uint w;
    uint h;
    ScopeLensTexture.GetDimensions(w, h);
    return float2(1.0f / max((float)w, 1.0f), 1.0f / max((float)h, 1.0f)) * radius;
}

float4 BlurSceneColor(float2 uv, float radius)
{
    float2 texel = GetSceneTexel(radius);
    float4 c = SceneColorTexture.SampleLevel(LinearClampSampler, uv, 0) * 0.20f;
    c += SceneColorTexture.SampleLevel(LinearClampSampler, uv + float2(texel.x, 0.0f), 0) * 0.12f;
    c += SceneColorTexture.SampleLevel(LinearClampSampler, uv - float2(texel.x, 0.0f), 0) * 0.12f;
    c += SceneColorTexture.SampleLevel(LinearClampSampler, uv + float2(0.0f, texel.y), 0) * 0.12f;
    c += SceneColorTexture.SampleLevel(LinearClampSampler, uv - float2(0.0f, texel.y), 0) * 0.12f;
    c += SceneColorTexture.SampleLevel(LinearClampSampler, uv + texel, 0) * 0.08f;
    c += SceneColorTexture.SampleLevel(LinearClampSampler, uv - texel, 0) * 0.08f;
    c += SceneColorTexture.SampleLevel(LinearClampSampler, uv + float2(texel.x, -texel.y), 0) * 0.08f;
    c += SceneColorTexture.SampleLevel(LinearClampSampler, uv + float2(-texel.x, texel.y), 0) * 0.08f;
    return c;
}

float4 BlurScopeColor(float2 uv, float radius)
{
    float2 texel = GetScopeTexel(radius);
    float4 c = ScopeLensTexture.SampleLevel(LinearClampSampler, uv, 0) * 0.20f;
    c += ScopeLensTexture.SampleLevel(LinearClampSampler, uv + float2(texel.x, 0.0f), 0) * 0.12f;
    c += ScopeLensTexture.SampleLevel(LinearClampSampler, uv - float2(texel.x, 0.0f), 0) * 0.12f;
    c += ScopeLensTexture.SampleLevel(LinearClampSampler, uv + float2(0.0f, texel.y), 0) * 0.12f;
    c += ScopeLensTexture.SampleLevel(LinearClampSampler, uv - float2(0.0f, texel.y), 0) * 0.12f;
    c += ScopeLensTexture.SampleLevel(LinearClampSampler, uv + texel, 0) * 0.08f;
    c += ScopeLensTexture.SampleLevel(LinearClampSampler, uv - texel, 0) * 0.08f;
    c += ScopeLensTexture.SampleLevel(LinearClampSampler, uv + float2(texel.x, -texel.y), 0) * 0.08f;
    c += ScopeLensTexture.SampleLevel(LinearClampSampler, uv + float2(-texel.x, texel.y), 0) * 0.08f;
    return c;
}

float4 PS(PS_Input_UV input) : SV_Target
{
    float2 uv = input.uv;
    float2 lensCenter = float2(CenterX, CenterY);
    float2 offsetCenter = lensCenter + CenterOffset * 0.5f;
    float2 p = (uv - offsetCenter) * 2.0f;
    p.x *= AspectRatio;

    float dist = length(p);
    float inner = max(0.0f, Radius - Feather);
    float lensMask = 1.0f - smoothstep(inner, Radius, dist);
    float edgeMask = 1.0f - abs(lensMask * 2.0f - 1.0f);

    float4 baseColor = SceneColorTexture.SampleLevel(LinearClampSampler, uv, 0);
    float4 outerColor = OuterBlurRadius > 0.01f
        ? BlurSceneColor(uv, OuterBlurRadius)
        : baseColor;

    float4 scopeColor = ScopeLensTexture.SampleLevel(LinearClampSampler, uv, 0);
    if (EdgeBlurRadius > 0.01f)
    {
        scopeColor = lerp(scopeColor, BlurScopeColor(uv, EdgeBlurRadius), saturate(edgeMask));
    }

    float4 composite = lerp(outerColor, scopeColor, lensMask);
    return lerp(baseColor, composite, saturate(Intensity));
}
