Texture2D<float4> SceneColor : register(t0);
Texture2D<float4> BloomTex : register(t1);
RWTexture2D<float4> OutputTex : register(u0);

cbuffer BloomCompositeCB : register(b0)
{
    float BloomIntensity;
    float3 Pad;
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

    float3 Scene = SceneColor[GlobalID.xy].rgb;
    float3 Bloom = BloomTex[GlobalID.xy].rgb;
    OutputTex[GlobalID.xy] = float4(Scene + Bloom * BloomIntensity, 1.0);
}
