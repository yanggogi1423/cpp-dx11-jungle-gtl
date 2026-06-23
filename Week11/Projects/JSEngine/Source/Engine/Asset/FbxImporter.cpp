#include "FbxImporter.h"
#include <fbxsdk.h>

#include "Animation/AnimDataModel.h"
#include "Animation/AnimSequence.h"
#include "Asset/StaticMeshTypes.h"
#include "Core/ExternalPathStaging.h"
#include "Core/Logging/Log.h"
#include "Core/PlatformTime.h"
#include "Object/ObjectFactory.h"
#include "SkeletalMesh.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>

using namespace fbxsdk;

namespace
{
struct FFbxSceneDestroyer
{
    void operator()(FbxScene* Scene) const
    {
        if (Scene)
        {
			const auto MeshStart = std::chrono::steady_clock::now();

            Scene->Destroy();
			const auto MeshEnd = std::chrono::steady_clock::now();
			const double MeshMs = std::chrono::duration<double, std::milli>(MeshEnd - MeshStart).count();

			UE_LOG(
				"[Scene Destroyer] FBX inspect mesh=%.3f ms",
				MeshMs);
        }
    }
};
class FFbxIOSettingRAII
{
public:
    FFbxIOSettingRAII(FbxManager* InManager, FbxIOSettings* InSettings)
        :Manager(InManager), NewSetting(InSettings)
    {
        if (Manager&&NewSetting)
        {
            PrevSettings = Manager->GetIOSettings();
            Manager->SetIOSettings(NewSetting);
        }
    }
        
    ~FFbxIOSettingRAII()
    {
        if (Manager)
        {
            Manager->SetIOSettings(PrevSettings);
        }
        if (NewSetting)
        {
            NewSetting->Destroy();
        }
    }
    FFbxIOSettingRAII(const FFbxIOSettingRAII&) = delete;
    FFbxIOSettingRAII& operator=(const FFbxIOSettingRAII&) = delete;
    
    
private:
    FbxManager* Manager = nullptr;
    FbxIOSettings* PrevSettings = nullptr;
    FbxIOSettings* NewSetting = nullptr;
};

static FVector ToFVector(const FbxVector4& V)
{
	return FVector(static_cast<float>(V[0]), static_cast<float>(V[1]), static_cast<float>(V[2]));
}

static FVector2 ToFVector2(const FbxVector2& V)
{
	// OBJ 로더와 동일하게 V 좌표 뒤집기
	return FVector2(static_cast<float>(V[0]), 1.0f - static_cast<float>(V[1]));
}

static void GetTangentBitangent(FVector& OutT, FVector& OutB,
	const FVector& P0, const FVector& P1, const FVector& P2,
	const FVector2& UV0, const FVector2& UV1, const FVector2& UV2)
{
	FVector E1 = P1 - P0;
	FVector E2 = P2 - P0;
	FVector2 dUV1 = UV1 - UV0;
	FVector2 dUV2 = UV2 - UV0;
	float Det = dUV1.X * dUV2.Y - dUV1.Y * dUV2.X;
	float r = (fabs(Det) > 1e-8f) ? (1.0f / Det) : 0.0f;

	OutT = (E1 * dUV2.Y - E2 * dUV1.Y) * r;
	OutB = (E2 * dUV1.X - E1 * dUV2.X) * r;
}

static FMatrix ToFMatrix(const FbxAMatrix& M)
{
	// row-vector convention
    return FMatrix(
        static_cast<float>(M.Get(0, 0)), static_cast<float>(M.Get(0, 1)), static_cast<float>(M.Get(0, 2)), static_cast<float>(M.Get(0, 3)),
        static_cast<float>(M.Get(1, 0)), static_cast<float>(M.Get(1, 1)), static_cast<float>(M.Get(1, 2)), static_cast<float>(M.Get(1, 3)),
        static_cast<float>(M.Get(2, 0)), static_cast<float>(M.Get(2, 1)), static_cast<float>(M.Get(2, 2)), static_cast<float>(M.Get(2, 3)),
        static_cast<float>(M.Get(3, 0)), static_cast<float>(M.Get(3, 1)), static_cast<float>(M.Get(3, 2)), static_cast<float>(M.Get(3, 3)));
}

static bool IsMirroredTransform(const FbxAMatrix& Matrix)
{
    const FMatrix EngineMatrix = ToFMatrix(Matrix);
    const FVector XAxis = EngineMatrix.GetScaledAxis(EAxis::X);
    const FVector YAxis = EngineMatrix.GetScaledAxis(EAxis::Y);
    const FVector ZAxis = EngineMatrix.GetScaledAxis(EAxis::Z);
    const float Determinant = FVector::DotProduct(FVector::CrossProduct(XAxis, YAxis), ZAxis);
    return Determinant < 0.0f;
}

static int32 GetTriangleCornerByWinding(int32 CornerIndex, bool bReverseWinding)
{
    static constexpr int32 NormalOrder[3] = { 0, 1, 2 };
    static constexpr int32 ReversedOrder[3] = { 0, 2, 1 };
    return bReverseWinding ? ReversedOrder[CornerIndex] : NormalOrder[CornerIndex];
}

static bool IsNearlyEqual(float A, float B, float Tolerance = 1.e-4f)
{
    return std::fabs(A - B) <= Tolerance;
}

static bool IsNearlyIdentityScale(const FVector& Scale, float Tolerance = 1.e-4f)
{
    return IsNearlyEqual(Scale.X, 1.0f, Tolerance)
        && IsNearlyEqual(Scale.Y, 1.0f, Tolerance)
        && IsNearlyEqual(Scale.Z, 1.0f, Tolerance);
}

static bool IsNonIdentityScale(const FVector& Scale, float Tolerance = 1.e-4f)
{
    return !IsNearlyIdentityScale(Scale, Tolerance);
}

static FVector AbsScale(const FVector& Scale)
{
    return FVector(std::fabs(Scale.X), std::fabs(Scale.Y), std::fabs(Scale.Z));
}

enum class ESignedScaleAxis : int32
{
    None = -1,
    X = 0,
    Y = 1,
    Z = 2
};

static ESignedScaleAxis ResolveNegativeScaleAxis(ESignedScaleAxis PreferredAxis)
{
    return PreferredAxis == ESignedScaleAxis::None ? ESignedScaleAxis::Y : PreferredAxis;
}

static ESignedScaleAxis GetNegativeScaleAxis(const FVector& Scale)
{
    if (Scale.X < 0.0f)
    {
        return ESignedScaleAxis::X;
    }
    if (Scale.Y < 0.0f)
    {
        return ESignedScaleAxis::Y;
    }
    if (Scale.Z < 0.0f)
    {
        return ESignedScaleAxis::Z;
    }
    return ESignedScaleAxis::None;
}

static void ApplyNegativeScaleAxis(
    ESignedScaleAxis PreferredAxis,
    FVector& XAxis,
    FVector& YAxis,
    FVector& ZAxis,
    FVector& OutScale)
{
    switch (ResolveNegativeScaleAxis(PreferredAxis))
    {
    case ESignedScaleAxis::X:
        XAxis = -XAxis;
        OutScale.X = -OutScale.X;
        break;
    case ESignedScaleAxis::Y:
        YAxis = -YAxis;
        OutScale.Y = -OutScale.Y;
        break;
    case ESignedScaleAxis::Z:
        ZAxis = -ZAxis;
        OutScale.Z = -OutScale.Z;
        break;
    default:
        break;
    }
}

static bool DecomposeWithSignedScale(
    const FMatrix& Matrix,
    FVector& OutTranslation,
    FMatrix& OutRotation,
    FVector& OutScale,
    float Tolerance = 1.e-8f)
{
    OutTranslation = Matrix.GetOrigin();

    FVector XAxis = Matrix.GetScaledAxis(EAxis::X);
    FVector YAxis = Matrix.GetScaledAxis(EAxis::Y);
    FVector ZAxis = Matrix.GetScaledAxis(EAxis::Z);

    OutScale = FVector(XAxis.Size(), YAxis.Size(), ZAxis.Size());
    if (OutScale.X <= Tolerance || OutScale.Y <= Tolerance || OutScale.Z <= Tolerance)
    {
        OutRotation = FMatrix::Identity;
        return false;
    }

    XAxis = XAxis / OutScale.X;
    YAxis = YAxis / OutScale.Y;
    ZAxis = ZAxis / OutScale.Z;

    const float Determinant =
        FVector::DotProduct(FVector::CrossProduct(XAxis, YAxis), ZAxis);
    if (Determinant < 0.0f)
    {
        // Keep reflected bind bases representable as quat + signed scale.
        // Converted roots commonly arrive as X, -Y, Z with uniform import scale.
        ApplyNegativeScaleAxis(ESignedScaleAxis::Y, XAxis, YAxis, ZAxis, OutScale);
    }

    OutRotation = FMatrix::Identity;
    OutRotation.SetAxes(XAxis, YAxis, ZAxis, FVector::ZeroVector);
    return true;
}

static float GetBasisDeterminant(const FVector& XAxis, const FVector& YAxis, const FVector& ZAxis)
{
    return FVector::DotProduct(FVector::CrossProduct(XAxis, YAxis), ZAxis);
}

static bool DecomposeWithSignedScaleForAnim(
    const FMatrix& Matrix,
    FVector& OutTranslation,
    FQuat& OutRotation,
    FVector& OutScale,
    ESignedScaleAxis PreferredNegativeAxis = ESignedScaleAxis::Y,
    bool* OutFixedReflection = nullptr,
    float Tolerance = 1.e-8f)
{
    if (OutFixedReflection)
    {
        *OutFixedReflection = false;
    }

    // FBX 애니메이션 local matrix에는 axis conversion / signed scale 때문에 reflection이 섞일 수 있다.
    // 일반 FMatrix::Decompose()는 축 길이를 양수 scale로 뽑기 때문에, 반사 성분이 rotation basis에 남을 수 있다.
    // determinant가 음수인 rotation matrix를 FQuat으로 변환하면 블렌딩 중 bone이 뒤집히므로,
    // bone의 bind pose 기준 음수 scale 축을 보존하고 해당 basis 축을 반전시켜 proper rotation(det +1)으로 만든다.
    OutTranslation = Matrix.GetTranslation();

    FVector XAxis = Matrix.GetScaledAxis(EAxis::X);
    FVector YAxis = Matrix.GetScaledAxis(EAxis::Y);
    FVector ZAxis = Matrix.GetScaledAxis(EAxis::Z);

    const float ScaleX = XAxis.Size();
    const float ScaleY = YAxis.Size();
    const float ScaleZ = ZAxis.Size();
    if (ScaleX <= Tolerance || ScaleY <= Tolerance || ScaleZ <= Tolerance)
    {
        OutRotation = FQuat::Identity;
        OutScale = FVector::OneVector;
        return false;
    }

    XAxis = XAxis / ScaleX;
    YAxis = YAxis / ScaleY;
    ZAxis = ZAxis / ScaleZ;
    OutScale = FVector(ScaleX, ScaleY, ScaleZ);

    const float Determinant = GetBasisDeterminant(XAxis, YAxis, ZAxis);
    if (Determinant < 0.0f)
    {
        if (OutFixedReflection)
        {
            *OutFixedReflection = true;
        }

        ApplyNegativeScaleAxis(PreferredNegativeAxis, XAxis, YAxis, ZAxis, OutScale);
    }

    const float RotationDeterminant = GetBasisDeterminant(XAxis, YAxis, ZAxis);
    if (RotationDeterminant <= 0.0f || std::fabs(RotationDeterminant - 1.0f) > 1.e-3f)
    {
        OutRotation = FQuat::Identity;
        return false;
    }

    FMatrix RotationMatrix = FMatrix::Identity;
    RotationMatrix.SetAxes(XAxis, YAxis, ZAxis, FVector::ZeroVector);
    OutRotation = FQuat(RotationMatrix).GetNormalized();
    return true;
}

struct FTempInfluence
{
    int32 BoneIndex = -1;
    float Weight = 0.0f;
};

namespace FbxVertexDedupInternal
{
static uint32 FloatToStableBits(float Value)
{
    if (Value == 0.0f)
    {
        Value = 0.0f;
    }

    uint32 Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(float));
    return Bits;
}

static size_t HashCombineUInt32(size_t Seed, uint32 Value)
{
    return Seed ^ (static_cast<size_t>(Value) + 0x9e3779b9u + (Seed << 6) + (Seed >> 2));
}

template <size_t Count>
static size_t HashBits(const std::array<uint32, Count>& Bits)
{
    size_t Hash = 0;
    for (uint32 Bit : Bits)
    {
        Hash = HashCombineUInt32(Hash, Bit);
    }
    return Hash;
}
}

