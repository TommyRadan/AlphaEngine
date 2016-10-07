#include "Matrix4.hpp"
#include <cmath>

namespace Math
{
	Matrix4::Matrix4(void)
	{
		*this = Matrix4(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		);
	}

	Matrix4::Matrix4(
		const float v00, const float v01, const float v02, const float v03,
		const float v10, const float v11, const float v12, const float v13,
		const float v20, const float v21, const float v22, const float v23,
		const float v30, const float v31, const float v32, const float v33
	) {
		m[0] = v00; m[4] = v01; m[8]  = v02; m[12] = v03;
		m[1] = v10; m[5] = v11; m[9]  = v12; m[13] = v13;
		m[2] = v20; m[6] = v21; m[10] = v22; m[14] = v23;
		m[3] = v30; m[7] = v31; m[11] = v32; m[15] = v33;
	}

	const Matrix4 Matrix4::operator*(const Matrix4& mat) const
	{
		return Matrix4(
			m[0]*mat.m[ 0] + m[4]*mat.m[ 1] + m[ 8]*mat.m[ 2] + m[12]*mat.m[ 3], 
			m[0]*mat.m[ 4] + m[4]*mat.m[ 5] + m[ 8]*mat.m[ 6] + m[12]*mat.m[ 7], 
			m[0]*mat.m[ 8] + m[4]*mat.m[ 9] + m[ 8]*mat.m[10] + m[12]*mat.m[11], 
			m[0]*mat.m[12] + m[4]*mat.m[13] + m[ 8]*mat.m[14] + m[12]*mat.m[15],
			m[1]*mat.m[ 0] + m[5]*mat.m[ 1] + m[ 9]*mat.m[ 2] + m[13]*mat.m[ 3], 
			m[1]*mat.m[ 4] + m[5]*mat.m[ 5] + m[ 9]*mat.m[ 6] + m[13]*mat.m[ 7], 
			m[1]*mat.m[ 8] + m[5]*mat.m[ 9] + m[ 9]*mat.m[10] + m[13]*mat.m[11], 
			m[1]*mat.m[12] + m[5]*mat.m[13] + m[ 9]*mat.m[14] + m[13]*mat.m[15],
			m[2]*mat.m[ 0] + m[6]*mat.m[ 1] + m[10]*mat.m[ 2] + m[14]*mat.m[ 3], 
			m[2]*mat.m[ 4] + m[6]*mat.m[ 5] + m[10]*mat.m[ 6] + m[14]*mat.m[ 7], 
			m[2]*mat.m[ 8] + m[6]*mat.m[ 9] + m[10]*mat.m[10] + m[14]*mat.m[11], 
			m[2]*mat.m[12] + m[6]*mat.m[13] + m[10]*mat.m[14] + m[14]*mat.m[15],
			m[3]*mat.m[ 0] + m[7]*mat.m[ 1] + m[11]*mat.m[ 2] + m[15]*mat.m[ 3], 
			m[3]*mat.m[ 4] + m[7]*mat.m[ 5] + m[11]*mat.m[ 6] + m[15]*mat.m[ 7], 
			m[3]*mat.m[ 8] + m[7]*mat.m[ 9] + m[11]*mat.m[10] + m[15]*mat.m[11], 
			m[3]*mat.m[12] + m[7]*mat.m[13] + m[11]*mat.m[14] + m[15]*mat.m[15]
		);
	}

	const Vector3 Matrix4::operator*(const Vector3& v) const
	{
		return Vector3(
			m[0]*v.X + m[4]*v.Y + m[8]*v.Z + m[12],
			m[1]*v.X + m[5]*v.Y + m[9]*v.Z + m[13],
			m[2]*v.X + m[6]*v.Y + m[10]*v.Z + m[14]
		);
	}

	const Vector4 Matrix4::operator*(const Vector4& v) const
	{
		return Vector4(
			m[0]*v.X + m[4]*v.Y + m[8]*v.Z + m[12]*v.W,
			m[1]*v.X + m[5]*v.Y + m[9]*v.Z + m[13]*v.W,
			m[2]*v.X + m[6]*v.Y + m[10]*v.Z + m[14]*v.W,
			m[3]*v.X + m[7]*v.Y + m[11]*v.Z + m[15]*v.W
		);
	}

	const Matrix4 Matrix4::Translate(const Vector3& v)
	{
		return Matrix4(
			1.0f, 0.0f, 0.0f, v.X,
			0.0f, 1.0f, 0.0f, v.Y,
			0.0f, 0.0f, 1.0f, v.Z,
			0.0f, 0.0f, 0.0f, 1.0f
		);
	}

	const Matrix4 Matrix4::Scale(const Vector3& v)
	{
		return Matrix4(
			v.X, 0.0f, 0.0f, 0.0f,
			0.0f, v.Y, 0.0f, 0.0f,
			0.0f, 0.0f, v.Z, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		);
	}

