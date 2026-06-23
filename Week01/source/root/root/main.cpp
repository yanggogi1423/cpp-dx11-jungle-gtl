
#include "sphere.h"
#include "Renderer.cpp"
#include "d3d11_mock.h"
const float leftBorder = -1.0f;
const float bottomBorder = -1.0f;
const float rightBorder = 1.0f;
const float topBorder = 1.0f;


extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// message handler function
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
	{
		return true;
	}

	switch (msg)
	{
	case WM_DESTROY:
		//signal that the app should quit
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
}




class UPrimitive
{
private:

public:
	UPrimitive() {};
	virtual ~UPrimitive() {};
	virtual FVector getLocation() const = 0;
	virtual float getRadius() const = 0;
	virtual float getMass() = 0;
	virtual FVector getVelocity() const = 0;
	virtual FVector getAngle() const = 0;

	virtual void checkSide() = 0;
	virtual void updateGravity() = 0;
	virtual void updateLocation() = 0;


	virtual void setLocation(const FVector& ref) = 0;
	virtual void setVelocity(const FVector& ref) = 0;


};
class UBall : public UPrimitive
{
public:
	

	// location이 곧 초기 위치, 중심점
	FVector Location;

	// velocity 로 변화, 이 때 등가속도 운동으로 velocity에 가속값을 더해주면 됨
	FVector Velocity;
	float Radius;
	float Mass;
	FVector angle;
	// 중력가속도는 mass로 계산
	
	static int TotalNumBalls;
	static FVertexSimple* sphereVertex;
	static UINT sphereVertexNumIndices;
	
	float overlapleft = 0.0f;
	float overlapright = 0.0f;
	float overlaptop = 0.0f;
	float overlapbottom = 0.0f;



	UBall()
		:UPrimitive()
	{
		float raw_rand = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
		Radius = 0.05f + (raw_rand * 0.15f); // 0.05 ~ 0.2 사이
		Mass = Radius * Radius;

		float minPos = -1.0f + Radius;
		float maxPos = 1.0f - Radius;

		float range = maxPos - minPos;

		float randX = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * range + minPos;
		float randY = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * range + minPos;
		
		float randVelX = randX / 10.f;
		float randVelY = randY / 10.f;

		// z축 무조건 0으로 설정할 것
		Location = { randX , randY , 0 };
		Velocity = { randVelX ,randVelY , 0 };
		angle = { 0.0f , 0.0f , 0.03f };
		TotalNumBalls++;

	}

	~UBall()
	{
		TotalNumBalls--;

	}
	
	void updateLocation()
	{
		Location.x += Velocity.x;
		Location.y += Velocity.y;

		checkSide();

	}

	FVector getLocation() const
	{

		return Location;
	}
	float getRadius() const
	{
		return Radius;
	}

	void checkSide()
	{
		if (Location.x < leftBorder + Radius)
		{
			Velocity.x *= -1;
			Location.x = leftBorder + Radius;
		}
		if (Location.x > rightBorder - Radius)
		{
			Velocity.x *= -1;
			Location.x = rightBorder - Radius;

		}
		if (Location.y > topBorder - Radius)
		{
			Velocity.y *= -1;
			Location.y = topBorder - Radius;
		}
		if (Location.y < bottomBorder + Radius)
		{
			Velocity.y *= -1;
			Location.y = bottomBorder + Radius;
		}
	}

	void updateGravity()
	{
		float gravity = 0.00098f;
		
		if (Location.y <= bottomBorder + Radius)
		{
			return;
		}

		Velocity.y -= gravity;
	}

	float getMass()
	{
		return Mass;
	}

	FVector getVelocity() const
	{
		return Velocity;
	}

	FVector getAngle() const {
		return angle;
	}


	void setLocation(const FVector& ref)
	{
		Location = ref;
	}

	void setVelocity(const FVector& ref)
	{
		Velocity = ref;
	}

	void setMass(FVector& ref)
	{
		Velocity = ref;
	}
};

