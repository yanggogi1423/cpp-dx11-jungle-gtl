Texture2D SceneTex : register(t0);
SamplerState Sampler : register(s0);

cbuffer PostProcessCB : register(b11)
{
    float2 InvResolution;
    float VignetteIntensity;
    float VignetteRadius;
    float VignetteSmoothness;
    uint GammaCorrectionEnabled;
    float Gamma;
    uint ToneMappingEnabled;
    uint ToneMappingMode;
    float Exposure;
    float HableWhitePoint;
    float Padding;
    float4 VignetteColor;
}

struct VSOutput
{
    float4 Pos : SV_POSITION;
};

VSOutput mainVS(uint id : SV_VertexID)
{
    float2 pos;
    if (id == 0)
        pos = float2(-1, -1);
    else if (id == 1)
        pos = float2(-1, 3);
    else
        pos = float2(3, -1);

    VSOutput o;
    o.Pos = float4(pos, 0, 1);
    return o;
}

float3 LinearToSRGB(float3 Color)
{
    return pow(saturate(Color), 1.0 / 2.2);
}

float3 ToneMapReinhard(float3 Color)
{
    return Color / (Color + 1.0);
}

float3 ToneMapACES(float3 Color)
{
    return saturate((Color * (2.51 * Color + 0.03)) / (Color * (2.43 * Color + 0.59) + 0.14));
}

float3 Uncharted2Tonemap(float3 Color)
{
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((Color * (A * Color + C * B) + D * E) / (Color * (A * Color + B) + D * F)) - E / F;
}

float3 ToneMapHable(float3 Color, float WhitePoint)
{
    float SafeWhite = max(WhitePoint, 0.001);
    float3 Mapped = Uncharted2Tonemap(Color);
    float3 WhiteScale = 1.0 / Uncharted2Tonemap(SafeWhite.xxx);
    return saturate(Mapped * WhiteScale);
}

float3 ApplyToneMapping(float3 Color)
{
    float3 Exposed = max(Color * max(Exposure, 0.0), 0.0);

    if (ToneMappingMode == 1)
    {
        return ToneMapReinhard(Exposed);
    }
    if (ToneMappingMode == 2)
    {
        return ToneMapACES(Exposed);
    }
    if (ToneMappingMode == 3)
    {
        return ToneMapHable(Exposed, HableWhitePoint);
    }

    return saturate(Exposed);
}

float4 mainPS(VSOutput input) : SV_TARGET
{
    float2 uv = input.Pos.xy * InvResolution;

    float3 color = SceneTex.Sample(Sampler, uv).rgb;

    if (VignetteIntensity > 0.001)
    {
        float aspect = InvResolution.y / InvResolution.x;
        float2 centered = uv - 0.5;
        centered.x *= aspect;
        float distanceFromCenter = length(centered);
        float outer = VignetteRadius + max(VignetteSmoothness, 0.001);
        float vignette = smoothstep(VignetteRadius, outer, distanceFromCenter);
        color = lerp(color, VignetteColor.rgb, vignette * saturate(VignetteIntensity));
    }

    if (ToneMappingEnabled != 0)
    {
        color = LinearToSRGB(ApplyToneMapping(color));
    }
    else if (GammaCorrectionEnabled != 0)
    {
        color = pow(color, 1.0 / Gamma);
    }
    
    return float4(color, 1.0);
}
