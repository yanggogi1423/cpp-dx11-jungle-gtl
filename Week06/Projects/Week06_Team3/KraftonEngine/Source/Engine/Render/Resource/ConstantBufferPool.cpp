#include "ConstantBufferPool.h"

void FConstantBufferPool::Initialize(ID3D11Device* InDevice)
{
	Device = InDevice;
}

void FConstantBufferPool::Release()
{
	for (auto& [Slot, CB] : Pool)
	{
		CB.Release();
	}
	Pool.clear();
	Device = nullptr;
}

FConstantBuffer* FConstantBufferPool::GetBuffer(uint32 Slot, uint32 ByteWidth)
{
	auto It = Pool.find(Slot);
	if (It != Pool.end())
	{
		FConstantBuffer& Existing = It->second;
		if (Device && ByteWidth > Existing.GetByteWidth())
		{
			Existing.Create(Device, ByteWidth);
		}
		return &It->second;
	}

	auto& CB = Pool[Slot];
	if (Device)
	{
		CB.Create(Device, ByteWidth);
	}
	return &CB;
}
