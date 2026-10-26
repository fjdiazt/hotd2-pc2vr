/* Fixture driver: never executes game code or opens OpenXR. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
static wchar_t protected_directory[MAX_PATH];
static UINT WINAPI test_temp_file(LPCWSTR directory,LPCWSTR prefix,UINT unique,LPWSTR output)
{
    if(protected_directory[0] && !_wcsicmp(directory,protected_directory)) {
        SetLastError(ERROR_ACCESS_DENIED);return 0;
    }
    return GetTempFileNameW(directory,prefix,unique,output);
}
#define GetTempFileNameW test_temp_file
#include "../src/startup.h"

int wmain(int argc,wchar_t **argv)
{
    if(argc==4 && (!wcscmp(argv[1],L"data") || !wcscmp(argv[1],L"prepare-font-protected"))) {
        wcscpy(protected_directory,argv[2]);
        wchar_t *slash=wcsrchr(protected_directory,L'\\');if(!slash)return 2;*slash=0;
        if(!wcscmp(argv[1],L"data")) {
            if(!wcscmp(argv[3],L"writable"))protected_directory[0]=0;
            wchar_t path[MAX_PATH];
            if(!hotdDataFileW(argv[2],L"HOTD2VR.ini",path)) {
                fprintf(stderr,"Windows error %lu\n",GetLastError());return 1;
            }
            char utf8[MAX_PATH*4];hotdStartupUtf8(utf8,sizeof(utf8),path);puts(utf8);return 0;
        }
        const char *error=hotdPrepareGame(argv[2],argv[3],L"",0,1);
        if(error)fputs(error,stderr);
        return error?1:0;
    }
    if(argc>=3 && !wcscmp(argv[1],L"find")) {
        wchar_t game[MAX_PATH];
        if(!hotdFindGame(argv[2],argc>3?argv[3]:NULL,game))return 1;
        wprintf(L"%ls\n",game);return 0;
    }
    if(argc==6 && !wcscmp(argv[1],L"font")) {
        size_t length=0;unsigned char *exe=hotdReadFile(argv[2],&length);char hash[65],actual[65];
        if(wcslen(argv[5])!=64)return 2;
        for(int i=0;i<=64;i++)hash[i]=(char)argv[5][i];
        if(!exe || !length) {
            free(exe);fputs("Could not read HOTD2 executable\n",stderr);return 1;
        }
        if(!hotdGameHash(exe,length,actual) || strcmp(actual,hash))
            fprintf(stderr,"Warning: unrecognized HOTD2 executable SHA256\nSHA256: %s\n",actual[0]?actual:"unavailable");
        HotdFontError error;
        int ok=hotdEnsureFont(argv[2],argv[3],exe,length,&error);free(exe);
        if(!ok)fputs("Unexpected decompressed font size or invalid font data\n",stderr);
        return ok?0:1;
    }
    if(argc==5 && !wcscmp(argv[1],L"start"))return hotdStartGame(argv[2],argv[3],argv[4])?0:1;
    if(argc==4 && (!wcscmp(argv[1],L"prepare") || !wcscmp(argv[1],L"prepare-font"))) {
        const char *error=hotdPrepareGame(argv[2],argv[3],L"",!wcscmp(argv[1],L"prepare"),1);
        if(error)fputs(error,stderr);
        return error?1:0;
    }
    return 2;
}
