#include "Vector4.hpp"
#include <cmath>

namespace Math
{
	Vector4& Vector4::operator+=( const Vector4& v )
	{
		X += v.X;
		Y += v.Y;
		Z += v.Z;
		W += v.W;
		return *this;
	}

	Vector4& Vector4::operator-=( const Vector4& v )
	{
		X -= v.X;
		Y -= v.Y;
		Z -= v.Z;
		W -= v.W;
		return *this;
	}

	const Vector4 Vector4::operator+( const Vector4& v ) const
	{
		return Vector4( X + v.X, Y + v.Y, Z + v.Z, W + v.W );
	}

	const Vector4 Vector4::operator-( const Vector4& v ) const
	{
		return Vector4( X - v.X, Y - v.Y, Z - v.Z, W - v.W );
	}

	Vector4 operator*( const Vector4& v, float n )
	{
		return Vector4( v.X * n, v.Y * n, v.Z * n, v.W * n );
	}

	Vector4 operator*( float n, const Vector4& v )
	{
		return Vector4( v.X * n, v.Y * n, v.Z * n, v.W * n );
	}

	Vector4 operator/( const Vector4& v, float n )
	{
		return Vector4( v.X / n, v.Y / n, v.Z / n, v.W / n );
	}

	Vector4 operator/( float n, const Vector4& v )
	{
		return Vector4( v.X / n, v.Y / n, v.Z / n, v.W / n );
	}
}
