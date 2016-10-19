#pragma once

struct Color
{
	Color(unsigned char r = 0x0, unsigned char g = 0x0, unsigned char b = 0x0, unsigned char a = 0xFF) :
		R{ r }, G{ g }, B{ b }, A{ a }
	{}

	unsigned char R, G, B, A;
};
