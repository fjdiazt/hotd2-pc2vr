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
    HotdTrigger trigger={0},other={0};
    HotdRapid repeat={0},repeat2={0};
    assert(hotd_rapid_fire(&repeat,1,1,10,0));
    assert(!hotd_rapid_fire(&repeat,1,1,10,99));
    assert(hotd_rapid_fire(&repeat,1,1,10,100));
    assert(hotd_rapid_fire(&repeat2,1,1,10,125)); /* Independent hands. */
    assert(!hotd_rapid_fire(&repeat,1,1,10,199));
    assert(!hotd_rapid_fire(&repeat,0,1,10,200));
    assert(hotd_rapid_fire(&repeat,1,1,10,201)); /* Repress fires immediately. */
    assert(!hotd_rapid_fire(&repeat,1,0,10,500));
    assert(hotd_rapid_fire(&repeat,1,1,10,501));
    repeat.at=UINT32_MAX-49;repeat.active=1;
    assert(!hotd_rapid_fire(&repeat,1,1,10,49));
    assert(hotd_rapid_fire(&repeat,1,1,10,50)); /* Tick counter wraps. */
    repeat.active=0;
    assert(hotd_rapid_fire(&repeat,1,1,2,0));
    assert(!hotd_rapid_fire(&repeat,1,1,2,499));
    assert(hotd_rapid_fire(&repeat,1,1,2,500));
    repeat.active=0;
    assert(hotd_rapid_fire(&repeat,1,1,4,0));
    assert(!hotd_rapid_fire(&repeat,1,1,4,249));
    assert(hotd_rapid_fire(&repeat,1,1,4,250));
    repeat.active=0;
    assert(hotd_rapid_fire(&repeat,1,1,3,0));
    assert(!hotd_rapid_fire(&repeat,1,1,3,333));
    assert(hotd_rapid_fire(&repeat,1,1,3,334));
    assert(!hotd_rapid_fire(&repeat,1,1,0,1000));
    assert(!hotd_rapid_fire(&repeat,1,1,31,1000));
    assert(hotd_rapid_fire(&repeat,1,1,30,1000));
    assert(!hotd_rapid_fire(&repeat,1,1,30,1033));
    assert(hotd_rapid_fire(&repeat,1,1,30,1034));
    unsigned rate=99;
    assert(hotd_parse_rapid_rate("2",&rate) && rate==2);
    assert(hotd_parse_rapid_rate("1",&rate) && rate==1);
    assert(hotd_parse_rapid_rate("30",&rate) && rate==30);
    const char *bad_rates[]={"0","31","-1","2.5","2x",""," 2","999999999999999999999999999"};
    for(unsigned i=0;i<sizeof(bad_rates)/sizeof(*bad_rates);i++)assert(!hotd_parse_rapid_rate(bad_rates[i],&rate) && rate==30);
    assert(hotd_menu_stick(0,1,.7f)==VC2_BTN_UP);
    assert(hotd_menu_stick(VC2_BTN_UP,1,.4f)==VC2_BTN_UP);
    assert(!hotd_menu_stick(VC2_BTN_UP,1,.2f));
    assert(hotd_menu_stick(VC2_BTN_UP,1,-.7f)==VC2_BTN_DOWN);
    assert(hotd_menu_stick(VC2_BTN_DOWN,1,-.4f)==VC2_BTN_DOWN);
    assert(!hotd_menu_stick(VC2_BTN_DOWN,0,-1));
    assert(!hotd_menu_stick(0,1,NAN) && !hotd_menu_stick(0,1,2));
    assert(hotd_bitmap_dc_caller(0x9EAAB) && hotd_bitmap_dc_caller(0x9F1A9));
    assert(!hotd_bitmap_dc_caller(0x9EAAA) && !hotd_bitmap_dc_caller(0x9F1AA) && !hotd_bitmap_dc_caller(0));
    const float angles[]={49.9f,50.1f,-44.9f,-45.1f,45.1f};
    const unsigned actions[]={VC2_BTN_FIRE,VC2_BTN_RELOAD,VC2_BTN_FIRE,VC2_BTN_RELOAD,VC2_BTN_FIRE};
    for(unsigned i=0;i<sizeof(angles)/sizeof(*angles);i++) {
        hotd_trigger_action(&trigger,0,1,0);
        assert(hotd_trigger_action(&trigger,1,1,sinf(angles[i]*0.01745329252f))==actions[i]);
    }
    hotd_trigger_action(&trigger,0,1,0);
    assert(hotd_trigger_action(&trigger,1,1,.5f)==VC2_BTN_FIRE);
    assert(hotd_trigger_action(&trigger,1,1,-1)==VC2_BTN_FIRE); /* Same pull stays a shot. */
    assert(!hotd_trigger_action(&trigger,0,1,0));
    assert(hotd_trigger_action(&trigger,1,1,-.866026f)==VC2_BTN_RELOAD);
    assert(hotd_trigger_action(&trigger,1,1,0)==VC2_BTN_RELOAD); /* No shot when raised while held. */
    assert(hotd_trigger_action(&other,1,1,0)==VC2_BTN_FIRE); /* Independent hands. */
    hotd_trigger_action(&trigger,0,1,0);
    assert(hotd_trigger_action(&trigger,1,1,.866026f)==VC2_BTN_RELOAD);
    hotd_trigger_action(&trigger,0,1,0);
    assert(hotd_trigger_action(&trigger,1,1,.76f)==VC2_BTN_FIRE);
    assert(!hotd_trigger_action(&trigger,1,0,0)); /* Tracking loss disarms this pull. */
    assert(!hotd_trigger_action(&trigger,1,1,0));
    hotd_trigger_action(&trigger,0,1,0);
    assert(!hotd_trigger_action(&trigger,1,1,NAN));
    assert(!hotd_trigger_action(&trigger,1,1,0));
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
    assert(hotd_apply_gun_buttons(&gun,1,&previous));
    assert(gun.valid && gun.tracked && gun.fire);
    gun.fire=0; /* Native mouse/keyboard polling replaces this each frame. */
    assert(!hotd_apply_gun_buttons(&gun,1,&previous) && !gun.fire);
    assert(!hotd_apply_gun_buttons(&gun,0,&previous) && !gun.fire);
    assert(hotd_apply_gun_buttons(&gun,1,&previous) && gun.fire);
    gun.fire=1;
    assert(!hotd_apply_gun_buttons(&gun,0,&previous) && gun.fire); /* Preserve physical shot. */
    previous=0;gun.fire=0; /* Watchdog/disconnect releases VR ownership. */
    assert(hotd_apply_gun_buttons(&gun,1,&previous) && gun.fire);
    hotd_apply_gun_buttons(&gun,0,&previous);
    assert(hotd_apply_gun_buttons(&gun,VC2_BTN_FIRE|VC2_BTN_RELOAD,&previous)==VC2_BTN_RELOAD);
    assert(!gun.valid && !gun.fire);
    assert(!hotd_apply_gun_buttons(&gun,VC2_BTN_RELOAD,&previous) && !gun.fire);
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