struct FFbxStaticVertexDedupKey
{
    std::array<uint32, 12> Bits = {};

    bool operator==(const FFbxStaticVertexDedupKey& Other) const
    {
        return Bits == Other.Bits;
    }
};

struct FFbxStaticVertexDedupKeyHasher
{
    size_t operator()(const FFbxStaticVertexDedupKey& Key) const
    {
        return FbxVertexDedupInternal::HashBits(Key.Bits);
    }
};

struct FFbxSkeletalVertexDedupKey
{
    std::array<uint32, 20> Bits = {};

    bool operator==(const FFbxSkeletalVertexDedupKey& Other) const
    {
        return Bits == Other.Bits;
    }
};

struct FFbxSkeletalVertexDedupKeyHasher
{
    size_t operator()(const FFbxSkeletalVertexDedupKey& Key) const
    {
        return FbxVertexDedupInternal::HashBits(Key.Bits);
    }
};

struct FFbxTrianglePositionKey
{
    std::array<int32, 9> QuantizedPositions = {};

    bool operator==(const FFbxTrianglePositionKey& Other) const
    {
        return QuantizedPositions == Other.QuantizedPositions;
    }
};

struct FFbxTrianglePositionKeyHasher
{
    size_t operator()(const FFbxTrianglePositionKey& Key) const
    {
        size_t Hash = 0;
        for (int32 Value : Key.QuantizedPositions)
        {
            Hash = FbxVertexDedupInternal::HashCombineUInt32(Hash, static_cast<uint32>(Value));
        }
        return Hash;
    }
};

static int32 QuantizeTrianglePosition(float Value)
{
    constexpr float PositionScale = 10000.0f;
    return static_cast<int32>(std::round(Value * PositionScale));
}

static std::array<int32, 3> MakeQuantizedPositionKey(const FVector& Position)
{
    return {
        QuantizeTrianglePosition(Position.X),
        QuantizeTrianglePosition(Position.Y),
        QuantizeTrianglePosition(Position.Z),
    };
}

static FFbxTrianglePositionKey MakeTrianglePositionKey(
    const TArray<FSkeletalMeshVertex>& Vertices,
    uint32 Index0,
    uint32 Index1,
    uint32 Index2)
{
    std::array<std::array<int32, 3>, 3> Positions = {
        MakeQuantizedPositionKey(Vertices[Index0].Position),
        MakeQuantizedPositionKey(Vertices[Index1].Position),
        MakeQuantizedPositionKey(Vertices[Index2].Position),
    };

    std::sort(Positions.begin(), Positions.end());

    FFbxTrianglePositionKey Key;
    for (int32 PositionIndex = 0; PositionIndex < 3; ++PositionIndex)
    {
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            Key.QuantizedPositions[PositionIndex * 3 + Axis] = Positions[PositionIndex][Axis];
        }
    }
    return Key;
}

static FFbxStaticVertexDedupKey MakeStaticVertexDedupKey(const FNormalVertex& Vertex)
{
    using namespace FbxVertexDedupInternal;

    FFbxStaticVertexDedupKey Key;
    Key.Bits = {
        FloatToStableBits(Vertex.Position.X),
        FloatToStableBits(Vertex.Position.Y),
        FloatToStableBits(Vertex.Position.Z),
        FloatToStableBits(Vertex.Color.R),
        FloatToStableBits(Vertex.Color.G),
        FloatToStableBits(Vertex.Color.B),
        FloatToStableBits(Vertex.Color.A),
        FloatToStableBits(Vertex.Normal.X),
        FloatToStableBits(Vertex.Normal.Y),
        FloatToStableBits(Vertex.Normal.Z),
        FloatToStableBits(Vertex.UVs.X),
        FloatToStableBits(Vertex.UVs.Y),
    };
    return Key;
}

static FFbxSkeletalVertexDedupKey MakeSkeletalVertexDedupKey(const FSkeletalMeshVertex& Vertex)
{
    using namespace FbxVertexDedupInternal;

    FFbxSkeletalVertexDedupKey Key;
    Key.Bits = {
        FloatToStableBits(Vertex.Position.X),
        FloatToStableBits(Vertex.Position.Y),
        FloatToStableBits(Vertex.Position.Z),
        FloatToStableBits(Vertex.Color.R),
        FloatToStableBits(Vertex.Color.G),
        FloatToStableBits(Vertex.Color.B),
        FloatToStableBits(Vertex.Color.A),
        FloatToStableBits(Vertex.Normal.X),
        FloatToStableBits(Vertex.Normal.Y),
        FloatToStableBits(Vertex.Normal.Z),
        FloatToStableBits(Vertex.UVs.X),
        FloatToStableBits(Vertex.UVs.Y),
        static_cast<uint32>(Vertex.BoneIndices[0]),
        static_cast<uint32>(Vertex.BoneIndices[1]),
        static_cast<uint32>(Vertex.BoneIndices[2]),
        static_cast<uint32>(Vertex.BoneIndices[3]),
        FloatToStableBits(Vertex.BoneWeights[0]),
        FloatToStableBits(Vertex.BoneWeights[1]),
        FloatToStableBits(Vertex.BoneWeights[2]),
        FloatToStableBits(Vertex.BoneWeights[3]),
    };
    return Key;
}

static void DeduplicateStaticMeshVertices(FStaticMesh* Mesh)
{
    if (!Mesh || Mesh->Vertices.empty() || Mesh->Indices.empty())
    {
        return;
    }

    const size_t OriginalVertexCount = Mesh->Vertices.size();
    TArray<FNormalVertex> DedupedVertices;
    DedupedVertices.reserve(Mesh->Vertices.size());
    TArray<uint32> RemappedIndices;
    RemappedIndices.reserve(Mesh->Indices.size());

    TMap<FFbxStaticVertexDedupKey, uint32, FFbxStaticVertexDedupKeyHasher> VertexToIndex;
    VertexToIndex.reserve(Mesh->Indices.size());

    for (uint32 OldIndex : Mesh->Indices)
    {
        if (OldIndex >= Mesh->Vertices.size())
        {
            UE_LOG_WARNING("[FbxImporter] Abort static vertex dedup due to invalid index: %u", OldIndex);
            return;
        }

        const FNormalVertex& Vertex = Mesh->Vertices[OldIndex];
        const FFbxStaticVertexDedupKey Key = MakeStaticVertexDedupKey(Vertex);
        auto Found = VertexToIndex.find(Key);
        if (Found != VertexToIndex.end())
        {
            RemappedIndices.push_back(Found->second);
            continue;
        }

        const uint32 NewIndex = static_cast<uint32>(DedupedVertices.size());
        DedupedVertices.push_back(Vertex);
        VertexToIndex.emplace(Key, NewIndex);
        RemappedIndices.push_back(NewIndex);
    }

    Mesh->Vertices = std::move(DedupedVertices);
    Mesh->Indices = std::move(RemappedIndices);

    if (Mesh->Vertices.size() < OriginalVertexCount)
    {
        UE_LOG("[FbxImporter] Static vertex dedup: %zu -> %zu",
               OriginalVertexCount,
               Mesh->Vertices.size());
    }
}

static void DeduplicateSkeletalMeshVertices(FSkeletalMesh* Mesh)
{
    if (!Mesh || Mesh->Vertices.empty() || Mesh->Indices.empty())
    {
        return;
    }

    const size_t OriginalVertexCount = Mesh->Vertices.size();
    TArray<FSkeletalMeshVertex> DedupedVertices;
    DedupedVertices.reserve(Mesh->Vertices.size());
    TArray<uint32> RemappedIndices;
    RemappedIndices.reserve(Mesh->Indices.size());

    TMap<FFbxSkeletalVertexDedupKey, uint32, FFbxSkeletalVertexDedupKeyHasher> VertexToIndex;
    VertexToIndex.reserve(Mesh->Indices.size());

    for (uint32 OldIndex : Mesh->Indices)
    {
        if (OldIndex >= Mesh->Vertices.size())
        {
            UE_LOG_WARNING("[FbxImporter] Abort skeletal vertex dedup due to invalid index: %u", OldIndex);
            return;
        }

        const FSkeletalMeshVertex& Vertex = Mesh->Vertices[OldIndex];
        const FFbxSkeletalVertexDedupKey Key = MakeSkeletalVertexDedupKey(Vertex);
        auto Found = VertexToIndex.find(Key);
        if (Found != VertexToIndex.end())
        {
            RemappedIndices.push_back(Found->second);
            continue;
        }

        const uint32 NewIndex = static_cast<uint32>(DedupedVertices.size());
        DedupedVertices.push_back(Vertex);
        VertexToIndex.emplace(Key, NewIndex);
        RemappedIndices.push_back(NewIndex);
    }

    Mesh->Vertices = std::move(DedupedVertices);
    Mesh->Indices = std::move(RemappedIndices);

    if (Mesh->Vertices.size() < OriginalVertexCount)
    {
        UE_LOG("[FbxImporter] Skeletal vertex dedup: %zu -> %zu",
               OriginalVertexCount,
               Mesh->Vertices.size());
    }
}

static void RemoveDuplicateSkeletalMeshTriangles(FSkeletalMesh* Mesh)
{
    if (!Mesh || Mesh->Vertices.empty() || Mesh->Indices.empty() || Mesh->Sections.empty())
    {
        return;
    }

    TMap<FFbxTrianglePositionKey, bool, FFbxTrianglePositionKeyHasher> SeenTriangles;
    SeenTriangles.reserve(Mesh->Indices.size() / 3);

    TArray<uint32> NewIndices;
    NewIndices.reserve(Mesh->Indices.size());

    size_t RemovedTriangleCount = 0;

    for (FStaticMeshSection& Section : Mesh->Sections)
    {
        if (Section.IndexCount == 0)
        {
            Section.StartIndex = static_cast<uint32>(NewIndices.size());
            continue;
        }

        const uint32 OldStartIndex = Section.StartIndex;
        const uint32 OldIndexCount = Section.IndexCount;
        if (OldStartIndex + OldIndexCount > Mesh->Indices.size() ||
            OldStartIndex % 3 != 0 ||
            OldIndexCount % 3 != 0)
        {
            UE_LOG_WARNING("[FbxImporter] Abort skeletal triangle dedup due to invalid section range");
            return;
        }

        Section.StartIndex = static_cast<uint32>(NewIndices.size());

        for (uint32 IndexOffset = 0; IndexOffset < OldIndexCount; IndexOffset += 3)
        {
            const uint32 SourceIndex = OldStartIndex + IndexOffset;
            const uint32 Index0 = Mesh->Indices[SourceIndex + 0];
            const uint32 Index1 = Mesh->Indices[SourceIndex + 1];
            const uint32 Index2 = Mesh->Indices[SourceIndex + 2];

            if (Index0 >= Mesh->Vertices.size() ||
                Index1 >= Mesh->Vertices.size() ||
                Index2 >= Mesh->Vertices.size())
            {
                UE_LOG_WARNING("[FbxImporter] Abort skeletal triangle dedup due to invalid triangle index");
                return;
            }

            const FFbxTrianglePositionKey Key =
                MakeTrianglePositionKey(Mesh->Vertices, Index0, Index1, Index2);
            if (SeenTriangles.find(Key) != SeenTriangles.end())
            {
                ++RemovedTriangleCount;
                continue;
            }

            SeenTriangles.emplace(Key, true);
            NewIndices.push_back(Index0);
            NewIndices.push_back(Index1);
            NewIndices.push_back(Index2);
        }

        Section.IndexCount = static_cast<uint32>(NewIndices.size()) - Section.StartIndex;
    }

    if (RemovedTriangleCount == 0)
    {
        return;
    }

    Mesh->Indices = std::move(NewIndices);
    UE_LOG("[FbxImporter] Skeletal triangle dedup removed %zu duplicate triangles", RemovedTriangleCount);
}

/**
 * @brief 영향력 상위 4개의 bone을 FSkeletalMeshVertex에 할당
 */
