#include "Transform.h"

#include <cmath>

namespace
{
	constexpr float MatrixDecomposeTolerance = 1.0e-6f;
}

FTransform::FTransform(const FMatrix& Mat)
{
	Location = Mat.GetLocation();
	Scale = Mat.GetScale();

	FMatrix RotationMatrix = Mat;
	RotationMatrix.M[3][0] = 0.0f;
	RotationMatrix.M[3][1] = 0.0f;
	RotationMatrix.M[3][2] = 0.0f;
	RotationMatrix.M[3][3] = 1.0f;

	if (std::fabs(Scale.X) > MatrixDecomposeTolerance)
	{
		RotationMatrix.M[0][0] /= Scale.X;
		RotationMatrix.M[0][1] /= Scale.X;
		RotationMatrix.M[0][2] /= Scale.X;
	}

	if (std::fabs(Scale.Y) > MatrixDecomposeTolerance)
	{
		RotationMatrix.M[1][0] /= Scale.Y;
		RotationMatrix.M[1][1] /= Scale.Y;
		RotationMatrix.M[1][2] /= Scale.Y;
	}

	if (std::fabs(Scale.Z) > MatrixDecomposeTolerance)
	{
		RotationMatrix.M[2][0] /= Scale.Z;
		RotationMatrix.M[2][1] /= Scale.Z;
		RotationMatrix.M[2][2] /= Scale.Z;
	}

	Rotation = RotationMatrix.ToQuat().GetNormalized();
}

FMatrix FTransform::ToMatrix() const
{
	FMatrix translateMatrix = FMatrix::MakeTranslationMatrix(Location);

	FMatrix rotationMatrix = Rotation.ToMatrix();

	FMatrix scaleMatrix = FMatrix::MakeScaleMatrix(Scale);

	return scaleMatrix * rotationMatrix * translateMatrix;
}
