#include "Executable.h"
#include "Version.h"

#include <Windows.h>
#include <Shlwapi.h>
#include <xmllite.h>
#include <wrl/client.h>

#include <cstring>
#include <cassert>
#include <exception>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

struct Resource
{
	WORD Type = 0;
	WORD Id = 0;
	WORD Language = 0;
	bool IsInteger = true;
	std::wstring Name;
};

struct Resources
{
	std::vector<Resource> Items;
	std::exception_ptr Error;
	DWORD Code = ERROR_SUCCESS;
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

static BOOL CALLBACK collectLanguage(HMODULE, LPCWSTR type, LPCWSTR name, WORD language, LONG_PTR parameter)
{
	auto* resources = reinterpret_cast<Resources*>(parameter);

	try
	{
		Resource resource;
		resource.Type = static_cast<WORD>(reinterpret_cast<ULONG_PTR>(type));
		resource.Language = language;
		resource.IsInteger = IS_INTRESOURCE(name);

		if (resource.IsInteger)
		{
			resource.Id = static_cast<WORD>(reinterpret_cast<ULONG_PTR>(name));
		}
		else
		{
			resource.Name = name;
		}

		resources->Items.push_back(std::move(resource));
		return TRUE;
	}
	catch (...)
	{
		resources->Error = std::current_exception();
		return FALSE;
	}
}

static BOOL CALLBACK collectName(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR parameter)
{
	if (not EnumResourceLanguagesW(module, type, name, collectLanguage, parameter))
	{
		auto* resources = reinterpret_cast<Resources*>(parameter);
		resources->Code = GetLastError();
		return FALSE;
	}

	return TRUE;
}

static void collect(HMODULE module, LPCWSTR type, Resources* resources)
{
	assert(resources != nullptr);

	if (not EnumResourceNamesW(module, type, collectName, reinterpret_cast<LONG_PTR>(resources)))
	{
		const auto code = GetLastError();

		if (resources->Error)
		{
			std::rethrow_exception(resources->Error);
		}

		if (resources->Code != ERROR_SUCCESS)
		{
			throw std::system_error(resources->Code, std::system_category(), "cannot enumerate resource languages");
		}

		if (code != ERROR_RESOURCE_TYPE_NOT_FOUND and code != ERROR_RESOURCE_DATA_NOT_FOUND)
		{
			throw std::system_error(code, std::system_category(), "cannot enumerate resources");
		}
	}
}

static std::vector<unsigned char> read(const std::filesystem::path& path)
{
	std::ifstream stream(path, std::ios::binary | std::ios::ate);

	if (not stream)
	{
		throw std::runtime_error("cannot open resource file: " + path.string());
	}

	const auto size = stream.tellg();
	if (size <= 0 or size > 64 * 1024 * 1024)
	{
		throw std::runtime_error("resource file must contain between 1 byte and 64 MiB");
	}

	std::vector<unsigned char> data(static_cast<std::size_t>(size));
	stream.seekg(0);

	if (not stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size())))
	{
		throw std::runtime_error("cannot read resource file");
	}

	return data;
}

static std::vector<Icon> parseIcon(const std::vector<unsigned char>& data)
{
	WORD header[3]{};
	if (data.size() < sizeof(header))
	{
		throw std::runtime_error("ICO header is truncated");
	}

	std::memcpy(header, data.data(), sizeof(header));
	if (header[0] != 0 or header[1] != 1 or header[2] == 0)
	{
		throw std::runtime_error("icon must be a Windows ICO file");
	}

	const std::size_t start = sizeof(header) + static_cast<std::size_t>(header[2]) * 16;
	if (start > data.size())
	{
		throw std::runtime_error("ICO directory is truncated");
	}

	std::vector<Icon> icons;
	for (std::size_t i = 0; i < header[2]; ++i)
	{
		Icon icon{};
		std::memcpy(&icon, data.data() + sizeof(header) + i * 16, 16);
		if (icon.Reserved != 0
			or icon.Size == 0
			or icon.Offset < start
			or icon.Offset > data.size()
			or icon.Size > data.size() - icon.Offset)
		{
			throw std::runtime_error("ICO image has an invalid offset or size");
		}

		icons.push_back(icon);
	}
	return icons;
}

