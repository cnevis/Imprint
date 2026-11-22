#include "Manifest.h"
#include "Executable.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

int wmain(int argc, wchar_t* argv[])
{
	if (argc < 3 or argc > 4)
	{
		return 2;
	}

	try
	{
		std::filesystem::path const input = std::filesystem::absolute(argv[2]);
		std::filesystem::path output;

		if (argc == 4)
		{
			output = std::filesystem::absolute(argv[3]);
		}
		else
		{
			output = input.parent_path() / (input.stem().wstring() + L".published.exe");
		}

		Manifest const manifest = Manifest::load(std::filesystem::absolute(argv[1]));
		Executable exe(input, output);
		exe.publish(manifest);
		std::wcout << L"Created " << output.native() << L'\n';

		return 0;
	}
	catch (std::exception const& error)
	{
		std::cerr << "imprint: " << error.what() << '\n';
		return 1;
	}
}