	const Matrix4 Matrix4::RotateX(const float ang)
	{
		return Matrix4(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, cos(ang), -sin(ang), 0.0f,
			0.0f, sin(ang), cos(ang), 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		);
	}

	const Matrix4 Matrix4::RotateY(const float ang)
	{
		return Matrix4(
			cos(ang), 0.0f, sin(ang), 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			-sin(ang), 0.0f, cos(ang), 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		);
	}

	const Matrix4 Matrix4::RotateZ(const float ang)
	{
		return Matrix4(
			cos(ang), -sin(ang), 0.0f, 0.0f,
			sin(ang), cos(ang), 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		);
	}

	const Matrix4 Matrix4::Rotate(const Vector3& axis, const float ang)
	{
		const float s = sin(ang);
		const float c = cos(ang);
		const float t = 1 - c;
		const Vector3 a = axis.Normal();

		return Matrix4(
			a.X * a.X * t + c, a.X * a.Y * t - a.Z * s, a.X * a.Z * t + a.Y * s, 0.0f,
			a.Y * a.X * t + a.Z * s, a.Y * a.Y * t + c, a.Y * a.Z * t - a.X * s, 0.0f,
			a.Z * a.X * t - a.Y * s, a.Z * a.Y * t + a.X * s, a.Z * a.Z * t + c, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		);
	}

	const Matrix4 Matrix4::Transpose(void) const
	{
		Matrix4 res;

		res.m[0] = m[0];
		res.m[1] = m[4];
		res.m[2] = m[8];
		res.m[3] = m[12];

		res.m[4] = m[1];
		res.m[5] = m[5];
		res.m[6] = m[9];
		res.m[7] = m[13];

		res.m[8] = m[2];
		res.m[9] = m[6];
		res.m[10] = m[10];
		res.m[11] = m[14];

		res.m[12] = m[3];
		res.m[13] = m[7];
		res.m[14] = m[11];
		res.m[15] = m[15];

		return res;
	}

	const float Matrix4::Determinant() const
	{
		return m[12] * m[9] * m[6] * m[3] - m[8] * m[13] * m[6] * m[3] - m[12] * m[5] * m[10] * m[3] + m[4] * m[13] * m[10] * m[3] +
               m[8] * m[5] * m[14] * m[3] - m[4] * m[9] * m[14] * m[3] - m[12] * m[9] * m[2] * m[7] + m[8] * m[13] * m[2] * m[7] +
               m[12] * m[1] * m[10] * m[7] - m[0] * m[13] * m[10] * m[7] - m[8] * m[1] * m[14] * m[7] + m[0] * m[9] * m[14] * m[7] +
               m[12] * m[5] * m[2] * m[11] - m[4] * m[13] * m[2] * m[11] - m[12] * m[1] * m[6] * m[11] + m[0] * m[13] * m[6] * m[11] +
               m[4] * m[1] * m[14] * m[11] - m[0] * m[5] * m[14] * m[11] - m[8] * m[5] * m[2] * m[15] + m[4] * m[9] * m[2] * m[15] +
               m[8] * m[1] * m[6] * m[15] - m[0] * m[9] * m[6] * m[15] - m[4] * m[1] * m[10] * m[15] + m[0] * m[5] * m[10] * m[15];
	}

	const Matrix4 Matrix4::Inverse() const
	{
		const float det = Determinant();

		Matrix4 res;

		const float t0 = m[0] * m[5] - m[1] * m[4];
        const float t1 = m[0] * m[6] - m[2] * m[4];
        const float t2 = m[0] * m[7] - m[3] * m[4];
        const float t3 = m[1] * m[6] - m[2] * m[5];
        const float t4 = m[1] * m[7] - m[3] * m[5];
        const float t5 = m[2] * m[7] - m[3] * m[6];
        const float t6 = m[8] * m[13] - m[9] * m[12];
        const float t7 = m[8] * m[14] - m[10] * m[12];
        const float t8 = m[8] * m[15] - m[11] * m[12];
        const float t9 = m[9] * m[14] - m[10] * m[13];
        const float t10 = m[9] * m[15] - m[11] * m[13];
        const float t11 = m[10] * m[15] - m[11] * m[14];

		res.m[0] = (m[5] * t11 - m[6] * t10 + m[7] * t9) / det;
        res.m[1] = (-m[1] * t11 + m[2] * t10 - m[3] * t9) / det;
        res.m[2] = (m[13] * t5 - m[14] * t4 + m[15] * t3) / det;
        res.m[3] = (-m[9] * t5 + m[10] * t4 - m[11] * t3) / det;
        res.m[4] = (-m[4] * t11 + m[6] * t8 - m[7] * t7) / det;
        res.m[5] = (m[0] * t11 - m[2] * t8 + m[3] * t7) / det;
        res.m[6] = (-m[12] * t5 + m[14] * t2 - m[15] * t1) / det;
        res.m[7] = (m[8] * t5 - m[10] * t2 + m[11] * t1) / det;
        res.m[8] = (m[4] * t10 - m[5] * t8 + m[7] * t6) / det;
        res.m[9] = (-m[0] * t10 + m[1] * t8 - m[3] * t6) / det;
        res.m[10] = (m[12] * t4 - m[13] * t2 + m[15] * t0) / det;
        res.m[11] = (-m[8] * t4 + m[9] * t2 - m[11] * t0) / det;
        res.m[12] = (-m[4] * t9 + m[5] * t7 - m[6] * t6) / det;
        res.m[13] = (m[0] * t9 - m[1] * t7 + m[2] * t6) / det;
        res.m[14] = (-m[12] * t3 + m[13] * t1 - m[14] * t0) / det;
        res.m[15] = (m[8] * t3 - m[9] * t1 + m[10] * t0) / det;

		return res;
	}