static void validateXml(const std::vector<unsigned char>& data)
{
	Microsoft::WRL::ComPtr<IStream> stream;
	stream.Attach(SHCreateMemStream(data.data(), static_cast<UINT>(data.size())));
	Microsoft::WRL::ComPtr<IXmlReader> reader;

	if (not stream
		or FAILED(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(reader.GetAddressOf()), nullptr))
		or FAILED(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit))
		or FAILED(reader->SetInput(stream.Get())))
	{
		throw std::runtime_error("cannot initialize application manifest XML reader");
	}

	bool root = false;
	XmlNodeType type;
	HRESULT status;

	while ((status = reader->Read(&type)) == S_OK)
	{
		if (type == XmlNodeType_Element and not root)
		{
			const wchar_t* name = nullptr;
			const wchar_t* uri = nullptr;

			if (FAILED(reader->GetLocalName(&name, nullptr))
				or FAILED(reader->GetNamespaceUri(&uri, nullptr))
				or name == nullptr
				or uri == nullptr
				or std::wstring_view(name) != L"assembly"
				or std::wstring_view(uri) != L"urn:schemas-microsoft-com:asm.v1")
			{
				throw std::runtime_error("application manifest requires an assembly root in urn:schemas-microsoft-com:asm.v1");
			}

			root = true;
		}
	}

	if (FAILED(status) or not root)
	{
		throw std::runtime_error("application manifest is not well-formed XML");
	}
}

static void update(HANDLE handle, const Resource& resource, void* data, DWORD size)
{
	const auto name = resource.IsInteger ? MAKEINTRESOURCEW(resource.Id) : resource.Name.c_str();
	if (not UpdateResourceW(handle, MAKEINTRESOURCEW(resource.Type), name, resource.Language, data, size))
	{
		throw std::system_error(GetLastError(), std::system_category(), "cannot update executable resource");
	}
}

