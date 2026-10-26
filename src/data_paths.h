/* Shared by the viewer, injector and bridge; safe during DLL initialization. */
#ifndef HOTD_DATA_PATHS_H
#define HOTD_DATA_PATHS_H
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static inline int hotdPathJoin(wchar_t out[MAX_PATH],const wchar_t *directory,const wchar_t *name)
{
    size_t length=wcslen(directory);
    const wchar_t *separator=length && directory[length-1]==L'\\'?L"":L"\\";
    if(length+wcslen(separator)+wcslen(name)+1>MAX_PATH){SetLastError(ERROR_FILENAME_EXCED_RANGE);return 0;}
    swprintf(out,MAX_PATH,L"%ls%ls%ls",directory,separator,name);return 1;
}

static inline int hotdDirectoryWritable(const wchar_t *directory)
{
    /* Directory handles bypass legacy x86 file-write virtualization. */
    HANDLE folder=CreateFileW(directory,FILE_ADD_FILE|FILE_DELETE_CHILD,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
    if(folder==INVALID_HANDLE_VALUE)return 0;
    CloseHandle(folder);
    wchar_t temporary[MAX_PATH];
    if(!GetTempFileNameW(directory,L"h2w",0,temporary))return 0;
    return DeleteFileW(temporary)!=0;
}

static inline int hotdMakeDirectory(const wchar_t *directory)
{
    if(CreateDirectoryW(directory,NULL))return 1;
    if(GetLastError()!=ERROR_ALREADY_EXISTS)return 0;
    DWORD attributes=GetFileAttributesW(directory);
    if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY))return 1;
    SetLastError(ERROR_DIRECTORY);return 0;
}

static inline int hotdSeedData(const wchar_t *source,const wchar_t *directory,const wchar_t *name)
{
    wchar_t from[MAX_PATH],to[MAX_PATH],temporary[MAX_PATH];
    if(!hotdPathJoin(from,source,name) || !hotdPathJoin(to,directory,name))return 0;
    DWORD attributes=GetFileAttributesW(to);
    if(attributes!=INVALID_FILE_ATTRIBUTES) {
        if(!(attributes&FILE_ATTRIBUTE_DIRECTORY))return 1;
        SetLastError(ERROR_DIRECTORY);return 0;
    }
    DWORD error=GetLastError();
    if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND)return 0;
    attributes=GetFileAttributesW(from);
    if(attributes==INVALID_FILE_ATTRIBUTES) {
        error=GetLastError();return error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND;
    }
    if(!GetTempFileNameW(directory,L"h2s",0,temporary))return 0;
    int ok=CopyFileW(from,temporary,FALSE) && SetFileAttributesW(temporary,FILE_ATTRIBUTE_NORMAL) &&
        MoveFileExW(temporary,to,MOVEFILE_WRITE_THROUGH);
    error=GetLastError();
    if(!ok) {
        SetFileAttributesW(temporary,FILE_ATTRIBUTE_NORMAL);DeleteFileW(temporary);
        /* A concurrent first launch may already have published this file. */
        if(error==ERROR_ALREADY_EXISTS || error==ERROR_FILE_EXISTS)ok=1;
    }
    if(!ok)SetLastError(error);
    return ok;
}

