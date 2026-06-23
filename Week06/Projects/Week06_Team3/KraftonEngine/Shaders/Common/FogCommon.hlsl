#ifndef CONSTANT_BUFFERS_HLSL
struct FogUniformParameters
{
    float4 ExponentialFogParameters;
    float4 ExponentialFogColorParameter;
    float4 ExponentialFogParameters3;
};
#endif

static const float FOG_DENSITY_SCALE = 0.1f;
static const float FOG_FAR_DEPTH_DISTANCE = 1000.0f;

// FogUniformParameters에 저장된 원본 fog density를 음수 없이 꺼냅니다.
// 컴포넌트가 가진 값 범위와 셰이더에서 체감되는 변화량을 분리하기 위해
// 실제 계산에는 GetScaledFogDensity()를 거쳐 사용합니다.
float GetFogDensity(FogUniformParameters fog)
{
    return max(fog.ExponentialFogParameters.x, 0.0f);
}

// 에디터/컴포넌트 쪽에서 다루는 fog density를 셰이더용 스케일로 변환합니다.
// deferred 참고 구현에서도 동일하게 0.1 스케일을 둬서 화면상 변화량을 제어했습니다.
float GetScaledFogDensity(FogUniformParameters fog)
{
    return GetFogDensity(fog) * FOG_DENSITY_SCALE;
}

// 높이 감쇠 기울기입니다.
// 값이 0이면 exp 계산이 사실상 평면 fog가 되므로, 너무 작은 값은 최소치로 보정합니다.
float GetFogHeightFalloff(FogUniformParameters fog)
{
    return max(fog.ExponentialFogParameters.y, 0.0001f);
}

// 카메라에서 이 거리 전까지는 fog를 적용하지 않습니다.
// reference 코드의 FogStart 역할과 동일합니다.
float GetFogStartDistance(FogUniformParameters fog)
{
    return max(fog.ExponentialFogParameters.w, 0.0f);
}

// 높이 기반 감쇠의 기준이 되는 fog 평면 높이입니다.
float GetFogHeight(FogUniformParameters fog)
{
    return fog.ExponentialFogParameters3.y;
}

// 이 거리 이상에서는 fog를 강제로 끊습니다.
// 0이면 cutoff를 사용하지 않는 것으로 해석합니다.
float GetFogCutoffDistance(FogUniformParameters fog)
{
    return max(fog.ExponentialFogParameters3.w, 0.0f);
}

// CPU 쪽에서 a 채널에 1 - MaxOpacity를 저장하므로 셰이더에서 다시 복원합니다.
float GetFogMaxOpacity(FogUniformParameters fog)
{
    return saturate(1.0f - fog.ExponentialFogColorParameter.a);
}

float ComputeFogHeightAttenuation(FogUniformParameters fog, float sampleHeight)
{
    // deferred 참고 코드의 heightFactor와 동일한 개념입니다.
    // fog 평면보다 위로 갈수록 안개가 옅어지고, fog 평면 아래는 최대 밀도로 유지합니다.
    const float relativeHeight = max(sampleHeight - GetFogHeight(fog), 0.0f);
    return exp(-relativeHeight * GetFogHeightFalloff(fog));
}

float ComputeFogTransmittanceFromDistance(FogUniformParameters fog, float distanceToCamera, float heightAttenuation)
{
    const float startDistance = GetFogStartDistance(fog);
    const float cutoffDistance = GetFogCutoffDistance(fog);

    if (distanceToCamera <= startDistance)
    {
        // 시작 거리 안쪽은 fog를 전혀 먹이지 않으므로 빛의 전달률을 1로 둡니다.
        return 1.0f;
    }

    if (cutoffDistance > 0.0f && distanceToCamera >= cutoffDistance)
    {
        // cutoff가 설정된 경우 해당 거리 너머는 fog 적용 대상에서 제외합니다.
        // 즉, fog를 적용하지 않은 원본 색을 그대로 유지합니다.
        return 1.0f;
    }

    // deferred 예제의 fogAmount = exp(-density * heightFactor * distance)와 같은 식입니다.
    // 여기서는 fog가 낀 후 남는 "원본 색의 비율(전달률, transmittance)"을 반환합니다.
    const float effectiveDistance = distanceToCamera - startDistance;
    const float fogIntegral = effectiveDistance * GetScaledFogDensity(fog) * heightAttenuation;
    return exp(-fogIntegral);
}

float ComputeFogWeightFromTransmittance(FogUniformParameters fog, float transmittance)
{
    // 최종 fog blend 비율은 1 - transmittance이지만, MaxOpacity를 넘지 않도록 상한을 둡니다.
    const float maxOpacity = GetFogMaxOpacity(fog);
    return min(saturate(1.0f - transmittance), maxOpacity);
}

float ComputeFogWeightFromDistance(FogUniformParameters fog, float distanceToCamera, float heightAttenuation)
{
    const float transmittance = ComputeFogTransmittanceFromDistance(fog, distanceToCamera, heightAttenuation);
    return ComputeFogWeightFromTransmittance(fog, transmittance);
}
