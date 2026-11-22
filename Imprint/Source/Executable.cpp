#include "Executable.h"
#include "Version.h"

#include <Shlwapi.h>
#include <xmllite.h>
#include <wrl/client.h>

#include <cstring>
#include <exception>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <system_error>

#pragma pack(push, 2)
struct GroupIcon
{
	unsigned char Width;
	unsigned char Height;
	unsigned char Colors;
	unsigned char Reserved;
	WORD Planes;
	WORD Bits;
	DWORD Size;
	WORD Id;
};
#pragma pack(pop)

struct Enumeration
{
	std::vector<Resource>* Items = nullptr;
	std::exception_ptr Error;
	DWORD Code = ERROR_SUCCESS;
};

static BOOL CALLBACK collectLanguage(HMODULE, LPCWSTR type, LPCWSTR name, WORD language, LONG_PTR param)
{
	Enumeration* en = reinterpret_cast<Enumeration*>(param);

	try
	{
		Resource r;
		r.Type = type;
		r.Language = language;

		if (IS_INTRESOURCE(name))
		{
			r.Id = (WORD)reinterpret_cast<ULONG_PTR>(name);
		}
		else
		{
			r.Name = name;
		}

		en->Items->push_back(std::move(r));
		return TRUE;
	}
	catch (...)
	{
		en->Error = std::current_exception();
		return FALSE;
	}
}

static BOOL CALLBACK collectName(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param)
{
	if (not EnumResourceLanguagesW(module, type, name, collectLanguage, param))
	{
		Enumeration* en = reinterpret_cast<Enumeration*>(param);
		en->Code = GetLastError();
		return FALSE;
	}

	return TRUE;
}

static std::vector<unsigned char> read(std::filesystem::path const& path)
{
	std::ifstream stream(path, std::ios::binary | std::ios::ate);

	if (not stream)
	{
		throw std::runtime_error("cannot open resource file: " + path.string());
	}

	std::streamoff const size = stream.tellg();

	if (size <= 0 or size > 64 * 1024 * 1024)
	{
		throw std::runtime_error("resource file must contain between 1 byte and 64 MiB");
	}

	std::vector<unsigned char> data((std::size_t)size);
	stream.seekg(0);

	if (not stream.read(reinterpret_cast<char*>(data.data()), (std::streamsize)data.size()))
	{
		throw std::runtime_error("cannot read resource file");
	}

	return data;
}

static std::vector<Icon> parseIcon(std::vector<unsigned char> const& data)
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

	std::size_t const start = sizeof(header) + (std::size_t)header[2] * sizeof(Icon);

	if (start > data.size())
	{
		throw std::runtime_error("ICO directory is truncated");
	}

	std::vector<Icon> icons;

	for (std::size_t i = 0; i < header[2]; ++i)
	{
		Icon icon{};
		std::memcpy(&icon, data.data() + sizeof(header) + i * sizeof(Icon), sizeof(Icon));

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

static void validateXml(std::vector<unsigned char> const& data)
{
	Microsoft::WRL::ComPtr<IStream> stream;
	Microsoft::WRL::ComPtr<IXmlReader> reader;
	stream.Attach(SHCreateMemStream(data.data(), (UINT)data.size()));

	if (not stream
		or FAILED(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(reader.GetAddressOf()), nullptr))
		or FAILED(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit))
		or FAILED(reader->SetInput(stream.Get())))
	{
		throw std::runtime_error("cannot initialize application manifest XML reader");
	}

	bool assembly = false;
	XmlNodeType type;
	HRESULT status;

	while ((status = reader->Read(&type)) == S_OK)
	{
		if (type == XmlNodeType_Element and not assembly)
		{
			wchar_t const* name = nullptr;
			wchar_t const* uri = nullptr;

			if (FAILED(reader->GetLocalName(&name, nullptr))
				or FAILED(reader->GetNamespaceUri(&uri, nullptr))
				or name == nullptr
				or uri == nullptr
				or std::wstring_view(name) != L"assembly"
				or std::wstring_view(uri) != L"urn:schemas-microsoft-com:asm.v1")
			{
				throw std::runtime_error("application manifest requires an assembly root in urn:schemas-microsoft-com:asm.v1");
			}

			assembly = true;
		}
	}

	if (FAILED(status) or not assembly)
	{
		throw std::runtime_error("application manifest is not well-formed XML");
	}
}

