#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <winbase.h>
#include <wine/test.h>


#include "windef.h"
#define NO_SHLWAPI_REG
#include "shlwapi.h"
#undef NO_SHLWAPI_REG
#include "winbase.h"
#include "../cabinet.h"


typedef VOID (__stdcall *f_dllget)(PCABINETDLLVERSIONINFO);


static void test_dllget(HMODULE libHandle)
{
    PCABINETDLLVERSIONINFO verInfo;
    char                   *version;
    int                    sizeVerInfo;
    DWORD                  FileVersionMS;
    DWORD                  FileVersionLS;
    int                    majorV;
    int                    minorV;
    int                    buildV;
    int                    revisV;

    f_dllget DllGetVersion = (f_dllget)GetProcAddress(libHandle, "DllGetVersion");
    ok(DllGetVersion != NULL, "Function DllGetVersion in DLL not found: Error = %ld.\n", GetLastError());


    verInfo = malloc(sizeof(CABINETDLLVERSIONINFO));
    ok(verInfo != NULL, "Couldn't allocate memory to run tests properly!\n");
    if (DllGetVersion)
        DllGetVersion(verInfo);

    FileVersionMS = verInfo->dwFileVersionMS;
    FileVersionLS = verInfo->dwFileVersionLS;

    /*length of 4 DWORDs + buffer*/
    sizeVerInfo = 32;

    version = malloc(sizeVerInfo);
    ok(version != NULL, "Couldn't allocate memory to run tests properly!\n");

    majorV = (int)( FileVersionMS >> 16 ) & 0xffff;
    minorV = (int)( FileVersionMS >>  0 ) & 0xffff;
    buildV = (int)( FileVersionLS >> 16 ) & 0xffff;
    revisV = (int)( FileVersionLS >>  0 ) & 0xffff;

    snprintf(version, sizeVerInfo, "%d.%d.%d.%d\n",majorV,minorV,buildV,revisV);;
    ok(strcmp(version,"0.0.0.0\n") != 0, "Cabinet struct doesn't contain correct version: Error = %ld.\n", GetLastError());
}

START_TEST(version)
{
    HMODULE libHandle;
    libHandle = LoadLibraryA("Cabinet.dll");
    ok(libHandle != NULL, "Cabinet.dll not found: Error = %ld.\n", GetLastError());
    test_dllget(libHandle);
}
