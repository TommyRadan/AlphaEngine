#include "Vector2.hpp"
#include <cmath>

namespace Math
{
	Vector2& Vector2::operator+=( const Vector2& v )
	{
		X += v.X;
		Y += v.Y;
		return *this;
	}

	Vector2& Vector2::operator-=( const Vector2& v )
	{
		X -= v.X;
		Y -= v.Y;
		return *this;
	}

	const Vector2 Vector2::operator+( const Vector2& v ) const
	{
		return Vector2( X + v.X, Y + v.Y );
	}

	const Vector2 Vector2::operator-( const Vector2& v ) const
	{
		return Vector2( X - v.X, Y - v.Y );
	}

	Vector2 operator*( const Vector2& v, float n )
	{
		return Vector2( v.X * n, v.Y * n );
	}

	Vector2 operator*( float n, const Vector2& v )
	{
		return Vector2( v.X * n, v.Y * n );
	}

	Vector2 operator/( const Vector2& v, float n )
	{
		return Vector2( v.X / n, v.Y / n );
	}

	Vector2 operator/( float n, const Vector2& v )
	{
		return Vector2( v.X / n, v.Y / n );
	}

	const float Vector2::Dot( const Vector2& v ) const
	{
		return X * v.X + Y * v.Y;
	}

	const float Vector2::Angle( const Vector2& v ) const
	{
		return acos( Dot( v ) / Length() / v.Length() );
	}

	const float Vector2::LengthSqr() const
	{
		return X*X + Y*Y;
	}

	const float Vector2::Length() const
	{
		return sqrt( X*X + Y*Y );
	}

	const float Vector2::Distance( const Vector2& v ) const
	{
		return ( *this - v ).Length();
	}

	const Vector2 Vector2::Normal() const
	{
		return *this / Length();
	}
}
