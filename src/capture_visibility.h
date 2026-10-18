#ifndef HOTD_CAPTURE_VISIBILITY_H
#define HOTD_CAPTURE_VISIBILITY_H
#include <stdint.h>
#include <string.h>

#define HOTD_STAGE_RECORDS 64
#define HOTD_MODEL_RECORD_SIZE 0x74
typedef struct {
    unsigned count, overflow, accepted;
    const unsigned char *address[HOTD_STAGE_RECORDS];
    unsigned char records[HOTD_STAGE_RECORDS][HOTD_MODEL_RECORD_SIZE];
} HotdStageRecords;

/* Tag only owned queue copies; an unrelated identical model is not provenance. */
static int hotd_stage_record(const HotdStageRecords *records,const unsigned char *record)
{
    for(unsigned i=0;i<HOTD_STAGE_RECORDS;i++)
        if(records->address[i]==record && !memcmp(records->records[i],record,HOTD_MODEL_RECORD_SIZE))return 1;
    return 0;
}
static int hotd_remember_stage(HotdStageRecords *records,const unsigned char *record)
{
    if(records->overflow)return 0;
    if(hotd_stage_record(records,record))return 1;
    for(unsigned i=0;i<HOTD_STAGE_RECORDS;i++)if(!records->address[i]) {
        records->address[i]=record;memcpy(records->records[i],record,HOTD_MODEL_RECORD_SIZE);
        records->count++;records->accepted++;return 1;
    }
    records->overflow=1;return 0;
}
static int hotd_take_stage(HotdStageRecords *records,const unsigned char *record)
{
    for(unsigned i=0;i<HOTD_STAGE_RECORDS;i++)if(records->address[i]==record) {
        int same=!memcmp(records->records[i],record,HOTD_MODEL_RECORD_SIZE);
        records->address[i]=NULL;records->count--;return same;
    }
    return 0;
}

typedef struct {
    uintptr_t list,cursor;
    uint32_t count,capacity;
} HotdNativeQueue;

static int hotd_native_queue_valid(HotdNativeQueue q,uintptr_t expected_list)
{
    uintptr_t start;
    if(q.list!=expected_list || !q.capacity || q.capacity>1024 || q.count>q.capacity ||
       q.list>UINTPTR_MAX-4*((q.capacity+3u)&~3u)-q.capacity*HOTD_MODEL_RECORD_SIZE)return 0;
    start=q.list+4*((q.capacity+3u)&~3u);
    return q.cursor>=start+q.count*0x3Cu && q.cursor<=start+q.count*HOTD_MODEL_RECORD_SIZE && !(q.cursor&3);
}
static int hotd_native_queue_room(HotdNativeQueue q,uintptr_t expected_list)
{return hotd_native_queue_valid(q,expected_list) && q.count<q.capacity;}
static int hotd_native_queue_appended(HotdNativeQueue before,HotdNativeQueue after,uintptr_t expected_list)
{
    return hotd_native_queue_room(before,expected_list) && hotd_native_queue_valid(after,expected_list) &&
        before.list==after.list && before.capacity==after.capacity && after.count==before.count+1 &&
        after.cursor==before.cursor+HOTD_MODEL_RECORD_SIZE;
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
    static const unsigned submit_sites[]={0xA7E50,0xA8390,0xA7A40};
    static const unsigned gate_sites[]={0x137B,0x13C7,0x140E};
    static const unsigned char mesh[]={0xF7,0x44,0x24,0x18,0x00,0xF0,0xFF,0x00};
    static const unsigned char submit[]={0x56,0x8B,0x74,0x24,0x08};
    static const unsigned char gate[3][5]={{0xE8,0x90,0x4F,0,0},{0xE8,0x44,0x4F,0,0},{0xE8,0xFD,0x4E,0,0}};
    static const unsigned char after_gate[]={0x83,0xC4,0x10,0x85,0xC0};
    static const unsigned char sphere[]={0x83,0xEC,0x38,0xD9,0x05,0xCC,0x49,0x4C,0x00};
    static const unsigned char queue[]={0xA1,0xC8,0x78,0x7E,0,0x8B,0x0D,0xC4,0x78,0x7E,0};
    for(unsigned i=0;i<2;i++)if(memcmp(base+mesh_sites[i],mesh,sizeof(mesh)))return 0;
    for(unsigned i=0;i<3;i++)if(memcmp(base+submit_sites[i],submit,sizeof(submit)))return 0;
    for(unsigned i=0;i<3;i++)
        if(memcmp(base+gate_sites[i],gate[i],sizeof(gate[i])) ||
           memcmp(base+gate_sites[i]+5,after_gate,sizeof(after_gate)))return 0;
    return !memcmp(base+0x6310,sphere,sizeof(sphere)) && !memcmp(base+0xA7A10,queue,sizeof(queue));
}
#endif
