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
		const std::filesystem::path input = std::filesystem::absolute(argv[2]);
		std::filesystem::path output;

		if (argc == 4)
		{
			output = std::filesystem::absolute(argv[3]);
		}
		else
		{
			output = input.parent_path() / (input.stem().wstring() + L".published.exe");
		}

		const auto manifest = Manifest::load(std::filesystem::absolute(argv[1]));
		publish(input, output, manifest);
		std::wcout << L"Created " << output.native() << L'\n';

		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "imprint: " << error.what() << '\n';
		return 1;
	}
}
