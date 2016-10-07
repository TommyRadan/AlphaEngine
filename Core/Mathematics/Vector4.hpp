#pragma once

#include "Vector3.hpp"

#include <Utilities/Exception/OutOfRange.hpp>

namespace Math
{
	struct Vector4
	{
		Vector4( float x = 0.0f, float y = 0.0f, float z = 0.0f, float w = 1.0f ) : X( x ), Y( y ), Z( z ), W( w ) {}
		Vector4( const Vector3& v, float w = 1.0f ) : X( v.X ), Y( v.Y ), Z( v.Z ), W( w ) {}

		Vector4& operator+=( const Vector4& v );
		Vector4& operator-=( const Vector4& v );

		const Vector4 operator+( const Vector4& v ) const;
		const Vector4 operator-( const Vector4& v ) const;

		friend Vector4 operator*( const Vector4& v, float n );
		friend Vector4 operator*( float n, const Vector4& v );

		friend Vector4 operator/( const Vector4& v, float n );
		friend Vector4 operator/( float n, const Vector4& v );

		// Data
		float X, Y, Z, W;
	};
}
