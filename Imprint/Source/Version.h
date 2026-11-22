#pragma once

#include "Manifest.h"

#include <Windows.h>
#include <vector>

class Version
{
public:
	std::vector<unsigned char> build(Manifest const& manifest);

private:
	std::size_t begin(std::wstring const& key, WORD length, WORD type);
	void end(std::size_t start);
	void append(void const* data, std::size_t size);

	std::vector<unsigned char> m_Data;
};
