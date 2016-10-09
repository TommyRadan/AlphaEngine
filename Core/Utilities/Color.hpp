#pragma once

struct Color
{
	Color(uint8_t r = 0x0, uint8_t g = 0x0, uint8_t b = 0x0, uint8_t a = 0xFF) :
		R{ r }, G{ g }, B{ b }, A{ a }
	{}

	uint8_t R, G, B, A;
};
