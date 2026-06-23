#include <cmath>
class UBall;
struct FVector;

inline void ResolveCollision(UPrimitive* p1, UPrimitive* p2)
{
	// 1. 위치 및 거리 계산 (Z축 제거)
	FVector pos1 = p1->getLocation(); pos1.z = 0.0f;
	FVector pos2 = p2->getLocation(); pos2.z = 0.0f;

	FVector delta = pos2 - pos1;
	delta.z = 0.0f;

	float distanceSq = delta.x * delta.x + delta.y * delta.y;
	float distance = std::sqrt(distanceSq);

	// 거리 0 예외 처리
	if (distance <= 0.0001f) return;

	float radiusSum = p1->getRadius() + p2->getRadius();

	// [충돌 발생!]
	if (distance < radiusSum)
	{
		// ---------------------------------------------------
		// A. 위치 보정 (Simple Position Correction)
		// 벽 충돌에서 "Location += 0.05" 하던 것과 같은 원리입니다.
		// ---------------------------------------------------

		FVector normal = delta / distance; // 충돌 방향
		float overlap = radiusSum - distance; // 겹친 깊이

		// 복잡한 질량 비율 대신, 단순히 반반씩(0.5) 나눠서 밀어냅니다.
		// (혹은 한쪽이 너무 가볍게 튕기는게 싫다면 이 비율만 Mass로 조절해도 됩니다)
		FVector correction = normal * (overlap * 0.5f);

		FVector newPos1 = p1->getLocation() - correction; // 뒤로 밀기
		FVector newPos2 = p2->getLocation() + correction; // 앞으로 밀기

		// Z축 안전장치
		newPos1.z = 0.0f; newPos2.z = 0.0f;

		p1->setLocation(newPos1);
		p2->setLocation(newPos2);


		// ---------------------------------------------------
		// B. 속도 반사 (Simple Velocity Reflection)
		// 벽 충돌에서 "Velocity *= -1" 하던 것의 2D 벡터 버전입니다.
		// ---------------------------------------------------

		FVector v1 = p1->getVelocity();
		FVector v2 = p2->getVelocity();

		// 기준 V2
		// 상대 속도 (v2가 v1을 볼 때의 속도)
		FVector relativeVel = v2 - v1;
		relativeVel.z = 0.0f;

		// 충돌 방향(Normal)으로의 속도 성분 추출
		float speedAlongNormal = (relativeVel.x * normal.x) + (relativeVel.y * normal.y);

		// 서로 멀어지고 있는 중이라면(이미 튕겼다면) 무시
		if (speedAlongNormal > 0) return;

		// 반사 힘 계산 (벽 충돌의 *= -1 과 유사한 효과)
		// -2.0f를 곱하면 에너지 손실 없는 완전 탄성 충돌이 됩니다.
		// (-1.0f은 멈춤, -1.5f 등은 약간 느려짐)
		float simpleImpulse = speedAlongNormal * -2.0f;

		// 질량을 고려해 속도 분배 (무거운 건 조금 변하고 가벼운 건 많이 변함)
		float m1 = p1->getMass();
		float m2 = p2->getMass();

		// 단순화를 위해 질량 합으로 나누어 비율만 정합니다.
		float totalMass = m1 + m2;
		float r1 = m2 / totalMass; // p1이 받을 영향 (상대방 무게에 비례)
		float r2 = m1 / totalMass; // p2가 받을 영향

		// 속도 적용
		// p1은 반대 방향으로 힘을 받으므로 뺍니다.
		FVector newV1 = v1 + (normal * (simpleImpulse * r1 * -1.0f));
		FVector newV2 = v2 + (normal * (simpleImpulse * r2));

		// Z축 제거
		newV1.z = 0.0f; newV2.z = 0.0f;

		p1->setVelocity(newV1);
		p2->setVelocity(newV2);
	}
}