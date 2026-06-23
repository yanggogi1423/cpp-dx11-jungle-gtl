#pragma once

#include <cmath>
#include <iostream>

struct FVector3
{
	union
	{
		struct
		{
			float X, Y, Z;
		};
		struct
		{
			float R, G, B;
		};

		//	배열 접근 가능
		float Data[3];
	};

	FVector3() : X(0), Y(0), Z(0) {}
	FVector3(float InX, float InY, float InZ) : X(InX), Y(InY), Z(InZ) {}

	FVector3 operator+(const FVector3& Other) const
	{
		return FVector3(X + Other.X, Y + Other.Y, Z + Other.Z);
	}
	FVector3 operator-(const FVector3& Other) const
	{
		return FVector3(X - Other.X, Y - Other.Y, Z - Other.Z);
	}
	FVector3 operator*(float Scalar) const
	{
		return FVector3(X * Scalar, Y * Scalar, Z * Scalar);
	}
	FVector3 operator/(float Scalar) const
	{
		return FVector3(X / Scalar, Y / Scalar, Z / Scalar);
	}
	FVector3& operator+=(const FVector3& Other)
	{
		X += Other.X;
		Y += Other.Y;
		Z += Other.Z;
		return *this;
	}
	FVector3& operator-=(const FVector3& Other)
	{
		X -= Other.X;
		Y -= Other.Y;
		Z -= Other.Z;
		return *this;
	}
	FVector3& operator*=(float Scalar)
	{
		X *= Scalar;
		Y *= Scalar;
		Z *= Scalar;
		return *this;
	}
	FVector3& operator/=(float Scalar)
	{
		X /= Scalar;
		Y /= Scalar;
		Z /= Scalar;
		return *this;
	}

	/*
		Global Scope에 두어도 되지만, FVector3의 멤버 함수로 정의하는 것이 더 직관적. 
		(namespace 사용하지 않도록 하기 위함)
	*/
	static float Dot(const FVector3& A, const FVector3& B)
	{
		return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
	}
	static FVector3 Cross(const FVector3& A, const FVector3& B)
	{
		return FVector3(
			A.Y * B.Z - A.Z * B.Y,
			A.Z * B.X - A.X * B.Z,
			A.X * B.Y - A.Y * B.X
		);
	}

	float Length() const
	{
		return std::sqrt(X * X + Y * Y + Z * Z);
	}

	float LengthSquared() const
	{
		return X * X + Y * Y + Z * Z;
	}

	//	Retruns a new FVector3. 자기 자신을 수정하지 않음.
	FVector3 Normalized() const
	{
		float length = Length();
		if (length > FLT_EPSILON)
		{
			return FVector3(X / length, Y / length, Z / length);
		}
		return FVector3(0, 0, 0); // Return zero vector if length is zero
	}

	//	자기 자신 수정	
	void Normalize()
	{
		float Length = std::sqrt(X * X + Y * Y + Z * Z);
		if (Length > FLT_EPSILON)
		{
			X /= Length;
			Y /= Length;
			Z /= Length;
		}
		else
		{
			X = Y = Z = 0; // Set to zero vector if length is zero
		}
	}

	std::ostream& operator<<(std::ostream& os) const
	{
		os << "FVector3(" << X << ", " << Y << ", " << Z << ")";
		return os;
	}

	const float& operator[](int Index) const
	{
		return Data[Index];
	}
	float & operator[](int Index)
	{
		return Data[Index];
	}
	


};

inline FVector3 operator*(float Scalar, const FVector3& V)
{
	return FVector3(V.X * Scalar, V.Y * Scalar, V.Z * Scalar);
}