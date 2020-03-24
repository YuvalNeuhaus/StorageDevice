#include <Windows.h>
#include <iostream>
#include <string>

int main() {
	std::cout << "Calling CreateFile" << std::endl;
	auto handle = CreateFileW(L"\\\\.\\e:",
							  FILE_READ_ACCESS | FILE_WRITE_ACCESS,
							  0,
							  nullptr,
							  OPEN_EXISTING,
							  FILE_ATTRIBUTE_DEVICE,
							  nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		std::cout << "CreateFile failed with GLE: " << std::to_string(GetLastError()) << std::endl;
		return 1;
	}

	std::cout << "Calling WriteFile" << std::endl;
	std::string writeBuffer(1024, 'A');
	DWORD bytesWritten = 0;
	if (!WriteFile(handle, writeBuffer.data(), writeBuffer.size(), &bytesWritten, nullptr)) {
		std::cout << "WriteFile failed with GLE: " << std::to_string(GetLastError()) << std::endl;
		return 1;
	}
	if (bytesWritten != writeBuffer.size()) {
		std::cout << "WriteFile wrote "<< std::to_string(bytesWritten) << " bytes but " << std::to_string(writeBuffer.size()) << " expected\n";
		return 1;
	}

	std::cout << "Calling CloseHandle" << std::endl;
	if (!CloseHandle(handle)) {
		std::cout << "CloseHandle failed with GLE: " << std::to_string(GetLastError()) << std::endl;
		return 1;
	}

	std::cout << "Calling CreateFile" << std::endl;
	auto handle2 = CreateFileW(L"\\\\.\\e:",
							  FILE_READ_ACCESS | FILE_WRITE_ACCESS,
							  0,
							  nullptr,
							  OPEN_EXISTING,
							  FILE_ATTRIBUTE_DEVICE,
							  nullptr);
	if (handle2 == INVALID_HANDLE_VALUE) {
		std::cout << "CreateFile failed with GLE: " << std::to_string(GetLastError()) << std::endl;
		return 1;
	}
	
	std::cout << "Calling ReadFile" << std::endl;
	std::string readBuffer(1024, 0);
	bytesWritten = 0;
	if (!ReadFile(handle2, const_cast<PCHAR>(readBuffer.data()), readBuffer.size(), &bytesWritten, nullptr)) {
		std::cout << "ReadFile failed with GLE: " << std::to_string(GetLastError()) << std::endl;
		return 1;
	}

	std::cout << readBuffer.c_str() << std::endl;
	DWORD bla = 0;
	std::cin >> bla;

	return 0;
}