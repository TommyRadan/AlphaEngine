#pragma once

#include "Vector3.hpp"
#include <cmath>

namespace Math
{
	struct Vector4
	{
		Vector4(const float x = 0.0f, const float y = 0.0f, const float z = 0.0f, const float w = 1.0f) : 
			X( x ), Y( y ), Z( z ), W( w ) 
		{}

		Vector4(const Vector3& v, const float w = 1.0f) : 
			X( v.X ), Y( v.Y ), Z( v.Z ), W( w ) 
		{}

		const float GetVectorLength(void) const;
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
		friend Vector4 operator+(const Vector4& v, const float n);
		friend Vector4 operator-(const Vector4& v, const float n);

		friend Vector4 operator+(const float n, const Vector4& v);
		friend Vector4 operator-(const float n, const Vector4& v);

		Vector4& operator+=(const float n);
		Vector4& operator-=(const float n);

		friend Vector4 operator*(const Vector4& v, const float n);
		friend Vector4 operator/(const Vector4& v, const float n);

		friend Vector4 operator*(const float n, const Vector4& v);
		friend Vector4 operator/(const float n, const Vector4& v);

		Vector4& operator*=(const float n);
		Vector4& operator/=(const float n);

		// Data
		float X, Y, Z, W;
	};
}
