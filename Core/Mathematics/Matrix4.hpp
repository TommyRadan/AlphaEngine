#pragma once

#include "Vec3.hpp"
#include "Vec4.hpp"

namespace Math
{
	struct Mat4
	{
	public:
		Mat4();
		Mat4(
			float v00, float v01, float v02, float v03,
			float v10, float v11, float v12, float v13,
			float v20, float v21, float v22, float v23,
			float v30, float v31, float v32, float v33
		);

		const Mat4 operator*( const Mat4& mat ) const;
		const Vec3 operator*( const Vec3& v ) const;
		const Vec4 operator*( const Vec4& v ) const;

		Mat4& Translate( const Vec3& v );
		Mat4& Scale( const Vec3& v );

		Mat4& RotateX( float ang );
		Mat4& RotateY( float ang );
		Mat4& RotateZ( float ang );
		Mat4& Rotate( const Vec3& axis, float ang );

		Mat4 Transpose() const;

		// The implementations of the functions below are based on the awesome
		// glMatrix library developed by Brandon Jones and Colin MacKenzie IV

		float Determinant() const;
		Mat4 Inverse() const;

		static Mat4 Frustum( float left, float right, float bottom, float top, float near, float far );
		static Mat4 Perspective( float fovy, float aspect, float near, float far );
		static Mat4 Ortho( float left, float right, float bottom, float top, float near, float far );
		static Mat4 LookAt( const Vec3& eye, const Vec3& center, const Vec3& up );

		static Vec3 UnProject( const Vec3& vec, const Mat4& view, const Mat4& proj, const float viewport[] );
		static Vec3 Project( const Vec3& vec, const Mat4& view, const Mat4& proj, const float viewport[] );
		
		float m[16];
	};
}
