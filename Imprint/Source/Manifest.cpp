#include "Manifest.h"

#include <Windows.h>
#include <yaml-cpp/yaml.h>

#include <fstream>
#include <set>
#include <stdexcept>

static std::wstring widen(const std::string& text)
{
	if (text.find('\0') != std::string::npos or text.size() > 32760)
	{
		throw std::runtime_error("YAML values must not contain NUL or exceed 32760 UTF-8 bytes");
	}

	if (text.empty())
	{
		return {};
	}

	const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
	if (n == 0)
	{
		throw std::runtime_error("YAML values must contain valid UTF-8");
	}

	std::wstring result(n, L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), n);
	return result;
}

static std::array<std::uint16_t, 4> parseVersion(const std::string& text)
{
	std::array<std::uint16_t, 4> result{};
	std::size_t i = 0;
	std::size_t j = 0;
	while (i < text.size() and j < result.size())
	{
		int n = 0;
		const std::size_t start = i;
		while (i < text.size() and text[i] >= '0' and text[i] <= '9')
		{
			n = n * 10 + text[i] - '0';

			if (n > 65535)
			{
				throw std::runtime_error("version components must be between 0 and 65535");
			}

			++i;
		}

		if (i == start)
		{
			throw std::runtime_error("versions require 1-4 decimal components separated by dots");
		}

		result[j++] = static_cast<std::uint16_t>(n);

		if (i == text.size())
		{
			return result;
		}

		if (text[i++] != '.' or i == text.size())
		{
			break;
		}
	}

	throw std::runtime_error("versions require 1-4 decimal components separated by dots");
}

Manifest Manifest::load(const std::filesystem::path& path)
{
	std::ifstream stream(path, std::ios::binary);
	if (not stream)
	{
		throw std::runtime_error("cannot open YAML manifest");
	}

	const auto documents = YAML::LoadAll(stream);
	if (documents.size() != 1 or not documents[0].IsMap() or documents[0].size() == 0)
	{
		throw std::runtime_error("manifest must contain one nonempty YAML mapping");
	}

	const std::map<std::string, std::wstring> fields =
	{
		{"author", L"Author"},
		{"company", L"CompanyName"},
		{"name", L"ProductName"},
		{"description", L"FileDescription"},
		{"internal_name", L"InternalName"},
		{"original_filename", L"OriginalFilename"},
		{"copyright", L"LegalCopyright"},
		{"trademarks", L"LegalTrademarks"},
		{"comments", L"Comments"}
	};

	Manifest result;

	std::map<std::string, std::string> versions;
	std::set<std::string> keys;

	for (const auto& item : documents[0])
	{
		if (not item.first.IsScalar() or not item.second.IsScalar())
		{
			throw std::runtime_error("manifest keys and values must be scalars");
		}

		const auto key = item.first.as<std::string>();
		const auto value = item.second.as<std::string>();

		if (not keys.insert(key).second)
		{
			throw std::runtime_error("duplicate YAML field: " + key);
		}

		const auto field = fields.find(key);

		if (field != fields.end())
		{
			result.Strings[field->second] = widen(value);
			result.HasMetadata = true;
		}
		else if (key == "version" or key == "file_version" or key == "product_version")
		{
			parseVersion(value);
			versions[key] = value;
			result.HasMetadata = true;
		}
		else if (key == "icon" or key == "application_manifest")
		{
			if (value.empty())
			{
				throw std::runtime_error(key + " must name a file");
			}

			const auto file = path.parent_path() / widen(value);
			if (key == "icon")
			{
				result.Icon = file;
			}
			else
			{
				result.ApplicationManifest = file;
			}
		}
		else
		{
			throw std::runtime_error("unknown YAML field: " + key);
		}
	}

	if (keys.contains("author") and not keys.contains("company"))
	{
		result.Strings[L"CompanyName"] = result.Strings[L"Author"];
	}

	const auto common = versions.contains("version") ? versions.at("version") : "0.0.0.0";
	const auto file = versions.contains("file_version") ? versions.at("file_version") : common;
	const auto product = versions.contains("product_version") ? versions.at("product_version") : common;

	result.FileVersion = parseVersion(file);
	result.ProductVersion = parseVersion(product);

	if (result.HasMetadata)
	{
		result.Strings[L"FileVersion"] = widen(file);
		result.Strings[L"ProductVersion"] = widen(product);
	}

	return result;
}