// pointer 연산자 아스타리스크 기준으로 좌측에 const 선언시 주소의 값을 수정하지 않겠다
// 우측에 const 선언시 pointer가 가진 주소를 바꾸지 않겠다는 선언
// 구현당시 헷갈리기 싫어서 값을 변경할 때는 pointer를 사용하고 check는 const ref를 사용함

inline void ResolveCollision(UPrimitive* p1, UPrimitive* p2)
{

	FVector pos1 = p1->getLocation(); pos1.z = 0.0f;
	FVector pos2 = p2->getLocation(); pos2.z = 0.0f;

	FVector delta = pos2 - pos1;
	delta.z = 0.0f;

	float distanceSq = delta.x * delta.x + delta.y * delta.y;
	float distance = sqrt(distanceSq);

	// 거리 0 예외 처리
	if (distance <= 0.0001f) return;

	float radiusSum = p1->getRadius() + p2->getRadius();

	if (distance < radiusSum)
	{

		FVector normal = delta / distance; 
		float overlap = radiusSum - distance; 

		FVector correction = normal * (overlap * 0.5f);

		FVector newPos1 = p1->getLocation() - correction; 
		FVector newPos2 = p2->getLocation() + correction; 

		// Z축 안전장치
		newPos1.z = 0.0f; newPos2.z = 0.0f;

		p1->setLocation(newPos1);
		p2->setLocation(newPos2);

		FVector v1 = p1->getVelocity();
		FVector v2 = p2->getVelocity();

		FVector relativeVel = v2 - v1;
		relativeVel.z = 0.0f;

		// 왜 내적함?
		// 그니까 충돌후 진행방향은 서로가 부딫힌 점에 그은 직선에 수직인 방향.
		// 정규화로 방향만 캐감
		// 그리고 두 공의 상대속도를 구해서 충돌 후 나눠먹을 속도의 크기를 알아냄
		// 그 크기를 진행방향으로 내적하면 크기가 투영되고 방향이 반영된다.

		float speedAlongNormal = (relativeVel.x * normal.x) + (relativeVel.y * normal.y);

		// 서로 멀어지고 있는 중이라면(이미 튕겼다면) 무시
		if (speedAlongNormal > 0) return;

		float simpleImpulse = speedAlongNormal * -2.0f;


		float m1 = p1->getMass();
		float m2 = p2->getMass();

		// 속도와 질량은 반비례
		float totalMass = m1 + m2;
		float r1 = m2 / totalMass; 
		float r2 = m1 / totalMass; 

		FVector newV1 = v1 + (normal * (simpleImpulse * r1 * -1.0f));
		FVector newV2 = v2 + (normal * (simpleImpulse * r2));

		newV1.z = 0.0f; newV2.z = 0.0f;

		p1->setVelocity(newV1);
		p2->setVelocity(newV2);
	}
}

int UBall::TotalNumBalls = 0;
FVertexSimple* UBall::sphereVertex = sphere_vertices;
UINT UBall::sphereVertexNumIndices = static_cast<UINT>(sizeof(sphere_vertices));

// 상수버퍼는 중력가속도로 써먹자
// 왜냐면 모두 동일하게 적용 되니까
// 탄성충돌과 공의 충돌은 객체끼리 계산을 맡기고
// 렌더러는 constantbuffer로 중력가속도만 제공
// 이 때 ImGui의 체크박스에 따라..... 상수 버퍼를 제공할지 말지 결정할 것
// 중력가속도 전용 Fvector 하나. 현재 가지고 있는 속도 or 가속도 하나
// 체크박스는 중력가속도 값을 더할지 말지 결정할 뿐 각각의 constantbuffer로 움직임을 관리할 것


