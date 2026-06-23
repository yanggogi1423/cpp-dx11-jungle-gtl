#pragma once

#include "Core/CoreTypes.h"
#include "Render/Resource/Buffer.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;

// Per-instance VertexBuffer 래퍼입니다.
// Dynamic VB(D3D11_USAGE_DYNAMIC + MAP_WRITE_DISCARD) 위에 인스턴스 단위 Update / 자동 재할당을 얹습니다.
// Stride / InstanceCount / Capacity는 내부 FVertexBuffer가 이미 추적하므로 중복하지 않습니다.
class FInstanceBuffer
{
public:
    FInstanceBuffer() = default;
    ~FInstanceBuffer() { Release(); }

    FInstanceBuffer(const FInstanceBuffer&) = delete;
    FInstanceBuffer& operator=(const FInstanceBuffer&) = delete;

    void Create(ID3D11Device* InDevice, uint32 InStride, uint32 InInitialCapacity);

    // Capacity 초과 시 grow-by-2x로 재할당합니다.
    // InData가 nullptr이거나 InInstanceCount가 0이면 InstanceCount만 0으로 갱신하고 D3D Update를 건너뜁니다.
    void Update(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, const void* InData, uint32 InInstanceCount);

    void Release();

    ID3D11Buffer* GetBuffer() const { return VertexBuffer.GetBuffer(); }
    uint32 GetStride() const { return VertexBuffer.GetStride(); }
    uint32 GetInstanceCount() const { return VertexBuffer.GetVertexCount(); }
    uint32 GetCapacity() const { return VertexBuffer.GetVertexCapacity(); }
    bool IsValid() const { return VertexBuffer.GetBuffer() != nullptr; }

private:
    FVertexBuffer VertexBuffer;
};
