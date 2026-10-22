#ifndef HOTD_PLAYER_HUD_H
#define HOTD_PLAYER_HUD_H
#include <float.h>
#include <math.h>
#include <string.h>
#include "hotd_share.h"

static inline unsigned hotd_player_hud_owner(unsigned player,unsigned kind)
{
    return player<2 && kind<3 ? ((kind*2+player+1)<<HOTD_PLAYER_HUD_SHIFT):0;
}
static inline unsigned hotd_player_hud_group(const unsigned char *task,unsigned kind)
{
    uint32_t player;
    if(!task || kind>1)return 0;
    memcpy(&player,task+0x34,4);
    return hotd_player_hud_owner(player,kind);
}
static inline int hotd_player_hud_profile(const unsigned char *base)
{
    static const unsigned char ammo[]={0x83,0xEC,0x14,0x8B,0x44,0x24,0x18};
    static const unsigned char lives[]={0x8B,0x44,0x24,0x04,0x56,0x57};
    static const unsigned char credits[]={0x83,0x3D,0x98,0x8E,0x9C,0x00,0x06};
    return !memcmp(base+0x177D0,ammo,sizeof(ammo)) && !memcmp(base+0x174A0,lives,sizeof(lives)) &&
        !memcmp(base+0x6CE0,credits,sizeof(credits));
}
static inline unsigned hotd_vertex_hud_group(const Vc2Vertex *v)
{
    unsigned group=(v->address&HOTD_PLAYER_HUD_MASK)>>HOTD_PLAYER_HUD_SHIFT;
    return group<=HOTD_PLAYER_HUD_GROUPS && !(v->address&HOTD_GFILE) && !(v->style&(HOTD_WORLD|HOTD_SHOT)) ? group:0;
}
static inline void hotd_swap_player_hud(Vc2Vertex *vertices,unsigned start,unsigned count)
{
    float low[HOTD_PLAYER_HUD_GROUPS],high[HOTD_PLAYER_HUD_GROUPS];
    for(unsigned g=0;g<HOTD_PLAYER_HUD_GROUPS;g++){low[g]=FLT_MAX;high[g]=-FLT_MAX;}
    for(unsigned i=start;i<count;i++) {
        unsigned group=hotd_vertex_hud_group(vertices+i);
        if(group && isfinite(vertices[i].x)) {
            unsigned g=group-1;
            if(vertices[i].x<low[g])low[g]=vertices[i].x;
            if(vertices[i].x>high[g])high[g]=vertices[i].x;
        }
    }
    /* Translate each complete row to the opposite side; preserve glyph/number order and UVs. */
    for(unsigned i=start;i<count;i++) {
        unsigned group=hotd_vertex_hud_group(vertices+i);
        if(group && low[group-1]!=FLT_MAX && isfinite(vertices[i].x)) {
            float offset=low[group-1]+high[group-1];
            if(isfinite(offset))vertices[i].x-=offset;
        }
    }
}
#endif
