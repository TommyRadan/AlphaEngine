#pragma once

#include <vector>
#include <climits>

struct ByteReader
{
	ByteReader(const unsigned int length, const bool littleEndian) :
		buffer{ new unsigned char[length] },
		length{ length },
		ptr{ 0u },
		littleEndian{ littleEndian }
	{}
	~ByteReader(void) { delete [] buffer; }

	void Reuse(const unsigned int length)
	{
		delete [] buffer;
		buffer = new unsigned char[length];
		this->length = length;
		ptr = 0u;
	}

	unsigned char* Data(void) const { return buffer; }
	const unsigned char Length(void) const { return length; }

	void Advance(const unsigned int count) { ptr += count; }
	void Move(const unsigned int location) { ptr = location; }

	const bool Compare(const unsigned int location, const unsigned int length, const unsigned char* data) const
	{
		if ( location + length > this->length ) return false;
			
		for (unsigned int i = 0; i < length; i++) {
			if (buffer[location + i] != data[i]) return false;
		}
			
		return true;
	}

	const unsigned char PeekByte(const unsigned int offset = 0u) const 
	{ 
		return buffer[ptr+offset]; 
	}

	const unsigned char ReadUbyte(void)
	{
		return buffer[ptr++];
	}

	const unsigned short ReadUshort(void)
	{
		unsigned short val;
		if (littleEndian) {
			val = buffer[ptr + 0] + (buffer[ptr + 1] << 8);
		} else {
			val = buffer[ptr + 1] + (buffer[ptr + 0] << 8);
		}
		ptr += 2;
		return val;
	}

	const unsigned int ReadUint(void)
	{
		unsigned int val;
		if (littleEndian) {
			val = buffer[ptr + 0] + (buffer[ptr + 1] << 8) + (buffer[ptr + 2] << 16) + (buffer[ptr + 3] << 24);
		} else {
			val = buffer[ptr + 3] + (buffer[ptr + 2] << 8) + (buffer[ptr + 1] << 16) + (buffer[ptr + 0] << 24);
		}
		ptr += 4;
		return val;
	}

	const int ReadInt(void)
	{
		unsigned int val = ReadUint();
		if ( val <= INT_MAX )
			return val;
		else
			return INT_MIN + val - INT_MAX - 1;
	}

	void Read(unsigned char* const dest,const unsigned int length )
	{
		for (unsigned int i = 0; i < length; i++) {
			dest[i] = buffer[ptr + i];
		}
		ptr += length;
	}

private:
	unsigned char* buffer;
	unsigned int length;
	unsigned int ptr;
	bool littleEndian;

	ByteReader( const ByteReader& );
	const ByteReader& operator=( const ByteReader& );
};

struct ByteWriter
{
	ByteWriter(const bool littleEndian) : 
		littleEndian{ littleEndian }
	{}

	unsigned char* Data(void) { return buffer.data(); }
	unsigned int Length(void) const { return (unsigned int)buffer.size(); }

	void Pad(const unsigned int count)
	{
		for (unsigned int i = 0; i < count; i++) {
			buffer.push_back(0x00);
		}
	}

	void WriteUbyte(const unsigned char val)
	{
		buffer.push_back(val);
	}
	
	void WriteUshort(const unsigned short val)
	{
		if ( littleEndian ) {
			buffer.push_back( val & 0x00FF );
			buffer.push_back( ( val & 0xFF00 ) >> 8 );
		} else {
			buffer.push_back( ( val & 0xFF00 ) >> 8 );
			buffer.push_back( val & 0x00FF );
		}
	}

	void WriteUint(const unsigned int val)
	{
		if ( littleEndian ) {
			buffer.push_back( (unsigned char)(   val & 0x000000FF ) );
			buffer.push_back( (unsigned char)( ( val & 0x0000FF00 ) >> 8 ) );
			buffer.push_back( (unsigned char)( ( val & 0x00FF0000 ) >> 16 ) );
			buffer.push_back( (unsigned char)( ( val & 0xFF000000 ) >> 24 ) );
		} else {
			buffer.push_back( (unsigned char)( ( val & 0xFF000000 ) >> 24 ) );
			buffer.push_back( (unsigned char)( ( val & 0x00FF0000 ) >> 16 ) );
			buffer.push_back( (unsigned char)( ( val & 0x0000FF00 ) >> 8 ) );
			buffer.push_back( (unsigned char)(   val & 0x000000FF ) );
		}
	}

	void WriteString(const char* str)
	{
		while (*str) {
			buffer.push_back(*(str++));
		}
		buffer.push_back( 0 );
	}

private:
	std::vector<unsigned char> buffer;
	bool littleEndian;

	ByteWriter( const ByteWriter& );
	const ByteWriter& operator=( const ByteWriter& );
};
