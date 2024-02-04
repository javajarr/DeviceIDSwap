#include <iostream>
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>

int APIENTRY WinMain
(
	_In_ HINSTANCE hinstance,
	_In_opt_ HINSTANCE hprevinstance,
	_In_ PSTR lpcmdline,
	_In_ int ncmdshow)
{
	AllocConsole();
	AttachConsole(GetCurrentProcessId());
	HWND hwnd = GetConsoleWindow();

	FILE* stream = NULL;
	freopen_s(&stream, "CONOUT$", "w", stdout);
	freopen_s(&stream, "CONOUT$", "w", stderr);
	freopen_s(&stream, "CONIN$", "r", stdin);

	HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
	DWORD prev_mode = 0;
	GetConsoleMode(hInput, &prev_mode);
	SetConsoleMode(hInput, prev_mode & ENABLE_EXTENDED_FLAGS);

	SetWindowLong(hwnd, GWL_STYLE, GetWindowLong(hwnd, GWL_STYLE) & ~WS_MAXIMIZEBOX & ~WS_SIZEBOX);
	system("color 0a && mode con: cols=90 lines=26");

	CONSOLE_FONT_INFOEX cfi;
	cfi.cbSize = sizeof cfi;
	cfi.nFont = 0;
	cfi.dwFontSize.X = 0;
	cfi.dwFontSize.Y = 14;
	cfi.FontFamily = FF_DONTCARE;
	cfi.FontWeight = FW_BOLD;

	wcscpy_s(cfi.FaceName, L"Cascadia Code");
	SetCurrentConsoleFontEx(GetStdHandle(STD_OUTPUT_HANDLE), FALSE, &cfi);

	std::string title = "DeviceIDSwap";
	SetConsoleTitle(title.c_str());
	ShowWindow(hwnd, SW_SHOW);

	CreateMutexA(0, FALSE, "DEVICEIDSWAP");
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		std::cerr << "An instance is already running";
		std::this_thread::sleep_for(std::chrono::seconds(2));

		return 1;
	}
	std::string old_uuid, new_uuid, uuid, path;
	size_t sz = 0;
	char* buf = nullptr;

	_dupenv_s(&buf, &sz, "LOCALAPPDATA");

	if (buf) path = buf;
	free(buf);

	path += "/Packages/Microsoft.MinecraftUWP_8wekyb3d8bbwe/LocalState/games/com.mojang/minecraftpe/hs";

	do {
		system("start minecraft:");
		std::this_thread::sleep_for(std::chrono::seconds(2));

		static int attempts = 0;
		if (attempts) {
			std::cerr << "Failed to locate hs file path";
			std::this_thread::sleep_for(std::chrono::seconds(2));

			return 1;
		}
		attempts++;
	} while (!std::filesystem::exists(path));

	std::ifstream hs_read(path);
	if (!hs_read.is_open())
	{
		std::cerr << "Failed to read the hs file";
		std::this_thread::sleep_for(std::chrono::seconds(2));

		return 1;
	}

	std::vector<std::string> contents;
	while (getline(hs_read, uuid))
	{
		contents.push_back(uuid);
		static int i = 0;

		switch (i)
		{
		case 1: old_uuid = uuid; break;
		case 2: new_uuid = uuid;
		}
		i++;
	}

	hs_read.close();
	if (contents.empty())
	{
		std::cerr << "Failed to store file contents";
		std::this_thread::sleep_for(std::chrono::seconds(2));

		return 1;
	}

	if (old_uuid.empty())
	{
		std::cerr << "Failed to read device ID";
		std::this_thread::sleep_for(std::chrono::seconds(2));

		return 1;
	}

	std::ofstream hs_write(path);
	if (!hs_write.is_open())
	{
		std::cerr << "Failed to access the hs file";
		std::this_thread::sleep_for(std::chrono::seconds(2));

		return 1;
	}
	bool gen_uuid = false, init_uuid = false, init = true;