void publish(const std::filesystem::path& input, const std::filesystem::path& output, const Manifest& manifest)
{
	if (not std::filesystem::is_regular_file(input))
	{
		throw std::runtime_error("input executable is not a regular file");
	}

	if (std::filesystem::exists(output))
	{
		throw std::runtime_error("output already exists; choose a new output path");
	}

	std::vector<unsigned char> version;
	if (manifest.HasMetadata)
	{
		Version writer;
		version = writer.build(manifest);
	}

	std::vector<unsigned char> iconData;
	std::vector<Icon> icons;
	if (not manifest.Icon.empty())
	{
		iconData = read(manifest.Icon);
		icons = parseIcon(iconData);
	}

	std::vector<unsigned char> xml;
	if (not manifest.ApplicationManifest.empty())
	{
		xml = read(manifest.ApplicationManifest);
		validateXml(xml);
	}

	wchar_t temporary[MAX_PATH]{};
	if (not GetTempFileNameW(output.parent_path().c_str(), L"pub", 0, temporary))
	{
		throw std::system_error(GetLastError(), std::system_category(), "cannot create temporary output file");
	}

	HMODULE module = nullptr;
	HANDLE handle = nullptr;
	try
	{
		if (not CopyFileW(input.c_str(), temporary, FALSE))
		{
			throw std::system_error(GetLastError(), std::system_category(), "cannot copy input executable");
		}

		const auto attributes = GetFileAttributesW(temporary);
		if (attributes == INVALID_FILE_ATTRIBUTES
			or not SetFileAttributesW(temporary, attributes & ~FILE_ATTRIBUTE_READONLY))
		{
			throw std::system_error(GetLastError(), std::system_category(), "cannot make temporary output writable");
		}

		module = LoadLibraryExW(temporary, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
		if (not module)
		{
			throw std::system_error(GetLastError(), std::system_category(), "input is not a readable Windows executable");
		}

		Resources resources;
		if (manifest.HasMetadata)
		{
			collect(module, RT_VERSION, &resources);
		}
		if (not xml.empty())
		{
			collect(module, RT_MANIFEST, &resources);
		}
		if (not icons.empty())
		{
			collect(module, RT_GROUP_ICON, &resources);
			collect(module, RT_ICON, &resources);
		}

		FreeLibrary(module);
		module = nullptr;
		handle = BeginUpdateResourceW(temporary, FALSE);
		if (not handle)
		{
			throw std::system_error(GetLastError(), std::system_category(), "cannot begin resource update");
		}

		for (const auto& resource : resources.Items)
		{
			if (resource.Type == 16 and not (resource.IsInteger and resource.Id == 1 and resource.Language == 0x0409))
			{
				update(handle, resource, nullptr, 0);
			}

			if (resource.Type == 24
				and resource.IsInteger
				and resource.Id == 1
				and resource.Language != 0)
			{
				update(handle, resource, nullptr, 0);
			}
		}

		if (not version.empty())
		{
			update(handle, Resource{ 16, 1, 0x0409 }, version.data(), static_cast<DWORD>(version.size()));
		}
		if (not xml.empty())
		{
			update(handle, Resource{ 24, 1, 0 }, xml.data(), static_cast<DWORD>(xml.size()));
		}

		if (not icons.empty())
		{
			std::set<WORD> used;
			std::vector<Resource> groups;
			for (const auto& resource : resources.Items)
			{
				if (resource.Type == 3 and resource.IsInteger)
				{
					used.insert(resource.Id);
				}

				if (resource.Type == 14)
				{
					if (groups.empty() or (resource.IsInteger == groups[0].IsInteger
						and resource.Id == groups[0].Id
						and resource.Name == groups[0].Name))
					{
						groups.push_back(resource);
					}
				}
			}

			if (groups.empty())
			{
				groups.push_back(Resource{ 14, 1, 0 });
			}

			std::vector<unsigned char> directory(6 + icons.size() * 14);
			const WORD header[] = { 0, 1, static_cast<WORD>(icons.size()) };
			std::memcpy(directory.data(), header, sizeof(header));
			int id = 1;

			for (std::size_t i = 0; i < icons.size(); ++i)
			{
				while (id <= 65535 and used.contains(static_cast<WORD>(id)))
				{
					++id;
				}

				if (id > 65535)
				{
					throw std::runtime_error("executable has no free icon resource IDs");
				}

				const auto& icon = icons[i];
				const auto number = static_cast<WORD>(id++);

				std::memcpy(directory.data() + 6 + i * 14, &icon, 12);
				std::memcpy(directory.data() + 6 + i * 14 + 12, &number, sizeof(number));

				for (const auto& group : groups)
				{
					update(handle, Resource{ 3, number, group.Language }, iconData.data() + icon.Offset, icon.Size);
				}
			}

			for (const auto& group : groups)
			{
				update(handle, group, directory.data(), static_cast<DWORD>(directory.size()));
			}
		}

		const auto pending = handle;
		handle = nullptr;
		if (not EndUpdateResourceW(pending, FALSE))
		{
			throw std::system_error(GetLastError(), std::system_category(), "cannot write executable resources");
		}

		if (not MoveFileExW(temporary, output.c_str(), MOVEFILE_WRITE_THROUGH))
		{
			throw std::system_error(GetLastError(), std::system_category(), "cannot publish output executable");
		}
	}
	catch (...)
	{
		if (module)
		{
			FreeLibrary(module);
		}

		if (handle)
		{
			EndUpdateResourceW(handle, TRUE);
		}

		DeleteFileW(temporary);
		throw;
	}
}