static void AssignTop4Influences(
    const TArray<FTempInfluence>& SourceInfluences,
    FSkeletalMeshVertex& OutVertex)
{
    for (int32 i = 0; i < 4; ++i)
    {
        OutVertex.BoneIndices[i] = 0;
        OutVertex.BoneWeights[i] = 0.0f;
    }

    if (SourceInfluences.empty())
    {
        return;
    }

	// 정렬은 그냥 standard sort 활용
    TArray<FTempInfluence> Sorted = SourceInfluences;
    std::sort(Sorted.begin(), Sorted.end(), [](const FTempInfluence& A, const FTempInfluence& B)
                { return A.Weight > B.Weight; });

    int32 WrittenCount = 0;
    float Sum = 0.0f;

    for (const FTempInfluence& Influence : Sorted)
    {
        if (WrittenCount >= 4)
        {
            break;
        }

        if (Influence.BoneIndex < 0 || Influence.BoneIndex > 65535 || Influence.Weight <= 0.0f)
        {
            continue;
        }

        OutVertex.BoneIndices[WrittenCount] = static_cast<uint16>(Influence.BoneIndex);
        OutVertex.BoneWeights[WrittenCount] = Influence.Weight;
        Sum += Influence.Weight;

        ++WrittenCount;
    }

    if (Sum <= 1e-6f)
    {
        for (int32 i = 0; i < 4; ++i)
        {
            OutVertex.BoneIndices[i] = 0;
            OutVertex.BoneWeights[i] = 0.0f;
        }

        return;
    }

    for (int32 i = 0; i < WrittenCount; ++i)
    {
        OutVertex.BoneWeights[i] /= Sum;
    }
}

/**
 * @brief FBX에는 node transform 뿐 아니라 mesh geometry 자체에 추가로 붙는
 *        숨은 보정 transform이 존재하기 때문에 이를 계산하는 함수
 */
static FbxAMatrix GetGeometryTransform(FbxNode* Node)
{
    FbxAMatrix Geometry;
    Geometry.SetIdentity();

    if (!Node)
    {
        return Geometry;
    }

    const FbxVector4 T = Node->GetGeometricTranslation(FbxNode::eSourcePivot);
    const FbxVector4 R = Node->GetGeometricRotation(FbxNode::eSourcePivot);
    const FbxVector4 S = Node->GetGeometricScaling(FbxNode::eSourcePivot);

    Geometry.SetTRS(T, R, S);
    return Geometry;
}

/**
 * @brief normal은 translate를 적용하지 않음
 */
static FbxAMatrix GetNormalTransform(FbxAMatrix Matrix)
{
    Matrix.SetT(FbxVector4(0, 0, 0, 0));
    return Matrix;
}

static FbxAMatrix MakeIdentityFbxMatrix()
{
    FbxAMatrix Matrix;
    Matrix.SetIdentity();
    return Matrix;
}

static FbxAMatrix GetGlobalTransformWithGeometry(FbxNode* Node)
{
    if (!Node)
    {
        return MakeIdentityFbxMatrix();
    }

    const FbxAMatrix GlobalTransform = Node->EvaluateGlobalTransform();
    const FbxAMatrix GeometryTransform = GetGeometryTransform(Node);

    // 기존 Static Mesh importer와 동일 정책
    return GlobalTransform * GeometryTransform;
}

static FbxAMatrix GetNormalTransformFromPositionTransform(FbxAMatrix Matrix)
{
    Matrix.SetT(FbxVector4(0, 0, 0, 0));
    return Matrix;
}

static int32 FindNearestImportedBoneIndex(
    FbxNode* StartNode,
    const TMap<FbxNode*, int32>& BoneNodeToIndex)
{
    FbxNode* Current = StartNode;
    while (Current)
    {
        auto It = BoneNodeToIndex.find(Current);
        if (It != BoneNodeToIndex.end())
        {
            return It->second;
        }

        Current = Current->GetParent();
    }

    return -1;
}

static std::string ToLowerCopy(const char* InName)
{
    std::string Result = InName ? InName : "";
    std::transform(Result.begin(), Result.end(), Result.begin(), [](unsigned char C)
                    { return static_cast<char>(std::tolower(C)); });
    return Result;
}

static bool ContainsAnyToken(const std::string& Name, const std::initializer_list<const char*> Tokens)
{
    for (const char* Token : Tokens)
    {
        if (Name.find(Token) != std::string::npos)
        {
            return true;
        }
    }

    return false;
}

static bool TryParseLODIndexFromNodeName(FbxNode* Node, int32& OutLODIndex)
{
    OutLODIndex = -1;
    const std::string Name = ToLowerCopy(Node ? Node->GetName() : "");

    size_t SearchOffset = 0;
    while (SearchOffset < Name.size())
    {
        const size_t LODPos = Name.find("lod", SearchOffset);
        if (LODPos == std::string::npos)
        {
            return false;
        }

        size_t DigitPos = LODPos + 3;
        while (DigitPos < Name.size() &&
               (Name[DigitPos] == '_' || Name[DigitPos] == '-' || Name[DigitPos] == ' '))
        {
            ++DigitPos;
        }

        if (DigitPos < Name.size() && std::isdigit(static_cast<unsigned char>(Name[DigitPos])))
        {
            int32 ParsedIndex = 0;
            while (DigitPos < Name.size() && std::isdigit(static_cast<unsigned char>(Name[DigitPos])))
            {
                ParsedIndex = ParsedIndex * 10 + static_cast<int32>(Name[DigitPos] - '0');
                ++DigitPos;
            }

            OutLODIndex = ParsedIndex;
            return true;
        }

        SearchOffset = LODPos + 3;
    }

    return false;
}

static bool IsLODGroupNode(FbxNode* Node)
{
    if (!Node)
    {
        return false;
    }

    FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
    if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eLODGroup)
    {
        return true;
    }

    const std::string Name = ToLowerCopy(Node->GetName());
    return Name.find("lodgroup") != std::string::npos ||
           Name.find("lod_group") != std::string::npos;
}

static bool HasMeshInSubtree(FbxNode* Node)
{
    if (!Node)
    {
        return false;
    }

    if (Node->GetMesh())
    {
        return true;
    }

    for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
    {
        if (HasMeshInSubtree(Node->GetChild(ChildIndex)))
        {
            return true;
        }
    }

    return false;
}

static FbxNode* FindLODChildToImport(FbxNode* LODGroupNode, int32& OutLODIndex)
{
    OutLODIndex = 0;
    if (!LODGroupNode)
    {
        return nullptr;
    }

    for (int32 ChildIndex = 0; ChildIndex < LODGroupNode->GetChildCount(); ++ChildIndex)
    {
        FbxNode* Child = LODGroupNode->GetChild(ChildIndex);
        int32 ParsedLODIndex = -1;
        if (TryParseLODIndexFromNodeName(Child, ParsedLODIndex) && ParsedLODIndex == 0)
        {
            OutLODIndex = ParsedLODIndex;
            return Child;
        }
    }

    for (int32 ChildIndex = 0; ChildIndex < LODGroupNode->GetChildCount(); ++ChildIndex)
    {
        FbxNode* Child = LODGroupNode->GetChild(ChildIndex);
        if (HasMeshInSubtree(Child))
        {
            int32 ParsedLODIndex = -1;
            if (TryParseLODIndexFromNodeName(Child, ParsedLODIndex))
            {
                OutLODIndex = ParsedLODIndex;
            }

            return Child;
        }
    }

    return nullptr;
}

static bool ShouldSkipRigidMeshByName(FbxNode* OwnerNode)
{
    const std::string Name = ToLowerCopy(OwnerNode ? OwnerNode->GetName() : "");

    // helper / reference 성격이 강한 이름은 skip
    return ContainsAnyToken(Name, { "floor",
                                    "ground",
                                    "grid",
                                    "reference",
                                    "helper",
                                    "collision",
                                    "collider",
                                    "dummy" });
}

static bool HasValidSkinInfluence(FbxMesh* Mesh)
{
    if (!Mesh)
    {
        return false;
    }

    const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
    for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
    {
        FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
        if (!Skin)
        {
            continue;
        }

        const int32 ClusterCount = Skin->GetClusterCount();
        for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
        {
            FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
            if (!Cluster || !Cluster->GetLink())
            {
                continue;
            }

            const int32 IndexCount = Cluster->GetControlPointIndicesCount();
            double* Weights = Cluster->GetControlPointWeights();
            if (IndexCount <= 0 || !Weights)
            {
                continue;
            }

            for (int32 Index = 0; Index < IndexCount; ++Index)
            {
                if (Weights[Index] > 0.0)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

static void InspectMeshContentRecursive(FbxNode* Node, FFbxMeshContentInfo& OutInfo)
{
    if (!Node)
    {
        return;
    }

    if (OutInfo.bHasStaticMesh && OutInfo.bHasSkeletalMesh)
    {
        return;
    }

    if (FbxMesh* Mesh = Node->GetMesh())
    {
        const bool bHasGeometry =
            Mesh->GetControlPointsCount() > 0 &&
            Mesh->GetPolygonCount() > 0;

        if (bHasGeometry)
        {
            if (HasValidSkinInfluence(Mesh))
            {
                OutInfo.bHasSkeletalMesh = true;
            }
            else
            {
                OutInfo.bHasStaticMesh = true;
            }
        }
    }

    if (OutInfo.bHasStaticMesh && OutInfo.bHasSkeletalMesh)
    {
        return;
    }

    for (int32 ChildIndex = 0; ChildIndex < Node->GetChildCount(); ++ChildIndex)
    {
        InspectMeshContentRecursive(Node->GetChild(ChildIndex), OutInfo);

        if (OutInfo.bHasStaticMesh && OutInfo.bHasSkeletalMesh)
        {
			// 둘 다 true임을 찾았으면 early exit
            return;
        }
    }
}

static void ResetVertexInfluences(FSkeletalMeshVertex& Vertex)
{
    for (int32 i = 0; i < 4; ++i)
    {
        Vertex.BoneIndices[i] = 0;
        Vertex.BoneWeights[i] = 0.0f;
    }
}

static void AssignRigidInfluence(FSkeletalMeshVertex& Vertex, int32 BoneIndex)
{
    ResetVertexInfluences(Vertex);

    if (BoneIndex < 0 || BoneIndex > 65535)
    {
        return;
    }

    Vertex.BoneIndices[0] = static_cast<uint16>(BoneIndex);
    Vertex.BoneWeights[0] = 1.0f;
}

}

FFbxImporter::~FFbxImporter()
{
    ClearCachedAnimationScene();

    if (CachedManager)
    {
        CachedManager->Destroy();
        CachedManager = nullptr;
        CachedIOSettings = nullptr;
    }
}

FbxManager* FFbxImporter::GetOrCreateManager()
{
    if (CachedManager)
    {
        return CachedManager;
    }

    CachedManager = FbxManager::Create();
    if (!CachedManager)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to create FbxManager");
        return nullptr;
    }

    CachedIOSettings = FbxIOSettings::Create(CachedManager, IOSROOT);
    CachedManager->SetIOSettings(CachedIOSettings);
    return CachedManager;
}

void FFbxImporter::ClearCachedAnimationScene()
{
    if (CachedAnimationScene)
    {
        CachedAnimationScene->Destroy();
        CachedAnimationScene = nullptr;
        CachedAnimationScenePath.clear();
    }
}

FbxScene* FFbxImporter::GetOrCreateAnimationScene(const FString& Path)
{
    if (CachedAnimationScene && CachedAnimationScenePath == Path)
    {
        return CachedAnimationScene;
    }

    ClearCachedAnimationScene();

    FbxManager* Manager = GetOrCreateManager();
    if (!Manager)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to create FbxManager for animation import");
        return nullptr;
    }

    CachedAnimationScene = FbxScene::Create(Manager, "ImportAnimScene");
    if (!CachedAnimationScene)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to create FbxScene for animation import");
        return nullptr;
    }

    if (!ImportScene(Path, Manager, CachedAnimationScene))
    {
        ClearCachedAnimationScene();
        return nullptr;
    }

    CachedAnimationScenePath = Path;
    return CachedAnimationScene;
}

