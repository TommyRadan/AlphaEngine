#include "Vector3.hpp"
#include <cmath>

namespace Math
{
	Vector3& Vector3::operator+=( const Vector3& v )
	{
		X += v.X;
		Y += v.Y;
		Z += v.Z;
		return *this;
	}

	Vector3& Vector3::operator-=( const Vector3& v )
	{
		X -= v.X;
		Y -= v.Y;
		Z -= v.Z;
		return *this;
	}

	const Vector3 Vector3::operator+( const Vector3& v ) const
	{
		return Vector3( X + v.X, Y + v.Y, Z + v.Z );
	}

	const Vector3 Vector3::operator-( const Vector3& v ) const
	{
		return Vector3( X - v.X, Y - v.Y, Z - v.Z );
	}

	Vector3 operator*( const Vector3& v, float n )
	{
		return Vector3( v.X * n, v.Y * n, v.Z * n );
	}

	Vector3 operator*( float n, const Vector3& v )
	{
		return Vector3( v.X * n, v.Y * n, v.Z * n );
	}

	Vector3 operator/( const Vector3& v, float n )
	{
		return Vector3( v.X / n, v.Y / n, v.Z / n );
	}

	Vector3 operator/( float n, const Vector3& v )
	{
		return Vector3( v.X / n, v.Y / n, v.Z / n );
	}

	float& Vector3::operator[](const int i)
	{
		if(i < 0 || i > 2)
		{
			throw OutOfRangeException("Vector3 operator[] received bad index!");
		}

		if(i == 0) return X;
		else if(i == 1) return Y;
		else return Z;
	}

	const Vector3 Vector3::Cross( const Vector3& v ) const
	{
		return Vector3( Y*v.Z - Z*v.Y, Z*v.X - X*v.Z, X*v.Y - Y*v.X );
	}

	const float Vector3::Dot( const Vector3& v ) const
	{
		return X * v.X + Y * v.Y + Z * v.Z;
	}

	const float Vector3::Angle( const Vector3& v ) const
	{
		return acos( Dot( v ) / Length() / v.Length() );
	}

	const float Vector3::LengthSqr() const
	{
		return X*X + Y*Y + Z*Z;
	}

	const float Vector3::Length() const
	{
		return sqrt( X*X + Y*Y + Z*Z );
	}

	const float Vector3::Distance( const Vector3& v ) const
	{
		return ( *this - v ).Length();
	}

	const Vector3 Vector3::Normal() const
	{
		return *this / Length();
	}
}