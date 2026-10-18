#ifndef HOTD_CHECKPOINT_H
#define HOTD_CHECKPOINT_H
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
static inline int hotd_checkpoint_table_word(const BYTE *tables,const unsigned lengths[2],uint32_t address,uint32_t *word)
{
    unsigned offset=address-0x977200u;
    if((address&3) || offset>0x2A800-4 ||
       !(offset+4<=lengths[0] || (offset>=0x200 && offset+4<=0x200+lengths[1])))return 0;
    memcpy(word,tables+offset,4);return 1;
}
/* Enumerate native Arcade scripts once per stage; every disk-derived pointer is bounded. */
static inline int hotd_checkpoint_file_scenes(const char *directory,int stage,uint32_t scenes[8])
{
    const unsigned capacity=0x2A800;unsigned lengths[2]={0};int ok=0;
    BYTE *tables=calloc(1,capacity);char path[MAX_PATH];FILE *file;
    memset(scenes,0,8*sizeof(*scenes));
    if(!tables || stage<1 || stage>6)goto done;
    for(unsigned i=0;i<2;i++) {
        int n=i?snprintf(path,sizeof(path),"%s\\evt\\st%devtbl.bin",directory,stage):
            snprintf(path,sizeof(path),"%s\\evt\\comevtbl.bin",directory);
        if(n<0 || n>=(int)sizeof(path))goto done;
        file=fopen(path,"rb");if(!file)goto done;
        unsigned limit=i?capacity-0x200:0x200;
        lengths[i]=(unsigned)fread(tables+(i?0x200:0),1,limit,file);
        int valid=!ferror(file) && fgetc(file)==EOF;fclose(file);
        if(!valid || lengths[i]<4)goto done;
    }
    for(unsigned offset=0;offset+4<=capacity;offset+=4) {
        DWORD word;memcpy(&word,tables+offset,4);
        if((word&0xFFF80000u)==0x0CE80000u){word+=0xF3AC1A00u;memcpy(tables+offset,&word,4);}
    }
    uint32_t list,variants,script,opcode;
    if(!hotd_checkpoint_table_word(tables,lengths,0x977200+(stage-1)*4,&list))goto done;
    for(unsigned scene=0;scene<=HOTD_CHECKPOINT_MAX_SCENE;scene++) {
        if(!hotd_checkpoint_table_word(tables,lengths,list+scene*4,&variants) || !variants || variants==0xFFFFFFFFu)break;
        if(hotd_checkpoint_table_word(tables,lengths,variants+4,&script) &&
           hotd_checkpoint_table_word(tables,lengths,script,&opcode) && opcode>0 && opcode<0x60 &&
           opcode!=0x2A && opcode!=0x34 && opcode!=0x3C && opcode!=0x4C) {
            scenes[scene/32]|=1u<<(scene%32);ok=1;
        }
    }
done:
    free(tables);return ok;
}
static inline int hotd_checkpoint_file_scene(const char *directory,HotdCheckpoint point)
{
    uint32_t scenes[8];
    return hotd_checkpoint_valid(point) && hotd_checkpoint_file_scenes(directory,point.stage,scenes) &&
        (scenes[point.scene/32]&(1u<<(point.scene%32)))!=0;
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