FStaticMesh* FFbxImporter::Load(const FString& Path, const FStaticMeshLoadOptions& LoadOptions)
{
	const double StartTime = FPlatformTime::Seconds();
	UE_LOG("[FbxImporter] Start loading FBX: %s", Path.c_str());

	FbxManager* Manager = GetOrCreateManager();
	if (!Manager)
	{
		UE_LOG_ERROR("[FbxImporter] Failed to create FbxManager");
		return nullptr;
	}

	std::unique_ptr<FbxScene, FFbxSceneDestroyer> Scene(FbxScene::Create(Manager, "ImportScene"));
	if (!Scene)
	{
		UE_LOG_ERROR("[FbxImporter] Failed to create FbxScene");
		return nullptr;
	}

	if (!ImportScene(Path, Manager, Scene.get()))
	{
		return nullptr;
	}

	// Triangulate 후 메시 처리
	FbxGeometryConverter Converter(Manager);
	Converter.Triangulate(Scene.get(), /*pReplace=*/true);

	FStaticMesh* StaticMesh = new FStaticMesh();
	StaticMesh->PathFileName = Path;

	if (FbxNode* RootNode = Scene->GetRootNode())
	{
		for (int32 i = 0; i < RootNode->GetChildCount(); ++i)
		{
			CollectMeshes(RootNode->GetChild(i), StaticMesh);
		}
	}

	if (StaticMesh->Vertices.empty() || StaticMesh->Indices.empty())
	{
		UE_LOG_ERROR("[FbxImporter] No geometry found in FBX: %s", Path.c_str());
		delete StaticMesh;
		return nullptr;
	}

    DeduplicateStaticMeshVertices(StaticMesh);

	if (LoadOptions.bNormalizeToUnitCube)
	{
		UE_LOG("[FbxImporter] NormalizeToUnitCube enabled: %s", Path.c_str());
		NormalizePositionsToUnitCube(StaticMesh);
	}

	StaticMesh->LocalBounds = BuildLocalBounds(StaticMesh);

	ComputeTangents(StaticMesh);

	UE_LOG("[FbxImporter] FBX Loaded: %s (Vertices: %zu, Indices: %zu, Sections: %zu, Slots: %zu)",
		Path.c_str(),
		StaticMesh->Vertices.size(),
		StaticMesh->Indices.size(),
		StaticMesh->Sections.size(),
		StaticMesh->Slots.size());

	const double EndTime = FPlatformTime::Seconds();
	UE_LOG("[FbxImporter] Loaded %s in %.3f sec", Path.c_str(), EndTime - StartTime);

	return StaticMesh;
}

bool FFbxImporter::SupportsExtension(const FString& Extension) const
{
	return Extension == FString("fbx") || Extension == FString(".fbx") ||
		   Extension == FString("FBX") || Extension == FString(".FBX");
}

FString FFbxImporter::GetLoaderName() const
{
	return FString{ "FFbxImporter" };
}

FSkeletalMesh* FFbxImporter::LoadSkeletalMesh(const FString& Path, const FStaticMeshLoadOptions& LoadOptions)
{
    (void)LoadOptions;

    const double StartTime = FPlatformTime::Seconds();
    UE_LOG("[FbxImporter] Start loading Skeletal FBX: %s", Path.c_str());

    FbxManager* Manager = GetOrCreateManager();
    if (!Manager)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to create FbxManager");
        return nullptr;
    }
    
    FbxIOSettings* NewIOSettings = FbxIOSettings::Create(Manager, "NewIOSettings");
    NewIOSettings->SetBoolProp(IMP_FBX_ANIMATION,false);
    FFbxIOSettingRAII IORAII(Manager,NewIOSettings);

    std::unique_ptr<FbxScene, FFbxSceneDestroyer> Scene(FbxScene::Create(Manager, "ImportSkeletalScene"));
    if (!Scene)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to create FbxScene");
        return nullptr;
    }

    if (!ImportScene(Path, Manager, Scene.get()))
    {
        return nullptr;
    }

    FbxGeometryConverter Converter(Manager);
    Converter.Triangulate(Scene.get(), true);

    FSkeletalMesh* SkeletalMesh = new FSkeletalMesh();
    SkeletalMesh->PathFileName = Path;

    TMap<FbxNode*, int32> BoneNodeToIndex;

    bool bHasImportedSkinnedMesh = false;

    if (FbxNode* RootNode = Scene->GetRootNode())
    {
        // 1-pass: skin deformer가 있는 mesh만 먼저 처리
        for (int32 i = 0; i < RootNode->GetChildCount(); ++i)
        {
            CollectSkeletalMeshes(
                RootNode->GetChild(i),
                SkeletalMesh,
                ESkeletalMeshImportPass::SkinnedMeshes,
                BoneNodeToIndex,
                bHasImportedSkinnedMesh);
        }

        // 2-pass: skin deformer가 없는 mesh 중 bone 아래에 붙은 mesh를 rigid mesh로 처리
        for (int32 i = 0; i < RootNode->GetChildCount(); ++i)
        {
            CollectSkeletalMeshes(
                RootNode->GetChild(i),
                SkeletalMesh,
                ESkeletalMeshImportPass::RigidAttachedMeshes,
                BoneNodeToIndex,
                bHasImportedSkinnedMesh);
        }
    }

    if (SkeletalMesh->Vertices.empty() || SkeletalMesh->Indices.empty() || SkeletalMesh->Bones.empty())
    {
        UE_LOG_ERROR("[FbxImporter] No skeletal geometry or bones found: %s", Path.c_str());
        delete SkeletalMesh;
        return nullptr;
    }

    DeduplicateSkeletalMeshVertices(SkeletalMesh);
    RemoveDuplicateSkeletalMeshTriangles(SkeletalMesh);
    DeduplicateSkeletalMeshVertices(SkeletalMesh);

    SkeletalMesh->LocalBounds = BuildLocalBounds(SkeletalMesh);
    ComputeTangents(SkeletalMesh);

    const double EndTime = FPlatformTime::Seconds();
    UE_LOG("[FbxImporter] Skeletal FBX Loaded: %s (Vertices=%zu, Indices=%zu, Bones=%zu, Sections=%zu, Slots=%zu, %.3f sec)",
           Path.c_str(),
           SkeletalMesh->Vertices.size(),
           SkeletalMesh->Indices.size(),
           SkeletalMesh->Bones.size(),
           SkeletalMesh->Sections.size(),
           SkeletalMesh->MaterialSlots.size(),
           EndTime - StartTime);

    return SkeletalMesh;
}

FFbxMeshContentInfo FFbxImporter::InspectMeshContent(const FString& Path)
{
    FFbxMeshContentInfo Result;

    FbxManager* Manager = GetOrCreateManager();
    if (!Manager)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to create FbxManager for inspection");
        return Result;
    }

    std::unique_ptr<FbxScene, FFbxSceneDestroyer> Scene(FbxScene::Create(Manager, "InspectFbxScene"));
    if (!Scene)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to create FbxScene for inspection");
        return Result;
    }

    if (ImportScene(Path, Manager, Scene.get(), false))
    {
        if (FbxNode* RootNode = Scene->GetRootNode())
        {
            InspectMeshContentRecursive(RootNode, Result);
        }
    }

    return Result;
}

UAnimSequence* FFbxImporter::LoadAnimSequence(const FString& Path, const USkeletalMesh* TargetMesh)
{
    return LoadAnimSequence(Path, TargetMesh, 0);
}

