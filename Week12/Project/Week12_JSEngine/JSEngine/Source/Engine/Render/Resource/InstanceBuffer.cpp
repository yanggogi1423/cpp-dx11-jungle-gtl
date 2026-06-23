#include "InstanceBuffer.h"

#include <d3d11.h>

void FInstanceBuffer::Create(ID3D11Device* InDevice, uint32 InStride, uint32 InInitialCapacity)
{
    Release();

    if (!InDevice || InStride == 0 || InInitialCapacity == 0)
    {
        return;
    }

    VertexBuffer.CreateRaw(InDevice, nullptr, InInitialCapacity, InStride, true);
}

void FInstanceBuffer::Update(ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext, const void* InData, uint32 InInstanceCount)
{
    if (!InDeviceContext)
    {
        return;
    }

    const uint32 Stride = VertexBuffer.GetStride();
    if (Stride == 0)
    {
        return;
    }

    // Capacity 초과 시 grow-by-2x로 재할당합니다.
    if (InInstanceCount > VertexBuffer.GetVertexCapacity())
    {
        if (!InDevice)
        {
            return;
        }

        uint32 NewCapacity = VertexBuffer.GetVertexCapacity() == 0 ? InInstanceCount : VertexBuffer.GetVertexCapacity();
        while (NewCapacity < InInstanceCount)
        {
            NewCapacity *= 2;
        }

        VertexBuffer.Release();
        VertexBuffer.CreateRaw(InDevice, nullptr, NewCapacity, Stride, true);
    }

    if (!InData || InInstanceCount == 0)
    {
        return;
    }

    VertexBuffer.UpdateRaw(InDeviceContext, InData, InInstanceCount);
}

void FInstanceBuffer::Release()
{
    VertexBuffer.Release();
}
