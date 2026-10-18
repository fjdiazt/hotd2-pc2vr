#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "checkpoint.h"

typedef struct { int wait,native_window,skip_splash,start_stage,start_scene,continue_game; } LaunchOptions;

static int readLaunchOptions(int argc,wchar_t **argv,const wchar_t *ini,LaunchOptions *options)
{
    memset(options,0,sizeof(*options));
    options->start_scene=options->continue_game=-1;
    options->skip_splash=GetPrivateProfileIntW(L"bridge",L"skip_splash",1,ini)!=0;
    if(argc<2)return 0;
    for(int i=2;i<argc;i++) {
        if(!wcscmp(argv[i],L"--wait"))options->wait=1;
        else if(!wcscmp(argv[i],L"--native-window"))options->native_window=1;
        else if(!wcscmp(argv[i],L"--skip-splash"))options->skip_splash=1;
        else if(!wcscmp(argv[i],L"--show-splash"))options->skip_splash=0;
        else if(!wcscmp(argv[i],L"--continue-game"))options->continue_game=1;
        else if(!wcscmp(argv[i],L"--no-continue-game"))options->continue_game=0;
        else if(!wcscmp(argv[i],L"--start-stage")) {
            if(++i>=argc || !hotd_checkpoint_number(argv[i],1,6,&options->start_stage))return 0;
        }
        else if(!wcscmp(argv[i],L"--start-scene")) {
            if(++i>=argc || !hotd_checkpoint_number(argv[i],0,HOTD_CHECKPOINT_MAX_SCENE,&options->start_scene))return 0;
        }
        else return 0;
    }
    return !(options->start_scene>=0 && !options->start_stage) &&
        !(options->native_window && (options->start_stage || options->continue_game==1));
}

static BOOL setCheckpointEnvironment(const LaunchOptions *options)
{
    const wchar_t *keys[]={L"HOTD2VR_START_STAGE",L"HOTD2VR_START_SCENE",L"HOTD2VR_CONTINUE_GAME"};
    int values[]={options->start_stage?options->start_stage:-1,options->start_scene,options->continue_game};
    wchar_t text[16];
    for(unsigned i=0;i<3;i++) {
        if(values[i]>=0)swprintf(text,16,L"%d",values[i]);
        if(!SetEnvironmentVariableW(keys[i],values[i]>=0?text:NULL))return FALSE;
    }
    return TRUE;
}

static BOOL skipSplash(HANDLE process,BYTE *base)
{
    const BYTE signature[]={0xC7,0x05,0x98,0x8E,0x9C,0x00,0x10,0x00,0x00,0x00},mode=3;
    BYTE actual[sizeof(signature)],check=0;SIZE_T bytes;DWORD protection,ignored;
    /* Preserve game initialization; enter the title instead of the two boot logos. */
    if(!ReadProcessMemory(process,base+0xE4AA,actual,sizeof(actual),&bytes))return FALSE;
    if(bytes!=sizeof(actual) || memcmp(actual,signature,sizeof(actual))) {
        SetLastError(ERROR_BAD_EXE_FORMAT);return FALSE;
    }
    if(!VirtualProtectEx(process,base+0xE4B0,1,PAGE_EXECUTE_READWRITE,&protection))return FALSE;
    BOOL patched=WriteProcessMemory(process,base+0xE4B0,&mode,1,&bytes) && bytes==1;
    BOOL restored=VirtualProtectEx(process,base+0xE4B0,1,protection,&ignored);
    if(!patched || !restored || !FlushInstructionCache(process,base+0xE4B0,1))return FALSE;
    if(!ReadProcessMemory(process,base+0xE4B0,&check,1,&bytes) || bytes!=1 || check!=mode) {
        SetLastError(ERROR_WRITE_FAULT);return FALSE;
    }
    return TRUE;
}

