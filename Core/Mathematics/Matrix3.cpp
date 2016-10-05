#include "Matrix3.hpp"

#include <cmath>

namespace Math
{
	Matrix3::Matrix3(void)
	{
		m[0] = 1.0f; m[3] = 0.0f; m[6] = 0.0f; 
		m[1] = 0.0f; m[4] = 1.0f; m[7] = 0.0f;
		m[2] = 0.0f; m[5] = 0.0f; m[8] = 1.0f;
	}

	Matrix3::Matrix3( 
		float v00, float v01, float v02, 
		float v10, float v11, float v12, 
		float v20, float v21, float v22 
	) {
		m[0] = v00; m[3] = v01; m[6] = v02; 
		m[1] = v10; m[4] = v11; m[7] = v12;
		m[2] = v20; m[5] = v21; m[8] = v22;
	}

	const Matrix3 Matrix3::operator*(const Matrix3& mat)
	{
		return Matrix3(
			mat.m[0]*m[0]+mat.m[1]*m[3]+mat.m[2]*m[6], 
			mat.m[3]*m[0]+mat.m[4]*m[3]+mat.m[5]*m[6], 
			mat.m[6]*m[0]+mat.m[7]*m[3]+mat.m[8]*m[6],
			mat.m[0]*m[1]+mat.m[1]*m[4]+mat.m[2]*m[7], 
			mat.m[3]*m[1]+mat.m[4]*m[4]+mat.m[5]*m[7], 
			mat.m[6]*m[1]+mat.m[7]*m[4]+mat.m[8]*m[7],
			mat.m[0]*m[2]+mat.m[1]*m[5]+mat.m[2]*m[8], 
			mat.m[3]*m[2]+mat.m[4]*m[5]+mat.m[5]*m[8], 
			mat.m[6]*m[2]+mat.m[7]*m[5]+mat.m[8]*m[8]
		);
	}

	const Vector2 Matrix3::operator*( const Vector2& v )
	{
		return Vector2(
			m[0]*v.X + m[3]*v.Y + m[6],
			m[1]*v.X + m[4]*v.Y + m[7]
		);
	}

	static Matrix3 Matrix3::Translate( const Vector2& v )
	{
		return Matrix3(
			1.0f, 0.0f, v.X,
			0.0f, 1.0f, v.Y,
			0.0f, 0.0f, 1.0f
		);
	}

	static Matrix3 Matrix3::Scale( const Vector2& v )
	{
		return Matrix3(
			 v.X, 0.0f, 0.0f,
			0.0f,  v.Y, 0.0f,
			0.0f, 0.0f, 1.0f
		);
	}

	static Matrix3 Matrix3::Rotation( float ang )
	{
		return Matrix3(
			cos( ang ), -sin( ang ), 0.0f,
			sin( ang ),  cos( ang ), 0.0f,
			0.0000000f,  0.0000000f, 1.0f
		);
	}

	const Matrix3 Matrix3::Transpose(void) const
	{
		return Matrix3(
			m[0],m[3],m[6],
			m[1],m[4],m[7],
			m[2],m[5],m[8]
		);
	}

	const float Matrix3::Determinant(void) const
	{
		return m[0] * (  m[8] * m[4] - m[5] * m[7] ) + 
			   m[1] * ( -m[8] * m[3] + m[5] * m[6] ) + 
			   m[2] * (  m[7] * m[3] - m[4] * m[6] );
	}

	const Matrix3 Matrix3::Inverse(void) const
	{
		const float det = Determinant();

		Matrix3 res;

		res.m[0] = (  m[8] * m[4] - m[5] * m[7] ) / det;
        res.m[1] = ( -m[8] * m[1] + m[2] * m[7] ) / det;
        res.m[2] = (  m[5] * m[1] - m[2] * m[4] ) / det;
        res.m[3] = ( -m[8] * m[3] + m[5] * m[6] ) / det;
        res.m[4] = (  m[8] * m[0] - m[2] * m[6] ) / det;
        res.m[5] = ( -m[5] * m[0] + m[2] * m[3] ) / det;
        res.m[6] = (  m[7] * m[3] - m[4] * m[6] ) / det;
        res.m[7] = ( -m[7] * m[0] + m[1] * m[6] ) / det;
        res.m[8] = (  m[4] * m[0] - m[1] * m[3] ) / det;

		return res;
	}
}
