// HZBBuild.hlsl
Texture2D<float> InTexture : register(t0);
RWTexture2D<float> OutMip0 : register(u0);
RWTexture2D<float> OutMip1 : register(u1);
RWTexture2D<float> OutMip2 : register(u2);
RWTexture2D<float> OutMip3 : register(u3);

cbuffer HZBConstants : register(b0)
{
    uint2 SrcResolution;
    uint2 DstResolution;
    uint NumMips;
    uint Padding;
};

groupshared float gs_mips[16][16];

[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID)
{
    // Mip 0
    float d = 1.0f;
    if (DTid.x < DstResolution.x && DTid.y < DstResolution.y)
    {
        uint2 srcCoord0 = DTid.xy * 2;
        uint2 srcCoord1 = min(srcCoord0 + uint2(1, 0), SrcResolution - 1);
        uint2 srcCoord2 = min(srcCoord0 + uint2(0, 1), SrcResolution - 1);
        uint2 srcCoord3 = min(srcCoord0 + uint2(1, 1), SrcResolution - 1);
        
        float d0 = InTexture.Load(uint3(srcCoord0, 0)).r;
        float d1 = InTexture.Load(uint3(srcCoord1, 0)).r;
        float d2 = InTexture.Load(uint3(srcCoord2, 0)).r;
        float d3 = InTexture.Load(uint3(srcCoord3, 0)).r;
        
        d = min(min(d0, d1), min(d2, d3));
        OutMip0[DTid.xy] = d;
    }
    
    if (NumMips == 1) return;
    
    gs_mips[GTid.y][GTid.x] = d;
    GroupMemoryBarrierWithGroupSync();
    
    // Mip 1
    if (NumMips > 1 && (GTid.x % 2 == 0) && (GTid.y % 2 == 0))
    {
        d = min(min(d, gs_mips[GTid.y][GTid.x + 1]), 
                min(gs_mips[GTid.y + 1][GTid.x], gs_mips[GTid.y + 1][GTid.x + 1]));
        
        uint2 outCoord = DTid.xy / 2;
        uint2 outRes = (DstResolution + 1) / 2;
        if (outCoord.x < outRes.x && outCoord.y < outRes.y)
        {
            OutMip1[outCoord] = d;
        }
        gs_mips[GTid.y][GTid.x] = d;
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    // Mip 2
    if (NumMips > 2 && (GTid.x % 4 == 0) && (GTid.y % 4 == 0))
    {
        d = min(min(d, gs_mips[GTid.y][GTid.x + 2]), 
                min(gs_mips[GTid.y + 2][GTid.x], gs_mips[GTid.y + 2][GTid.x + 2]));
        
        uint2 outCoord = DTid.xy / 4;
        uint2 outRes = (DstResolution + 3) / 4;
        if (outCoord.x < outRes.x && outCoord.y < outRes.y)
        {
            OutMip2[outCoord] = d;
        }
        gs_mips[GTid.y][GTid.x] = d;
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    // Mip 3
    if (NumMips > 3 && (GTid.x % 8 == 0) && (GTid.y % 8 == 0))
    {
        d = min(min(d, gs_mips[GTid.y][GTid.x + 4]), 
                min(gs_mips[GTid.y + 4][GTid.x], gs_mips[GTid.y + 4][GTid.x + 4]));
        
        uint2 outCoord = DTid.xy / 8;
        uint2 outRes = (DstResolution + 7) / 8;
        if (outCoord.x < outRes.x && outCoord.y < outRes.y)
        {
            OutMip3[outCoord] = d;
        }
    }
}
