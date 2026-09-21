#include "Version.h"

#include <cstring>
#include <stdexcept>

std::vector<unsigned char> Version::build(const Manifest& manifest)
{
	m_Data.clear();
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

	const auto root = begin(L"VS_VERSION_INFO", sizeof(fixed), 0);
	append(&fixed, sizeof(fixed));
	const auto strings = begin(L"StringFileInfo", 0, 1);
	const auto table = begin(L"040904B0", 0, 1);

	for (const auto& [key, value] : manifest.Strings)
	{
		const auto start = begin(key, static_cast<WORD>(value.size() + 1), 1);
		append(value.c_str(), (value.size() + 1) * sizeof(wchar_t));
		end(start);
	}

	end(table);
	end(strings);

	const auto variables = begin(L"VarFileInfo", 0, 1);
	const WORD translation[] = { 0x0409, 1200 };
	const auto value = begin(L"Translation", sizeof(translation), 0);
	append(translation, sizeof(translation));
	end(value);
	end(variables);
	end(root);
	return std::move(m_Data);
}

std::size_t Version::begin(const std::wstring& key, WORD length, WORD type)
{
	while (m_Data.size() % 4 != 0)
	{
		m_Data.push_back(0);
	}

	const auto start = m_Data.size();
	const WORD header[] = { 0, length, type };
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
	const auto size = m_Data.size() - start;
	if (size > 65535)
	{
		throw std::runtime_error("version metadata exceeds the Windows resource size limit");
	}

	const auto length = static_cast<WORD>(size);
	std::memcpy(m_Data.data() + start, &length, sizeof(length));
}

void Version::append(const void* data, std::size_t size)
{
	const auto bytes = static_cast<const unsigned char*>(data);
	m_Data.insert(m_Data.end(), bytes, bytes + size);
}
