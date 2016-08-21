#pragma once

#include<exception>
#include<string>

class Exception : public std::exception
{
public:
	Exception(const std::string& m) : exception(), m_Message{ m } {}
	virtual ~Exception() throw() {}
	virtual const char* what() const throw() { return m_Message.c_str(); }

private:
	std::string m_Message;
};
