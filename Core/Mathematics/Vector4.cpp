#include "Vector4.hpp"
#include <cmath>

namespace Math
{
	float DotProduct(const Vector4& v1, const Vector4& v2)
	{
		return v1.X * v2.X + v1.Y * v2.Y + v1.Z * v2.Z + v1.W * v2.W;
	}

	Vector4& Vector4::operator+=(const Vector4& v)
	{
		X += v.X;
		Y += v.Y;
		Z += v.Z;
		W += v.W;
		return *this;
	}

	Vector4& Vector4::operator-=(const Vector4& v)
	{
		X -= v.X;
		Y -= v.Y;
		Z -= v.Z;
		W -= v.W;
		return *this;
	}

	const Vector4 Vector4::operator+(const Vector4& v) const
	{
		return Vector4(X + v.X, Y + v.Y, Z + v.Z, W + v.W);
	}

	const Vector4 Vector4::operator-(const Vector4& v) const
	{
		return Vector4(X - v.X, Y - v.Y, Z - v.Z, W - v.W);
	}

	const Vector4 Vector4::operator/(const Vector4& v) const {
		return *this / v.GetVectorLength();
	}

	Vector4& Vector4::operator/=(const Vector4& v)
	{
		float l = v.GetVectorLength();

		X /= l;
		Y /= l;
		Z /= l;
		W /= l;

		return *this;
	}

	Vector4 operator+(const Vector4& v, float n)
	{
		return Vector4(v.X + n, v.Y + n, v.Z + n, v.W + n);
	}

	Vector4 operator-(const Vector4& v, float n)
	{
		return Vector4(v.X - n, v.Y - n, v.Z - n, v.W - n);
	}

	Vector4 operator+(float n, const Vector4& v)
	{
		return Vector4(n + v.X, n + v.Y, n + v.Z, n + v.W);
	}

	Vector4 operator-(float n, const Vector4& v)
	{
		return Vector4(n - v.X, n - v.Y, n - v.Z, n - v.W);
	}

	Vector4& Vector4::operator+=(float n)
	{
		X += n;
		Y += n;
		Z += n;
		W += n;
		return *this;
	}

	Vector4& Vector4::operator-=(float n)
	{
		X -= n;
		Y -= n;
		Z -= n;
		W -= n;
		return *this;
	}

	Vector4 operator*(const Vector4& v, float n)
	{
		return Vector4(v.X * n, v.Y * n, v.Z * n, v.W * n);
	}

	Vector4 operator/(const Vector4& v, float n)
	{
		return Vector4(v.X / n, v.Y / n, v.Z / n, v.W / n);
	}

	Vector4 operator*(float n, const Vector4& v)
	{
		return Vector4(n * v.X, n * v.Y, n * v.Z, n * v.W);
	}

	Vector4 operator/(float n, const Vector4& v)
	{
		return Vector4(n / v.X, n / v.Y, n / v.Z, n / v.W);
	}

	Vector4& Vector4::operator*=(float n)
	{
		X *= n;
		Y *= n;
		Z *= n;
		W *= n;
		return *this;
	}

	Vector4& Vector4::operator/=(float n)
	{
		X /= n;
		Y /= n;
		Z /= n;
		W /= n;
		return *this;
	}

	bool Vector4::operator==(const Vector4& v)
	{
		if (X == v.X && Y == v.Y && Z == v.Z && W == v.W)
			return true;
		return false;
	}

	bool Vector4::operator!=(const Vector4& v)
	{
		if (X == v.X && Y == v.Y && Z == v.Z && W == v.W)
			return false;
		return true;
	}
}