static inline int hotdDataDirectoryW(const wchar_t *module_path,wchar_t out[MAX_PATH])
{
    wchar_t source[MAX_PATH],identity[MAX_PATH],local[MAX_PATH],base[MAX_PATH],fallback[MAX_PATH],key[17];
    DWORD n=GetFullPathNameW(module_path,MAX_PATH,source,NULL);
    if(!n || n>=MAX_PATH){SetLastError(ERROR_FILENAME_EXCED_RANGE);return 0;}
    wchar_t *slash=wcsrchr(source,L'\\');
    if(!slash){SetLastError(ERROR_BAD_PATHNAME);return 0;}
    if(slash==source+2 && source[1]==L':')slash[1]=0;else *slash=0;
    if(!LCMapStringW(LOCALE_INVARIANT,LCMAP_LOWERCASE,source,-1,identity,MAX_PATH))return 0;
    /* Installation identity only; not a security hash. Same result in x86/x64. */
    uint64_t hash=UINT64_C(14695981039346656037);
    for(const wchar_t *p=identity;*p;p++)hash=(hash^(uint16_t)*p)*UINT64_C(1099511628211);
    swprintf(key,17,L"%016llx",(unsigned long long)hash);
    n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,MAX_PATH);
    int available=n && n<MAX_PATH;
    DWORD local_error=n?ERROR_FILENAME_EXCED_RANGE:ERROR_ENVVAR_NOT_FOUND;
    if(available) {
        available=hotdPathJoin(base,local,L"HOTD2VR") && hotdPathJoin(fallback,base,key);
        if(!available)local_error=GetLastError();
    }
    DWORD attributes=available?GetFileAttributesW(fallback):INVALID_FILE_ATTRIBUTES;
    if(available && attributes==INVALID_FILE_ATTRIBUTES &&
       GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND)return 0;
    /* Once selected, keep user data authoritative even if permissions change. */
    if(attributes==INVALID_FILE_ATTRIBUTES && hotdDirectoryWritable(source)) {
        wcscpy(out,source);return 1;
    }
    if(!available){SetLastError(local_error);return 0;}
    if(!hotdMakeDirectory(base) || !hotdMakeDirectory(fallback) || !hotdDirectoryWritable(fallback))return 0;
    wchar_t marker[MAX_PATH];
    if(!hotdPathJoin(marker,fallback,L"portable-import-v1.done"))return 0;
    if(GetFileAttributesW(marker)==INVALID_FILE_ATTRIBUTES) {
        const wchar_t *files[]={L"HOTD2VR.ini",L"HOTD2VR.checkpoint.ini",L"HOTD2VR.original.checkpoint.ini"};
        for(unsigned i=0;i<sizeof(files)/sizeof(files[0]);i++)if(!hotdSeedData(source,fallback,files[i]))return 0;
        HANDLE file=CreateFileW(marker,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        if(file==INVALID_HANDLE_VALUE && GetLastError()!=ERROR_FILE_EXISTS && GetLastError()!=ERROR_ALREADY_EXISTS)return 0;
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    }
    wcscpy(out,fallback);return 1;
}

static inline int hotdDataFileW(const wchar_t *module_path,const wchar_t *name,wchar_t out[MAX_PATH])
{
    wchar_t directory[MAX_PATH];
    return hotdDataDirectoryW(module_path,directory) && hotdPathJoin(out,directory,name);
}

static inline int hotdDataFileA(const char *module_path,const char *name,char out[MAX_PATH])
{
    wchar_t module[MAX_PATH],directory[MAX_PATH],short_path[MAX_PATH];char path[MAX_PATH];BOOL replaced=FALSE;
    if(!MultiByteToWideChar(CP_ACP,0,module_path,-1,module,MAX_PATH) || !hotdDataDirectoryW(module,directory))return 0;
    BOOL *loss=GetACP()==CP_UTF8?NULL:&replaced;
    DWORD flags=loss?WC_NO_BEST_FIT_CHARS:0;
    int n=WideCharToMultiByte(CP_ACP,flags,directory,-1,path,MAX_PATH,NULL,loss);
    if(!n || replaced) {
        /* Existing ANSI game services need a lossless path on non-ANSI profiles. */
        DWORD length=GetShortPathNameW(directory,short_path,MAX_PATH);replaced=FALSE;
        n=length && length<MAX_PATH?WideCharToMultiByte(CP_ACP,flags,short_path,-1,path,MAX_PATH,NULL,loss):0;
        if(!n || replaced){SetLastError(ERROR_NO_UNICODE_TRANSLATION);return 0;}
    }
    size_t prefix=strlen(path),length=strlen(name);
    if(prefix+length+2>MAX_PATH){SetLastError(ERROR_FILENAME_EXCED_RANGE);return 0;}
    memcpy(out,path,prefix);out[prefix]='\\';memcpy(out+prefix+1,name,length+1);return 1;
}
#endif
