#include <windows.h>
#include <direct.h>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

std::string read_dtring(std::ifstream& stream)
{
	std::uint16_t len;
	stream.read(reinterpret_cast<char*>(&len), 2);

	std::vector<char> str(len);
	stream.read(reinterpret_cast<char*>(str.data()), len);

	return std::string(str.data(), len);
}

void write_word(std::ofstream& stream, std::uint16_t word)
{
	stream.write(reinterpret_cast<char*>(&word), 2);
}

void write_string(std::ofstream& stream, const std::string& str)
{
	write_word(stream, str.length());
	stream << str;
}

int main(int argc, const char* argv[])
{
	if (argc != 2)
		return -1;

	std::vector<std::string> params;
	bool after_at = false;

	// Read an input file with command parameters
	std::ifstream in_file;
	in_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);

	try
	{
		in_file.open(argv[1], std::ifstream::binary);

		while (true)
		{
			std::string str = read_dtring(in_file);

			if (after_at)
				params.push_back(std::move(str));
			else if (str == "@")
				after_at = true;
		}
	}
	catch (...)
	{
		if (!in_file.eof() || params.size() != 4)
			return -1;
	}

	in_file.close();

	// Get console parameters
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	const HANDLE output_handle = GetStdHandle(STD_OUTPUT_HANDLE);

	if (GetConsoleScreenBufferInfo(output_handle, &csbi) == FALSE)
		return -1;

	const SHORT width = csbi.dwMaximumWindowSize.X;
	const SHORT height = csbi.dwMaximumWindowSize.Y;

	std::vector<CHAR_INFO> chi_buffer(width * height, {L' ', 7});
	SMALL_RECT read_rect = {0, 0, width, height};
	const COORD buf_size = {width, height};
	const COORD buf_coord = {0, 0};

	// Execute a command
	SetFileApisToOEM();
	SetConsoleScreenBufferSize(output_handle, buf_size);
	SetCurrentDirectoryA(params[1].c_str());

	const std::string command = params[2] + " " + params[3];
	system(command.c_str());
	
	ReadConsoleOutputA(output_handle, chi_buffer.data(), buf_size, buf_coord, &read_rect);
	
	// Write response
	std::ofstream out_file;
	out_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);

	try
	{
		out_file.open(argv[1], std::ifstream::binary);

		write_word(out_file, width);
		write_word(out_file, height);
		for (const auto& c : chi_buffer)
			write_word(out_file, c.Char.AsciiChar);

		write_string(out_file, "@");
		write_string(out_file, params[0]);
		write_string(out_file, params[1]);
	}
	catch (...)
	{
		return -1;
	}

	return 0;
}
