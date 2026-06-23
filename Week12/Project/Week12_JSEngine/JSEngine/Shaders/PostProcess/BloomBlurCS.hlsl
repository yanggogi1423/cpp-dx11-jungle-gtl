Texture2D<float4> InputTex : register(t0);
RWTexture2D<float4> OutputTex : register(u0);

cbuffer BloomBlurCB : register(b0)
{
    float2 TexelSize;
    float2 Pad;
};

static const float Weights[5] =
{
    0.227027,
    0.194595,
    0.121622,
    0.054054,
    0.016216
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

    int2 Center = int2(GlobalID.xy);
    float3 Color = InputTex[Center].rgb * Weights[0] * Weights[0];

    [unroll]
    for (int X = -4; X <= 4; ++X)
    {
        [unroll]
        for (int Y = -4; Y <= 4; ++Y)
        {
            if (X == 0 && Y == 0)
            {
                continue;
            }

            int2 MaxPos = int2((int)Width - 1, (int)Height - 1);
            int2 SamplePos = clamp(Center + int2(X, Y), int2(0, 0), MaxPos);
            Color += InputTex[SamplePos].rgb * Weights[abs(X)] * Weights[abs(Y)];
        }
    }

    OutputTex[GlobalID.xy] = float4(Color, 1.0);
}