Executable::Executable(std::filesystem::path const& input, std::filesystem::path const& output)
	: m_Input(input)
	, m_Output(output)
{
	if (not std::filesystem::is_regular_file(input))
	{
		throw std::runtime_error("input executable is not a regular file");
	}

	if (std::filesystem::exists(output))
	{
		throw std::runtime_error("output already exists; choose a new output path");
	}
}

Executable::~Executable()
{
	if (m_Module)
	{
		FreeLibrary(m_Module);
	}

	if (m_Update)
	{
		EndUpdateResourceW(m_Update, TRUE);
	}

	if (not m_Temp.empty())
	{
		DeleteFileW(m_Temp.c_str());
	}
}

void Executable::publish(Manifest const& manifest)
{
	std::vector<unsigned char> version;
	std::vector<unsigned char> iconData;
	std::vector<Icon> icons;
	std::vector<unsigned char> xml;

	if (not manifest.Strings.empty())
	{
		Version v;
		version = v.build(manifest);
	}

	if (not manifest.Icon.empty())
	{
		iconData = read(manifest.Icon);
		icons = parseIcon(iconData);
	}

	if (not manifest.ApplicationManifest.empty())
	{
		xml = read(manifest.ApplicationManifest);
		validateXml(xml);
	}

	copy();
	m_Module = LoadLibraryExW(m_Temp.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);

	if (not m_Module)
	{
		throw std::system_error(GetLastError(), std::system_category(), "input is not a readable Windows executable");
	}

	if (not version.empty())
	{
		collect(RT_VERSION);
	}

	if (not xml.empty())
	{
		collect(RT_MANIFEST);
	}

	if (not icons.empty())
	{
		collect(RT_GROUP_ICON);
		collect(RT_ICON);
	}

	FreeLibrary(m_Module);
	m_Module = nullptr;
	m_Update = BeginUpdateResourceW(m_Temp.c_str(), FALSE);

	if (not m_Update)
	{
		throw std::system_error(GetLastError(), std::system_category(), "cannot begin resource update");
	}

	removeVariants();

	if (not version.empty())
	{
		Resource const r{ RT_VERSION, VS_VERSION_INFO, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US) };
		update(r, version.data(), (DWORD)version.size());
	}

	if (not xml.empty())
	{
		Resource const r{ RT_MANIFEST, 1, LANG_NEUTRAL };
		update(r, xml.data(), (DWORD)xml.size());
	}

	if (not icons.empty())
	{
		writeIcons(iconData, icons);
	}

	commit();
}

void Executable::copy()
{
	wchar_t tmp[MAX_PATH]{};

	if (not GetTempFileNameW(m_Output.parent_path().c_str(), L"pub", 0, tmp))
	{
		throw std::system_error(GetLastError(), std::system_category(), "cannot create temporary output file");
	}

	m_Temp = tmp;

	if (not CopyFileW(m_Input.c_str(), tmp, FALSE))
	{
		throw std::system_error(GetLastError(), std::system_category(), "cannot copy input executable");
	}

	DWORD const attributes = GetFileAttributesW(tmp);

	if (attributes == INVALID_FILE_ATTRIBUTES
		or not SetFileAttributesW(tmp, attributes & ~FILE_ATTRIBUTE_READONLY))
	{
		throw std::system_error(GetLastError(), std::system_category(), "cannot make temporary output writable");
	}
}