inline bool CheckCollision(const UPrimitive& a, const UPrimitive& b)
{
	float distanceSq = (a.getLocation().x - b.getLocation().x) * (a.getLocation().x - b.getLocation().x) +
		(a.getLocation().y - b.getLocation().y) * (a.getLocation().y - b.getLocation().y);
	// z축 동일

	float radiusSum = a.getRadius() + b.getRadius();
	float radiusSumSq = radiusSum * radiusSum;

	return distanceSq <= radiusSumSq;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {

	//window class name

	WCHAR WindowClass[] = L"JunglWindowClass";

	// window title 
	WCHAR Title[] = L"Game Tech Lab";

	//set message handler function address
	WNDCLASSW wndclass = { 0,WndProc , 0,0,0,0,0,0,0, WindowClass };

	// register window class
	RegisterClassW(&wndclass);

	// 1024 x 1024 size window create

	HWND hWnd = CreateWindowExW(0, WindowClass, Title, WS_POPUP | WS_VISIBLE | WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1024, 1024, nullptr, nullptr, hInstance, nullptr);

	// add any creature code

	bool bIsExit = false;

	///////////////////////////////////////////////////////////////////////////
	//////////////////////   렌더링 객체와 관련된 구현 /////////////////////////////
	///////////////////////////////////////////////////////////////////////////



	URenderer renderer;

	// hWnd window에 renderer 만듬
	renderer.Create(hWnd);

	// renderer에 shader 만듬
	renderer.CreateShader();

	FVertexSimple* sphere_vertices = UBall::sphereVertex;
	UINT numIndices = UBall::sphereVertexNumIndices;

	// buffer만 만듬
	ID3D11Buffer * sphere_vertexBuffer = renderer.CreateVertexBuffer(sphere_vertices, numIndices);

	renderer.CreateConstantBuffer();
	//generate ImGUi 

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplWin32_Init((void*)hWnd);
	ImGui_ImplDX11_Init(renderer.Device, renderer.DeviceContext);


	///////////////////////////////////////////////////////////////////////////
	//////////////////////   공의 객체와 관련된 구현 /////////////////////////////
	///////////////////////////////////////////////////////////////////////////

	int ball_arr_size = 10;
	UPrimitive** ball_arr = new UPrimitive * [ball_arr_size];
	for (UINT i = 0; i < ball_arr_size; i++)
	{
		ball_arr[i] = nullptr;
	}
	
	// 최초생성
	ball_arr[0] = new UBall();

	/////////////////////// 그 외 //////////////////////////////
		// ImGui로 개수 파악

	 float rotateVal = 0.0f;
	 int imGui_Ball_count = UBall::TotalNumBalls;
	 bool bGravity = true;
	 bool bRotate = true;


	 // FPS 제한을 위한 설정
	 const int targetFPS = 30;
	 const double targetFrameTime = 1000.0 / targetFPS; // 한 프레임의 목표 시간 (밀리초 단위)

	 // 고성능 타이머 초기화
	 LARGE_INTEGER frequency;
	 QueryPerformanceFrequency(&frequency);

	 LARGE_INTEGER startTime, endTime;
	 double elapsedTime = 0.0;


	/////////////////////// main loop ///////////////////////////
	//////////////////////// frame /////////////////////////////
	while (bIsExit == false) {

		QueryPerformanceCounter(&startTime);
		MSG msg;

		//operate no more message
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {

			//translate key message
			TranslateMessage(&msg);

			//send message property window procesure , message moved to WndProc
			DispatchMessage(&msg);

			if (msg.message == WM_QUIT) {
				bIsExit = true;
				break;
			}

		}


		// prepare
		renderer.Prepare();
		renderer.PrepareShader();


		///////////////////////////////////////
		/////// write code down here  /////////
		///////////////////////////////////////

		for (int i = 0; i < UBall::TotalNumBalls; i++)
		{
			for (int j = i + 1; j < UBall::TotalNumBalls; j++)
			{
				if (ball_arr[i] == nullptr or ball_arr[j] == nullptr)
				{
					continue;
				}
				// 공의 충돌 여부 확인
				if (CheckCollision(*ball_arr[i], *ball_arr[j]))
				{
					// 충돌시 완전 탄성충돌
					ResolveCollision(ball_arr[i], ball_arr[j]);
				}

			}
		}

		for (int i = 0; i < UBall::TotalNumBalls; i++)
		{

			if(ball_arr[i] == nullptr)
			{
				continue;
			}

			// 중력 여부 확인
			if (bGravity)
			{
				ball_arr[i]->updateGravity();
			}
			ball_arr[i]->updateLocation();


			// 각속도를 조절하는데 아니라 각가속도를 조절하도록 구현해서
			// 의도와 다르게 동작함
			// 명백한 구현 실수이다.
			// 변수 관리와 hlsl파일에서 회전연산이 매 프레임마다 이루어질텐데 이를 고려하지 못한 실수다.
			// 만약 회전운동을 하지 않도록 하려면 각속도의 값을 0으로 설정해야함
			if (bRotate)
			{
				rotateVal += ball_arr[i]->getAngle().z;
				rotateVal = fmodf(rotateVal, 360.0f);
			}


			// 각 공에 적용될 상수버퍼 생성
			FConstants temp;
			temp.offset = ball_arr[i]->getLocation();
			temp.radius = ball_arr[i]->getRadius();
			temp.angle = { 0.0f , 0.0f , rotateVal};
			temp.padding = 0.0f;

			//상수버퍼 update
			renderer.UpdateConstant(temp);

			// draw call
			renderer.RenderPrimitive(sphere_vertexBuffer, numIndices);

		}



		// ready for ImGUi rendering , controll setting, rendering request
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// 이후 ImGui UI 컨트롤 추가는 ImGui::NewFrame()과 ImGui::Render() 사이인 여기에 위치합니다.
		ImGui::Begin("Jungle Property Window");

		ImGui::Checkbox("Gravity", &bGravity);
		ImGui::Checkbox("Rotate", &bRotate);

		// 최소 한개 보장하게 해야함
		ImGui::InputInt("Number of Balls", &imGui_Ball_count);
		if (imGui_Ball_count < 1) imGui_Ball_count = 1;

		while (imGui_Ball_count != UBall::TotalNumBalls)
		{
			// 줄일 때
			if (imGui_Ball_count < UBall::TotalNumBalls)
			{
				int idx = rand() % UBall::TotalNumBalls;
				delete ball_arr[idx];
				// 3. Swap & Pop 구현 & 자기 참조 방지
				if (idx != UBall::TotalNumBalls) 
				{
					ball_arr[idx] = ball_arr[UBall::TotalNumBalls];
				}

				ball_arr[UBall::TotalNumBalls] = nullptr;
			}
			// 만들 때
			else if (imGui_Ball_count > UBall::TotalNumBalls)
			{
				if (UBall::TotalNumBalls >= ball_arr_size)
				{
					int newCapacity = ball_arr_size * 2;
					UPrimitive** tempList = new UPrimitive * [newCapacity];

					// 주소복사
					for (int i = 0; i < ball_arr_size; i++)
					{
						tempList[i] = ball_arr[i];
					}

					// 구 배열 메모리 해제
					delete[] ball_arr;

					// 포인터 교체 및 용량 변수 갱신
					ball_arr = tempList;
					ball_arr_size = newCapacity;
				}

				UBall* newBall = new UBall();
				ball_arr[UBall::TotalNumBalls - 1] = newBall;
			}
		}


		ImGui::End();
		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());



		//change back buffer with front buffer
		renderer.SwapBuffer();
		do
		{
			Sleep(0);

			// 루프 종료 시간 기록
			QueryPerformanceCounter(&endTime);

			// 한 프레임이 소요된 시간 계산 (밀리초 단위로 변환)
			elapsedTime = (endTime.QuadPart - startTime.QuadPart) * 1000.0 / frequency.QuadPart;

		} while (elapsedTime < targetFrameTime);

	};

	for (int i = 0; i < ball_arr_size; i++)
	{
		delete ball_arr[i];

	}
	delete[] ball_arr;

	//vertexbuffer , constantbuffer 해제 놓침
	// 면접에서는 이런경우를 대비해서 RAII 객체를 사용하거나 소멸자에 release를 순서에 맞게 몰아넣는 방식을 사용한다 대답할 예정
	// wrapper 클래스는 내가 잘 모름

	//delete ImGui
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	renderer.ReleaseShader();
	renderer.Release();
	return 0;
}
