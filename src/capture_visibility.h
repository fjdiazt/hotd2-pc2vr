#ifndef HOTD_CAPTURE_VISIBILITY_H
#define HOTD_CAPTURE_VISIBILITY_H
#include <stdint.h>
#include <string.h>

#define HOTD_STAGE_RECORDS 64
#define HOTD_MODEL_RECORD_SIZE 0x74
typedef struct {
    unsigned count, overflow;
    unsigned char records[HOTD_STAGE_RECORDS][HOTD_MODEL_RECORD_SIZE-8];
} HotdStageRecords;

/* Match queued identity independently of its replaced sort flags/depth. */
static int hotd_stage_record(const HotdStageRecords *records,const unsigned char *record)
{
    if(records->overflow)return 0;
    for(unsigned i=0;i<records->count;i++)
        if(!memcmp(records->records[i],record+8,HOTD_MODEL_RECORD_SIZE-8))return 1;
    return 0;
}
static int hotd_remember_stage(HotdStageRecords *records,const unsigned char *record)
{
    if(records->overflow)return 0;
    if(hotd_stage_record(records,record))return 1;
    if(records->count==HOTD_STAGE_RECORDS){records->overflow=1;return 0;}
    memcpy(records->records[records->count++],record+8,HOTD_MODEL_RECORD_SIZE-8);
    return 1;
}
static uint32_t hotd_visibility_expand(uint32_t flags,int stage,int alternate)
{
    if(stage && (flags&HOTD_RENDER_EXPAND_WORLD))return 0x0003F000u;
    /* Existing body-mesh expansion affected the normal renderer only. */
    return !alternate && !(flags&HOTD_RENDER_NATIVE_MESH_CULLING)?0x0000F000u:0;
}
static int hotd_stage_visibility_caller(uintptr_t rva)
{return rva==0x1380 || rva==0x13CC || rva==0x1413;}

static int hotd_visibility_profile(const unsigned char *base)
{
    static const unsigned mesh_sites[]={0xA7F81,0xA84C1};
    static const unsigned submit_sites[]={0xA7E50,0xA8390};
    static const unsigned gate_sites[]={0x137B,0x13C7,0x140E};
    static const unsigned char mesh[]={0xF7,0x44,0x24,0x18,0x00,0xF0,0xFF,0x00};
    static const unsigned char submit[]={0x56,0x8B,0x74,0x24,0x08};
    static const unsigned char gate[3][5]={{0xE8,0x90,0x4F,0,0},{0xE8,0x44,0x4F,0,0},{0xE8,0xFD,0x4E,0,0}};
    static const unsigned char after_gate[]={0x83,0xC4,0x10,0x85,0xC0};
    static const unsigned char sphere[]={0x83,0xEC,0x38,0xD9,0x05,0xCC,0x49,0x4C,0x00};
    for(unsigned i=0;i<2;i++)
        if(memcmp(base+mesh_sites[i],mesh,sizeof(mesh)) ||
           memcmp(base+submit_sites[i],submit,sizeof(submit)))return 0;
    for(unsigned i=0;i<3;i++)
        if(memcmp(base+gate_sites[i],gate[i],sizeof(gate[i])) ||
           memcmp(base+gate_sites[i]+5,after_gate,sizeof(after_gate)))return 0;
    return !memcmp(base+0x6310,sphere,sizeof(sphere));
}
#endif
