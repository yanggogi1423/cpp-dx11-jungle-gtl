// sphere.h
#pragma once

#include <vector>

struct FVertexSimple
{
    float x, y, z;      // Position
    float r, g, b, a;   // Color
};

// 렌더링용 더미 데이터
static FVertexSimple sphere_vertices[] = {
    { 0.0f, 0.5f, 0.0f,  1.0f, 0.0f, 0.0f, 1.0f },
    { 0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f },
    { -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f }
};