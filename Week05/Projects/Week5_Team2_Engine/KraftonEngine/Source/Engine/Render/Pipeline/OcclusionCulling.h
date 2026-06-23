#pragma once

class FViewContext;

class OcclusionCulling
{
public:
	// 오클루전 컬링 필터 적용 (ViewContext 내의 CandidateProxies 필터링)
	static void ApplyOcclusionCulling(FViewContext& Context);
};
