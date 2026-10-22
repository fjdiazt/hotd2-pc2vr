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
    assert(!options.launchfix);
    assert(WritePrivateProfileStringW(L"bridge",L"launchfix",L"1",ini));
    assert(readLaunchOptions(2,plain,ini,&options) && options.launchfix);
    assert(WritePrivateProfileStringW(L"bridge",L"skip_splash",L"1",ini));
    assert(readLaunchOptions(2,plain,ini,&options) && options.skip_splash);
    assert(readLaunchOptions(4,show,ini,&options) && !options.skip_splash && options.native_window && !options.wait);
    assert(WritePrivateProfileStringW(L"bridge",L"skip_splash",L"0",ini));
    assert(readLaunchOptions(2,plain,ini,&options) && !options.skip_splash);
    assert(readLaunchOptions(4,skip,ini,&options) && options.skip_splash && options.wait && !options.native_window);
    assert(!readLaunchOptions(3,bad,ini,&options));
    assert(!readLaunchOptions(1,plain,ini,&options));
    wchar_t *native_services[]={L"launcher",L"game.exe",L"--no-window",L"--no-vrwindow",L"--start-stage",L"4",L"--start-scene",L"0"};
    assert(readLaunchOptions(8,native_services,ini,&options) && !options.capture && !options.native_window);
    assert(options.start_stage==4 && options.start_scene==0 && setCheckpointEnvironment(&options));
    wchar_t *no_graphics[]={L"launcher",L"game.exe",L"--no-graphics-hooks",L"--vrwindow",L"--window",L"--stereo"};
    assert(readLaunchOptions(6,no_graphics,ini,&options) && !options.capture && !options.native_window);
    wchar_t capture[16];
    assert(GetEnvironmentVariableW(L"HOTD2VR_CAPTURE",capture,16) && !wcscmp(capture,L"0"));
    for(int window=0;window<2;window++)for(int vr=0;vr<2;vr++) {
        assert(WritePrivateProfileStringW(L"vr",L"window",window?L"1":L"0",ini));
        assert(WritePrivateProfileStringW(L"vr",L"vrwindow",vr?L"1":L"0",ini));
        assert(readLaunchOptions(2,plain,ini,&options) && options.capture==(window||vr));
        wchar_t *outputs[]={L"launcher",L"game.exe",window?L"--no-window":L"--window",vr?L"--no-vrwindow":L"--vrwindow"};
        assert(readLaunchOptions(4,outputs,ini,&options) && options.capture==(!window||!vr));
    }
    wchar_t *stereo[]={L"launcher",L"game.exe",L"--no-window",L"--no-vrwindow",L"--stereo"};
    assert(readLaunchOptions(5,stereo,ini,&options) && options.capture);
    wchar_t *last_output[]={L"launcher",L"game.exe",L"--window",L"--vrwindow",L"--no-window",L"--no-vrwindow"};
    assert(readLaunchOptions(6,last_output,ini,&options) && !options.capture);
    wchar_t *stage[]={L"launcher",L"game.exe",L"--start-stage",L"6",L"--start-scene",L"3",L"--continue-game"};
    assert(readLaunchOptions(7,stage,ini,&options) && options.start_stage==6 && options.start_scene==3 && options.continue_game==1);
    assert(setCheckpointEnvironment(&options));
    wchar_t value[16];assert(GetEnvironmentVariableW(L"HOTD2VR_START_STAGE",value,16) && !wcscmp(value,L"6"));
    assert(GetEnvironmentVariableW(L"HOTD2VR_START_SCENE",value,16) && !wcscmp(value,L"3"));
    assert(GetEnvironmentVariableW(L"HOTD2VR_CONTINUE_GAME",value,16) && !wcscmp(value,L"1"));
    assert(readLaunchOptions(2,plain,ini,&options) && !options.start_stage && options.start_scene==-1 && options.continue_game==-1);
    assert(setCheckpointEnvironment(&options));
    assert(!GetEnvironmentVariableW(L"HOTD2VR_START_STAGE",value,16) && GetLastError()==ERROR_ENVVAR_NOT_FOUND);
    assert(!GetEnvironmentVariableW(L"HOTD2VR_START_SCENE",value,16) && GetLastError()==ERROR_ENVVAR_NOT_FOUND);
    assert(!GetEnvironmentVariableW(L"HOTD2VR_CONTINUE_GAME",value,16) && GetLastError()==ERROR_ENVVAR_NOT_FOUND);
    assert(GetEnvironmentVariableW(L"HOTD2VR_CAPTURE",value,16) && !wcscmp(value,L"1"));
    assert(SetEnvironmentVariableW(L"HOTD2VR_CAPTURE",NULL));
    wchar_t *invalid[][5]={
        {L"launcher",L"game.exe",L"--start-scene",L"2"},
        {L"launcher",L"game.exe",L"--start-stage",L"0"},
        {L"launcher",L"game.exe",L"--start-stage",L"7"},
        {L"launcher",L"game.exe",L"--start-stage",L"1junk"},
        {L"launcher",L"game.exe",L"--start-stage",L"-1"},
        {L"launcher",L"game.exe",L"--start-stage",L"9999999999999999999999999999"},
        {L"launcher",L"game.exe",L"--native-window",L"--continue-game"}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++)assert(!readLaunchOptions(4,invalid[i],ini,&options));
    assert(!readLaunchOptions(3,stage,ini,&options));
    assert(!wcscmp(options.invalid_kind,L"stage") && !options.invalid_value);
    wchar_t *stage20[]={L"launcher",L"game.exe",L"--start-stage",L"20"};
    assert(!readLaunchOptions(4,stage20,ini,&options) && !wcscmp(options.invalid_kind,L"stage") && !wcscmp(options.invalid_value,L"20"));
    wchar_t *scene_bad[]={L"launcher",L"game.exe",L"--start-stage",L"1",L"--start-scene",L"256"};
    assert(!readLaunchOptions(6,scene_bad,ini,&options) && !wcscmp(options.invalid_kind,L"scene") && !wcscmp(options.invalid_value,L"256"));
    wchar_t *resume[]={L"launcher",L"game.exe",L"--continue-game",L"--no-continue-game"};
    assert(readLaunchOptions(4,resume,ini,&options) && options.continue_game==0);
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
    const BYTE disc_signature[]={0xF6,0x05,0x80,0xEB,0x7D,0x00,0x40,0x74,0x0D};
    memory=VirtualAlloc(NULL,0xA7000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    before=malloc(0xA7000);assert(memory && before);memset(memory,0xCC,0xA7000);
    memcpy(memory+0xA6850,disc_signature,sizeof(disc_signature));memcpy(before,memory,0xA7000);
    assert(VirtualProtect(memory,0xA7000,PAGE_EXECUTE_READ,&old));
    assert(bypassDiscCheck(GetCurrentProcess(),memory));before[0xA6858]=0;
    assert(!memcmp(memory,before,0xA7000));
    assert(VirtualQuery(memory+0xA6858,&info,sizeof(info)) && info.Protect==PAGE_EXECUTE_READ);
    assert(!bypassDiscCheck(GetCurrentProcess(),memory) && GetLastError()==ERROR_BAD_EXE_FORMAT);
    assert(!memcmp(memory,before,0xA7000));
    assert(VirtualProtect(memory,0xA7000,PAGE_READWRITE,&ignored));
    memcpy(memory+0xA6850,disc_signature,sizeof(disc_signature));memory[0xA6850]^=1;
    memcpy(before,memory,0xA7000);
    assert(!bypassDiscCheck(GetCurrentProcess(),memory) && GetLastError()==ERROR_BAD_EXE_FORMAT);
    assert(!memcmp(memory,before,0xA7000));
    assert(VirtualFree(memory,0,MEM_RELEASE));free(before);
    puts("Startup: INI/CLI precedence, one-byte splash/CD bypasses, signature rejection and protection restore passed.");
    return 0;
}
