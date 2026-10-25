/* Native first-run setup; no scripting runtime or game-derived release assets. */
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
    FILE *file=_wfopen(path,L"rb");
    if(!file)return NULL;
    /* Both inputs are small; reject damaged or unrelated multi-gigabyte files. */
    if(fseek(file,0,SEEK_END)) {fclose(file);return NULL;}
    long n=ftell(file);
    if(n<0 || n>32*1024*1024 || fseek(file,0,SEEK_SET)) {fclose(file);return NULL;}
    unsigned char *data=malloc((size_t)n+1);
    if(!data || fread(data,1,(size_t)n,file)!=(size_t)n) {free(data);fclose(file);return NULL;}
    fclose(file);*length=(size_t)n;return data;
}

static int hotdGameHash(const unsigned char *data,size_t length,const char *expected)
{
    BCRYPT_ALG_HANDLE algorithm=NULL;BCRYPT_HASH_HANDLE hash=NULL;
    unsigned char digest[32];char hex[65];int ok=0;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)goto done;
    if(BCryptCreateHash(algorithm,&hash,NULL,0,NULL,0,0)<0 ||
       BCryptHashData(hash,(PUCHAR)data,(ULONG)length,0)<0 ||
       BCryptFinishHash(hash,digest,sizeof(digest),0)<0)goto done;
    for(int i=0;i<32;i++) {hex[i*2]="0123456789ABCDEF"[digest[i]>>4];hex[i*2+1]="0123456789ABCDEF"[digest[i]&15];}
    hex[64]=0;
    ok=!strcmp(hex,expected);
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

static int hotdDecodeFont(const unsigned char *exe,size_t exe_length,const unsigned char *source,size_t size,unsigned char *font)
{
    unsigned char pixels[64512];
    if(!hotdUnpackMenuTexture(source,size,pixels,sizeof(pixels)))return 0;
    memset(font,0,HOTD_FONT_SIZE);memcpy(font,HOTD_FONT_MAGIC,4);
    const char *characters="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for(const char *c=characters;*c;c++) {
        uint32_t glyph;
        if(!hotdFontValue(exe,exe_length,0x56AED0+(unsigned)*c*2,2,&glyph) ||
           !hotdMenuGlyph(exe,exe_length,pixels,sizeof(pixels),glyph,0x15E,font+4+(unsigned)*c*4096))return 0;
    }
    return 1;
}

static int hotdEnsureFont(const wchar_t *game,const wchar_t *viewer,const unsigned char *exe,size_t exe_length)
{
    wchar_t output[MAX_PATH],texture[MAX_PATH],temporary[MAX_PATH];size_t length;
    if(!hotdSibling(output,viewer,L"menu-font.rgba") || !hotdSibling(texture,game,L"tex\\scr_opt_moji05.bin"))return 0;
    unsigned char *data=hotdReadFile(output,&length);
    int valid=data && length==HOTD_FONT_SIZE && !memcmp(data,HOTD_FONT_MAGIC,4);free(data);
    if(valid)return 1;
    data=hotdReadFile(texture,&length);
    unsigned char *font=malloc(HOTD_FONT_SIZE);
    valid=data && font && hotdDecodeFont(exe,exe_length,data,length,font);free(data);
    if(valid) {
        unsigned char *pixels=malloc(811008);
        data=hotdSibling(texture,game,L"tex\\scr_item.bin")?hotdReadFile(texture,&length):NULL;
        valid=data && pixels && hotdUnpackMenuTexture(data,length,pixels,811008) &&
            hotdMenuGlyph(exe,exe_length,pixels,811008,0x5F7,0x156,font+4+4096);
        free(data);free(pixels);
        if(valid)for(unsigned row=0;row<32;row++)
            memcpy(font+4+2*4096+row*128,font+4+4096+(31-row)*128,128);
    }
    if(valid) {
        wchar_t directory[MAX_PATH];wcscpy(directory,output);*wcsrchr(directory,L'\\')=0;
        valid=GetTempFileNameW(directory,L"h2f",0,temporary)!=0;
        if(valid) {
            FILE *file=_wfopen(temporary,L"wb");
            valid=file!=NULL;
            if(file) {valid=fwrite(font,1,HOTD_FONT_SIZE,file)==HOTD_FONT_SIZE;if(fclose(file))valid=0;}
            if(valid)valid=MoveFileExW(temporary,output,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
            if(!valid)DeleteFileW(temporary);
        }
    }
    free(font);return valid;
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
    if(!exe || !hotdGameHash(exe,length,HOTD_GAME_SHA256)) {
        free(exe);return "Unsupported HOTD2 executable (SHA256 mismatch). Game not started.";
    }
    int ready=!font || hotdEnsureFont(game,viewer,exe,length);free(exe);
    if(!ready)return "Menu asset extraction failed. Check tex/scr_opt_moji05.bin, tex/scr_item.bin and write access to the mod folder.";
    if(launch && !hotdStartGame(viewer,game,options))return "Game launch failed. Check injector.exe, HOTD2Bridge.dll and HOTD2Bridge.log.";
    return NULL;
}
#endif
