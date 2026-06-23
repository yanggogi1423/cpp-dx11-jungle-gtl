Texture2D<float4> InputTex : register(t0);
RWTexture2D<float4> OutputTex : register(u0);

cbuffer BloomThresholdCB : register(b0)
{
    float Threshold;
    float Knee;
    float2 Pad;
};

[numthreads(8, 8, 1)]
void main(uint3 GlobalID : SV_DispatchThreadID)
{
    uint Width;
    uint Height;
    OutputTex.GetDimensions(Width, Height);
    if (GlobalID.x >= Width || GlobalID.y >= Height)
    {
        return;
    }

    float3 Color = InputTex[GlobalID.xy].rgb;
    float Brightness = max(Color.r, max(Color.g, Color.b));

    float SafeKnee = max(Knee, 0.0001);
    float Soft = clamp(Brightness - Threshold + SafeKnee, 0.0, 2.0 * SafeKnee);
    Soft = (Soft * Soft) / (4.0 * SafeKnee);
    float Weight = max(Soft, Brightness - Threshold) / max(Brightness, 0.0001);

    OutputTex[GlobalID.xy] = float4(Color * saturate(Weight), 1.0);
}
