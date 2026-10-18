#ifndef HOTD_SPRITE_CAPTURE_H
#define HOTD_SPRITE_CAPTURE_H
#include <stdint.h>
#include <string.h>
#define HOTD_SPRITE_RECORDS 16
#define HOTD_SPRITE_RECORD_SIZE 0x3C
typedef struct {
    const unsigned char *address[HOTD_SPRITE_RECORDS];
    unsigned char records[HOTD_SPRITE_RECORDS][HOTD_SPRITE_RECORD_SIZE];
} HotdSpriteRecords;

static void hotd_remember_sprite(HotdSpriteRecords *records,const unsigned char *record)
{
    unsigned free_slot=HOTD_SPRITE_RECORDS;
    for(unsigned i=0;i<HOTD_SPRITE_RECORDS;i++) {
        if(records->address[i]==record){free_slot=i;break;}
        if(!records->address[i] && free_slot==HOTD_SPRITE_RECORDS)free_slot=i;
    }
    if(free_slot<HOTD_SPRITE_RECORDS) {
        records->address[free_slot]=record;
        memcpy(records->records[free_slot],record,HOTD_SPRITE_RECORD_SIZE);
    }
    /* A missed ownership tag retains the original capture. */
}
static int hotd_take_sprite(HotdSpriteRecords *records,const unsigned char *record)
{
    for(unsigned i=0;i<HOTD_SPRITE_RECORDS;i++)if(records->address[i]==record) {
        int same=!memcmp(records->records[i],record,HOTD_SPRITE_RECORD_SIZE);
        records->address[i]=NULL;return same;
    }
    return 0;
}
#endif
