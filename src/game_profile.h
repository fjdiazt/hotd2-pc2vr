#ifndef HOTD_GAME_PROFILE_H
#define HOTD_GAME_PROFILE_H
#include <string.h>
static inline int hotd_device_prompt(const char *text,const char *caption,unsigned flags)
{
    static const char suffix[]=" will be used. \n";
    size_t n;
    if(!text || !caption || strcmp(caption,"Attention") || (flags&15)!=4)return 0;
    n=strlen(text);
    return n>=sizeof(suffix)-1 && !strcmp(text+n-(sizeof(suffix)-1),suffix);
}
#endif
