#pragma once

#include "Vector3.hpp"
#include <cmath>

namespace Math
{
	struct Vector4
	{
		Vector4( float x = 0.0f, float y = 0.0f, float z = 0.0f, float w = 1.0f ) : X( x ), Y( y ), Z( z ), W( w ) {}
		Vector4( const Vector3& v, float w = 1.0f ) : X( v.X ), Y( v.Y ), Z( v.Z ), W( w ) {}

		const float GetVectorLength() const {
			return sqrt(X*X + Y*Y + Z*Z + W*W);
		}
		friend float DotProduct(const Vector4& v1, const Vector4& v2);

		// Vector-Vector operations
		Vector4& operator+=(const Vector4& v);
		Vector4& operator-=(const Vector4& v);

		const Vector4 operator+(const Vector4& v) const;
		const Vector4 operator-(const Vector4& v) const;

		const Vector4 operator/(const Vector4& v) const;
		Vector4& operator/=(const Vector4& v);

		bool operator==(const Vector4& v);
		bool operator!=(const Vector4& v);

		// Vector-Scalar operations
		friend Vector4 operator+(const Vector4& v, float n);
		friend Vector4 operator-(const Vector4& v, float n);

		friend Vector4 operator+(float n, const Vector4& v);
		friend Vector4 operator-(float n, const Vector4& v);

		Vector4& operator+=(float n);
		Vector4& operator-=(float n);

		friend Vector4 operator*(const Vector4& v, float n);
		friend Vector4 operator/(const Vector4& v, float n);

		friend Vector4 operator*(float n, const Vector4& v);
		friend Vector4 operator/(float n, const Vector4& v);

		Vector4& operator*=(float n);
		Vector4& operator/=(float n);

		// Data
		float X, Y, Z, W;
	};
}
