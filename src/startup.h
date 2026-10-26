/* Native setup and optional font regeneration; no scripting runtime. */
#ifndef HOTD_STARTUP_H
#define HOTD_STARTUP_H
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#ifdef _MSC_VER
#pragma comment(lib,"bcrypt.lib")
#endif

#define HOTD_GAME_SHA256 "C6B4116788B7F68C56860FB9CC8A94BF984E620907031E4BC3DB43623DBE579A"
#define HOTD_FONT_SIZE (4+128*32*32*4)
#define HOTD_FONT_MAGIC "H2P2"

static int hotdSibling(wchar_t *out,const wchar_t *path,const wchar_t *name)
{
    const wchar_t *slash=wcsrchr(path,L'\\');
    size_t prefix=slash?(size_t)(slash-path+1):0;
    if(prefix+wcslen(name)>=MAX_PATH)return 0;
    wmemcpy(out,path,prefix);wcscpy(out+prefix,name);return 1;
}

static int hotdFindGame(const wchar_t *viewer,const wchar_t *explicit_path,wchar_t *game)
{
    if(explicit_path) {
        DWORD n=GetFullPathNameW(explicit_path,MAX_PATH,game,NULL);
        return n && n<MAX_PATH && GetFileAttributesW(game)!=INVALID_FILE_ATTRIBUTES &&
            !(GetFileAttributesW(game)&FILE_ATTRIBUTE_DIRECTORY);
    }
    wchar_t directory[MAX_PATH];
    if(wcslen(viewer)>=MAX_PATH)return 0;
    wcscpy(directory,viewer);
    for(wchar_t *slash=wcsrchr(directory,L'\\');slash;slash=wcsrchr(directory,L'\\')) {
        *slash=0;
        if(wcslen(directory)+wcslen(L"\\Hod2.exe")>=MAX_PATH)return 0;
        if(swprintf(game,MAX_PATH,L"%ls\\Hod2.exe",directory)<0)return 0;
        DWORD attributes=GetFileAttributesW(game);
        if(attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY))return 1;
        /* Stop at a drive root or UNC share; never search other shares. */
        if(wcslen(directory)==2 && directory[1]==L':')break;
        if(directory[0]==L'\\' && directory[1]==L'\\') {
            const wchar_t *server=wcschr(directory+2,L'\\');
            if(!server || !wcschr(server+1,L'\\'))break;
        }
    }
    game[0]=0;return 0;
}

static unsigned char *hotdReadFile(const wchar_t *path,size_t *length)
{
    *length=0;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
    if(file==INVALID_HANDLE_VALUE)return NULL;
    LARGE_INTEGER size;DWORD count=0,error=0;unsigned char *data=NULL;
    if(!GetFileSizeEx(file,&size))error=GetLastError();
    else if(size.QuadPart<0 || size.QuadPart>32*1024*1024)error=ERROR_FILE_TOO_LARGE;
    else {
        data=malloc((size_t)size.QuadPart+1);
        if(!data)error=ERROR_NOT_ENOUGH_MEMORY;
        else if(!ReadFile(file,data,(DWORD)size.QuadPart,&count,NULL))error=GetLastError();
        else if(count!=(DWORD)size.QuadPart)error=ERROR_HANDLE_EOF;
    }
    CloseHandle(file);
    if(error){free(data);SetLastError(error);return NULL;}
    *length=count;return data;
}

static int hotdGameHash(const unsigned char *data,size_t length,char hex[65])
{
    BCRYPT_ALG_HANDLE algorithm=NULL;BCRYPT_HASH_HANDLE hash=NULL;
    unsigned char digest[32];int ok=0;hex[0]=0;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)goto done;
    if(BCryptCreateHash(algorithm,&hash,NULL,0,NULL,0,0)<0 ||
       BCryptHashData(hash,(PUCHAR)data,(ULONG)length,0)<0 ||
       BCryptFinishHash(hash,digest,sizeof(digest),0)<0)goto done;
    for(int i=0;i<32;i++) {hex[i*2]="0123456789ABCDEF"[digest[i]>>4];hex[i*2+1]="0123456789ABCDEF"[digest[i]&15];}
    hex[64]=0;
    ok=1;
done:
    if(hash)BCryptDestroyHash(hash);
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    return ok;
}

