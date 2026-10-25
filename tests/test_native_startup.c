/* Fixture driver: never executes game code or opens OpenXR. */
#define WIN32_LEAN_AND_MEAN
#include "../src/startup.h"

int wmain(int argc,wchar_t **argv)
{
    if(argc>=3 && !wcscmp(argv[1],L"find")) {
        wchar_t game[MAX_PATH];
        if(!hotdFindGame(argv[2],argc>3?argv[3]:NULL,game))return 1;
        wprintf(L"%ls\n",game);return 0;
    }
    if(argc==6 && !wcscmp(argv[1],L"font")) {
        size_t length=0;unsigned char *exe=hotdReadFile(argv[2],&length);char hash[65];
        if(wcslen(argv[5])!=64)return 2;
        for(int i=0;i<=64;i++)hash[i]=(char)argv[5][i];
        if(!exe || !length) {
            free(exe);fputs("Could not read HOTD2 executable\n",stderr);return 1;
        }
        if(!hotdGameHash(exe,length,hash))fputs("Warning: unrecognized HOTD2 executable SHA256\n",stderr);
        int ok=hotdEnsureFont(argv[2],argv[3],exe,length);free(exe);
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
