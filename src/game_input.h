#ifndef HOTD_GAME_INPUT_H
#define HOTD_GAME_INPUT_H
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include "hotd_share.h"

typedef struct { unsigned held,action; } HotdTrigger;
static inline unsigned hotd_menu_stick(unsigned previous,int active,float y)
{
    if(!active || !isfinite(y) || fabsf(y)>1.0001f)return 0;
    if(y>=.65f || (previous==VC2_BTN_UP && y>.30f))return VC2_BTN_UP;
    if(y<=-.65f || (previous==VC2_BTN_DOWN && y<-.30f))return VC2_BTN_DOWN;
    return 0;
}
static inline unsigned hotd_trigger_action(HotdTrigger *trigger,int held,int pose_valid,float vertical)
{
    if(!held){trigger->held=trigger->action=0;return 0;}
    pose_valid=pose_valid && isfinite(vertical) && fabsf(vertical)<=1.0001f;
    if(!trigger->held)
        trigger->action=pose_valid ? ((vertical>=0.76604444f || vertical<=-0.70710678f) ? VC2_BTN_RELOAD:VC2_BTN_FIRE):0; /* Up 50deg, down 45deg. */
    trigger->held=1;
    if(!pose_valid)trigger->action=0; /* Tracking loss disarms until release. */
    return trigger->action;
}

/* Native 0x28-byte gun state, shared by mouse and keyboard control schemes. */
typedef struct {
    int16_t raw_x, raw_y, x, y;
    float smooth_x, smooth_y;
    int16_t valid, tracked, fire;
    unsigned char rest[18];
} HotdGunInput;
_Static_assert(sizeof(HotdGunInput)==0x28,"Native gun input layout");
_Static_assert(offsetof(HotdGunInput,valid)==0x10 && offsetof(HotdGunInput,fire)==0x14,
    "Native gun valid/fire offsets");

static inline unsigned hotd_apply_gun_buttons(HotdGunInput *gun,unsigned buttons,unsigned *previous)
{
    unsigned action=buttons&(VC2_BTN_FIRE|VC2_BTN_RELOAD),pressed=action&~*previous;
    *previous=action;
    gun->valid=gun->tracked=1;
    if(action&VC2_BTN_RELOAD) {
        gun->valid=gun->fire=0;
        return pressed&VC2_BTN_RELOAD;
    }
    gun->fire|=(int16_t)((pressed&VC2_BTN_FIRE)!=0);
    return pressed&VC2_BTN_FIRE;
}
#endif
