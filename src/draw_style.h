#ifndef HOTD_DRAW_STYLE_H
#define HOTD_DRAW_STYLE_H
#include <math.h>
#include <stdint.h>
#define HOTD_NO_ZWRITE 256u
#define HOTD_DIFFUSE_ALPHA 512u
#define HOTD_ALPHA_TEST 1024u
#define HOTD_DRAW_STATE_MASK 511u

static inline uint32_t hotd_style(unsigned blend,unsigned src,unsigned dst,unsigned zwrite,
    unsigned diffuse_alpha,unsigned test,unsigned func,unsigned ref)
{
    if(src==12){src=5;dst=6;}
    if(src==13){src=6;dst=5;}
    return (blend && src>=1 && src<=11 && dst>=1 && dst<=11 ? src|(dst<<4):0) |
        (!zwrite?HOTD_NO_ZWRITE:0) | (diffuse_alpha?HOTD_DIFFUSE_ALPHA:0) |
        (test?HOTD_ALPHA_TEST:0) | ((func&7)<<11) | ((ref&255)<<14);
}
static inline unsigned hotd_unorm(float value)
{
    if(!isfinite(value) || value<=0)return 0;
    if(value>=1)return 255;
    return (unsigned)(value*255+0.5f);
}
static inline uint32_t hotd_rgba(float r,float g,float b,float a)
{
    return hotd_unorm(r)|(hotd_unorm(g)<<8)|(hotd_unorm(b)<<16)|(hotd_unorm(a)<<24);
}
#endif
