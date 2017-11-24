//#ifdef _MSC_VER
//#define _CRT_SECURE_NO_WARNINGS
//#endif

#include <iostream>
#include <vector>
#include <string>
#include "util.h"

using namespace std;

//Note: Be sure to properly match 32/64 bit architecture

int main()
{
	try
	{
		typedef void* (*funcPtr)(...);		
        	const char* exePath = "./corehost";
	        const char* clrPath = "/usr/share/dotnet/shared/Microsoft.NETCore.App/2.0.0/";
        	const char* appPath = "/home/craig/projects/corehost/";

		//init clr
		initCoreClr(exePath, clrPath, appPath);

		//get delegates
		funcPtr test = (funcPtr)getCoreClrDele("ClassLibrary2", "ClassLibrary2.Class1", "Test");

		//call delegates
		char* result = (char*)test();
		cout << "Result: " << result << endl;

		//free memory
		freeManaged(result);
		
		//shutdown clr
		shutdownCoreClr();

		return 0;
	}
	catch (const exception& ex)
	{
		cout << "Error: " << ex.what() << endl;
		return -1;
	}
}
