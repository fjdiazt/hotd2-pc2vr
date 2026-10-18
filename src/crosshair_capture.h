#ifndef HOTD_CROSSHAIR_CAPTURE_H
#define HOTD_CROSSHAIR_CAPTURE_H
#include <stdint.h>
#include <string.h>

#define HOTD_CROSSHAIR_RECORDS 16
#define HOTD_SPRITE_RECORD_SIZE 0x3C
typedef struct {
    const unsigned char *address[HOTD_CROSSHAIR_RECORDS];
    unsigned char records[HOTD_CROSSHAIR_RECORDS][HOTD_SPRITE_RECORD_SIZE];
} HotdCrosshairRecords;

static int hotd_crosshair_definition(const unsigned char *base,const unsigned char *definition,uintptr_t caller)
{
    uint32_t id;
    if(caller!=0x16AE7 && caller!=0x169B6)return 0;
    for(unsigned player=0;player<2;player++)if(definition==base+0x5A5D44+player*0x130) {
        memcpy(&id,definition,sizeof(id));
        for(unsigned style=0;style<4;style++) {
            uint16_t candidate;
            memcpy(&candidate,base+0x179F58+(player*4+style)*2,2);
            if(id==candidate)return 1;
        }
    }
    return 0;
}
static void hotd_remember_crosshair(HotdCrosshairRecords *records,const unsigned char *record)
{
    unsigned free_slot=HOTD_CROSSHAIR_RECORDS;
    for(unsigned i=0;i<HOTD_CROSSHAIR_RECORDS;i++) {
        if(records->address[i]==record){free_slot=i;break;}
        if(!records->address[i] && free_slot==HOTD_CROSSHAIR_RECORDS)free_slot=i;
    }
    if(free_slot<HOTD_CROSSHAIR_RECORDS) {
        records->address[free_slot]=record;
        memcpy(records->records[free_slot],record,HOTD_SPRITE_RECORD_SIZE);
    }
    /* A missed tag leaves a native crosshair visible; unrelated HUD stays intact. */
}
static int hotd_take_crosshair(HotdCrosshairRecords *records,const unsigned char *record)
{
    for(unsigned i=0;i<HOTD_CROSSHAIR_RECORDS;i++)if(records->address[i]==record) {
        int same=!memcmp(records->records[i],record,HOTD_SPRITE_RECORD_SIZE);
        records->address[i]=NULL;return same;
    }
    return 0;
}
static int hotd_crosshair_profile(const unsigned char *base)
{
    static const unsigned char bitmap[]={0x8B,0x44,0x24,0x04,0x83,0xEC,0x38,0x8B,0x08};
    static const unsigned char draw[]={0x81,0xEC,0xB0,0,0,0,0x53,0x55,0x56};
    static const unsigned char submit[]={0x56,0x8B,0x74,0x24,0x08};
    static const unsigned char calls[2][8]={{0xE8,0x49,0x5B,0,0,0x83,0xC4,4},{0xE8,0x7A,0x5C,0,0,0x83,0xC4,0x10}};
    static const uint16_t ids[]={0xAA8,0xAAA,0xAAB,0xAAC,0xAA9,0xAAD,0xAAE,0xAAF};
    return !memcmp(base+0x1C630,bitmap,sizeof(bitmap)) && !memcmp(base+0xA7AB0,draw,sizeof(draw)) &&
        !memcmp(base+0xA7A40,submit,sizeof(submit)) && !memcmp(base+0x16AE2,calls[0],8) &&
        !memcmp(base+0x169B1,calls[1],8) && !memcmp(base+0x179F58,ids,sizeof(ids));
}
#endif