UAnimSequence* FFbxImporter::LoadAnimSequence(const FString& Path, const USkeletalMesh* TargetMesh, int32 AnimStackIndex)
{
    if (!TargetMesh || !TargetMesh->HasValidMeshData())
    {
        UE_LOG_ERROR("[FbxImporter] Invalid target skeletal mesh for animation import: %s", Path.c_str());
        return nullptr;
    }

    FbxScene* Scene = GetOrCreateAnimationScene(Path);
    if (!Scene)
    {
        return nullptr;
    }

    //"LeftArm" -> 4
    //"Head" -> 1
    TMap<FString,int32> BoneNameToIndex;
    const TArray<FBoneInfo>& Bones = TargetMesh->GetBones();
    for (int32 BoneIndex = 0; BoneIndex < Bones.size();++BoneIndex)
    {
        BoneNameToIndex[Bones[BoneIndex].Name] = BoneIndex;
    }

    TMap<FString,FbxNode*> BoneNameToNode;
    // Animation에서 움직일 Bone의 FBX node들을 모은다. 최종 track key는 FBX immediate parent가 아니라
    // 엔진에 실제 import된 parent bone 기준 local transform으로 다시 계산한다.
    CollectAnimationBoneNodes(Scene->GetRootNode(),BoneNameToIndex,BoneNameToNode);

    //Clip Info를 다 들고온다. 즉,AnimStack의 정보를 다 긁어온다
    TArray<FFbxAnimationClipInfo> AnimationClipInfos = CollectAnimationClipInfos(Scene);
    if (AnimationClipInfos.empty())
    {
        UE_LOG_ERROR("[FbxImporter] No animation stack found: %s", Path.c_str());
        return nullptr;
    }
    
    //유저가 원하는 Stack을 선택한다(Index기반)
    const FFbxAnimationClipInfo* SelectedClipInfo = nullptr;
    for (const FFbxAnimationClipInfo& ClipInfo : AnimationClipInfos)
    {
        if (ClipInfo.AnimStackIndex == AnimStackIndex)
        {
            SelectedClipInfo = &ClipInfo;
            break;
        }
    }

    //원하는 Stack이 없으면 return
    if (!SelectedClipInfo)
    {
        UE_LOG_ERROR("[FbxImporter] Invalid animation stack index %d: %s", AnimStackIndex, Path.c_str());
        return nullptr;
    }

    //유저가 원하는 Stack = AnimStack
    FbxAnimStack* AnimStack = Scene->GetSrcObject<FbxAnimStack>(SelectedClipInfo->AnimStackIndex);
    if (!AnimStack)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to get animation stack %d: %s", SelectedClipInfo->AnimStackIndex, Path.c_str());
        return nullptr;
    }

    //어떤 AnimStack을 쓸지 Scene의 컨텍스트에 저장
    Scene->SetCurrentAnimationStack(AnimStack);

    //UAnimDataModel에 현재 스택의 정보를 담는다 
    const FFrameRate FrameRate = { 30, 1 };
    const float FramesPerSecond = FrameRate.AsDecimal();
    const float PlayLength = static_cast<float>(SelectedClipInfo->DurationSeconds);
    const int32 NumberOfKeys = std::max(1, static_cast<int32>(std::floor(PlayLength * FramesPerSecond)) + 1);
    const int32 NumberOfFrames = NumberOfKeys;

    UAnimDataModel* AnimDataModel = UObjectManager::Get().CreateObject<UAnimDataModel>();
    AnimDataModel->SetTiming(PlayLength, FrameRate, NumberOfFrames, NumberOfKeys);

    //한 TrackElement가 이 애니메이션에서의 전체 Key를 가지고 있음.
    TArray<FBoneAnimationTrack>& BoneAnimationTracks = AnimDataModel->GetMutableBoneAnimationTracks();
    BoneAnimationTracks.reserve(BoneNameToNode.size());

    for (const auto& BoneName_Node : BoneNameToNode)
    {
        //애니메이션이 움직일 Bone인지 확인
        const FString& BoneName = BoneName_Node.first;
        FbxNode* BoneNode = BoneName_Node.second;
        auto BoneName_Index = BoneNameToIndex.find(BoneName);
        if (!BoneNode || BoneName_Index == BoneNameToIndex.end())
        {
            continue;
        }

        //움직일 Bone이라면 Track에 넣음 안움직일 Bone이라면 AnimSequence::EvaluatePose 과정에서 Fallback으로 대체함.
        FBoneAnimationTrack BoneAnimationTrack;
        BoneAnimationTrack.Name = BoneName_Index->first; //Bone의 이름
        BoneAnimationTrack.BoneIndex = BoneName_Index->second; //SkeletalMesh의 Bone배열을 향한 인덱스
        const int32 BoneIndex = BoneAnimationTrack.BoneIndex;
        const FBoneInfo& TargetBone = Bones[BoneIndex]; //TargetMesh->GetBones()
        const bool bIsRootBone = TargetBone.ParentIndex < 0;
        FbxNode* ParentBoneNode = nullptr;
        if (TargetBone.ParentIndex >= 0 &&
            TargetBone.ParentIndex < static_cast<int32>(Bones.size()))
        {
            const FString& ParentBoneName = Bones[TargetBone.ParentIndex].Name;
            auto ParentNodeIt = BoneNameToNode.find(ParentBoneName);
            if (ParentNodeIt != BoneNameToNode.end())
            {
                ParentBoneNode = ParentNodeIt->second;
            }
        }

        FVector BindTranslation = FVector::ZeroVector;
        FMatrix BindRotationMatrix = FMatrix::Identity;
        FVector BindScale = FVector::OneVector;
        DecomposeWithSignedScale(
            TargetBone.LocalBindTransform,
            BindTranslation,
            BindRotationMatrix,
            BindScale);

        const ESignedScaleAxis PreferredAnimNegativeAxis =
            ResolveNegativeScaleAxis(GetNegativeScaleAxis(BindScale));
        const bool bBindHasNonIdentityScale = IsNonIdentityScale(BindScale);

        FRawAnimSequenceTrack& RawTrack = BoneAnimationTrack.InternalTrack;
        RawTrack.PosKeys.reserve(NumberOfKeys);
        RawTrack.RotKeys.reserve(NumberOfKeys);
        RawTrack.ScaleKeys.reserve(NumberOfKeys);

        for (int32 KeyIndex = 0; KeyIndex < NumberOfKeys; ++KeyIndex)
        {
            //총 키 갯수 / 1초당 Key = second
            const double SampleSeconds = std::min(
                SelectedClipInfo->EndSeconds,
                SelectedClipInfo->StartSeconds + (static_cast<double>(KeyIndex) / static_cast<double>(FramesPerSecond)));

            FbxTime SampleTime;
            SampleTime.SetSecondDouble(SampleSeconds);

            // EvaluateLocalTransform은 FBX 원본 immediate parent 기준 local transform이다.
            // Skeleton/Armature/Null/helper node가 엔진 bone hierarchy에서 빠지면 root/local animation 기준이 틀어진다.
            // 그래서 global pose를 샘플링한 뒤, SkeletalMesh bind pose와 같은 순서로 imported parent bone 기준 local로 재계산한다.
            const FMatrix BoneGlobalTransform =
                ToFMatrix(BoneNode->EvaluateGlobalTransform(SampleTime));

            FMatrix LocalTransform = BoneGlobalTransform;
            if (ParentBoneNode)
            {
                const FMatrix ParentGlobalTransform =
                    ToFMatrix(ParentBoneNode->EvaluateGlobalTransform(SampleTime));
                LocalTransform = BoneGlobalTransform * ParentGlobalTransform.GetInverse();
            }

            FVector Translation = FVector::ZeroVector;
            FQuat Rotation = FQuat::Identity;
            FVector Scale = FVector::OneVector;
            bool bFixedReflection = false;
            if (!DecomposeWithSignedScaleForAnim(
                    LocalTransform,
                    Translation,
                    Rotation,
                    Scale,
                    PreferredAnimNegativeAxis,
                    &bFixedReflection))
            {
                UE_LOG_WARNING(
                    "[FBXAnim] Failed to decompose anim matrix Bone=%s Key=%d",
                    BoneName.c_str(),
                    KeyIndex);

                // determinant가 음수인 basis를 그대로 FQuat에 넣으면 블렌딩 중 뒤집힘이 발생한다.
                // 분해 실패 시에는 잘못된 rotation key를 넣지 말고 이전 key나 안전한 기본값을 사용한다.
                if (!RawTrack.PosKeys.empty() && !RawTrack.RotKeys.empty() && !RawTrack.ScaleKeys.empty())
                {
                    RawTrack.PosKeys.push_back(RawTrack.PosKeys.back());
                    RawTrack.RotKeys.push_back(RawTrack.RotKeys.back());
                    RawTrack.ScaleKeys.push_back(RawTrack.ScaleKeys.back());
                }
                else
                {
                    RawTrack.PosKeys.push_back(LocalTransform.GetTranslation());
                    RawTrack.RotKeys.push_back(FQuat::Identity);
                    RawTrack.ScaleKeys.push_back(FVector::OneVector);
                }
                continue;
            }

            if (bFixedReflection)
            {
                UE_LOG(
                    "[FBXAnim] Reflection fixed Bone=%s Key=%d Scale=(%.6f, %.6f, %.6f)",
                    BoneName.c_str(),
                    KeyIndex,
                    Scale.X,
                    Scale.Y,
                    Scale.Z);
            }

            const bool bSampledScaleIsIdentity = IsNearlyIdentityScale(Scale);
            if (bIsRootBone && bBindHasNonIdentityScale && bSampledScaleIsIdentity)
            {
                const FVector TranslationDelta = Translation - BindTranslation;
                const FVector UnitScale = AbsScale(BindScale);
                Translation = BindTranslation + (TranslationDelta * UnitScale);
                Rotation = FQuat(BindRotationMatrix).GetNormalized();
                Scale = BindScale;
            }
            else if (!bIsRootBone && bBindHasNonIdentityScale && bSampledScaleIsIdentity)
            {
                Scale = BindScale;
            }

            RawTrack.PosKeys.push_back(Translation);
            RawTrack.RotKeys.push_back(Rotation.GetNormalized());
            RawTrack.ScaleKeys.push_back(Scale);
        }

        BoneAnimationTracks.push_back(BoneAnimationTrack);
    }

    if (BoneAnimationTracks.empty())
    {
        UE_LOG_ERROR("[FbxImporter] No matched animated bone found: %s", Path.c_str());
        UObjectManager::Get().DestroyObject(AnimDataModel);
        return nullptr;
    }

    UAnimSequence* AnimSequence = UObjectManager::Get().CreateObject<UAnimSequence>();
    AnimSequence->SetDataModel(AnimDataModel);

    return AnimSequence;
}

TArray<FFbxAnimationClipInfo> FFbxImporter::InspectAnimationClips(const FString& Path)
{
    return CollectAnimationClipInfos(Path);
}

bool FFbxImporter::InspectMeshAndAnimClips(const FString& Path, FFbxMeshContentInfo& OutInfo, TArray<FFbxAnimationClipInfo>& OutAnimationClips)
{
    FbxManager* Manager = GetOrCreateManager();
    if (!Manager)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to create FbxManager for Mesh And Anim");
        return false;
    }

    std::unique_ptr<FbxScene, FFbxSceneDestroyer> Scene(FbxScene::Create(Manager, "InspectFbxScene"));
    if (!Scene)
    {
        UE_LOG_ERROR("[FbxImporter] Failed to create FbxScene for Mesh And Anim");
        return false;
    }

    if (!ImportScene(Path, Manager, Scene.get(), false))
    {
        return false;
    }

    OutAnimationClips.clear();
    OutInfo.bHasSkeletalMesh = false;
    OutInfo.bHasStaticMesh = false;
    OutAnimationClips = CollectAnimationClipInfos(Scene.get());
    if (FbxNode* RootNode = Scene->GetRootNode())
    {
        InspectMeshContentRecursive(RootNode, OutInfo);
    }
    return true;
}

TArray<FFbxAnimationClipInfo> FFbxImporter::CollectAnimationClipInfos(const FString& Path)
{
	TArray<FFbxAnimationClipInfo> Result;

	FbxManager* Manager = GetOrCreateManager();
	if (!Manager)
	{
		return Result;
	}

	FbxIOSettings* IOSettings = FbxIOSettings::Create(Manager, IOSROOT);
	if (!IOSettings)
	{
		return Result;
	}

	IOSettings->SetBoolProp(IMP_FBX_TEXTURE, false);
	IOSettings->SetBoolProp(IMP_FBX_MATERIAL, false);
	IOSettings->SetBoolProp(IMP_FBX_EXTRACT_EMBEDDED_DATA, false);
	IOSettings->SetBoolProp(IMP_FBX_AUDIO, false);
	IOSettings->SetBoolProp(IMP_GEOMETRY, false);
	IOSettings->SetBoolProp(IMP_DEFORMATION, false);
	IOSettings->SetBoolProp(IMP_CAMERA, false);
	IOSettings->SetBoolProp(IMP_LIGHT, false);
	IOSettings->SetBoolProp(IMP_AUDIO, false);
	IOSettings->SetBoolProp(IMP_VIEW_CUBE, false);
	IOSettings->SetBoolProp(IMP_FBX_MODEL, false);
	IOSettings->SetBoolProp(IMP_FBX_LINK, false);
	IOSettings->SetBoolProp(IMP_FBX_SHAPE, false);
	IOSettings->SetBoolProp(IMP_FBX_CONSTRAINT, false);
	IOSettings->SetBoolProp(IMP_FBX_CHARACTER, false);

	FbxImporter* Importer = FbxImporter::Create(Manager, "");
	if (!Importer)
	{
		IOSettings->Destroy();
		return Result;
	}

	FExternalPathStaging StagedPath;
	if (!StagedPath.PrepareFile(Path, "FBX clip inspect"))
	{
		Importer->Destroy();
		IOSettings->Destroy();
		return Result;
	}

	const FString& ImporterPath = StagedPath.GetLibraryPath();
	if (!Importer->Initialize(ImporterPath.c_str(), -1, IOSettings))
	{
		UE_LOG_ERROR("[FbxImporter] Fast clip inspect initialize failed: %s (%s)",
			Path.c_str(),
			Importer->GetStatus().GetErrorString());

		Importer->Destroy();
		IOSettings->Destroy();
		return Result;
	}

	// Read take metadata without importing an FbxScene.
	Importer->GetImportOptions(FbxImporter::eParseFile);

	const int32 ClipCount = Importer->GetAnimStackCount();
	for (int32 ClipIndex = 0; ClipIndex < ClipCount; ++ClipIndex)
	{
		FbxTakeInfo* TakeInfo = Importer->GetTakeInfo(ClipIndex);
		if (!TakeInfo)
		{
			continue;
		}

		FbxTimeSpan TimeSpan = TakeInfo->mLocalTimeSpan;
		const double StartSeconds = TimeSpan.GetStart().GetSecondDouble();
		const double EndSeconds = TimeSpan.GetStop().GetSecondDouble();

		FFbxAnimationClipInfo ClipInfo;
		ClipInfo.Name = FString(TakeInfo->mName.Buffer());
		ClipInfo.StartSeconds = StartSeconds;
		ClipInfo.EndSeconds = EndSeconds;
		ClipInfo.DurationSeconds = EndSeconds - StartSeconds;
		ClipInfo.AnimStackIndex = ClipIndex;

		Result.push_back(ClipInfo);
	}

	Importer->Destroy();
	IOSettings->Destroy();
	return Result;
}

bool FFbxImporter::ImportScene(const FString& Path, FbxManager* Manager, FbxScene* Scene, bool bConvertScene)
{
	FbxImporter* Importer = FbxImporter::Create(Manager, "");
	FExternalPathStaging StagedPath;
	if (!StagedPath.PrepareFile(Path, "FBX scene import"))
	{
		Importer->Destroy();
		return false;
	}

	const FString& ImporterPath = StagedPath.GetLibraryPath();
	if (!Importer->Initialize(ImporterPath.c_str(), -1, Manager->GetIOSettings()))
	{
		UE_LOG_ERROR("[FbxImporter] Initialize failed: %s (%s)", Path.c_str(), Importer->GetStatus().GetErrorString());
		Importer->Destroy();
		return false;
	}

	const bool bResult = Importer->Import(Scene);
	if (!bResult)
	{
		UE_LOG_ERROR("[FbxImporter] Import failed: %s (%s)", Path.c_str(), Importer->GetStatus().GetErrorString());
	}

	Importer->Destroy();

	if (bResult && bConvertScene)
	{
		// Engine import policy: left-handed, Z-up, X-forward, meter.
		// FBX SDK가 mesh/transform/anim까지 일관되게 변환해주므로 정점 단계에서 축 swap 금지.
		const FbxAxisSystem TargetAxis(
			FbxAxisSystem::eZAxis,
			FbxAxisSystem::eParityOdd,
			FbxAxisSystem::eLeftHanded);
		TargetAxis.DeepConvertScene(Scene);
		FbxSystemUnit::m.ConvertScene(Scene);
	}

	return bResult;
}

void FFbxImporter::CollectMeshes(FbxNode* Node, FStaticMesh* InStaticMesh)
{
	if (!Node) return;

	if (FbxNodeAttribute* Attr = Node->GetNodeAttribute())
	{
		if (Attr->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			ProcessMesh(static_cast<FbxMesh*>(Attr), InStaticMesh);
		}
	}

	for (int32 i = 0; i < Node->GetChildCount(); ++i)
	{
		CollectMeshes(Node->GetChild(i), InStaticMesh);
	}
}

