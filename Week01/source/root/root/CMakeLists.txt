cmake_minimum_required(VERSION 3.20)
project(DirectX11_Review)

set(CMAKE_CXX_STANDARD 17)

# macOS 환경에서만 더미 헤더를 사용하도록 설정
if(APPLE)
    add_compile_definitions(MACOS_REVIEW_MODE)
    # 헤더 경로 추가
    include_directories(${CMAKE_CURRENT_SOURCE_DIR})
endif()

# 소스 파일 추가
# 주의: main.cpp가 Renderer.cpp를 #include 하고 있으므로 
# 컴파일 대상에는 main.cpp만 추가합니다.
add_executable(DirectX11_Review main.cpp)

# Windows 라이브러리 링크 (macOS에서는 무시됨)
if(WIN32)
    target_link_libraries(DirectX11_Review d3d11 d3dcompiler)
endif()