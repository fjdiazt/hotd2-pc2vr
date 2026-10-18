#ifndef HOTD_CHECKPOINT_H
#define HOTD_CHECKPOINT_H
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define HOTD_CHECKPOINT_MAX_SCENE 255
typedef struct { int stage,scene; } HotdCheckpoint;

static inline int hotd_checkpoint_number(const wchar_t *text,int minimum,int maximum,int *out)
{
    int value=0;
    if(!text || !*text)return 0;
    for(;*text;text++) {
        int digit=*text-L'0';
        if(digit<0 || digit>9 || value>maximum/10 ||
           (value==maximum/10 && digit>maximum%10))return 0;
        value=value*10+digit;
    }
    if(value<minimum)return 0;
    *out=value;return 1;
}
static inline int hotd_checkpoint_first_scene(int stage)
{
    return stage==3?7:0;
}
static inline int hotd_checkpoint_valid(HotdCheckpoint point)
{
    return point.stage>=1 && point.stage<=6 && point.scene>=0 && point.scene<=HOTD_CHECKPOINT_MAX_SCENE;
}
static inline int hotd_checkpoint_read(const char *path,HotdCheckpoint *point)
{
    char value[32];wchar_t wide[32];
    const char *keys[]={"version","stage","scene"};int numbers[3];
    for(unsigned i=0;i<3;i++) {
        DWORD n=GetPrivateProfileStringA("checkpoint",keys[i],"",value,sizeof(value),path);
        if(!n || n>=sizeof(value)-1)return 0;
        for(unsigned j=0;j<=n;j++)wide[j]=(unsigned char)value[j];
        if(!hotd_checkpoint_number(wide,i==2?0:1,i==0?1:i==1?6:HOTD_CHECKPOINT_MAX_SCENE,&numbers[i]))return 0;
    }
    *point=(HotdCheckpoint){numbers[1],numbers[2]};return 1;
}
static inline int hotd_checkpoint_write(const char *path,HotdCheckpoint point)
{
    char temporary[MAX_PATH],text[128];DWORD written,error;HANDLE file;int ok,n;
    if(!hotd_checkpoint_valid(point) || strlen(path)+5>=sizeof(temporary))return 0;
    snprintf(temporary,sizeof(temporary),"%s.tmp",path);
    n=snprintf(text,sizeof(text),"[checkpoint]\r\nversion=1\r\nstage=%d\r\nscene=%d\r\n",point.stage,point.scene);
    file=CreateFileA(temporary,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return 0;
    ok=WriteFile(file,text,n,&written,NULL) && written==(DWORD)n && FlushFileBuffers(file);
    error=ok?ERROR_SUCCESS:GetLastError();
    if(!CloseHandle(file) && ok){ok=0;error=GetLastError();}
    if(ok && !MoveFileExA(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){ok=0;error=GetLastError();}
    if(!ok){DeleteFileA(temporary);SetLastError(error);}
    return ok;
}
#endif
