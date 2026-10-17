#include <assert.h>
#include <math.h>
#include "../src/capture_math.h"
#include "../src/game_profile.h"
#include "../src/game_input.h"
#include "../src/texture_atlas.h"
#include "../src/draw_style.h"

static int near(float a, float b) { return fabsf(a-b) < 0.001f; }

int main(void)
{
    float p[3], sx, sy;
    HotdLens lens = { 500, 600, 320, 240 };
    HotdGunInput gun={0};
    unsigned previous=0;
    HotdAtlas atlas={0};
    unsigned ax,ay,bx,by;
    assert(hotd_rgba(0,0,0,.5f)==0x80000000u); /* Native black shadow, half opacity. */
    assert(hotd_rgba(1,1,1,.25f)==0x40FFFFFFu);
    assert(hotd_rgba(NAN,-1,2,1)==0xFFFF0000u);
    assert((hotd_style(1,5,6,1,1,0,8,0)&HOTD_DRAW_STATE_MASK)==0x65);
    assert((hotd_style(1,12,1,0,0,0,8,0)&HOTD_DRAW_STATE_MASK)==0x165);
    assert((hotd_style(0,5,6,1,0,1,5,127)&255)==0);
    assert(!hotd_atlas_alloc(&atlas,4097,1,&ax,&ay));
    assert(!hotd_atlas_alloc(&atlas,0,1,&ax,&ay));
    assert(hotd_atlas_alloc(&atlas,2048,4096,&ax,&ay) && ax==0 && ay==0);
    assert(hotd_atlas_alloc(&atlas,2048,4096,&bx,&by) && bx==2048 && by==0);
    assert(!hotd_atlas_alloc(&atlas,1,1,&ax,&ay));
    hotd_atlas_free(&atlas,0,0,2048,4096);
    assert(hotd_atlas_alloc(&atlas,2048,4096,&ax,&ay) && ax==0 && ay==0);
    assert(!hotd_atlas_alloc(&atlas,16,16,&ax,&ay)); /* Live neighbor survives reuse. */
    hotd_atlas_free(&atlas,0,0,2048,4096);
    hotd_atlas_free(&atlas,2048,0,2048,4096);
    assert(hotd_atlas_alloc(&atlas,66,258,&ax,&ay) && ax==0 && ay==0);
    hotd_atlas_free(&atlas,ax,ay,66,258);
    assert(hotd_atlas_alloc(&atlas,4096,4096,&ax,&ay)); /* Padding freed completely. */
    assert(hotd_apply_fire(&gun,1,&previous));
    assert(gun.valid && gun.tracked && gun.fire);
    gun.fire=0; /* Native mouse/keyboard polling replaces this each frame. */
    assert(!hotd_apply_fire(&gun,1,&previous) && !gun.fire);
    assert(!hotd_apply_fire(&gun,0,&previous) && !gun.fire);
    assert(hotd_apply_fire(&gun,1,&previous) && gun.fire);
    gun.fire=1;
    assert(!hotd_apply_fire(&gun,0,&previous) && gun.fire); /* Preserve physical shot. */
    previous=0;gun.fire=0; /* Watchdog/disconnect releases VR ownership. */
    assert(hotd_apply_fire(&gun,1,&previous) && gun.fire);
    assert(hotd_unproject(&lens, 420, 300, 0.01f, p));
    assert(near(p[0], 20) && near(p[1], -10) && near(p[2], -100));
    assert(hotd_project(&lens, p, &sx, &sy));
    assert(near(sx, 420) && near(sy, 300));
    assert(!hotd_unproject(&lens, 320, 240, 0, p));
    assert(!hotd_unproject(&lens, 320, 240, NAN, p));
    p[2] = 1;
    assert(!hotd_project(&lens, p, &sx, &sy));
    lens.fx = 0;
    assert(!hotd_unproject(&lens, 320, 240, 0.1f, p));
    assert(hotd_fvf_stride(0x1C4) == 32); /* XYZRHW | DIFFUSE | SPECULAR | TEX1 */
    assert(hotd_fvf_stride(0x112) == 32); /* XYZ | NORMAL | TEX1 */
    assert(hotd_fvf_stride(0x144) == 28); /* XYZRHW | DIFFUSE | TEX1 */
    assert(hotd_fvf_stride(0x002) == 12);
    assert(hotd_fvf_stride(0x000) == 0);
    assert(hotd_device_prompt("NVIDIA device will be used. \n","Attention",0x34));
    assert(!hotd_device_prompt("Graphics error","Attention",0x34));
    assert(!hotd_device_prompt("NVIDIA device will be used. \n","Error",0x34));
    assert(!hotd_device_prompt("NVIDIA device will be used. \n","Attention",0x30));
    return 0;
}
