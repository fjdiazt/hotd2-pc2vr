/* Real INI and process-memory checks; no game or viewer starts. */
#include <assert.h>
#include <stdlib.h>
#define wmain hotd_launch_main
#include "../src/launch.c"
#undef wmain

int main(void)
{
    wchar_t temp[MAX_PATH],ini[MAX_PATH];LaunchOptions options;
    wchar_t *plain[]={L"launcher",L"game.exe"};
    wchar_t *skip[]={L"launcher",L"game.exe",L"--skip-splash",L"--wait"};
    wchar_t *show[]={L"launcher",L"game.exe",L"--native-window",L"--show-splash"};
    wchar_t *bad[]={L"launcher",L"game.exe",L"--skip-splas"};
    assert(GetTempPathW(MAX_PATH,temp) && GetTempFileNameW(temp,L"hsp",0,ini));
    assert(readLaunchOptions(2,plain,ini,&options) && options.skip_splash && !options.wait && !options.native_window);
    assert(WritePrivateProfileStringW(L"bridge",L"skip_splash",L"1",ini));
    assert(readLaunchOptions(2,plain,ini,&options) && options.skip_splash);
    assert(readLaunchOptions(4,show,ini,&options) && !options.skip_splash && options.native_window && !options.wait);
    assert(WritePrivateProfileStringW(L"bridge",L"skip_splash",L"0",ini));
    assert(readLaunchOptions(2,plain,ini,&options) && !options.skip_splash);
    assert(readLaunchOptions(4,skip,ini,&options) && options.skip_splash && options.wait && !options.native_window);
    assert(!readLaunchOptions(3,bad,ini,&options));
    assert(!readLaunchOptions(1,plain,ini,&options));
    assert(DeleteFileW(ini));

    const BYTE signature[]={0xC7,0x05,0x98,0x8E,0x9C,0x00,0x10,0x00,0x00,0x00};
    BYTE *memory=VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    BYTE *before=malloc(0x10000);DWORD old,ignored;MEMORY_BASIC_INFORMATION info;
    assert(memory && before);memset(memory,0xCC,0x10000);
    memcpy(memory+0xE4AA,signature,sizeof(signature));memcpy(before,memory,0x10000);
    assert(VirtualProtect(memory,0x10000,PAGE_EXECUTE_READ,&old));
    assert(skipSplash(GetCurrentProcess(),memory));
    before[0xE4B0]=3;
    assert(!memcmp(memory,before,0x10000));
    assert(VirtualQuery(memory+0xE4B0,&info,sizeof(info)) && info.Protect==PAGE_EXECUTE_READ);
    assert(!skipSplash(GetCurrentProcess(),memory) && GetLastError()==ERROR_BAD_EXE_FORMAT);
    assert(!memcmp(memory,before,0x10000));
    assert(VirtualProtect(memory,0x10000,PAGE_READWRITE,&ignored));
    memcpy(memory+0xE4AA,signature,sizeof(signature));memory[0xE4AA]^=1;
    memcpy(before,memory,0x10000);
    assert(!skipSplash(GetCurrentProcess(),memory) && GetLastError()==ERROR_BAD_EXE_FORMAT);
    assert(!memcmp(memory,before,0x10000));
    assert(VirtualFree(memory,0,MEM_RELEASE));free(before);
    puts("Startup: INI/CLI precedence, one-byte splash bypass, signature rejection and protection restore passed.");
    return 0;
}
