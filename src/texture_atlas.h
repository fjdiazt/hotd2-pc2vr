#ifndef HOTD_TEXTURE_ATLAS_H
#define HOTD_TEXTURE_ATLAS_H
#include <stdint.h>

/* Small cells allow freed tiles to be reused without moving live textures. */
#define HOTD_ATLAS_CELL 16u
#define HOTD_ATLAS_SIDE 256u
typedef struct { uint32_t rows[HOTD_ATLAS_SIDE][HOTD_ATLAS_SIDE/32]; } HotdAtlas;

static inline int hotd_atlas_alloc(HotdAtlas *atlas,unsigned w,unsigned h,unsigned *px,unsigned *py)
{
    unsigned x,y,xx,yy,cw=(w+15)/16,ch=(h+15)/16;
    if(!w || !h || w>4096 || h>4096)return 0;
    for(y=0;y+ch<=HOTD_ATLAS_SIDE;y++)for(x=0;x+cw<=HOTD_ATLAS_SIDE;x++) {
        for(yy=y;yy<y+ch;yy++) {
            for(xx=x;xx<x+cw;xx++)if(atlas->rows[yy][xx/32] & (1u<<(xx%32)))break;
            if(xx<x+cw)break;
        }
        if(yy<y+ch){x=xx;continue;}
        for(yy=y;yy<y+ch;yy++)for(xx=x;xx<x+cw;xx++)atlas->rows[yy][xx/32]|=1u<<(xx%32);
        *px=x*HOTD_ATLAS_CELL;*py=y*HOTD_ATLAS_CELL;
        return 1;
    }
    return 0;
}

static inline void hotd_atlas_free(HotdAtlas *atlas,unsigned x,unsigned y,unsigned w,unsigned h)
{
    unsigned xx,yy,cw=(w+15)/16,ch=(h+15)/16;
    x/=HOTD_ATLAS_CELL;y/=HOTD_ATLAS_CELL;
    for(yy=y;yy<y+ch;yy++)for(xx=x;xx<x+cw;xx++)atlas->rows[yy][xx/32]&=~(1u<<(xx%32));
}
#endif
