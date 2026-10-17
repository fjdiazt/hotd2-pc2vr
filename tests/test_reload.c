/* Native input hook against isolated game memory; no game starts. */
#include <assert.h>
#include "../src/bridge.c"

static DWORD __cdecl poll_input(void)
{
    memset(game_base+0x5C8FC0,0,2*sizeof(HotdGunInput));
    *(DWORD *)(game_base+0x5C9028)=0x100;
    return 123;
}
static HRESULT WINAPI poll_keyboard(void *self,DWORD size,void *out)
{(void)self;memset(out,0,size);return DI_OK;}
static void tick(unsigned p1,unsigned p2)
{
    frame->cheat_flags=p1&HOTD_CHEAT_MASK;frame->cheat_tick=GetTickCount();
    frame->rapid_fire_rate=10;
    frame->aim_buttons=p1&~HOTD_CHEAT_MASK;frame->aim2_buttons=p2;frame->aim_seq++;
    assert(native_input()==123);
}
int main(void)
{
    BYTE keys[256];
    game_base=calloc(1,0x5CA200);frame=calloc(1,sizeof(*frame));
    assert(game_base && frame);
    memcpy(game_base+0x15342,"\x8A\x0C\x85\xD8\x9F\x9C\x00",7);
    memcpy(game_base+0x15464,game_base+0x15342,7);
    memcpy(game_base+0x149D9,"\x8A\x51\x79",3);
    memcpy(game_base+0x14C47,"\x8A\x42\x79",3);
    assert(native_cheat_profile(game_base));
    game_base[0x15342]^=1;assert(!native_cheat_profile(game_base));game_base[0x15342]^=1;
    HotdGunInput *gun1=(HotdGunInput *)(game_base+0x5C8FC0),*gun2=gun1+1;
    frame->akimbo=1;frame->aim_x=frame->aim2_x=320;frame->aim_y=frame->aim2_y=240;
    input_ready=1;native_input_original=poll_input;
    *(DWORD *)(game_base+0x5C8E98)=6;
    *(short *)(game_base+0x5A5C62)=*(short *)(game_base+0x5A5D92)=5;
    game_base[0x5A5D85]=1; /* P1 mouse/gun control. */
    game_base[0x5A5EB5]=0; /* P2 keyboard control, scheme 1. */
    *(DWORD *)(game_base+0x5C9F6C+0x7C+20)=0x20000;
    cheats_supported=1;
    game_base[0x5C9FD8+0x7C]=2;game_base[0x5C9FD9+0x7C]=3;
    native_cheats(HOTD_CHEAT_LIVES|HOTD_CHEAT_AMMO);
    for(unsigned p=0;p<2;p++)assert(game_base[0x5C9FD8+p*0x7C]==1 && game_base[0x5C9FD9+p*0x7C]==1);
    native_cheats(HOTD_CHEAT_AMMO);
    assert(!game_base[0x5C9FD8] && game_base[0x5C9FD8+0x7C]==2 && game_base[0x5C9FD9]==1);
    native_cheats(0);
    assert(!game_base[0x5C9FD9] && game_base[0x5C9FD9+0x7C]==3);
    cheats_supported=0;native_cheats(HOTD_CHEAT_MASK);
    assert(!game_base[0x5C9FD8] && !game_base[0x5C9FD9]);
    cheats_supported=1;

    *(DWORD *)(game_base+0x5C8E00)=1;
    tick(VC2_BTN_FIRE|HOTD_CHEAT_MASK,VC2_BTN_FIRE);
    assert(gun1->fire && gun2->fire && game_base[0x5C9FD8] && game_base[0x5C9FD9]);
    tick(VC2_BTN_FIRE|HOTD_CHEAT_MASK,VC2_BTN_FIRE);
    assert(!gun1->fire && !gun2->fire);
    rapid_fire[0].at=GetTickCount()-100;
    tick(VC2_BTN_FIRE|HOTD_CHEAT_MASK,VC2_BTN_FIRE);
    assert(gun1->fire && !gun2->fire);
    rapid_fire[1].at=GetTickCount()-100;
    tick(VC2_BTN_FIRE|HOTD_CHEAT_MASK,VC2_BTN_FIRE);
    assert(!gun1->fire && gun2->fire);
    frame->rapid_fire_rate=2;
    rapid_fire[0].at=GetTickCount()-100;native_input();assert(!gun1->fire);
    rapid_fire[0].at=GetTickCount()-500;native_input();assert(gun1->fire);
    *(short *)(game_base+0x5A1A00)=1;
    rapid_fire[0].at=GetTickCount()-100;
    tick(VC2_BTN_FIRE|HOTD_CHEAT_MASK,VC2_BTN_FIRE);
    assert(!gun1->fire && !gun2->fire); /* Paused: no repeats. */
    *(short *)(game_base+0x5A1A00)=0;
    *(DWORD *)(game_base+0x5C8E00)=0;
    tick(VC2_BTN_FIRE|HOTD_CHEAT_MASK,VC2_BTN_FIRE);
    assert(!gun1->fire && !gun2->fire); /* Cutscene: no repeats. */
    *(DWORD *)(game_base+0x5C8E00)=1;*(DWORD *)(game_base+0x5C8E98)=4;
    tick(VC2_BTN_FIRE|HOTD_CHEAT_MASK,VC2_BTN_FIRE);
    assert(!gun1->fire && !gun2->fire); /* Menu: no repeats. */
    *(DWORD *)(game_base+0x5C8E98)=6;
    *(short *)(game_base+0x5A5C62)=*(short *)(game_base+0x5A5D92)=4;
    tick(VC2_BTN_FIRE|HOTD_CHEAT_MASK,VC2_BTN_FIRE);
    assert(!gun1->fire && !gun2->fire); /* Inactive players: no repeats. */
    *(short *)(game_base+0x5A5C62)=*(short *)(game_base+0x5A5D92)=5;
    tick(VC2_BTN_RELOAD|HOTD_CHEAT_MASK,0);
    assert(!gun1->valid && gun1->fire); /* Reload wins. */
    aim_seq=frame->aim_seq;aim_tick=GetTickCount()-501;
    native_input();
    assert(!gun1->fire && !gun2->fire && game_base[0x5C9FD8] && game_base[0x5C9FD9]);
    frame->cheat_tick=GetTickCount()-501;native_input();
    assert(!gun1->fire && !gun2->fire && !game_base[0x5C9FD8] && !game_base[0x5C9FD9]);
    assert(game_base[0x5C9FD8+0x7C]==2 && game_base[0x5C9FD9+0x7C]==3);
    tick(0,0);

    tick(VC2_BTN_RELOAD,VC2_BTN_FIRE);
    assert(!gun1->valid && gun1->fire==1 && gun2->valid && gun2->fire==1);
    assert(*(DWORD *)(game_base+0x5C9028)==0x100);
    tick(VC2_BTN_RELOAD,VC2_BTN_FIRE);
    assert(!gun1->valid && !gun1->fire && !gun2->fire); /* Held inputs have no new edges. */
    tick(0,0);
    tick(VC2_BTN_FIRE,VC2_BTN_RELOAD);
    assert(gun1->valid && gun1->fire && !gun2->valid && !gun2->fire);
    assert(*(DWORD *)(game_base+0x5C9028)==0x20100); /* Native reload mask + physical input. */
    tick(VC2_BTN_FIRE,VC2_BTN_RELOAD);
    assert(*(DWORD *)(game_base+0x5C9028)==0x100);
    tick(0,0);
    tick(VC2_BTN_BACK,VC2_BTN_FIRE);
    assert(!gun1->valid && gun1->fire && gun2->valid && gun2->fire);
    tick(VC2_BTN_BACK,VC2_BTN_FIRE);
    assert(!gun1->fire && !gun2->fire);
    tick(VC2_BTN_BACK|VC2_BTN_FIRE,0);
    assert(gun1->valid && gun1->fire); /* Held cancel does not suppress a fresh shot. */
    tick(0,0);
    tick(VC2_BTN_FIRE,VC2_BTN_BACK);
    assert(gun1->valid && gun1->fire && *(DWORD *)(game_base+0x5C9028)==0x20100);
    tick(0,0);
    *(DWORD *)(game_base+0x5C8E98)=1; /* Menu, not gameplay. */
    tick(VC2_BTN_RELOAD,VC2_BTN_RELOAD);
    assert(!gun1->fire && !gun2->fire && *(DWORD *)(game_base+0x5C9028)==0x100);
    tick(0,0);
    *(DWORD *)(game_base+0x5C8E98)=6;*(short *)(game_base+0x5A5C62)=9;
    tick(VC2_BTN_RELOAD,0);assert(!gun1->fire); /* Inactive player cannot reload. */
    tick(0,0);
    *(short *)(game_base+0x5A5C62)=4; /* Continue state, not the active weapon handler. */
    tick(VC2_BTN_RELOAD,0);assert(!gun1->fire);
    tick(0,0);
    original_keyboard_state=poll_keyboard;
    frame->aim_buttons=frame->aim2_buttons=VC2_BTN_START;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)));
    assert(keys[DIK_RETURN] && keys[DIK_TAB] && !keys[DIK_RCONTROL] && !keys[DIK_LCONTROL]);
    frame->aim_buttons=VC2_BTN_BACK;frame->aim2_buttons=0;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)));
    assert(keys[DIK_RCONTROL] && !keys[DIK_LCONTROL] && !keys[DIK_ESCAPE]);
    frame->aim_buttons=0;frame->aim2_buttons=VC2_BTN_BACK;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)));
    assert(!keys[DIK_RCONTROL] && keys[DIK_LCONTROL] && !keys[DIK_ESCAPE]);
    *(DWORD *)(game_base+0x5C8E98)=4;
    frame->aim_buttons=VC2_BTN_UP;frame->aim2_buttons=0;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && keys[DIK_UP] && !keys[DIK_DOWN]);
    frame->aim_buttons=0;frame->aim2_buttons=VC2_BTN_DOWN;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && !keys[DIK_UP] && keys[DIK_DOWN]);
    frame->aim_buttons=VC2_BTN_UP;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && !keys[DIK_UP] && !keys[DIK_DOWN]);
    frame->aim_buttons=VC2_BTN_DOWN;frame->aim2_buttons=0;
    *(DWORD *)(game_base+0x5C8E98)=6;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && !keys[DIK_UP] && !keys[DIK_DOWN]);
    aim_seq=frame->aim_seq;aim_tick=GetTickCount()-501;
    poll_input();native_input();
    assert(!gun1->fire && !gun2->fire && !gun1->valid && !gun2->valid);
    *(DWORD *)(game_base+0x5C8E98)=4;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && !keys[DIK_UP] && !keys[DIK_DOWN]);
    free(game_base);free(frame);
    puts("Input/cheats: reload edges, native flag restore, independent rapid fire, menu/pause guards and watchdogs passed.");
    return 0;
}