void FFbxImporter::ProcessMesh(FbxMesh* Mesh, FStaticMesh* InStaticMesh)
{
	if (!Mesh || Mesh->GetPolygonCount() <= 0)
	{
		return;
	}

	FbxNode* OwnerNode = Mesh->GetNode();

	// FbxAxisSystem/FbxSystemUnit::ConvertScene은 노드 transform에 변환을 baked함.
	// → control point에 GlobalTransform * GeometricTransform을 적용해야 단위/축이 반영됨.
	FbxAMatrix VertexTransform;
    VertexTransform.SetIdentity();
	FbxAMatrix NormalTransform;
    NormalTransform.SetIdentity();
	if (OwnerNode)
	{
		const FbxVector4 T = OwnerNode->GetGeometricTranslation(FbxNode::eSourcePivot);
		const FbxVector4 R = OwnerNode->GetGeometricRotation(FbxNode::eSourcePivot);
		const FbxVector4 S = OwnerNode->GetGeometricScaling(FbxNode::eSourcePivot);
		FbxAMatrix GeomTransform;
		GeomTransform.SetTRS(T, R, S);

		const FbxAMatrix GlobalTransform = OwnerNode->EvaluateGlobalTransform();
		VertexTransform = GlobalTransform * GeomTransform;

		// Normal은 회전·스케일만 — translation 제거
		NormalTransform = VertexTransform;
		NormalTransform.SetT(FbxVector4(0, 0, 0, 0));
	}
    const bool bReverseWinding = IsMirroredTransform(VertexTransform);

	const FbxVector4* ControlPoints = Mesh->GetControlPoints();
	if (!ControlPoints) return;

	// 머티리얼 매핑 모드 확인 (per-polygon으로 가정, 그 외엔 단일 슬롯으로 처리)
	FbxLayerElementArrayTemplate<int32>* MaterialIndices = nullptr;
	FbxGeometryElement::EMappingMode MaterialMappingMode = FbxGeometryElement::eByPolygon;
	if (Mesh->GetElementMaterial())
	{
		MaterialIndices = &Mesh->GetElementMaterial()->GetIndexArray();
		MaterialMappingMode = Mesh->GetElementMaterial()->GetMappingMode();
	}

	// 슬롯별 인덱스 임시 저장 (OBJ 로더와 동일한 패턴)
	TArray<TArray<uint32>> SlotIndices;

	const int32 PolygonCount = Mesh->GetPolygonCount();
	for (int32 PolyIdx = 0; PolyIdx < PolygonCount; ++PolyIdx)
	{
		// Triangulate 이후이므로 PolygonSize == 3 가정
		const int32 PolygonSize = Mesh->GetPolygonSize(PolyIdx);
		if (PolygonSize != 3) continue;

		// 머티리얼 슬롯 결정
		FString MaterialName = "DefaultWhite";
		if (MaterialIndices && OwnerNode)
		{
			int32 MatIdx = 0;
			if (MaterialMappingMode == FbxGeometryElement::eByPolygon &&
				PolyIdx < MaterialIndices->GetCount())
			{
				MatIdx = MaterialIndices->GetAt(PolyIdx);
			}
			else if (MaterialMappingMode == FbxGeometryElement::eAllSame &&
				MaterialIndices->GetCount() > 0)
			{
				MatIdx = MaterialIndices->GetAt(0);
			}

			if (MatIdx >= 0 && MatIdx < OwnerNode->GetMaterialCount())
			{
				if (FbxSurfaceMaterial* SurfMat = OwnerNode->GetMaterial(MatIdx))
				{
					MaterialName = FString(SurfMat->GetName());
				}
			}
		}

		const int32 SlotIdx = GetOrAddMaterialSlot(InStaticMesh, MaterialName);
		if (SlotIdx >= static_cast<int32>(SlotIndices.size()))
		{
			SlotIndices.resize(SlotIdx + 1);
		}

		for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
		{
            const int32 Corner = GetTriangleCornerByWinding(CornerIndex, bReverseWinding);
			const int32 CtrlPointIdx = Mesh->GetPolygonVertex(PolyIdx, Corner);

			FNormalVertex Vertex = {};

			// Position
			FbxVector4 Pos = ControlPoints[CtrlPointIdx];
			Pos = VertexTransform.MultT(Pos);
			Vertex.Position = ToFVector(Pos);

			// Normal
			FbxVector4 Normal(0, 0, 1, 0);
			if (Mesh->GetPolygonVertexNormal(PolyIdx, Corner, Normal))
			{
				Normal[3] = 0.0;  // direction vector — translation은 무시
				Normal = NormalTransform.MultT(Normal);
				Vertex.Normal = ToFVector(Normal);
				const float Len = Vertex.Normal.Size();
				if (Len > 1e-6f) Vertex.Normal = Vertex.Normal / Len;
			}
			else
			{
				Vertex.Normal = FVector(0.0f, 0.0f, 1.0f);
			}

			// UV (첫 번째 채널만 사용)
			Vertex.UVs = FVector2(0.0f, 0.0f);
			if (Mesh->GetElementUVCount() > 0)
			{
				FbxStringList UVNames;
				Mesh->GetUVSetNames(UVNames);
				if (const char* UVName = UVNames.GetStringAt(0))
				{
					FbxVector2 UV;
					bool bUnmapped = false;
					if (Mesh->GetPolygonVertexUV(PolyIdx, Corner, UVName, UV, bUnmapped))
					{
						Vertex.UVs = ToFVector2(UV);
					}
				}
			}

			Vertex.Color = FColor{ 1.0f, 1.0f, 1.0f, 1.0f };

			const uint32 NewIndex = static_cast<uint32>(InStaticMesh->Vertices.size());
			InStaticMesh->Vertices.push_back(Vertex);
			SlotIndices[SlotIdx].push_back(NewIndex);
		}
	}

	// 슬롯별 인덱스를 Mesh.Indices에 합치고 Section 생성
	for (int32 SlotIdx = 0; SlotIdx < static_cast<int32>(SlotIndices.size()); ++SlotIdx)
	{
		TArray<uint32>& IndicesPerSlot = SlotIndices[SlotIdx];
		if (IndicesPerSlot.empty()) continue;

		FStaticMeshSection NewSection;
		NewSection.StartIndex = static_cast<uint32>(InStaticMesh->Indices.size());
		NewSection.IndexCount = static_cast<uint32>(IndicesPerSlot.size());
		NewSection.MaterialSlotIndex = SlotIdx;

		InStaticMesh->Indices.insert(
			InStaticMesh->Indices.end(),
			IndicesPerSlot.begin(),
			IndicesPerSlot.end());

		InStaticMesh->Sections.push_back(NewSection);
	}
}

int32 FFbxImporter::GetOrAddMaterialSlot(FStaticMesh* InStaticMesh, const FString& MaterialName)
{
	const FString SlotName = MaterialName.empty() ? FString("DefaultWhite") : MaterialName;

	for (int32 i = 0; i < static_cast<int32>(InStaticMesh->Slots.size()); ++i)
	{
		if (InStaticMesh->Slots[i].SlotName == SlotName)
		{
			return i;
		}
	}

	FStaticMeshMaterialSlot NewSlot;
	NewSlot.SlotName = SlotName;
	NewSlot.Material = nullptr;
	InStaticMesh->Slots.push_back(NewSlot);
	return static_cast<int32>(InStaticMesh->Slots.size() - 1);
}

FAABB FFbxImporter::BuildLocalBounds(FStaticMesh* InStaticMesh) const
{
	FAABB Bounds;
	Bounds.Reset();

	for (const FNormalVertex& Vertex : InStaticMesh->Vertices)
	{
		Bounds.Expand(Vertex.Position);
	}

	return Bounds;
}

