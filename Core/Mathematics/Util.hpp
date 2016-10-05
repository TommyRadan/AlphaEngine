#pragma once

namespace Math
{
	const float PI = 3.14159265358979323846f;

	inline float Rad( float degrees )
	{
		return degrees / 180.0f * PI;
	}

	inline float Deg( float radians )
	{
		return radians / PI * 180.0f;
	}
}