static uint32_t hotdU32(const unsigned char *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static unsigned hotdU16(const unsigned char *p) {return p[0]|((unsigned)p[1]<<8);}

static int hotdFontValue(const unsigned char *exe,size_t length,uint64_t address,unsigned bytes,uint32_t *value)
{
    if(length<64 || address<0x400000)return 0;
    uint64_t pe=hotdU32(exe+60),rva=address-0x400000;
    if(pe+24>length)return 0;
    uint64_t sections=pe+24+hotdU16(exe+pe+20);
    unsigned count=hotdU16(exe+pe+6);
    if(sections+(uint64_t)count*40>length)return 0;
    for(unsigned i=0;i<count;i++) {
        const unsigned char *entry=exe+sections+i*40;
        uint64_t start=hotdU32(entry+12),size=hotdU32(entry+8),raw=hotdU32(entry+20);
        if(size>hotdU32(entry+16))size=hotdU32(entry+16);
        if(rva>=start && rva+bytes<=start+size) {
            uint64_t offset=raw+rva-start;
            if(offset+bytes>length)return 0;
            *value=bytes==2?hotdU16(exe+offset):hotdU32(exe+offset);return 1;
        }
    }
    return 0;
}

typedef struct {const unsigned char *data;size_t length,position;unsigned bits,left;int failed;} HotdFontStream;
static unsigned hotdFontByte(HotdFontStream *s)
{if(s->position>=s->length){s->failed=1;return 0;}return s->data[s->position++];}
static unsigned hotdFontBit(HotdFontStream *s)
{
    if(!s->left){s->bits=hotdFontByte(s);s->left=8;}
    unsigned bit=s->bits&1;s->bits>>=1;s->left--;return bit;
}

static int hotdUnpackMenuTexture(const unsigned char *source,size_t size,unsigned char *pixels,unsigned capacity)
{
    unsigned length=0;
    HotdFontStream stream={source,size,4,0,0,0};
    if(size<4 || hotdU32(source)!=capacity)return 0;
    for(;;) {
        if(hotdFontBit(&stream)) {
            if(length>=capacity)return 0;
            pixels[length++]=(unsigned char)hotdFontByte(&stream);
        }else {
            int offset;unsigned count;
            if(hotdFontBit(&stream)) {
                unsigned word=hotdFontByte(&stream);word|=hotdFontByte(&stream)<<8;
                if(!word) {if(stream.failed || length!=capacity)return 0;break;}
                offset=(int)(word>>3)-8192;count=word&7;
                count=count?count+2:hotdFontByte(&stream)+1;
            }else {
                count=hotdFontBit(&stream)<<1;count=(count|hotdFontBit(&stream))+2;
                offset=(int)hotdFontByte(&stream)-256;
            }
            if(stream.failed || offset>=0 || offset<-(int)length || count>capacity-length)return 0;
            while(count--) {pixels[length]=pixels[(int)length+offset];length++;}
        }
        if(stream.failed)return 0;
    }
    return 1;
}

static int hotdMenuGlyph(const unsigned char *exe,size_t exe_length,const unsigned char *pixels,size_t pixel_length,
    unsigned glyph,unsigned expected_group,unsigned char *output)
{
    uint32_t group,descriptors,count,texture,width=0,height=0,format=0,offset=0;unsigned matches=0;
    if(!hotdFontValue(exe,exe_length,0x57A5BC+(uint64_t)glyph*4,4,&group) || group!=expected_group ||
       !hotdFontValue(exe,exe_length,0x55B9B8+(uint64_t)group*4,4,&descriptors) ||
       !hotdFontValue(exe,exe_length,0x55C0A8+(uint64_t)group*2,2,&count) || !count || count>4096 ||
       !hotdFontValue(exe,exe_length,0x57D448+(uint64_t)glyph*4,4,&texture))return 0;
    for(unsigned i=0;i<count;i++) {
        uint64_t descriptor=(uint64_t)descriptors+i*16;uint32_t id;
        if(!hotdFontValue(exe,exe_length,descriptor+12,4,&id))return 0;
        if(id!=texture)continue;
        matches++;
        if(!hotdFontValue(exe,exe_length,descriptor,2,&width) ||
           !hotdFontValue(exe,exe_length,descriptor+2,2,&height) ||
           !hotdFontValue(exe,exe_length,descriptor+4,4,&format) ||
           !hotdFontValue(exe,exe_length,descriptor+8,4,&offset))return 0;
    }
    if(matches!=1 || format!=0x902 || !width || width>32 || !height || height>32 ||
       (uint64_t)offset+width*height*2>pixel_length)return 0;
    memset(output,0,32*32*4);
    for(unsigned i=0;i<width*height;i++) {
        unsigned value=hotdU16(pixels+offset+i*2);
        unsigned char *p=output+((height-1-i/width)*32+i%width)*4;
        p[0]=(unsigned char)(((value>>8)&15)*17);p[1]=(unsigned char)(((value>>4)&15)*17);
        p[2]=(unsigned char)((value&15)*17);p[3]=(unsigned char)(((value>>12)&15)*17);
    }
    return 1;
}

static int hotdDecodeFont(const unsigned char *exe,size_t exe_length,const unsigned char *pixels,unsigned char *font)
{
    memset(font,0,HOTD_FONT_SIZE);memcpy(font,HOTD_FONT_MAGIC,4);
    const char *characters="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for(const char *c=characters;*c;c++) {
        uint32_t glyph;
        if(!hotdFontValue(exe,exe_length,0x56AED0+(unsigned)*c*2,2,&glyph) ||
           !hotdMenuGlyph(exe,exe_length,pixels,64512,glyph,0x15E,font+4+(unsigned)*c*4096))return 0;
    }
    return 1;
}

