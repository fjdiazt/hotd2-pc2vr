/* Native input hook against isolated game memory; no game starts. */
#include <assert.h>
#include "../src/bridge.c"

static DWORD polled_input=0x100;
static DWORD __cdecl poll_input(void)
{
    memset(game_base+0x5C8FC0,0,2*sizeof(HotdGunInput));
    *(DWORD *)(game_base+0x5C9028)=polled_input;
    return 123;
}
static unsigned physical_keys;
static HRESULT WINAPI poll_keyboard(void *self,DWORD size,void *out)
{
    (void)self;memset(out,0,size);
    if(size==256) {
        BYTE *keys=out;
        if(physical_keys&1)keys[DIK_RETURN]=0x80;
        if(physical_keys&2)keys[DIK_TAB]=0x80;
        if(physical_keys&4)keys[DIK_RCONTROL]=0x80;
        if(physical_keys&8)keys[DIK_LCONTROL]=0x80;
    }
    return DI_OK;
}
static DWORD pause_seen;
static int __cdecl pause_probe(void)
{pause_seen=*(DWORD *)(game_base+0x5C9028);return 77;}
static void pause_tick(unsigned p1,unsigned p2,DWORD native,DWORD expected)
{
    frame->aim_buttons=p1;frame->aim2_buttons=p2;frame->aim_seq++;
    *(DWORD *)(game_base+0x5C9028)=native;
    assert(native_pause()==77 && pause_seen==expected);
    assert(*(DWORD *)(game_base+0x5C9028)==native); /* All other native consumers retain Start. */
}
static void check_pause_input(void)
{
    BYTE keys[256];
    native_pause_original=pause_probe;frame->akimbo=1;physical_start_keys=0;
    pause_tick(VC2_BTN_START,VC2_BTN_START,0x80108,0x100);
    pause_tick(VC2_BTN_START,0,0x80108,0x80100);
    pause_tick(0,VC2_BTN_START,0x80108,0x108);
    pause_tick(VC2_BTN_PAIR_START|VC2_BTN_START,0,0x80108,0x100);
    pause_tick(VC2_BTN_START|VC2_BTN_PAUSE,VC2_BTN_START,0x80108,0x108);
    pause_tick(VC2_BTN_PAUSE,0,0x100,0x100); /* Held pause does not repeat. */
    pause_tick(0,0,0x100,0x100);
    pause_tick(0,VC2_BTN_PAUSE,0x100,0x80100);
    pause_tick(0,0,0x100,0x100);
    frame->aim_buttons=VC2_BTN_PAUSE;frame->aim2_buttons=VC2_BTN_PAUSE;frame->aim_seq++;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)));
    assert(!keys[DIK_RETURN] && !keys[DIK_TAB] && !keys[DIK_RCONTROL] && !keys[DIK_LCONTROL]);
    physical_keys=3;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && physical_start_keys==3);
    pause_tick(VC2_BTN_START,VC2_BTN_START,0x80108,0x80108); /* Physical Start still pauses. */
    physical_keys=0;assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && !physical_start_keys);
    frame->aim_buttons=frame->aim2_buttons=VC2_BTN_PAUSE;
    aim_tick=GetTickCount()-501;
    *(DWORD *)(game_base+0x5C9028)=0x80108;
    assert(native_pause()==77 && pause_seen==0x80108); /* Watchdog releases VR ownership. */
    assert(*(DWORD *)(game_base+0x5C9028)==0x80108);
}
static int __cdecl model_draw_probe(const BYTE *record,int pass)
{
    (void)record;assert(pass==1 && (capture_kind&HOTD_SHOT));return 37;
}
static void tick(unsigned p1,unsigned p2)
{
    frame->cheat_flags=p1&HOTD_CHEAT_MASK;frame->cheat_tick=GetTickCount();
    frame->rapid_fire_rate=10;
    frame->aim_buttons=p1&~HOTD_CHEAT_MASK;frame->aim2_buttons=p2;frame->aim_seq++;
    assert(native_input()==123);
}
static void check_reload_input(void)
{
    BYTE keys[256];
    *(DWORD *)(game_base+0x5C8E98)=6;
    *(short *)(game_base+0x5A5C62)=*(short *)(game_base+0x5A5D92)=5;
    const BYTE reader[]={0xA1,0x28,0x90,0x9C,0x00};
    const DWORD sites[]={0x14A9A,0x14DD0};
    for(unsigned i=0;i<2;i++)memcpy(game_base+sites[i],reader,sizeof(reader));
    game_base[sites[1]]^=1;
    assert(!install_reload_input() && !memcmp(game_base+sites[0],reader,sizeof(reader)));
    game_base[sites[1]]^=1;
    assert(install_reload_input());
    for(unsigned i=0;i<2;i++) {
        DWORD address;
        memcpy(&address,game_base+sites[i]+1,4);
        assert(game_base[sites[i]]==0xA1 && address==(DWORD)(uintptr_t)&reload_input);
    }
    tick(0,0);polled_input=0x20102;
    tick(VC2_BTN_BACK,VC2_BTN_BACK);
    assert(reload_input==0x100 && *(DWORD *)(game_base+0x5C9028)==polled_input);
    physical_keys=12;assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && physical_back_keys==3);
    tick(VC2_BTN_BACK,VC2_BTN_BACK);
    assert(reload_input==polled_input); /* Physical Ctrl retains native reload, even with VR Cancel. */
    physical_keys=0;assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && !physical_back_keys);
    tick(0,0);polled_input=0x100;
    tick(0,VC2_BTN_RELOAD);
    assert(reload_input==0x20100 && *(DWORD *)(game_base+0x5C9028)==0x100);
    tick(0,VC2_BTN_RELOAD);assert(reload_input==0x100); /* One reload per press. */
    tick(VC2_BTN_BACK,VC2_BTN_BACK);polled_input=0x20102;
    aim_tick=GetTickCount()-501;native_input();
    assert(reload_input==polled_input); /* Stale viewer stops masking native input. */
    polled_input=0x100;tick(0,0);
}
int main(void)
{
    BYTE keys[256];
    game_base=calloc(1,0x5CA200);frame=calloc(1,sizeof(*frame));
    assert(game_base && frame);
    BYTE record[0x74]={0};DWORD model=0x12345678;float plane=-1;
    *(unsigned short *)(game_base+0x179F78)=373;
    *(DWORD *)(game_base+0x5A66A4+373*16)=model;
    memcpy(record+0xC,&model,4);memcpy(record+0x34+14*4,&plane,4);
    assert(native_model_kind(record)&HOTD_SHOT);
    capture_kind=HOTD_WORLD;assert(native_model_draw(record,1,model_draw_probe)==37 && capture_kind==HOTD_WORLD);
    plane=-21;memcpy(record+0x34+14*4,&plane,4);
    assert(native_model_kind(record)==0); /* Traveling tracer: true position, no aim collision. */
    plane=-1;memcpy(record+0x34+14*4,&plane,4);model++;
    memcpy(record+0xC,&model,4);assert(native_model_kind(record)==HOTD_WORLD);
    capture_kind=0;
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
    assert(native_active_players()==3);
    *(short *)(game_base+0x5A5C62)=9;assert(native_active_players()==2);
    *(short *)(game_base+0x5A5C62)=10;assert(native_active_players()==3);
    *(short *)(game_base+0x5A5D92)=4;assert(native_active_players()==1);
    *(DWORD *)(game_base+0x5C8E98)=5;assert(!native_active_players());
    *(DWORD *)(game_base+0x5C8E98)=6;
    *(short *)(game_base+0x5A5C62)=*(short *)(game_base+0x5A5D92)=5;
    game_base[0x5A5D85]=1; /* P1 mouse/gun control. */
    game_base[0x5A5EB5]=0; /* P2 keyboard control, scheme 1. */
    frame->aim_x=-100;frame->aim_y=-80;frame->aim2_x=740;frame->aim2_y=560;
    tick(0,0);
    assert(gun1->x==-420 && gun1->y==320 && gun2->x==420 && gun2->y==-320);
    assert(gun1->valid && gun2->valid); /* Outside the old screen is still a valid 3D shot. */
    frame->aim_x=frame->aim2_y=1e30f;frame->aim_y=frame->aim2_x=-1e30f;
    tick(0,0);
    assert(gun1->x==32767 && gun1->y==32767 && gun2->x==-32768 && gun2->y==-32768);
    frame->aim_x=NAN;frame->aim2_y=INFINITY;tick(0,0);
    assert(!gun1->x && !gun1->y && !gun2->x && !gun2->y);
    frame->aim_x=frame->aim2_x=320;frame->aim_y=frame->aim2_y=240;
    *(DWORD *)(game_base+0x5C9F6C+0x7C+20)=0x20000;
    cheats_supported=1;
    game_base[0x5C9FD8]=game_base[0x5C9FD9]=1;
    game_base[0x5C9FD8+0x7C]=2;game_base[0x5C9FD9+0x7C]=3;
    native_cheats(0); /* Explicit off overrides pre-enabled native settings for both players. */
    for(unsigned p=0;p<2;p++)assert(!game_base[0x5C9FD8+p*0x7C] && !game_base[0x5C9FD9+p*0x7C]);
    native_cheats(HOTD_CHEAT_LIVES|HOTD_CHEAT_AMMO);
    for(unsigned p=0;p<2;p++)assert(game_base[0x5C9FD8+p*0x7C]==1 && game_base[0x5C9FD9+p*0x7C]==1);
    native_cheats(HOTD_CHEAT_AMMO);
    assert(!game_base[0x5C9FD8] && !game_base[0x5C9FD8+0x7C] && game_base[0x5C9FD9]==1);
    native_cheats(0);
    for(unsigned p=0;p<2;p++)assert(!game_base[0x5C9FD8+p*0x7C] && !game_base[0x5C9FD9+p*0x7C]);
    frame->cheat_tick=GetTickCount()-500;native_cheats(0);
    assert(game_base[0x5C9FD8]==1 && game_base[0x5C9FD9]==1);
    assert(game_base[0x5C9FD8+0x7C]==2 && game_base[0x5C9FD9+0x7C]==3);
    cheats_supported=0;frame->cheat_tick=GetTickCount();native_cheats(HOTD_CHEAT_MASK);
    assert(game_base[0x5C9FD8]==1 && game_base[0x5C9FD9]==1);
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
    assert(!gun1->fire && !gun2->fire && game_base[0x5C9FD8]==1 && game_base[0x5C9FD9]==1);
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
    assert(reload_input==0x20100 && *(DWORD *)(game_base+0x5C9028)==0x100); /* Reload cannot reach skip/cancel. */
    tick(VC2_BTN_FIRE,VC2_BTN_RELOAD);
    assert(*(DWORD *)(game_base+0x5C9028)==0x100);
    tick(0,0);
    tick(VC2_BTN_BACK,VC2_BTN_FIRE);
    assert(gun1->valid && !gun1->fire && gun2->valid && gun2->fire); /* Cancel never reloads. */
    tick(VC2_BTN_BACK,VC2_BTN_FIRE);
    assert(!gun1->fire && !gun2->fire);
    tick(VC2_BTN_BACK|VC2_BTN_FIRE,0);
    assert(gun1->valid && gun1->fire); /* Held cancel does not suppress a fresh shot. */
    tick(0,0);
    tick(VC2_BTN_FIRE,VC2_BTN_BACK);
    assert(gun1->valid && gun1->fire && *(DWORD *)(game_base+0x5C9028)==0x100);
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
    frame->aim_buttons=VC2_BTN_PAIR_START|VC2_BTN_START;frame->aim2_buttons=0;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && keys[DIK_RETURN] && keys[DIK_TAB]);
    frame->aim_buttons=VC2_BTN_PAIR_START;frame->aim2_buttons=VC2_BTN_START;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && keys[DIK_RETURN] && keys[DIK_TAB]);
    frame->aim_buttons=VC2_BTN_START;frame->aim2_buttons=0;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && keys[DIK_RETURN] && !keys[DIK_TAB]);
    frame->aim_buttons=0;frame->aim2_buttons=VC2_BTN_START;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && !keys[DIK_RETURN] && keys[DIK_TAB]);
    frame->akimbo=0;frame->aim_buttons=VC2_BTN_START;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && keys[DIK_RETURN] && !keys[DIK_TAB]);
    frame->aim_buttons=0;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && !keys[DIK_RETURN] && !keys[DIK_TAB]);
    frame->akimbo=1;
    frame->aim_buttons=frame->aim2_buttons=VC2_BTN_START;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)));
    assert(keys[DIK_RETURN] && keys[DIK_TAB] && !keys[DIK_RCONTROL] && !keys[DIK_LCONTROL]);
    frame->aim_buttons=VC2_BTN_BACK;frame->aim2_buttons=0;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)));
    assert(keys[DIK_RCONTROL] && !keys[DIK_LCONTROL] && !keys[DIK_ESCAPE]);
    frame->aim_buttons=0;frame->aim2_buttons=VC2_BTN_BACK;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)));
    assert(!keys[DIK_RCONTROL] && keys[DIK_LCONTROL] && !keys[DIK_ESCAPE]);
    /* Native keyboard navigation works in every interface, including Options. */
    for(unsigned phase=0;phase<=16;phase++) {
        *(DWORD *)(game_base+0x5C8E98)=phase;
        frame->aim_buttons=VC2_BTN_UP;frame->aim2_buttons=0;
        assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && keys[DIK_UP] && !keys[DIK_DOWN]);
        frame->aim_buttons=0;frame->aim2_buttons=VC2_BTN_DOWN;
        assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && !keys[DIK_UP] && keys[DIK_DOWN]);
        frame->aim_buttons=VC2_BTN_UP;
        assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && !keys[DIK_UP] && !keys[DIK_DOWN]);
        frame->aim_buttons=VC2_BTN_RIGHT;frame->aim2_buttons=0;
        assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && keys[DIK_RIGHT] && !keys[DIK_LEFT]);
        frame->aim_buttons=0;frame->aim2_buttons=VC2_BTN_LEFT;
        assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && !keys[DIK_RIGHT] && keys[DIK_LEFT]);
        frame->aim_buttons=VC2_BTN_RIGHT;
        assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && !keys[DIK_RIGHT] && !keys[DIK_LEFT]);
        frame->aim_buttons=VC2_BTN_START;frame->aim2_buttons=VC2_BTN_START;
        assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && keys[DIK_RETURN] && keys[DIK_TAB] && !keys[DIK_SPACE]);
    }
    *(DWORD *)(game_base+0x5C8E98)=6;
    frame->aim_x=400;frame->aim_y=150;frame->aim2_x=120;frame->aim2_y=350;
    tick(VC2_BTN_UP|VC2_BTN_LEFT,VC2_BTN_UP|VC2_BTN_RIGHT);
    assert(!gun1->fire && !gun2->fire);
    assert(gun1->x==80 && gun1->y==90 && gun2->x==-200 && gun2->y==-110);
    aim_seq=frame->aim_seq;aim_tick=GetTickCount()-501;
    poll_input();native_input();
    assert(!gun1->fire && !gun2->fire && !gun1->valid && !gun2->valid);
    *(DWORD *)(game_base+0x5C8E98)=4;
    assert(SUCCEEDED(keyboard_state(NULL,256,keys)) && !keys[DIK_UP] && !keys[DIK_DOWN] &&
        !keys[DIK_LEFT] && !keys[DIK_RIGHT]);
    check_pause_input();
    check_reload_input();
    free(game_base);free(frame);
    puts("Input/cheats: separate pause edges, preserved Start, reload, rapid fire and watchdogs passed.");
    return 0;
}
