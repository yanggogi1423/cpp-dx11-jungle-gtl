// ShaderW0.hlsl

cbuffer constants : register(b0)
{
    float3 Offset; // transpose
    float radius;  // scale
    float3 Angle;   // rotation
    float Padding;
}

struct VS_INPUT
{
    float4 position : POSITION; // Input position from vertex buffer
    float4 color : COLOR; // Input color from vertex buffer
};

struct PS_INPUT
{
    float4 position : SV_POSITION; // Transformed position to pass to the pixel shader
    float4 color : COLOR; // Color to pass to the pixel shader
};

// 회전 행렬 연산 , Z축 고정으로 3차원 회전이 아니기 때문에 오일러 회전을 적용해도 짐벌락 없음
float4 RotateVector4(float4 pos, float3 ang)
{
    float s = sin(ang.z);
    float c = cos(ang.z);
    
    
    float3 rotated;
    rotated.x = pos.x * c - pos.y * s;
    rotated.y = pos.x * s + pos.y * c;
    rotated.z = pos.z;
    
    return float4(rotated, pos.w);

}

// 행렬 곱 기준으로 조금 비효율적인 연산임
// SRT 연산을 shader 단계에서 하나씩 하고 있는데 행렬곱 기준 3 * n 의 연산이 됨
// 만약 SRT 연산을 마치고 shader 단계에서 SRT의 결과만 행렬곱을 진행하면 n + 3의 연산을 할 수 있음
// 아래는 꽤 느린 연산임


PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;

    // 1. Scale 
    float4 scaledPos = float4(input.position.xyz * radius, 1.0f);

    // 2. rotation
    float4 rotatePos = RotateVector4(scaledPos, Angle);
    
    // 3. translate
    output.position = rotatePos + float4(Offset, 0.0f);

    // 3. Color 전달
    output.color = input.color;
    
    return output;
}


float4 mainPS(PS_INPUT input) : SV_TARGET
{
    // Output the color directly
    return input.color;
}
