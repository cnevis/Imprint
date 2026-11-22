#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

struct Manifest
{
	static Manifest load(std::filesystem::path const& path);

	std::map<std::wstring, std::wstring> Strings;
	std::array<std::uint16_t, 4> FileVersion{};
	std::array<std::uint16_t, 4> ProductVersion{};
	std::filesystem::path Icon;
	std::filesystem::path ApplicationManifest;
};
