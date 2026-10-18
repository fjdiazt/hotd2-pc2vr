#ifndef HOTD_AUDIO_CONTROL_H
#define HOTD_AUDIO_CONTROL_H
#include <stdint.h>
#include <string.h>
#include <math.h>

#define HOTD_AUDIO_CHANNELS 21
#define HOTD_AUDIO_DEFAULT 0x00646464u
static int hotd_parse_volume(const char *text,unsigned *out)
{
    unsigned value=0;
    if(!text || !*text || !out)return 0;
    for(;*text;text++) {
        if(*text<'0' || *text>'9')return 0;
        value=value*10+(unsigned)(*text-'0');if(value>100)return 0;
    }
    *out=value;return 1;
}
static uint32_t hotd_pack_audio(unsigned master,unsigned music,unsigned effects)
{return (master<=100?master:100)|((music<=100?music:100)<<8)|((effects<=100?effects:100)<<16);}
static uint32_t hotd_audio_sanitize(uint32_t levels)
{
    if(levels&0xFF000000u)return HOTD_AUDIO_DEFAULT;
    return hotd_pack_audio(levels&255,(levels>>8)&255,(levels>>16)&255);
}
static int32_t hotd_audio_attenuation(uint32_t levels,unsigned channel)
{
    levels=hotd_audio_sanitize(levels);
    unsigned gain=(levels&255)*((levels>>(channel==15?8:16))&255);
    return gain ? (int32_t)lround(2000.0*log10(gain/10000.0)) : -10000;
}
static int hotd_audio_profile(const unsigned char *base)
{
    static const unsigned char play[]={0xA1,0xB8,0xE5,0x7D,0};
    static const unsigned char count[]={0x3B,0x3D,0xBC,0xE5,0x7D,0};
    static const unsigned char array[]={0xA1,0xA8,0xE5,0x7D,0};
    static const unsigned char stride[]={0xC1,0xE6,0x04,0x03,0xF7,0xC1,0xE6,0x02};
    static const unsigned char channels[]={0x6A,0x15,0x50,0xE8,0xEA,0x6F,0xFF,0xFF};
    static const unsigned char stream[]={0x25,0xB0,0xFF,0x01,0,0x05,0xE0,0x81,0x01,0,0x0C,0x08};
    static const unsigned char sample[]={0x25,0xD0,0xFF,0x01,0,0x05,0xE0,0x80,0,0,0x0C,0x02};
    return !memcmp(base+0xA34B0,play,sizeof(play)) && !memcmp(base+0xA34CE,count,sizeof(count)) &&
        !memcmp(base+0xA34DA,array,sizeof(array)) && !memcmp(base+0xA34E1,stride,sizeof(stride)) &&
        !memcmp(base+0xABF5E,channels,sizeof(channels)) && !memcmp(base+0xA4192,stream,sizeof(stream)) &&
        !memcmp(base+0xA42CD,sample,sizeof(sample));
}
#endif