	const Matrix4 Matrix4::Frustum(float left, float right, float bottom, float top, float near, float far)
	{
		Matrix4 res;

		res.m[0] = near * 2.0f / (right - left);
		res.m[5] = near * 2.0f / (top - bottom);
		res.m[8] = (right + left) / (right - left);
		res.m[9] = (top + bottom) / (top - bottom);
		res.m[10] = (-far - near) / (far - near);
		res.m[11] = -1.0f;
		res.m[14] = -2.0f * far * near / (far - near);
		res.m[15] = 0.0f;

		return res;
	}

	const Matrix4 Matrix4::Perspective(const float fovy, const float aspect, const float near, const float far)
	{
		const float top = near * tan(fovy / 2.0f);
		const float right = top * aspect;
		return Frustum(-right, right, -top, top, near, far);
	}

	const Matrix4 Matrix4::Ortho(
		const float left, const float right, 
		const float bottom, const float top, 
		const float near, const float far 
	) {
		Matrix4 res;

		res.m[0] = 2.0f / (right - left);
		res.m[5] = 2.0f / (top - bottom);
		res.m[10] = -2.0f / (far - near);
		res.m[12] = -(left + right) / (right - left);
		res.m[13] = -(top + bottom) / (top - bottom);
		res.m[14] = -(far + near) / (far - near);

		return res;
	}

	const Matrix4 Matrix4::LookAt(const Vector3& eye, const Vector3& center, const Vector3& up)
	{
		Matrix4 res;

		Vector3 Z = (eye - center).Normal();

		Vector3 X = Vector3(
			up.Y * Z.Z - up.Z * Z.Y,
			up.Z * Z.X - up.X * Z.Z,
			up.X * Z.Y - up.Y * Z.X
		).Normal();

		Vector3 Y = Vector3(
			Z.Y * X.Z - Z.Z * X.Y,
			Z.Z * X.X - Z.X * X.Z,
			Z.X * X.Y - Z.Y * X.X
		).Normal();

		res.m[0] = X.X;
        res.m[1] = Y.X;
        res.m[2] = Z.X;
        res.m[4] = X.Y;
        res.m[5] = Y.Y;
        res.m[6] = Z.Y;
        res.m[8] = X.Z;
        res.m[9] = Y.Z;
        res.m[10] = Z.Z;
		res.m[12] = -X.Dot(eye);
		res.m[13] = -Y.Dot(eye);
		res.m[14] = -Z.Dot(eye);

		return res;
	}

	const Vector3 Matrix4::UnProject(const Vector3& vec, const Matrix4& view, const Matrix4& proj, const float viewport[])
	{
		Matrix4 inv = (proj * view).Inverse();
		Vector3 v(
			(vec.X - viewport[0]) * 2.0f / viewport[2] - 1.0f,
			(vec.Y - viewport[1]) * 2.0f / viewport[3] - 1.0f,
			2.0f * vec.Z - 1.0f
		);

		Vector3 res = inv * v;
		float w = inv.m[3] * v.X + inv.m[7] * v.Y + inv.m[11] * v.Z + inv.m[15];

		return res / w;
	}

	const Vector3 Matrix4::Project(const Vector3& vec, const Matrix4& view, const Matrix4& proj, const float viewport[])
	{
		Matrix4 trans = proj * view;
		Vector3 v = trans * vec;

		float w = trans.m[3] * vec.X + trans.m[7] * vec.Y + trans.m[11] * vec.Z + trans.m[15];
		v = v / w;

		return Vector3(
			viewport[0] + viewport[2] * (v.X + 1.0f) / 2.0f,
			viewport[1] + viewport[3] * (v.Y + 1.0f) / 2.0f,
			(v.Z + 1.0f) / 2.0f
		);
	}
}
