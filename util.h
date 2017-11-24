#include <vector>
#include <string>
#include <sstream>
#include <set>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#endif

#include "coreclrhost.h"

#if defined(_WIN32)
#define PATH_SEPARATOR ";"
#define CLR_DLL "coreclr.dll";
#elif defined(__linux__)
#define PATH_SEPARATOR ":"
#define CLR_DLL "libcoreclr.so"
#elif defined (__APPLE__)
#define PATH_SEPARATOR ":"
#define CLR_DLL "libcoreclr.dylib"
#endif

//Global Variables
void* _coreClrLib;
void *_coreClrHandle;
unsigned int _coreClrDomainId;
coreclr_initialize_ptr _coreClrInit;
coreclr_shutdown_2_ptr _coreClrShutdown;
coreclr_create_delegate_ptr _coreClrGetDele;

void* loadLib(const std::string &path)
{
	#ifdef _WIN32
		return LoadLibraryEx(path.c_str(), NULL, 0);
	#else
		return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	#endif
}

int pinLib(const std::string &path)
{
	#ifdef _WIN32
		HMODULE dummy;
		return GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_PIN, path.c_str(), &dummy) ? 0 : -1;
	#else
		return 0;
	#endif
}

int unloadLib(void* library)
{
	#ifdef _WIN32
		return FreeLibrary((HMODULE)library) ? 0 : -1;
	#else
		return dlclose(library);
	#endif
}

void* getLibFunc(void* library, const std::string &proc)
{
	#ifdef _WIN32
		return GetProcAddress((HMODULE)library, proc.c_str());
	#else
		return dlsym(library, proc.c_str());
	#endif
}

void freeManaged(void* block)
{
	#ifdef _WIN32
		CoTaskMemFree(block);
	#else
		free(block);
	#endif
}

