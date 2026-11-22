#pragma once

#include "Manifest.h"

#include <Windows.h>

#include <filesystem>
#include <string>
#include <vector>

struct Resource
{
	LPCWSTR Type = nullptr;
	WORD Id = 0;
	WORD Language = 0;
	std::wstring Name;
};

struct Icon
{
	unsigned char Width;
	unsigned char Height;
	unsigned char Colors;
	unsigned char Reserved;
	WORD Planes;
	WORD Bits;
	DWORD Size;
	DWORD Offset;
};

class Executable
{
public:
	Executable(std::filesystem::path const& input, std::filesystem::path const& output);
	Executable(Executable const&) = delete;
	~Executable();

	Executable& operator=(Executable const&) = delete;

	void publish(Manifest const& manifest);

private:
	void copy();
	void collect(LPCWSTR type);
	void update(Resource const& resource, void const* data, DWORD size);
	void removeVariants();
	void writeIcons(std::vector<unsigned char> const& data, std::vector<Icon> const& icons);
	void commit();

	std::filesystem::path m_Input;
	std::filesystem::path m_Output;
	std::wstring m_Temp;
	HMODULE m_Module = nullptr;
	HANDLE m_Update = nullptr;
	std::vector<Resource> m_Resources;
};
