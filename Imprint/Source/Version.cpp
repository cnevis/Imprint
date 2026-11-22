#include "Version.h"

#include <cstring>
#include <stdexcept>
#include <utility>

std::vector<unsigned char> Version::build(Manifest const& manifest)
{
	VS_FIXEDFILEINFO fixed{};
	fixed.dwSignature = VS_FFI_SIGNATURE;
	fixed.dwStrucVersion = VS_FFI_STRUCVERSION;
	fixed.dwFileVersionMS = MAKELONG(manifest.FileVersion[1], manifest.FileVersion[0]);
	fixed.dwFileVersionLS = MAKELONG(manifest.FileVersion[3], manifest.FileVersion[2]);
	fixed.dwProductVersionMS = MAKELONG(manifest.ProductVersion[1], manifest.ProductVersion[0]);
	fixed.dwProductVersionLS = MAKELONG(manifest.ProductVersion[3], manifest.ProductVersion[2]);
	fixed.dwFileFlagsMask = VS_FFI_FILEFLAGSMASK;
	fixed.dwFileOS = VOS_NT_WINDOWS32;
	fixed.dwFileType = VFT_APP;

	m_Data.clear();

	std::size_t const info = begin(L"VS_VERSION_INFO", sizeof(fixed), 0);
	append(&fixed, sizeof(fixed));
	std::size_t const strings = begin(L"StringFileInfo", 0, 1);
	std::size_t const table = begin(L"040904B0", 0, 1);

	for (auto const& [key, value] : manifest.Strings)
	{
		std::size_t const start = begin(key, (WORD)(value.size() + 1), 1);
		append(value.c_str(), (value.size() + 1) * sizeof(wchar_t));
		end(start);
	}

	end(table);
	end(strings);

	WORD const translation[] = { MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), 1200 };
	std::size_t const variables = begin(L"VarFileInfo", 0, 1);
	std::size_t const var = begin(L"Translation", sizeof(translation), 0);
	append(translation, sizeof(translation));
	end(var);
	end(variables);
	end(info);
	return std::move(m_Data);
}

std::size_t Version::begin(std::wstring const& key, WORD length, WORD type)
{
	while (m_Data.size() % 4 != 0)
	{
		m_Data.push_back(0);
	}

	std::size_t const start = m_Data.size();
	WORD const header[] = { 0, length, type };
	append(header, sizeof(header));
	append(key.c_str(), (key.size() + 1) * sizeof(wchar_t));

	while (m_Data.size() % 4 != 0)
	{
		m_Data.push_back(0);
	}

	return start;
}

void Version::end(std::size_t start)
{
	std::size_t const size = m_Data.size() - start;

	if (size > 65535)
	{
		throw std::runtime_error("version metadata exceeds the Windows resource size limit");
	}

	WORD const length = (WORD)size;
	std::memcpy(m_Data.data() + start, &length, sizeof(length));
}

void Version::append(void const* data, std::size_t size)
{
	unsigned char const* bytes = static_cast<unsigned char const*>(data);
	m_Data.insert(m_Data.end(), bytes, bytes + size);
}
