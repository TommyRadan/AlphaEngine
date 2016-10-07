#pragma once

#include <Utilities/Exceptions/OutOfRange.hpp>

namespace Math
{
	struct Vector3
	{
		Vector3( float x = 0.0f, float y = 0.0f, float z = 0.0f ) : X( x ), Y( y ), Z( z ) {}

		Vector3& operator+=( const Vector3& v );
		Vector3& operator-=( const Vector3& v );

		const Vector3 operator+( const Vector3& v ) const;
		const Vector3 operator-( const Vector3& v ) const;

		friend Vector3 operator*( const Vector3& v, float n );
		friend Vector3 operator*( float n, const Vector3& v );

		friend Vector3 operator/( const Vector3& v, float n );
		friend Vector3 operator/( float n, const Vector3& v );

		const Vector3 Cross( const Vector3& v ) const;

		const float Dot( const Vector3& v ) const;
		const float Angle( const Vector3& v ) const;

		const float LengthSqr(void) const;
		const float Length(void) const;
		const float Distance( const Vector3& v ) const;

		const Vector3 Normal(void) const;

		// Data
		float X, Y, Z;
	};
}