generate:

	if (new_uuid.empty()) gen_uuid = true, init_uuid = true;
	if (gen_uuid)
	{
		const std::string chars = "0123456789abcdef";
		new_uuid.erase();

		std::random_device random_device;
		std::mt19937 gen(random_device());
		std::uniform_int_distribution<> dist(0, chars.size() - 1);

		std::size_t group_count = 0;
		for (std::size_t groups : {8, 4, 4, 4, 12})
		{
			if (group_count > 0) new_uuid += '-';
			for (std::size_t i = 0; i < groups; ++i) new_uuid += chars[dist(gen)];
			group_count++;
		}
		new_uuid[14] = '3';
	}

	if (init) {
		init = false;

		std::cout << "Old device ID: " << old_uuid << std::endl;
		std::cout << "New device ID: " << new_uuid << std::endl;
	}
	while (!gen_uuid)
	{
		std::cout << std::endl << "Generate a new device ID (y/n): ";

		std::string input;
		std::cin >> input;

		std::transform(input.begin(), input.end(), input.begin(), ::tolower);

		if (input == "y") { gen_uuid = true; goto generate; }
		if (input == "n") break;
	}
	title += " - " + new_uuid;
	SetConsoleTitle(title.c_str());

	if (!init_uuid) contents.back() = new_uuid;
	else contents.push_back(new_uuid);

	for (const auto& content : contents) hs_write << content << std::endl;
	hs_write.close();

	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 pEntry;
	pEntry.dwSize = sizeof(pEntry);

	DWORD processID = NULL;
	if (hSnap == INVALID_HANDLE_VALUE)
	{
		std::cerr << "Failed to obtain a snapshot of running processes";
		std::this_thread::sleep_for(std::chrono::seconds(2));

		return 1;
	}

	if (Process32First(hSnap, &pEntry)) {
		do {
			if (strcmp(pEntry.szExeFile, "Minecraft.Windows.exe") == 0)
			{
				processID = pEntry.th32ProcessID;
				break;
			}
		} while (Process32Next(hSnap, &pEntry));
	}
	CloseHandle(hSnap);

	if (!processID)
	{
		std::cerr << "Failed to obtain a process ID";
		std::this_thread::sleep_for(std::chrono::seconds(2));

		return 1;
	}

	HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, NULL, processID);
	while (hProc)
	{
		MEMORY_BASIC_INFORMATION mbi;
		LPCVOID addr = NULL;

		bool written = false;
		static int done = 0;
		if (done >= 10) break;

		while (VirtualQueryEx(hProc, addr, &mbi, sizeof(mbi))) {
			if (mbi.State == MEM_COMMIT && (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE))
			{
				std::vector<BYTE> memBuffer(mbi.RegionSize);
				uintptr_t found = 0;

				ReadProcessMemory(hProc, mbi.BaseAddress, memBuffer.data(), mbi.RegionSize, nullptr);

				for (int i = 0; i < memBuffer.size(); i++) {
					for (uintptr_t j = 0; j < old_uuid.length(); j++)
					{
						if (old_uuid[j] != memBuffer[i + j]) break;
						if (j + 1 == old_uuid.length()) found = (uintptr_t)i;
					}
				}
				if (found)
				{
					const char* deviceID = new_uuid.c_str();
					if (WriteProcessMemory(hProc, (LPVOID)((uintptr_t)mbi.BaseAddress + found), deviceID, new_uuid.length() + 1, nullptr))
						std::cout << std::endl << "Modified: 0x" << std::hex << std::uppercase << (uintptr_t)mbi.BaseAddress + found;

					written = true;
				}
			}
			addr = (LPCVOID)((uintptr_t)mbi.BaseAddress + mbi.RegionSize);
		}
		if (!written) {
			ShowWindow(hwnd, SW_HIDE);
			done++;
		}
	}

	if (!hProc)
	{
		std::cerr << "Failed to open a handle to the process";
		std::this_thread::sleep_for(std::chrono::seconds(2));

		return 1;
	}
	CloseHandle(hProc);
	return 0;
}