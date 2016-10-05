#pragma once

#include "Exception.hpp"

#include <string>

class OutOfRangeException : public Exception
{
public:
	OutOfRangeException(const std::string& m) : Exception(m) {}
	virtual ~OutOfRangeException() throw() {}
};