#pragma once

#include "Vector2.hpp"

namespace Math
{
	struct Matrix3
	{
		Matrix3(void);
		Matrix3(
			float v00, float v01, float v02,
			float v10, float v11, float v12,
			float v20, float v21, float v22
		);

		const Matrix3 operator*(const Matrix3& mat);
		const Vector2 operator*(const Vector2& v);

		static Matrix3 Translate(const Vector2& v);
		static Matrix3 Scale(const Vector2& v);
		static Matrix3 Rotation(const float ang);

		const Matrix3 Transpose(void) const;

		const float Determinant(void) const;
		const Matrix3 Inverse(void) const;

		float m[9];
	};
}
