#pragma once

#include <Utilities/Exception/OutOfRange.hpp>

namespace Math
{
	struct Vector2
	{
		Vector2(float x = 0.0f, float y = 0.0f) : X( x ), Y( y ) {}

		Vector2& operator+=(const Vector2& v);
		Vector2& operator-=(const Vector2& v);

		const Vector2 operator+(const Vector2& v) const;
		const Vector2 operator-(const Vector2& v) const;

		friend Vector2 operator*(const Vector2& v, float n);
		friend Vector2 operator*(float n, const Vector2& v);

		friend Vector2 operator/(const Vector2& v, float n);
		friend Vector2 operator/(float n, const Vector2& v);

		float& operator[](const int i);

		const float Dot(const Vector2& v) const;
		const float Angle(const Vector2& v) const;

		const float LengthSqr(void) const;
		const float Length(void) const;
		const float Distance(const Vector2& v) const;

		const Vector2 Normal() const;

		// Data
		float X, Y;
	};
}