std::vector<std::string> getDirectoryFiles(const std::string &directory)
{
	#ifdef _WIN32
		std::vector<std::string> results;
		HANDLE dir;
		WIN32_FIND_DATA entry;

		if ((dir = FindFirstFile((directory + "/*").c_str(), &entry)) == INVALID_HANDLE_VALUE)
			return results;

		do
		{
			const std::string fileName = entry.cFileName;
			const std::string fullFileName = directory + fileName;
			const bool isDirectory = (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

			if (isDirectory)
				continue;

			results.push_back(fullFileName);
		}
		while (FindNextFile(dir, &entry));

		FindClose(dir);
		return results;
	#else
		std::vector<std::string> results;
		DIR *dir;
		class dirent *entry;

		dir = opendir(directory.c_str());
	
		while ((entry = readdir(dir)) != NULL)
		{
			const std::string fileName = entry->d_name;
			const std::string fullFileName = directory + fileName;
			const bool isDirectory = entry->d_type == DT_DIR;

			if (isDirectory)
				continue;

			results.push_back(fullFileName);
		}

		closedir(dir);
		return results;
	#endif
}

std::string getTpaFiles(const std::string &directory)
{
	std::string results;

	const char* exts[] = {
		".ni.dll",
		".dll",
		".ni.exe",
		".exe",
		".ni.winmd",
		".winmd"
	};

	std::set<std::string> added;
	std::vector<std::string> files = getDirectoryFiles(directory);

	for (int i = 0; i < sizeof(exts) / sizeof(exts[0]); i++)
	{
		std::string ext = exts[i];
		std::vector<std::string>::iterator it;

		for (it = files.begin(); it != files.end(); it++)
		{
			std::string file = *it;
			size_t extLen = ext.length();
			size_t extPos = file.length() - extLen;
			bool extMatch = (extPos >= 0) && (file.compare(extPos, extLen, ext) == 0);
			if (!extMatch)
				continue;

			std::string noExt = file.substr(0, extPos);
			if (added.find(noExt) == added.end())
			{
				added.insert(noExt);
				results.append(file).append(PATH_SEPARATOR);
			}
		}
	}

	return results;
}

void _loadLibCoreClr(const std::string& clrPath)
{
	std::string clrDll = clrPath + CLR_DLL;
	
	_coreClrLib = loadLib(clrDll);
	if (_coreClrLib == NULL)
	{
		std::stringstream sstream;
		sstream << "Failed to load CoreCLR library";
		throw std::runtime_error(sstream.str());
	}

	int ret = pinLib(clrDll);
	if (ret != 0)
	{
		std::stringstream sstream;
		sstream << "Failed to pin CoreCLR library with error: 0x" << std::hex << ret;
		throw std::runtime_error(sstream.str());
	}

	_coreClrInit = (coreclr_initialize_ptr)getLibFunc(_coreClrLib, "coreclr_initialize");
	if (_coreClrInit == NULL)
	{
		std::stringstream sstream;
		sstream << "Function coreclr_initialize not found in CoreCLR library";
		throw std::runtime_error(sstream.str());
	}	
	
	_coreClrShutdown = (coreclr_shutdown_2_ptr)getLibFunc(_coreClrLib, "coreclr_shutdown_2");
	if (_coreClrShutdown == NULL)
	{
		std::stringstream sstream;
		sstream << "Function coreclr_shutdown_2 not found in CoreCLR library";
		throw std::runtime_error(sstream.str());
	}
	
	_coreClrGetDele = (coreclr_create_delegate_ptr)getLibFunc(_coreClrLib, "coreclr_create_delegate");
	if (_coreClrGetDele == NULL)
	{
		std::stringstream sstream;
		sstream << "Function coreclr_create_delegate not found in CoreCLR library";
		throw std::runtime_error(sstream.str());
	}
}

void _unloadLibCoreClr()
{
	int ret = unloadLib(_coreClrLib);
	if (ret != 0)
	{
		std::stringstream sstream;
		sstream << "Failed to unload CoreCLR library with error: 0x" << std::hex << ret;
		throw std::runtime_error(sstream.str());
	}
}

void initCoreClr(const std::string& exePath, const std::string& clrPath, const std::string& appPath)
{
	_loadLibCoreClr(clrPath);

	std::string tpaFiles = getTpaFiles(clrPath);

	const char *property_keys[] = {
		"TRUSTED_PLATFORM_ASSEMBLIES",
		"APP_PATHS",
		"APP_NI_PATHS"
	};

	const char *property_values[] = {
		tpaFiles.c_str(),
		appPath.c_str(),
		appPath.c_str()
	};

	int ret = _coreClrInit(
		exePath.c_str(),
		"corerun",
		sizeof(property_values) / sizeof(char *),
		property_keys,
		property_values,
		&_coreClrHandle,
		&_coreClrDomainId
	);

	if (ret != 0)
	{
		std::stringstream sstream;
		sstream << "coreclr_initialize failed - status: 0x" << std::hex << ret;
		throw std::runtime_error(sstream.str());
	}
}

int shutdownCoreClr()
{
	int exitCode = 0;

	int ret = _coreClrShutdown(
		_coreClrHandle,
		_coreClrDomainId,
		&exitCode);

	if (ret != 0)
	{
		std::stringstream sstream;
		sstream << "coreclr_shutdown_2 failed - status: 0x" << std::hex << ret;
		throw std::runtime_error(sstream.str());
	}

	_unloadLibCoreClr();

	return exitCode;
}

void* getCoreClrDele(const std::string& assembly, const std::string& type, const std::string& method)
{
	void* dele;

	int ret = _coreClrGetDele(
		_coreClrHandle,
		_coreClrDomainId,
		assembly.c_str(),
		type.c_str(),
		method.c_str(),
		&dele
	);

	if (ret != 0)
	{
		std::stringstream sstream;
		sstream << "coreclr_create_delegate failed - status: 0x" << std::hex << ret;
		throw std::runtime_error(sstream.str());
	}

	return dele;
}