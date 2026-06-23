#pragma once

#include "Core/CoreTypes.h"

struct FSkinningRuntimeStats
{
	uint32 UpdateCallCount = 0;
	uint32 CPUVertexCallCount = 0;
	uint32 PoseUpdateCallCount = 0;
	uint32 MatrixUpdateCallCount = 0;
	uint32 GPUMatrixUploadCallCount = 0;
	uint32 CPUBufferUploadCallCount = 0;
	uint32 SubmittedComponentCount = 0;
	uint32 SubmittedCPUComponentCount = 0;
	uint32 SubmittedGPUComponentCount = 0;
	uint32 SubmittedSectionCount = 0;
	uint32 SubmittedDrawCommandCount = 0;
	uint32 SubmittedVertexCount = 0;
	uint32 SubmittedIndexCount = 0;
	uint32 SubmittedBoneCount = 0;
	uint32 CPUVertexCount = 0;
	uint32 GPUMatrixBoneCount = 0;
	double TotalUpdateMs = 0.0;
	double TotalCPUVertexMs = 0.0;
	double TotalPoseUpdateMs = 0.0;
	double TotalMatrixUpdateMs = 0.0;
	double TotalGPUMatrixUploadMs = 0.0;
	double TotalCPUBufferUploadMs = 0.0;
	double LastUpdateMs = 0.0;
	double LastCPUVertexMs = 0.0;
	double LastPoseUpdateMs = 0.0;
	double LastMatrixUpdateMs = 0.0;
	double LastGPUMatrixUploadMs = 0.0;
	double LastCPUBufferUploadMs = 0.0;

	double GetAvgUpdateMs() const
	{
		return UpdateCallCount > 0 ? TotalUpdateMs / static_cast<double>(UpdateCallCount) : 0.0;
	}

	double GetAvgCPUVertexMs() const
	{
		return CPUVertexCallCount > 0 ? TotalCPUVertexMs / static_cast<double>(CPUVertexCallCount) : 0.0;
	}

	double GetAvgPoseUpdateMs() const
	{
		return PoseUpdateCallCount > 0 ? TotalPoseUpdateMs / static_cast<double>(PoseUpdateCallCount) : 0.0;
	}

	double GetAvgMatrixUpdateMs() const
	{
		return MatrixUpdateCallCount > 0 ? TotalMatrixUpdateMs / static_cast<double>(MatrixUpdateCallCount) : 0.0;
	}

	double GetAvgGPUMatrixUploadMs() const
	{
		return GPUMatrixUploadCallCount > 0 ? TotalGPUMatrixUploadMs / static_cast<double>(GPUMatrixUploadCallCount) : 0.0;
	}

	double GetAvgCPUBufferUploadMs() const
	{
		return CPUBufferUploadCallCount > 0 ? TotalCPUBufferUploadMs / static_cast<double>(CPUBufferUploadCallCount) : 0.0;
	}
};

class FSkinningRuntimeStatCollector
{
public:
	static void ResetFrame()
	{
		FrameStats = {};
	}

	static void RecordUpdate(double ElapsedMs)
	{
		FrameStats.UpdateCallCount++;
		FrameStats.TotalUpdateMs += ElapsedMs;
		FrameStats.LastUpdateMs = ElapsedMs;
	}

	static void RecordCPUVertex(double ElapsedMs, uint32 VertexCount)
	{
		FrameStats.CPUVertexCallCount++;
		FrameStats.CPUVertexCount += VertexCount;
		FrameStats.TotalCPUVertexMs += ElapsedMs;
		FrameStats.LastCPUVertexMs = ElapsedMs;
	}

	static void RecordPoseUpdate(double ElapsedMs)
	{
		FrameStats.PoseUpdateCallCount++;
		FrameStats.TotalPoseUpdateMs += ElapsedMs;
		FrameStats.LastPoseUpdateMs = ElapsedMs;
	}

	static void RecordMatrixUpdate(double ElapsedMs)
	{
		FrameStats.MatrixUpdateCallCount++;
		FrameStats.TotalMatrixUpdateMs += ElapsedMs;
		FrameStats.LastMatrixUpdateMs = ElapsedMs;
	}

	static void RecordGPUMatrixUpload(double ElapsedMs, uint32 BoneCount)
	{
		FrameStats.GPUMatrixUploadCallCount++;
		FrameStats.GPUMatrixBoneCount += BoneCount;
		FrameStats.TotalGPUMatrixUploadMs += ElapsedMs;
		FrameStats.LastGPUMatrixUploadMs = ElapsedMs;
	}

	static void RecordCPUBufferUpload(double ElapsedMs)
	{
		FrameStats.CPUBufferUploadCallCount++;
		FrameStats.TotalCPUBufferUploadMs += ElapsedMs;
		FrameStats.LastCPUBufferUploadMs = ElapsedMs;
	}

	static void RecordSubmittedComponent(uint32 VertexCount, uint32 IndexCount, uint32 BoneCount, bool bGPUSkinning)
	{
		FrameStats.SubmittedComponentCount++;
		FrameStats.SubmittedVertexCount += VertexCount;
		FrameStats.SubmittedIndexCount += IndexCount;
		FrameStats.SubmittedBoneCount += BoneCount;
		if (bGPUSkinning)
		{
			FrameStats.SubmittedGPUComponentCount++;
		}
		else
		{
			FrameStats.SubmittedCPUComponentCount++;
		}
	}

	static void RecordSubmittedDrawCommands(uint32 SectionCount, uint32 DrawCommandCount)
	{
		FrameStats.SubmittedSectionCount += SectionCount;
		FrameStats.SubmittedDrawCommandCount += DrawCommandCount;
	}

	static const FSkinningRuntimeStats& GetFrameStats()
	{
		return FrameStats;
	}

private:
	inline static FSkinningRuntimeStats FrameStats = {};
};
