struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD0;
};

cbuffer RmlCompositeCB : register(b0)
{
	float UseMask;
	float3 Padding;
};

Texture2D SourceTexture : register(t0);
Texture2D MaskTexture : register(t1);
SamplerState UISampler : register(s0);

VSOutput VS(uint VertexId : SV_VertexID)
{
	VSOutput Output;
	float2 Positions[3] = {
		float2(-1.0f, -1.0f),
		float2(-1.0f,  3.0f),
		float2( 3.0f, -1.0f)
	};
	float2 TexCoords[3] = {
		float2(0.0f, 1.0f),
		float2(0.0f, -1.0f),
		float2(2.0f, 1.0f)
	};

	Output.Position = float4(Positions[VertexId], 0.0f, 1.0f);
	Output.TexCoord = TexCoords[VertexId];
	return Output;
}

float4 PS(VSOutput Input) : SV_Target
{
	float4 Source = SourceTexture.Sample(UISampler, Input.TexCoord);
	if (Source.a > 0.0001f)
	{
		Source.rgb /= Source.a;
	}

	if (UseMask > 0.5f)
	{
		float4 Mask = MaskTexture.Sample(UISampler, Input.TexCoord);
		Source.a *= Mask.a;
	}
	return Source;
}