void FFbxImporter::NormalizePositionsToUnitCube(FStaticMesh* InStaticMesh)
{
	if (InStaticMesh->Vertices.empty()) return;

	FVector Min(FLT_MAX, FLT_MAX, FLT_MAX);
	FVector Max(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (const FNormalVertex& V : InStaticMesh->Vertices)
	{
		Min.X = std::min(Min.X, V.Position.X);
		Min.Y = std::min(Min.Y, V.Position.Y);
		Min.Z = std::min(Min.Z, V.Position.Z);
		Max.X = std::max(Max.X, V.Position.X);
		Max.Y = std::max(Max.Y, V.Position.Y);
		Max.Z = std::max(Max.Z, V.Position.Z);
	}

	const FVector Center = (Min + Max) * 0.5f;
	const FVector Size = Max - Min;
	const float MaxDim = std::max(Size.X, std::max(Size.Y, Size.Z));
	if (MaxDim <= 1e-6f) return;

	const float Scale = 1.0f / MaxDim;
	for (FNormalVertex& V : InStaticMesh->Vertices)
	{
		V.Position = (V.Position - Center) * Scale;
	}
}

void FFbxImporter::ComputeTangents(FStaticMesh* InStaticMesh)
{
	const uint64 VertexCount = InStaticMesh->Vertices.size();
	TArray<FVector> TangentAcc(VertexCount, FVector(0, 0, 0));
	TArray<FVector> BitangentAcc(VertexCount, FVector(0, 0, 0));

	const TArray<uint32>& Idx = InStaticMesh->Indices;
	for (uint64 i = 0; i + 2 < Idx.size(); i += 3)
	{
		const uint32 I0 = Idx[i], I1 = Idx[i + 1], I2 = Idx[i + 2];
		const FNormalVertex& V0 = InStaticMesh->Vertices[I0];
		const FNormalVertex& V1 = InStaticMesh->Vertices[I1];
		const FNormalVertex& V2 = InStaticMesh->Vertices[I2];

		FVector T, B;
		GetTangentBitangent(T, B, V0.Position, V1.Position, V2.Position,
			V0.UVs, V1.UVs, V2.UVs);
		TangentAcc[I0] += T; TangentAcc[I1] += T; TangentAcc[I2] += T;
		BitangentAcc[I0] += B; BitangentAcc[I1] += B; BitangentAcc[I2] += B;
	}

	for (uint64 i = 0; i < VertexCount; ++i)
	{
		const FVector& N = InStaticMesh->Vertices[i].Normal;
		FVector T = TangentAcc[i];

		// Gram-Schmidt
		T = (T - N * FVector::DotProduct(N, T));
		const float Len = T.Size();
		T = (Len > 1e-6f) ? T / Len : FVector(1, 0, 0);

		const FVector ExpectedB = FVector::CrossProduct(N, T);
		const float Sign = (FVector::DotProduct(ExpectedB, BitangentAcc[i]) < 0.0f) ? -1.0f : 1.0f;

		InStaticMesh->Vertices[i].Tangent = FVector4(T.X, T.Y, T.Z, Sign);
	}
}

void FFbxImporter::CollectSkeletalMeshes(
    FbxNode* Node,
    FSkeletalMesh* InSkeletalMesh,
    ESkeletalMeshImportPass Pass,
    TMap<FbxNode*, int32>& BoneNodeToIndex,
    bool& bHasImportedSkinnedMesh)
{
    if (!Node)
    {
        return;
    }

    //  LOD 0만 받도록 처리함 - 현석
    if (IsLODGroupNode(Node))
    {
        int32 LODIndexToImport = 0;
        FbxNode* LODChildToImport = FindLODChildToImport(Node, LODIndexToImport);
        if (!LODChildToImport)
        {
            if (Pass == ESkeletalMeshImportPass::SkinnedMeshes)
            {
                UE_LOG_WARNING("[FbxImporter] Skeletal LODGroup has no mesh child: %s", Node->GetName());
            }
            return;
        }

        if (Pass == ESkeletalMeshImportPass::SkinnedMeshes)
        {
            UE_LOG("[FbxImporter] Skeletal LODGroup detected: %s. Importing LOD%d only: %s",
                   Node->GetName(),
                   LODIndexToImport,
                   LODChildToImport->GetName());
        }

        CollectSkeletalMeshes(
            LODChildToImport,
            InSkeletalMesh,
            Pass,
            BoneNodeToIndex,
            bHasImportedSkinnedMesh);
        return;
    }

    int32 CurrentLODIndex = -1;
    if (TryParseLODIndexFromNodeName(Node, CurrentLODIndex) && CurrentLODIndex > 0)
    {
        if (Pass == ESkeletalMeshImportPass::SkinnedMeshes)
        {
            UE_LOG("[FbxImporter] Skipping skeletal LOD%d mesh branch: %s",
                   CurrentLODIndex,
                   Node->GetName());
        }
        return;
    }

    if (FbxMesh* Mesh = Node->GetMesh())
    {
        ProcessSkeletalMesh(
            Mesh,
            InSkeletalMesh,
            Pass,
            BoneNodeToIndex,
            bHasImportedSkinnedMesh);
    }

    for (int32 i = 0; i < Node->GetChildCount(); ++i)
    {
        CollectSkeletalMeshes(
            Node->GetChild(i),
            InSkeletalMesh,
            Pass,
            BoneNodeToIndex,
            bHasImportedSkinnedMesh);
    }
}

void FFbxImporter::ProcessSkeletalMesh(
    FbxMesh* Mesh,
    FSkeletalMesh* InSkeletalMesh,
    ESkeletalMeshImportPass Pass,
    TMap<FbxNode*, int32>& BoneNodeToIndex,
    bool& bHasImportedSkinnedMesh)
{
    if (!Mesh || !InSkeletalMesh || Mesh->GetPolygonCount() <= 0)
    {
        return;
    }

    const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);

    if (Pass == ESkeletalMeshImportPass::RigidAttachedMeshes)
    {
        if (SkinCount > 0)
        {
            return;
        }

        ProcessRigidAttachedMesh(
            Mesh,
            InSkeletalMesh,
            BoneNodeToIndex,
            bHasImportedSkinnedMesh);
        return;
    }

    if (Pass != ESkeletalMeshImportPass::SkinnedMeshes)
    {
        return;
    }

    if (SkinCount <= 0)
    {
        return;
    }

    FbxNode* OwnerNode = Mesh->GetNode();

    const FbxVector4* ControlPoints = Mesh->GetControlPoints();
    if (!ControlPoints)
    {
        return;
    }

    const int32 ControlPointCount = Mesh->GetControlPointsCount();

    const FbxAMatrix MeshGeometry = GetGeometryTransform(OwnerNode);

    FbxAMatrix MeshBindGlobalWithGeometry;
    MeshBindGlobalWithGeometry.SetIdentity();
    bool bHasMeshBindGlobalWithGeometry = false;

    TArray<TArray<FTempInfluence>> InfluencesByControlPoint;
    InfluencesByControlPoint.resize(ControlPointCount);

    // cluster link node를 bone으로 등록
    for (int32 SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)
    {
        FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
        if (!Skin)
        {
            continue;
        }

        const int32 ClusterCount = Skin->GetClusterCount();
        for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
        {
            FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
            if (!Cluster || !Cluster->GetLink())
            {
                continue;
            }

            FbxNode* BoneNode = Cluster->GetLink();

            FbxAMatrix MeshBindGlobal;
            FbxAMatrix LinkBindGlobal;

            Cluster->GetTransformMatrix(MeshBindGlobal);
            Cluster->GetTransformLinkMatrix(LinkBindGlobal);

            const FbxAMatrix ClusterMeshBindGlobalWithGeometry = MeshBindGlobal * MeshGeometry;
            if (!bHasMeshBindGlobalWithGeometry)
            {
                MeshBindGlobalWithGeometry = ClusterMeshBindGlobalWithGeometry;
                bHasMeshBindGlobalWithGeometry = true;
            }

            if (!bHasImportedSkinnedMesh)
            {
                bHasImportedSkinnedMesh = true;
            }

            if (BoneNodeToIndex.find(BoneNode) != BoneNodeToIndex.end())
            {
                continue;
            }

            const int32 NewBoneIndex = static_cast<int32>(InSkeletalMesh->Bones.size());
            BoneNodeToIndex[BoneNode] = NewBoneIndex;

            FBoneInfo Bone = {};
            Bone.Name = FString(BoneNode->GetName());
            Bone.ParentIndex = -1;

            Bone.GlobalBindTransform = ToFMatrix(LinkBindGlobal);
            Bone.InverseBindPose = Bone.GlobalBindTransform.GetInverse();
            Bone.LocalBindTransform = Bone.GlobalBindTransform;

            InSkeletalMesh->Bones.push_back(Bone);
        }
    }

    if (!bHasMeshBindGlobalWithGeometry)
    {
        return;
    }

    const FbxAMatrix NormalBindGlobalWithGeometry =
        GetNormalTransformFromPositionTransform(MeshBindGlobalWithGeometry);
    const bool bReverseWinding = IsMirroredTransform(MeshBindGlobalWithGeometry);

    // parentIndex와 LocalBindTransform을 계산
    for (auto& Pair : BoneNodeToIndex)
    {
        FbxNode* BoneNode = Pair.first;
        const int32 BoneIndex = Pair.second;

        int32 ParentIndex = -1;
        FbxNode* ParentNode = BoneNode ? BoneNode->GetParent() : nullptr;

        while (ParentNode)
        {
            auto ParentIt = BoneNodeToIndex.find(ParentNode);
            if (ParentIt != BoneNodeToIndex.end())
            {
                ParentIndex = ParentIt->second;
                break;
            }

            ParentNode = ParentNode->GetParent();
        }

        FBoneInfo& Bone = InSkeletalMesh->Bones[BoneIndex];
        Bone.ParentIndex = ParentIndex;

        if (ParentIndex >= 0)
        {
            const FMatrix ParentGlobalInv =
                InSkeletalMesh->Bones[ParentIndex].GlobalBindTransform.GetInverse();

            Bone.LocalBindTransform = Bone.GlobalBindTransform * ParentGlobalInv;
        }
        else
        {
            Bone.LocalBindTransform = Bone.GlobalBindTransform;
        }
    }

    // control point별 influence를 수집
    for (int32 SkinIndex = 0; SkinIndex < SkinCount; SkinIndex++)
    {
        FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
        if (!Skin)
        {
            continue;
        }

        const int32 ClusterCount = Skin->GetClusterCount();
        for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ClusterIndex++)
        {
            FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
            if (!Cluster || !Cluster->GetLink())
            {
                continue;
            }

            auto BoneIt = BoneNodeToIndex.find(Cluster->GetLink());
            if (BoneIt == BoneNodeToIndex.end())
            {
                continue;
            }

            const int32 BoneIndex = BoneIt->second;

            const int32 IndexCount = Cluster->GetControlPointIndicesCount();
            int* ControlPointIndices = Cluster->GetControlPointIndices();
            double* ControlPointWeights = Cluster->GetControlPointWeights();

            if (!ControlPointIndices || !ControlPointWeights)
            {
                continue;
            }

            for (int32 i = 0; i < IndexCount; i++)
            {
                const int32 CtrlPointIndex = ControlPointIndices[i];
                const float Weight = static_cast<float>(ControlPointWeights[i]);

                if (CtrlPointIndex < 0 || CtrlPointIndex >= ControlPointCount || Weight <= 0.0f)
                {
                    continue;
                }

                InfluencesByControlPoint[CtrlPointIndex].push_back({ BoneIndex, Weight });
            }
        }
    }

    // material mapping 정보 준비
    FbxLayerElementArrayTemplate<int32>* MaterialIndices = nullptr;
    FbxGeometryElement::EMappingMode MaterialMappingMode = FbxGeometryElement::eByPolygon;

    if (Mesh->GetElementMaterial())
    {
        MaterialIndices = &Mesh->GetElementMaterial()->GetIndexArray();
        MaterialMappingMode = Mesh->GetElementMaterial()->GetMappingMode();
    }

    TArray<TArray<uint32>> SlotIndices;

    // polygon corner를 FSkeletalMeshVertex로 변환
    const int32 PolygonCount = Mesh->GetPolygonCount();
    for (int32 PolyIdx = 0; PolyIdx < PolygonCount; PolyIdx++)
    {
        const int32 PolygonSize = Mesh->GetPolygonSize(PolyIdx);
        if (PolygonSize != 3)
        {
            continue;
        }

        FString MaterialName = "DefaultWhite";

        if (MaterialIndices && OwnerNode)
        {
            int32 MatIdx = 0;

            if (MaterialMappingMode == FbxGeometryElement::eByPolygon &&
                PolyIdx < MaterialIndices->GetCount())
            {
                MatIdx = MaterialIndices->GetAt(PolyIdx);
            }
            else if (MaterialMappingMode == FbxGeometryElement::eAllSame &&
                     MaterialIndices->GetCount() > 0)
            {
                MatIdx = MaterialIndices->GetAt(0);
            }

            if (MatIdx >= 0 && MatIdx < OwnerNode->GetMaterialCount())
            {
                if (FbxSurfaceMaterial* SurfMat = OwnerNode->GetMaterial(MatIdx))
                {
                    MaterialName = FString(SurfMat->GetName());
                }
            }
        }

        const int32 SlotIdx = GetOrAddMaterialSlot(InSkeletalMesh, MaterialName);
        if (SlotIdx >= static_cast<int32>(SlotIndices.size()))
        {
            SlotIndices.resize(SlotIdx + 1);
        }

        for (int32 CornerIndex = 0; CornerIndex < 3; CornerIndex++)
        {
            const int32 Corner = GetTriangleCornerByWinding(CornerIndex, bReverseWinding);
            const int32 CtrlPointIdx = Mesh->GetPolygonVertex(PolyIdx, Corner);
            if (CtrlPointIdx < 0 || CtrlPointIdx >= ControlPointCount)
            {
                continue;
            }

            FSkeletalMeshVertex Vertex = {};
            ResetVertexInfluences(Vertex);

            FbxVector4 Pos = ControlPoints[CtrlPointIdx];
            Pos = MeshBindGlobalWithGeometry.MultT(Pos);
            Vertex.Position = ToFVector(Pos);

            FbxVector4 Normal(0, 0, 1, 0);
            if (Mesh->GetPolygonVertexNormal(PolyIdx, Corner, Normal))
            {
                Normal[3] = 0.0;
                Normal = NormalBindGlobalWithGeometry.MultT(Normal);

                Vertex.Normal = ToFVector(Normal);
                Vertex.Normal.NormalizeSafe();
            }
            else
            {
                Vertex.Normal = FVector(0.0f, 0.0f, 1.0f);
            }

            Vertex.UVs = FVector2(0.0f, 0.0f);
            if (Mesh->GetElementUVCount() > 0)
            {
                FbxStringList UVNames;
                Mesh->GetUVSetNames(UVNames);

                if (const char* UVName = UVNames.GetStringAt(0))
                {
                    FbxVector2 UV;
                    bool bUnmapped = false;

                    if (Mesh->GetPolygonVertexUV(PolyIdx, Corner, UVName, UV, bUnmapped))
                    {
                        Vertex.UVs = ToFVector2(UV);
                    }
                }
            }

            Vertex.Color = FColor{ 1.0f, 1.0f, 1.0f, 1.0f };

            AssignTop4Influences(InfluencesByControlPoint[CtrlPointIdx], Vertex);

            const uint32 NewIndex = static_cast<uint32>(InSkeletalMesh->Vertices.size());
            InSkeletalMesh->Vertices.push_back(Vertex);
            SlotIndices[SlotIdx].push_back(NewIndex);
        }
    }

    for (int32 SlotIdx = 0; SlotIdx < static_cast<int32>(SlotIndices.size()); SlotIdx++)
    {
        TArray<uint32>& IndicesPerSlot = SlotIndices[SlotIdx];
        if (IndicesPerSlot.empty())
        {
            continue;
        }

        FStaticMeshSection NewSection;
        NewSection.StartIndex = static_cast<uint32>(InSkeletalMesh->Indices.size());
        NewSection.IndexCount = static_cast<uint32>(IndicesPerSlot.size());
        NewSection.MaterialSlotIndex = SlotIdx;

        InSkeletalMesh->Indices.insert(
            InSkeletalMesh->Indices.end(),
            IndicesPerSlot.begin(),
            IndicesPerSlot.end());

        InSkeletalMesh->Sections.push_back(NewSection);
    }
}