typedef struct {const char *reason,*action;wchar_t path[MAX_PATH];DWORD code;} HotdFontError;

static void hotdFontError(HotdFontError *error,const wchar_t *path,const char *reason,const char *action,DWORD code)
{
    error->reason=reason;error->action=action;error->code=code;
    wcsncpy(error->path,path,MAX_PATH-1);error->path[MAX_PATH-1]=0;
}

static int hotdEnsureFont(const wchar_t *game,const wchar_t *viewer,const unsigned char *exe,size_t exe_length,HotdFontError *error)
{
    wchar_t output[MAX_PATH],texture[MAX_PATH],temporary[MAX_PATH]={0};size_t length;
    unsigned char *data=NULL,*font=NULL,*pixels=NULL;int valid=0;
    const char *write_help="Check free disk space and allow writes to the mod folder, or move the game/mod to a writable folder.";
    if(!hotdSibling(output,viewer,L"menu-font.rgba")) {
        hotdFontError(error,viewer,"Mod folder path is too long","Move the game/mod to a shorter folder path.",ERROR_FILENAME_EXCED_RANGE);return 0;
    }
    data=hotdReadFile(output,&length);
    valid=data && length==HOTD_FONT_SIZE && !memcmp(data,HOTD_FONT_MAGIC,4);free(data);data=NULL;
    if(valid)return 1;
    font=malloc(HOTD_FONT_SIZE);pixels=malloc(811008);
    if(!font || !pixels) {
        hotdFontError(error,output,"Not enough memory to extract the settings font","Close other applications and retry.",ERROR_NOT_ENOUGH_MEMORY);goto done;
    }
    const wchar_t *names[]={L"tex\\scr_opt_moji05.bin",L"tex\\scr_item.bin"};
    for(unsigned i=0;i<2;i++) {
        if(!hotdSibling(texture,game,names[i])) {
            hotdFontError(error,game,"Game texture path is too long","Move the game/mod to a shorter folder path.",ERROR_FILENAME_EXCED_RANGE);goto done;
        }
        data=hotdReadFile(texture,&length);
        if(!data) {
            DWORD code=GetLastError();
            int missing=code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND;
            hotdFontError(error,texture,missing?"Required game texture is missing":"Cannot read required game texture",
                missing?"Restore the complete game files. Keep the tex folder beside Hod2.exe; use the installed game folder, not the CD root.":
                "Check read access to this game file and close applications locking it.",code);goto done;
        }
        if(!hotdUnpackMenuTexture(data,length,pixels,i?811008:64512)) {
            hotdFontError(error,texture,i?"Cannot decompress menu arrow texture":"Cannot decompress menu font texture",
                "Restore this texture from the original game files. It is damaged or uses an unsupported format.",0);goto done;
        }
        free(data);data=NULL;
        if(!(i?hotdMenuGlyph(exe,exe_length,pixels,811008,0x5F7,0x156,font+4+4096):hotdDecodeFont(exe,exe_length,pixels,font))) {
            hotdFontError(error,game,i?"Unsupported menu arrow tables in Hod2.exe":"Unsupported menu font tables in Hod2.exe",
                "Send the startup report with your game version so this executable layout can be checked.",0);goto done;
        }
    }
    for(unsigned row=0;row<32;row++)memcpy(font+4+2*4096+row*128,font+4+4096+(31-row)*128,128);
    wchar_t directory[MAX_PATH];wcscpy(directory,output);*wcsrchr(directory,L'\\')=0;
    if(!GetTempFileNameW(directory,L"h2f",0,temporary)) {
        hotdFontError(error,output,"Cannot create settings font in the mod folder",write_help,GetLastError());goto done;
    }
    HANDLE file=CreateFileW(temporary,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    if(file==INVALID_HANDLE_VALUE) {
        hotdFontError(error,output,"Cannot open temporary settings font for writing",write_help,GetLastError());goto done;
    }
    DWORD written=0,code=0;
    if(!WriteFile(file,font,HOTD_FONT_SIZE,&written,NULL))code=GetLastError();
    else if(written!=HOTD_FONT_SIZE)code=ERROR_WRITE_FAULT;
    else if(!FlushFileBuffers(file))code=GetLastError();
    if(!CloseHandle(file) && !code)code=GetLastError();
    if(code) {hotdFontError(error,output,"Cannot write settings font",write_help,code);goto done;}
    if(!MoveFileExW(temporary,output,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        hotdFontError(error,output,"Cannot replace menu-font.rgba",
            "Allow writes to the mod folder and remove any read-only flag from menu-font.rgba, or move the game/mod to a writable folder.",GetLastError());goto done;
    }
    valid=1;
done:
    free(data);free(font);free(pixels);
    if(!valid && temporary[0])DeleteFileW(temporary);
    return valid;
}

static void hotdStartupUtf8(char *out,int capacity,const wchar_t *text)
{
    if(!WideCharToMultiByte(CP_UTF8,0,text,-1,out,capacity,NULL,NULL))strcpy(out,"<path conversion failed>");
}

static void hotdReportFile(FILE *report,const char *label,const wchar_t *path)
{
    char name[MAX_PATH*4],hash[65];size_t size=0;
    hotdStartupUtf8(name,sizeof(name),path);
    unsigned char *data=hotdReadFile(path,&size);DWORD code=data?0:GetLastError();
    fprintf(report,"\n%s: %s\n",label,name);
    if(data) {
        fprintf(report,"Bytes: %zu\nSHA256: %s\n",size,hotdGameHash(data,size,hash)?hash:"unavailable");
        fprintf(report,"Attributes: 0x%08lX\n",(unsigned long)GetFileAttributesW(path));
    }else fprintf(report,"Unavailable: Windows error %lu\n",(unsigned long)code);
    free(data);
}

static int hotdWriteFontReport(const wchar_t *path,const HotdFontError *error,const char *windows_error,
    const wchar_t *game,const wchar_t *viewer,const char *hash)
{
    FILE *report=_wfopen(path,L"wb");
    if(!report)return 0;
    SYSTEMTIME utc;GetSystemTime(&utc);
    char failed[MAX_PATH*4];hotdStartupUtf8(failed,sizeof(failed),error->path);
    fprintf(report,"HOTD2 PC2VR startup report\nBuild: %s %s (%u-bit)\nUTC: %04u-%02u-%02u %02u:%02u:%02u\n",
        __DATE__,__TIME__,(unsigned)(sizeof(void *)*8),utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute,utc.wSecond);
    fprintf(report,"\nFailure: %s\nFile: %s\n%s\nRecovery: %s\nGame SHA256 at startup: %s\n",
        error->reason,failed,windows_error,error->action,hash[0]?hash:"unavailable");
    hotdReportFile(report,"Game EXE",game);hotdReportFile(report,"Mod EXE",viewer);
    const wchar_t *names[]={L"tex\\scr_opt_moji05.bin",L"tex\\scr_item.bin"};wchar_t input[MAX_PATH];
    for(unsigned i=0;i<2;i++)if(hotdSibling(input,game,names[i]))hotdReportFile(report,"Game texture",input);
    if(hotdSibling(input,viewer,L"menu-font.rgba"))hotdReportFile(report,"Font cache",input);
    int ok=!ferror(report);if(fclose(report))ok=0;
    return ok;
}

static const char *hotdFontFailure(const HotdFontError *error,const wchar_t *game,const wchar_t *viewer,const char *hash)
{
    static char message[4096];char failed[MAX_PATH*4],windows_error[1200]={0};
    hotdStartupUtf8(failed,sizeof(failed),error->path);
    if(error->code) {
        wchar_t native[256]={0};char text[1024];
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,NULL,error->code,0,native,256,NULL);
        native[wcscspn(native,L"\r\n")]=0;hotdStartupUtf8(text,sizeof(text),native);
        snprintf(windows_error,sizeof(windows_error),"Windows error %lu: %s",(unsigned long)error->code,text);
    }
    snprintf(message,sizeof(message),"Menu asset extraction failed: %s\nFile: %s\n%s%sWhat to do: %s\n",
        error->reason,failed,windows_error,windows_error[0]?"\n":"",error->action);
    wchar_t report[MAX_PATH];int saved=hotdSibling(report,viewer,L"HOTD2VR-startup.log") &&
        hotdWriteFontReport(report,error,windows_error,game,viewer,hash);
    if(!saved) {
        wchar_t temporary[MAX_PATH];DWORD n=GetTempPathW(MAX_PATH,temporary);
        if(n && n<MAX_PATH && swprintf(report,MAX_PATH,L"%lsHOTD2VR-startup-%lu.log",temporary,GetCurrentProcessId())>0)
            saved=hotdWriteFontReport(report,error,windows_error,game,viewer,hash);
    }
    size_t used=strlen(message);
    if(saved) {
        char name[MAX_PATH*4];hotdStartupUtf8(name,sizeof(name),report);
        snprintf(message+used,sizeof(message)-used,"Startup report: %s\nSend this file when reporting the issue.",name);
    }else snprintf(message+used,sizeof(message)-used,"Could not save a startup report. Copy this full message and include your mod version and game folder path.");
    return message;
}

static int hotdStartGame(const wchar_t *viewer,const wchar_t *game,const wchar_t *options)
{
    wchar_t helper[MAX_PATH],command[2048];STARTUPINFOW startup={0};PROCESS_INFORMATION process={0};
    if(!hotdSibling(helper,viewer,L"injector.exe") || wcschr(game,L'"') ||
       swprintf(command,2048,L"\"%ls\" \"%ls\" %ls",helper,game,options)<0)return 0;
    startup.cb=sizeof(startup);
    if(!CreateProcessW(helper,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&process))return 0;
    CloseHandle(process.hThread);
    DWORD result=1;
    if(WaitForSingleObject(process.hProcess,30000)==WAIT_OBJECT_0)GetExitCodeProcess(process.hProcess,&result);
    else {TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,5000);}
    CloseHandle(process.hProcess);return result==0;
}

