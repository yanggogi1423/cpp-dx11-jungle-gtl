#include "Common/Functions.hlsli"

cbuffer CameraLetterboxCB : register(b2)
{
    float4 LetterboxColor;
    float LetterboxAmount;
    float LetterboxThickness;
    float ViewportAspect;
    float ReferenceAspect;
};

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    float thickness = saturate(LetterboxThickness * LetterboxAmount);

    bool inTopBar = input.uv.y < thickness;
    bool inBottomBar = input.uv.y > 1.0 - thickness;
    bool inAspectBar = false;
    if (ViewportAspect > 0.0f && ReferenceAspect > 0.0f)
    {
        if (ViewportAspect > ReferenceAspect)
        {
            float sideThickness = saturate((1.0f - ReferenceAspect / ViewportAspect) * 0.5f);
            inAspectBar = input.uv.x < sideThickness || input.uv.x > 1.0f - sideThickness;
        }
        else if (ViewportAspect < ReferenceAspect)
        {
            float topThickness = saturate((1.0f - ViewportAspect / ReferenceAspect) * 0.5f);
            inAspectBar = input.uv.y < topThickness || input.uv.y > 1.0f - topThickness;
        }
    }

    if (!inTopBar && !inBottomBar && !inAspectBar)
    {
        discard;
    }
    
    return LetterboxColor;
}
