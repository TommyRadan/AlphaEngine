#pragma once

#include <string>
#include <fstream>
#include <streambuf>

#include "Exception.hpp"

static std::string FileToString(std::string& filename)
{
	std::ifstream file(filename);
	if (!file.is_open()) {
		throw Exception("Could not open file \"" + filename + "\"");
	}
	std::string string;

	file.seekg(0, std::ios::end);
	string.reserve(file.tellg());
	file.seekg(0, std::ios::beg);

	string.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	return string;
}
