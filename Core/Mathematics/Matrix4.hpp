#pragma once

#include "Vector3.hpp"
#include "Vector4.hpp"

namespace Math
{
	struct Matrix4
	{
	public:
		Matrix4(void);
		Matrix4(
			float v00, float v01, float v02, float v03,
			float v10, float v11, float v12, float v13,
			float v20, float v21, float v22, float v23,
			float v30, float v31, float v32, float v33
		);

		const Matrix4 operator*(const Matrix4& mat) const;
		const Vector3 operator*(const Vector3& v) const;
		const Vector4 operator*(const Vector4& v) const;

		static const Matrix4 Translate(const Vector3& v);
		static const Matrix4 Scale(const Vector3& v);

		static const Matrix4 RotateX(const float ang);
		static const Matrix4 RotateY(const float ang);
		static const Matrix4 RotateZ(const float ang);
		static const Matrix4 Rotate(const Vector3& axis, float ang);

		const Matrix4 Transpose(void) const;

		// The implementations of the functions below are based on the awesome
		// glMatrix library developed by Brandon Jones and Colin MacKenzie IV

		const float Determinant(void) const;
		const Matrix4 Inverse(void) const;

		static const Matrix4 Frustum(const float left, const float right, const float bottom, const float top, const float near, const float far);
		static const Matrix4 Perspective(const float fovy, const float aspect, const float near, const float far);
		static const Matrix4 Ortho(const float left, const float right, const float bottom, const float top, const float near, const float far);
		static const Matrix4 LookAt(const Vector3& eye, const Vector3& center, const Vector3& up);

		static const Vector3 UnProject(const Vector3& vec, const Matrix4& view, const Matrix4& proj, const float viewport[]);
		static const Vector3 Project(const Vector3& vec, const Matrix4& view, const Matrix4& proj, const float viewport[]);

		float m[16];
	};
}