void FFbxImporter::ProcessRigidAttachedMesh(
    FbxMesh* Mesh,
    FSkeletalMesh* InSkeletalMesh,
    TMap<FbxNode*, int32>& BoneNodeToIndex,
    bool bHasImportedSkinnedMesh)
{
    if (!Mesh || !InSkeletalMesh || Mesh->GetPolygonCount() <= 0)
    {
        return;
    }

    FbxNode* OwnerNode = Mesh->GetNode();
    if (!OwnerNode)
    {
        return;
    }

    if (!bHasImportedSkinnedMesh)
    {
        UE_LOG_WARNING("[FbxImporter] Skip rigid mesh before skinned mesh import | Node=%s", OwnerNode->GetName());
        return;
    }

    if (ShouldSkipRigidMeshByName(OwnerNode))
    {
        return;
    }

    const int32 AttachBoneIndex = FindNearestImportedBoneIndex(OwnerNode, BoneNodeToIndex);
    if (AttachBoneIndex < 0)
    {
        return;
    }

    if (AttachBoneIndex > 65535)
    {
        UE_LOG_WARNING(
            "[FbxImporter] Skip rigid mesh because attach bone index exceeds uint16 limit | Node=%s | BoneIndex=%d",
            OwnerNode->GetName(),
            AttachBoneIndex);
        return;
    }

    const FbxVector4* ControlPoints = Mesh->GetControlPoints();
    if (!ControlPoints)
    {
        return;
    }

    const int32 ControlPointCount = Mesh->GetControlPointsCount();

    const FbxAMatrix OwnerGlobalWithGeometry = GetGlobalTransformWithGeometry(OwnerNode);
    const FbxAMatrix OwnerNormalGlobalWithGeometry =
        GetNormalTransformFromPositionTransform(OwnerGlobalWithGeometry);
    const bool bReverseWinding = IsMirroredTransform(OwnerGlobalWithGeometry);

    FbxLayerElementArrayTemplate<int32>* MaterialIndices = nullptr;
    FbxGeometryElement::EMappingMode MaterialMappingMode = FbxGeometryElement::eByPolygon;

    if (Mesh->GetElementMaterial())
    {
        MaterialIndices = &Mesh->GetElementMaterial()->GetIndexArray();
        MaterialMappingMode = Mesh->GetElementMaterial()->GetMappingMode();
    }

    TArray<TArray<uint32>> SlotIndices;

    const int32 PolygonCount = Mesh->GetPolygonCount();

    for (int32 PolyIdx = 0; PolyIdx < PolygonCount; PolyIdx++)
    {
        const int32 PolygonSize = Mesh->GetPolygonSize(PolyIdx);
        if (PolygonSize != 3)
        {
            continue;
        }

        FString MaterialName = "DefaultWhite";

        if (MaterialIndices && OwnerNode)
        {
            int32 MatIdx = 0;

            if (MaterialMappingMode == FbxGeometryElement::eByPolygon &&
                PolyIdx < MaterialIndices->GetCount())
            {
                MatIdx = MaterialIndices->GetAt(PolyIdx);
            }
            else if (MaterialMappingMode == FbxGeometryElement::eAllSame &&
                     MaterialIndices->GetCount() > 0)
            {
                MatIdx = MaterialIndices->GetAt(0);
            }

            if (MatIdx >= 0 && MatIdx < OwnerNode->GetMaterialCount())
            {
                if (FbxSurfaceMaterial* SurfMat = OwnerNode->GetMaterial(MatIdx))
                {
                    MaterialName = FString(SurfMat->GetName());
                }
            }
        }

        const int32 SlotIdx = GetOrAddMaterialSlot(InSkeletalMesh, MaterialName);
        if (SlotIdx >= static_cast<int32>(SlotIndices.size()))
        {
            SlotIndices.resize(SlotIdx + 1);
        }

        for (int32 CornerIndex = 0; CornerIndex < 3; CornerIndex++)
        {
            const int32 Corner = GetTriangleCornerByWinding(CornerIndex, bReverseWinding);
            const int32 CtrlPointIdx = Mesh->GetPolygonVertex(PolyIdx, Corner);
            if (CtrlPointIdx < 0 || CtrlPointIdx >= ControlPointCount)
            {
                continue;
            }

            FSkeletalMeshVertex Vertex = {};
            ResetVertexInfluences(Vertex);

            FbxVector4 Pos = ControlPoints[CtrlPointIdx];
            Pos = OwnerGlobalWithGeometry.MultT(Pos);
            Vertex.Position = ToFVector(Pos);

            FbxVector4 Normal(0, 0, 1, 0);
            if (Mesh->GetPolygonVertexNormal(PolyIdx, Corner, Normal))
            {
                Normal[3] = 0.0;
                Normal = OwnerNormalGlobalWithGeometry.MultT(Normal);

                Vertex.Normal = ToFVector(Normal);
                Vertex.Normal.NormalizeSafe();
            }
            else
            {
                Vertex.Normal = FVector(0.0f, 0.0f, 1.0f);
            }

            Vertex.UVs = FVector2(0.0f, 0.0f);
            if (Mesh->GetElementUVCount() > 0)
            {
                FbxStringList UVNames;
                Mesh->GetUVSetNames(UVNames);

                if (const char* UVName = UVNames.GetStringAt(0))
                {
                    FbxVector2 UV;
                    bool bUnmapped = false;

                    if (Mesh->GetPolygonVertexUV(PolyIdx, Corner, UVName, UV, bUnmapped))
                    {
                        Vertex.UVs = ToFVector2(UV);
                    }
                }
            }

            Vertex.Color = FColor{ 1.0f, 1.0f, 1.0f, 1.0f };

            // skin이 없는 rigid mesh이므로 parent bone 하나에 100% 붙임
            AssignRigidInfluence(Vertex, AttachBoneIndex);

            const uint32 NewIndex = static_cast<uint32>(InSkeletalMesh->Vertices.size());
            InSkeletalMesh->Vertices.push_back(Vertex);
            SlotIndices[SlotIdx].push_back(NewIndex);
        }
    }

    for (int32 SlotIdx = 0; SlotIdx < static_cast<int32>(SlotIndices.size()); SlotIdx++)
    {
        TArray<uint32>& IndicesPerSlot = SlotIndices[SlotIdx];
        if (IndicesPerSlot.empty())
        {
            continue;
        }

        FStaticMeshSection NewSection;
        NewSection.StartIndex = static_cast<uint32>(InSkeletalMesh->Indices.size());
        NewSection.IndexCount = static_cast<uint32>(IndicesPerSlot.size());
        NewSection.MaterialSlotIndex = SlotIdx;

        InSkeletalMesh->Indices.insert(
            InSkeletalMesh->Indices.end(),
            IndicesPerSlot.begin(),
            IndicesPerSlot.end());

        InSkeletalMesh->Sections.push_back(NewSection);
    }
}

int32 FFbxImporter::GetOrAddMaterialSlot(FSkeletalMesh* InSkeletalMesh, const FString& MaterialName)
{
    const FString SlotName = MaterialName.empty() ? FString("DefaultWhite") : MaterialName;

    for (int32 i = 0; i < static_cast<int32>(InSkeletalMesh->MaterialSlots.size()); i++)
    {
        if (InSkeletalMesh->MaterialSlots[i].SlotName == SlotName)
        {
            return i;
        }
    }

    FStaticMeshMaterialSlot NewSlot;
    NewSlot.SlotName = SlotName;
    NewSlot.Material = nullptr;
    InSkeletalMesh->MaterialSlots.push_back(NewSlot);
    return static_cast<int32>(InSkeletalMesh->MaterialSlots.size() - 1);
}

FAABB FFbxImporter::BuildLocalBounds(FSkeletalMesh* InSkeletalMesh) const
{
    FAABB Bounds;
    Bounds.Reset();

    if (!InSkeletalMesh)
    {
        return Bounds;
    }

    for (const FSkeletalMeshVertex& Vertex : InSkeletalMesh->Vertices)
    {
        Bounds.Expand(Vertex.Position);
    }

    return Bounds;
}

void FFbxImporter::ComputeTangents(FSkeletalMesh* InSkeletalMesh)
{
    if (!InSkeletalMesh)
    {
        return;
    }

    const uint64 VertexCount = InSkeletalMesh->Vertices.size();
    if (VertexCount == 0)
    {
        return;
    }

    TArray<FVector> TangentAcc(VertexCount, FVector(0.0f, 0.0f, 0.0f));
    TArray<FVector> BitangentAcc(VertexCount, FVector(0.0f, 0.0f, 0.0f));

    const TArray<uint32>& Idx = InSkeletalMesh->Indices;

    for (uint64 i = 0; i + 2 < Idx.size(); i += 3)
    {
        const uint32 I0 = Idx[i];
        const uint32 I1 = Idx[i + 1];
        const uint32 I2 = Idx[i + 2];

        if (I0 >= VertexCount || I1 >= VertexCount || I2 >= VertexCount)
        {
            continue;
        }

        const FSkeletalMeshVertex& V0 = InSkeletalMesh->Vertices[I0];
        const FSkeletalMeshVertex& V1 = InSkeletalMesh->Vertices[I1];
        const FSkeletalMeshVertex& V2 = InSkeletalMesh->Vertices[I2];

        FVector T;
        FVector B;

        GetTangentBitangent(
            T,
            B,
            V0.Position,
            V1.Position,
            V2.Position,
            V0.UVs,
            V1.UVs,
            V2.UVs);

        TangentAcc[I0] += T;
        TangentAcc[I1] += T;
        TangentAcc[I2] += T;

        BitangentAcc[I0] += B;
        BitangentAcc[I1] += B;
        BitangentAcc[I2] += B;
    }

    for (uint64 i = 0; i < VertexCount; i++)
    {
        const FVector& N = InSkeletalMesh->Vertices[i].Normal;
        FVector T = TangentAcc[i];

        T = T - N * FVector::DotProduct(N, T);

        const float Len = T.Size();
        T = (Len > 1e-6f) ? T / Len : FVector(1.0f, 0.0f, 0.0f);

        const FVector ExpectedB = FVector::CrossProduct(N, T);
        const float Sign =
            (FVector::DotProduct(ExpectedB, BitangentAcc[i]) < 0.0f)
                ? -1.0f
                : 1.0f;

        InSkeletalMesh->Vertices[i].Tangent = FVector4(T.X, T.Y, T.Z, Sign);
    }
}

void FFbxImporter::CollectAnimationBoneNodes(FbxNode* Node, const TMap<FString,int32>& BoneNameToIndex,  TMap<FString, FbxNode*>& OutBoneNameToNode) const
{

    if (!Node) return;

    FString NodeName = Node->GetName();

    //BoneNameToIndex에 존재한다면
    if (BoneNameToIndex.find(NodeName)!=BoneNameToIndex.end())
    {
        OutBoneNameToNode[NodeName] = Node;
    }

    int32 ChildCnt = Node->GetChildCount();
    for (int32 i = 0; i<ChildCnt; ++i)
    {
        CollectAnimationBoneNodes(Node->GetChild(i),BoneNameToIndex,OutBoneNameToNode);
    }
}

/** Example 
 *  Idle index_0 0.0 ~ 2.0
 *  Walk index_1 0.0 ~ 1.1
 *  Run index_2 0.0 ~ 0.8
 */
TArray<FFbxAnimationClipInfo> FFbxImporter::CollectAnimationClipInfos(FbxScene* Scene) const
{
    TArray<FFbxAnimationClipInfo> Result;
    
    if (!Scene) return Result;
    
    const int32 AnimCnt = Scene->GetSrcObjectCount<FbxAnimStack>();
    for (int32 AnimStackIndex = 0; AnimStackIndex < AnimCnt; AnimStackIndex++)
    {
        FbxAnimStack* AnimStack = Scene->GetSrcObject<FbxAnimStack>(AnimStackIndex);
        if (!AnimStack) continue;
        
        FbxTimeSpan TimeSpan = AnimStack->GetLocalTimeSpan();
        const FbxTime StartTime = TimeSpan.GetStart();
        const FbxTime EndTime = TimeSpan.GetStop();
        
        const double StartSeconds = StartTime.GetSecondDouble();
        const double EndSeconds = EndTime.GetSecondDouble();
        const double DurationSeconds = EndSeconds - StartSeconds;
        
        if (DurationSeconds<=0) continue;
        
        FFbxAnimationClipInfo ClipInfo;
        ClipInfo.Name = FString(AnimStack->GetName());
        ClipInfo.StartSeconds = StartSeconds;
        ClipInfo.EndSeconds = EndSeconds;
        ClipInfo.DurationSeconds = DurationSeconds;
        ClipInfo.AnimStackIndex = AnimStackIndex;
        
        Result.push_back(ClipInfo);
    }
    return Result;
}
