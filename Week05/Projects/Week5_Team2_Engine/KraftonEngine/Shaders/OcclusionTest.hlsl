// OcclusionTest.hlsl
Texture2D<float> HZB : register(t0);
SamplerState PointClampSampler : register(s0);

struct ProxyAABB
{
    float3 Min;
    uint Id;
    float3 Max;
    uint Padding;
};

StructuredBuffer<ProxyAABB> InProxies : register(t1);
RWStructuredBuffer<uint> OutVisibility : register(u0);

cbuffer PassConstants : register(b0)
{
    float4x4 ViewProjection;
    uint ProxyCount;
    uint HZBMipCount;
    float2 HZBSize;
    float2 ViewportSize;
};

[numthreads(64, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ProxyCount) return;

    ProxyAABB proxy = InProxies[DTid.x];
    
    float3 minP = proxy.Min;
    float3 maxP = proxy.Max;

    float3 corners[8] = {
        minP,
        float3(maxP.x, minP.y, minP.z),
        float3(minP.x, maxP.y, minP.z),
        float3(maxP.x, maxP.y, minP.z),
        float3(minP.x, minP.y, maxP.z),
        float3(maxP.x, minP.y, maxP.z),
        float3(minP.x, maxP.y, maxP.z),
        maxP
    };

    float3 minNDC = float3(1.1, 1.1, 1.1);
    float3 maxNDC = float3(-1.1, -1.1, -1.1);

    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        float4 clip = mul(float4(corners[i], 1.0), ViewProjection);
        
        // If any corner is behind or on the near plane, it's visible.
        // In Reverse-Z with D3D, near is Z=W, far is Z=0. 
        // So behind near plane means clip.z > clip.w (if using standard projection)
        // Actually, let's just use clip.w <= 0 logic as it's safe.
        if (clip.w <= 0.0001)
        {
            OutVisibility[DTid.x] = 1;
            return;
        }

        float3 ndc = clip.xyz / clip.w;
        minNDC = min(minNDC, ndc);
        maxNDC = max(maxNDC, ndc);
    }

    // Frustum Culling
    if (maxNDC.x < -1.0 || minNDC.x > 1.0 || maxNDC.y < -1.0 || minNDC.y > 1.0 || maxNDC.z < 0.0 || minNDC.z > 1.0)
    {
        OutVisibility[DTid.x] = 0;
        return;
    }

    // Convert to [0, 1] UV
    float2 minUV = saturate(minNDC.xy * float2(0.5, -0.5) + 0.5);
    float2 maxUV = saturate(maxNDC.xy * float2(0.5, -0.5) + 0.5);
    
    // Ensure minUV is top-left and maxUV is bottom-right
    if (minUV.x > maxUV.x) { float t = minUV.x; minUV.x = maxUV.x; maxUV.x = t; }
    if (minUV.y > maxUV.y) { float t = minUV.y; minUV.y = maxUV.y; maxUV.y = t; }
    
    // Adjust for HZB texture aspect/region
    float2 uvScale = (ViewportSize * 0.5f) / HZBSize;
    minUV *= uvScale;
    maxUV *= uvScale;

    // Calculate mip level for 4-tap sampling
    float2 size = (maxUV - minUV) * HZBSize;
    float maxSide = max(size.x, size.y);
    float mip = ceil(log2(maxSide));
    mip = clamp(mip, 0, (float)HZBMipCount - 1.0);
    
    // 4-tap HZB test at the calculated mip level
    float d0 = HZB.SampleLevel(PointClampSampler, float2(minUV.x, minUV.y), mip).r;
    float d1 = HZB.SampleLevel(PointClampSampler, float2(maxUV.x, minUV.y), mip).r;
    float d2 = HZB.SampleLevel(PointClampSampler, float2(minUV.x, maxUV.y), mip).r;
    float d3 = HZB.SampleLevel(PointClampSampler, float2(maxUV.x, maxUV.y), mip).r;

    // In Reverse-Z, min is furthest. 
    float minH = min(min(d0, d1), min(d2, d3));
    
    // Closest point of the object in Reverse-Z is maxNDC.z
    uint isVisible = (maxNDC.z >= minH - 0.0001f) ? 1 : 0;
    
    OutVisibility[DTid.x] = isVisible;
}
