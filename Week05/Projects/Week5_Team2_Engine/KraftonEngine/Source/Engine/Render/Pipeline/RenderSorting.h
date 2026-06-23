#pragma once
#include <algorithm>
#include "Core/CoreTypes.h"
#include "Render/Pipeline/RenderCommand.h"
#include "Profiling/Stats.h"
namespace FRenderSorting
{
	inline void SortIndices(FPassQueueSoA& Queue)
	{
		if (Queue.SortedIndices.size() < 2) return;

		std::sort(Queue.SortedIndices.begin(), Queue.SortedIndices.end(), [&Queue](uint32 A, uint32 B) {
			// 1. Shader 우선 정렬
			if (Queue.Shaders[A] != Queue.Shaders[B])
				return Queue.Shaders[A] < Queue.Shaders[B];

			// 2. Mesh Buffer 정렬
			if (Queue.MeshBuffers[A] != Queue.MeshBuffers[B])
				return Queue.MeshBuffers[A] < Queue.MeshBuffers[B];

			// 3. Texture(SRV) 정렬
			if (Queue.FirstSRVs[A] != Queue.FirstSRVs[B])
				return Queue.FirstSRVs[A] < Queue.FirstSRVs[B];

			// 모두 같으면 순서 유지
			return false;
		});
	}
}