void Executable::collect(LPCWSTR type)
{
	Enumeration en;
	en.Items = &m_Resources;

	if (not EnumResourceNamesW(m_Module, type, collectName, reinterpret_cast<LONG_PTR>(&en)))
	{
		DWORD const code = GetLastError();

		if (en.Error)
		{
			std::rethrow_exception(en.Error);
		}

		if (en.Code != ERROR_SUCCESS)
		{
			throw std::system_error(en.Code, std::system_category(), "cannot enumerate resource languages");
		}

		if (code != ERROR_RESOURCE_TYPE_NOT_FOUND and code != ERROR_RESOURCE_DATA_NOT_FOUND)
		{
			throw std::system_error(code, std::system_category(), "cannot enumerate resources");
		}
	}
}

void Executable::update(Resource const& resource, void const* data, DWORD size)
{
	LPCWSTR const name = resource.Name.empty() ? MAKEINTRESOURCEW(resource.Id) : resource.Name.c_str();

	if (not UpdateResourceW(m_Update, resource.Type, name, resource.Language, const_cast<void*>(data), size))
	{
		throw std::system_error(GetLastError(), std::system_category(), "cannot update executable resource");
	}
}

void Executable::removeVariants()
{
	LANGID const english = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);

	for (Resource const& r : m_Resources)
	{
		if (r.Type == RT_VERSION and not (r.Name.empty() and r.Id == VS_VERSION_INFO and r.Language == english))
		{
			update(r, nullptr, 0);
		}

		if (r.Type == RT_MANIFEST and r.Name.empty() and r.Id == 1 and r.Language != LANG_NEUTRAL)
		{
			update(r, nullptr, 0);
		}
	}
}

void Executable::writeIcons(std::vector<unsigned char> const& data, std::vector<Icon> const& icons)
{
	std::set<WORD> used;
	std::vector<Resource> groups;

	for (Resource const& r : m_Resources)
	{
		if (r.Type == RT_ICON and r.Name.empty())
		{
			used.insert(r.Id);
		}

		if (r.Type == RT_GROUP_ICON and (groups.empty() or (r.Id == groups[0].Id and r.Name == groups[0].Name)))
		{
			groups.push_back(r);
		}
	}

	if (groups.empty())
	{
		groups.push_back(Resource{ RT_GROUP_ICON, 1, LANG_NEUTRAL });
	}

	WORD const header[] = { 0, 1, (WORD)icons.size() };
	std::vector<unsigned char> directory(sizeof(header) + icons.size() * sizeof(GroupIcon));
	int id = 1;

	std::memcpy(directory.data(), header, sizeof(header));

	for (std::size_t i = 0; i < icons.size(); ++i)
	{
		while (id <= 65535 and used.contains((WORD)id))
		{
			++id;
		}

		if (id > 65535)
		{
			throw std::runtime_error("executable has no free icon resource IDs");
		}

		Icon const& icon = icons[i];
		WORD const number = (WORD)id++;
		GroupIcon const entry{ icon.Width, icon.Height, icon.Colors, icon.Reserved, icon.Planes, icon.Bits, icon.Size, number };
		std::memcpy(directory.data() + sizeof(header) + i * sizeof(GroupIcon), &entry, sizeof(entry));

		for (Resource const& group : groups)
		{
			Resource const r{ RT_ICON, number, group.Language };
			update(r, data.data() + icon.Offset, icon.Size);
		}
	}

	for (Resource const& group : groups)
	{
		update(group, directory.data(), (DWORD)directory.size());
	}
}

void Executable::commit()
{
	HANDLE const pending = m_Update;
	m_Update = nullptr;

	if (not EndUpdateResourceW(pending, FALSE))
	{
		throw std::system_error(GetLastError(), std::system_category(), "cannot write executable resources");
	}

	if (not MoveFileExW(m_Temp.c_str(), m_Output.c_str(), MOVEFILE_WRITE_THROUGH))
	{
		throw std::system_error(GetLastError(), std::system_category(), "cannot publish output executable");
	}

	m_Temp.clear();
}