int wmain(int argc,wchar_t **argv)
{
    wchar_t game[MAX_PATH],dir[MAX_PATH],dll[MAX_PATH],ini[MAX_PATH],command[MAX_PATH+4],*slash;
    STARTUPINFOW startup={0};PROCESS_INFORMATION process={0};
    HANDLE thread=NULL,snapshot=INVALID_HANDLE_VALUE;
    void *remote=NULL;SIZE_T written;DWORD module=0,error=0;
    LaunchOptions options;
    int result=1,resumed=0,native_window;
    module=GetModuleFileNameW(NULL,ini,MAX_PATH);if(!module || module>=MAX_PATH)return 2;
    slash=wcsrchr(ini,L'\\');if(!slash || slash-ini>MAX_PATH-14)return 2;
    wcscpy(slash+1,L"HOTD2VR.ini");
    if(!readLaunchOptions(argc,argv,ini,&options)) {
        fwprintf(stderr,L"usage: HOTD2Launch.exe path-to-game.exe [--wait] [--native-window] [--skip-splash | --show-splash] [--start-stage 1..6 [--start-scene 0..255]] [--continue-game | --no-continue-game]\n");return 2;
    }
    native_window=options.native_window;
    module=GetFullPathNameW(argv[1],MAX_PATH,game,NULL);
    if(!module || module>=MAX_PATH || GetFileAttributesW(game)==INVALID_FILE_ATTRIBUTES) {
        fwprintf(stderr,L"Game executable missing.\n");return 2;
    }
    wcscpy(dir,game);slash=wcsrchr(dir,L'\\');if(!slash)return 2;*slash=0;
    snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot!=INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe={0};pe.dwSize=sizeof(pe);
        if(Process32FirstW(snapshot,&pe))do {
            if(!_wcsicmp(pe.szExeFile,wcsrchr(game,L'\\')+1)) {
                fwprintf(stderr,L"%ls already running (PID %lu). Close it before injecting.\n",pe.szExeFile,pe.th32ProcessID);
                CloseHandle(snapshot);return 2;
            }
        }while(Process32NextW(snapshot,&pe));
        CloseHandle(snapshot);
    }
    module=GetModuleFileNameW(NULL,dll,MAX_PATH);if(!module || module>=MAX_PATH)return 2;
    slash=wcsrchr(dll,L'\\');if(!slash || slash-dll>MAX_PATH-18)return 2;
    wcscpy(slash+1,L"HOTD2Bridge.dll");
    if(!native_window && GetFileAttributesW(dll)==INVALID_FILE_ATTRIBUTES){fwprintf(stderr,L"HOTD2Bridge.dll missing.\n");return 2;}
    module=0;
    if(!setCheckpointEnvironment(&options))goto cleanup;
    swprintf(command,MAX_PATH+4,L"\"%ls\"",game);startup.cb=sizeof(startup);
    if(!CreateProcessW(game,command,NULL,NULL,FALSE,CREATE_SUSPENDED,NULL,dir,&startup,&process))goto cleanup;
    if(options.skip_splash && !skipSplash(process.hProcess,(BYTE *)0x400000))goto cleanup;
    if(native_window) {
        const BYTE signature[]={0x6A,0x0B,0xA3,0x28,0xE6,0x7D,0x00},mode=7;
        BYTE actual[sizeof(signature)],check=0;DWORD protection,ignored;
        /* Select the existing native windowed mode before any fullscreen setup. */
        if(!ReadProcessMemory(process.hProcess,(void *)0x49E461,actual,sizeof(actual),&written))goto cleanup;
        if(written!=sizeof(actual) || memcmp(actual,signature,sizeof(actual))) {
            SetLastError(ERROR_BAD_EXE_FORMAT);goto cleanup;
        }
        if(!VirtualProtectEx(process.hProcess,(void *)0x49E462,1,PAGE_EXECUTE_READWRITE,&protection))goto cleanup;
        BOOL patched=WriteProcessMemory(process.hProcess,(void *)0x49E462,&mode,1,&written) && written==1;
        BOOL restored=VirtualProtectEx(process.hProcess,(void *)0x49E462,1,protection,&ignored);
        if(!patched || !restored || !FlushInstructionCache(process.hProcess,(void *)0x49E462,1))goto cleanup;
        if(!ReadProcessMemory(process.hProcess,(void *)0x49E462,&check,1,&written) || written!=1 || check!=mode) {
            SetLastError(ERROR_WRITE_FAULT);goto cleanup;
        }
        goto resume;
    }
    remote=VirtualAllocEx(process.hProcess,NULL,(wcslen(dll)+1)*sizeof(wchar_t),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!remote || !WriteProcessMemory(process.hProcess,remote,dll,(wcslen(dll)+1)*sizeof(wchar_t),&written))goto cleanup;
    thread=CreateRemoteThread(process.hProcess,NULL,0,(LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"),remote,0,NULL);
    if(!thread)goto cleanup;
    if(WaitForSingleObject(thread,15000)!=WAIT_OBJECT_0){SetLastError(ERROR_TIMEOUT);goto cleanup;}
    if(!GetExitCodeThread(thread,&module) || !module){SetLastError(ERROR_DLL_INIT_FAILED);goto cleanup;}
    VirtualFreeEx(process.hProcess,remote,0,MEM_RELEASE);remote=NULL;
resume:
    if(ResumeThread(process.hThread)==(DWORD)-1)goto cleanup;
    resumed=1;
    wprintf(native_window?L"Native windowed game; game PID %lu\n":L"Injected HOTD2Bridge.dll; game PID %lu\n",process.dwProcessId);fflush(stdout);
    if(options.skip_splash){wprintf(L"Startup splashes skipped.\n");fflush(stdout);}
    result=0;
    if(options.wait){WaitForSingleObject(process.hProcess,INFINITE);GetExitCodeProcess(process.hProcess,&module);result=(int)module;}
cleanup:
    error=GetLastError();
    if(result && process.hProcess && !resumed)TerminateProcess(process.hProcess,1);
    if(thread)CloseHandle(thread);
    if(process.hThread)CloseHandle(process.hThread);
    if(process.hProcess)CloseHandle(process.hProcess);
    if(result)fwprintf(stderr,L"Launch failed (%lu).%ls\n",error,native_window?L"":L" See HOTD2Bridge.log.");
    return result;
}
