#ifndef HOTD_GAME_INPUT_H
#define HOTD_GAME_INPUT_H
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include "hotd_share.h"

typedef struct { unsigned held,action; } HotdTrigger;
static inline int hotd_player_active(unsigned phase,int status)
{
    /* Join/rejoin transitions, active play and accepted joins waiting for a cutscene. */
    return phase==6 && ((status>=0 && status<=3) || status==5 || status==10);
}
static inline int hotd_hand_visible(int akimbo,unsigned active_players,int hand)
{
    return akimbo || (active_players&(1u<<(1-hand))); /* Right P1, left P2. */
}
typedef struct { uint32_t at; unsigned active; } HotdRapid;
static inline int hotd_parse_rapid_rate(const char *text,unsigned *out)
{
    char *end;
    unsigned long rate;
    if(!text || *text<'0' || *text>'9')return 0;
    rate=strtoul(text,&end,10);
    if(*end || rate<1 || rate>30)return 0;
    *out=(unsigned)rate;return 1;
}
static inline int hotd_rapid_fire(HotdRapid *repeat,int held,int enabled,unsigned rate,uint32_t now)
{
    if(!held || !enabled || rate<1 || rate>30){repeat->active=0;return 0;}
    if(!repeat->active || (uint64_t)(uint32_t)(now-repeat->at)*rate>=1000) {
        repeat->active=1;repeat->at=now;return 1;
    }
    return 0;
}
static inline unsigned hotd_menu_stick(unsigned previous,int active,float y)
{
    if(!active || !isfinite(y) || fabsf(y)>1.0001f)return 0;
    if(y>=.65f || (previous==VC2_BTN_UP && y>.30f))return VC2_BTN_UP;
    if(y<=-.65f || (previous==VC2_BTN_DOWN && y<-.30f))return VC2_BTN_DOWN;
    return 0;
}
/* Head-local forward-vector Y and horizontal yaw in radians. */
static inline unsigned hotd_trigger_action(HotdTrigger *trigger,int held,int pose_valid,float vertical,float yaw_radians)
{
    if(!held){trigger->held=trigger->action=0;return 0;}
    pose_valid=pose_valid && isfinite(vertical) && fabsf(vertical)<=1.0001f &&
        isfinite(yaw_radians) && fabsf(yaw_radians)<=3.141593f;
    if(!trigger->held)
        trigger->action=pose_valid ? ((vertical>=0.76604444f || vertical<=-0.70710678f || fabsf(yaw_radians)>=1.04719755f) ? VC2_BTN_RELOAD:VC2_BTN_FIRE):0; /* Headset-relative: up 50deg, down 45deg, sides 60deg. */
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
