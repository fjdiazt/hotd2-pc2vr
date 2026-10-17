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

int wmain(int argc,wchar_t **argv)
{
    wchar_t game[MAX_PATH],dir[MAX_PATH],dll[MAX_PATH],command[MAX_PATH+4],*slash;
    STARTUPINFOW startup={0};PROCESS_INFORMATION process={0};
    HANDLE thread=NULL,snapshot=INVALID_HANDLE_VALUE;
    void *remote=NULL;SIZE_T written;DWORD module=0,error=0;
    int result=1,resumed=0,native_window=argc==3 && !wcscmp(argv[2],L"--native-window");
    if(argc<2 || argc>3 || (argc==3 && wcscmp(argv[2],L"--wait") && !native_window)) {
        fwprintf(stderr,L"usage: HOTD2Launch.exe path-to-game.exe [--wait | --native-window]\n");return 2;
    }
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
    swprintf(command,MAX_PATH+4,L"\"%ls\"",game);startup.cb=sizeof(startup);
    if(!CreateProcessW(game,command,NULL,NULL,FALSE,CREATE_SUSPENDED,NULL,dir,&startup,&process))goto cleanup;
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
    result=0;
    if(argc==3 && !native_window){WaitForSingleObject(process.hProcess,INFINITE);GetExitCodeProcess(process.hProcess,&module);result=(int)module;}
cleanup:
    error=GetLastError();
    if(result && process.hProcess && !resumed)TerminateProcess(process.hProcess,1);
    if(thread)CloseHandle(thread);
    if(process.hThread)CloseHandle(process.hThread);
    if(process.hProcess)CloseHandle(process.hProcess);
    if(result)fwprintf(stderr,L"Launch failed (%lu).%ls\n",error,native_window?L"":L" See HOTD2Bridge.log.");
    return result;
}