static const char *hotdPrepareGame(const wchar_t *viewer,const wchar_t *explicit_path,const wchar_t *options,int launch,int font)
{
    wchar_t game[MAX_PATH];size_t length=0;
    if(!hotdFindGame(viewer,explicit_path,game))
        return "Hod2.exe not found. Place the mod in the game's vr folder, or use --game-exe PATH.";
    unsigned char *exe=hotdReadFile(game,&length);
    if(!exe || !length) {
        free(exe);return "Could not read HOTD2 executable, or file is empty. Game not started.";
    }
    char hash[65];
    if(!hotdGameHash(exe,length,hash) || strcmp(hash,HOTD_GAME_SHA256))
        fprintf(stderr,"Warning: unrecognized HOTD2 executable SHA256; continuing with unverified compatibility.\nSHA256: %s\nInclude this SHA256 when reporting issues.\n",hash[0]?hash:"unavailable");
    HotdFontError error;
    int ready=!font || hotdEnsureFont(game,viewer,exe,length,&error);free(exe);
    if(!ready)return hotdFontFailure(&error,game,viewer,hash);
    if(launch && !hotdStartGame(viewer,game,options))return "Game launch failed. Check injector.exe, HOTD2Bridge.dll and HOTD2Bridge.log.";
    return NULL;
}
#endif
