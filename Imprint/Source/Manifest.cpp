#include "Manifest.h"

#include <Windows.h>
#include <yaml-cpp/yaml.h>

#include <fstream>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

static std::map<std::string, std::wstring> const g_Fields =
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

static std::wstring widen(std::string const& text)
{
	if (text.find('\0') != std::string::npos or text.size() > 32760)
	{
		throw std::runtime_error("YAML values must not contain NUL or exceed 32760 UTF-8 bytes");
	}

	if (text.empty())
	{
		return {};
	}

	int const n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0);

	if (n == 0)
	{
		throw std::runtime_error("YAML values must contain valid UTF-8");
	}

	std::wstring result(n, L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), result.data(), n);
	return result;
}

static std::array<std::uint16_t, 4> parseVersion(std::string const& text)
{
	std::array<std::uint16_t, 4> result{};
	std::size_t i = 0;
	std::size_t j = 0;

	while (i < text.size() and j < result.size())
	{
		int n = 0;
		std::size_t const start = i;

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

		result[j++] = (std::uint16_t)n;

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

Manifest Manifest::load(std::filesystem::path const& path)
{
	std::ifstream stream(path, std::ios::binary);

	if (not stream)
	{
		throw std::runtime_error("cannot open YAML manifest");
	}

	std::vector<YAML::Node> const documents = YAML::LoadAll(stream);

	if (documents.size() != 1 or not documents[0].IsMap() or documents[0].size() == 0)
	{
		throw std::runtime_error("manifest must contain one nonempty YAML mapping");
	}

	Manifest result;
	std::map<std::string, std::string> versions;
	std::set<std::string> keys;
	bool metadata = false;

	for (std::pair<YAML::Node, YAML::Node> const& item : documents[0])
	{
		if (not item.first.IsScalar() or not item.second.IsScalar())
		{
			throw std::runtime_error("manifest keys and values must be scalars");
		}

		std::string const key = item.first.as<std::string>();
		std::string const value = item.second.as<std::string>();

		if (not keys.insert(key).second)
		{
			throw std::runtime_error("duplicate YAML field: " + key);
		}

		std::map<std::string, std::wstring>::const_iterator const field = g_Fields.find(key);

		if (field != g_Fields.end())
		{
			result.Strings[field->second] = widen(value);
			metadata = true;
		}
		else if (key == "version" or key == "file_version" or key == "product_version")
		{
			parseVersion(value);
			versions[key] = value;
			metadata = true;
		}
		else if (key == "icon" or key == "application_manifest")
		{
			if (value.empty())
			{
				throw std::runtime_error(key + " must name a file");
			}

			std::filesystem::path const file = path.parent_path() / widen(value);

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

	std::string const common = versions.contains("version") ? versions.at("version") : "0.0.0.0";
	std::string const file = versions.contains("file_version") ? versions.at("file_version") : common;
	std::string const product = versions.contains("product_version") ? versions.at("product_version") : common;

	result.FileVersion = parseVersion(file);
	result.ProductVersion = parseVersion(product);

	if (metadata)
	{
		result.Strings[L"FileVersion"] = widen(file);
		result.Strings[L"ProductVersion"] = widen(product);
	}

	return result;
}
