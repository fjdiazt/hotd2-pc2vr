/* Damage dispatch and native free play against isolated memory; no game starts. */
#include <assert.h>
#include "../src/bridge.c"

static unsigned damage_calls[2],forced_calls[2];
static void __cdecl damage_probe(int player,int hit,int kind)
{
    assert(player>=0 && player<2 && hit==1 && kind==9);
    damage_calls[player]++;
    if(!game_base[0x5C9FD8+player*0x7C])(*(short *)(game_base+0x5A5C66+player*0x130))--;
    *(short *)(game_base+0x5A5CD0+player*0x130)=1;
}
static void __cdecl forced_probe(int player,int hit,int kind,int force,int timer)
{
    assert(force==1 && timer==-1);forced_calls[player]++;
    damage_probe(player,hit,kind);
}
static void request(unsigned flags)
{frame->cheat_flags=flags;frame->cheat_tick=GetTickCount();native_cheats(requested_cheats());}
static int (__cdecl *probe_original)(int);
static int __cdecl hook_probe(int value)
{return probe_original(value)+10;}
static void check_detours(void)
{
    static const BYTE short_function[]={0x56,0x8B,0x74,0x24,0x08,0x8B,0xC6,0x5E,0xC3};
    static const BYTE long_function[]={0x83,0xEC,0x20,0x53,0x55,0x56,0x57,0x33,0xFF,
        0x8B,0x44,0x24,0x34,0x5F,0x5E,0x5D,0x5B,0x83,0xC4,0x20,0xC3};
    static const BYTE civilian_function[]={0x83,0xEC,0x18,0x53,0x56,0x8B,0x44,0x24,0x24,
        0x5E,0x5B,0x83,0xC4,0x18,0xC3};
    BYTE *saved_base=game_base;
    game_base=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(game_base);
    for(unsigned kind=0;kind<3;kind++) {
        unsigned n=kind==1?9:5;
        const BYTE *code=kind==0?short_function:kind==1?long_function:civilian_function;
        unsigned size=kind==0?sizeof(short_function):kind==1?sizeof(long_function):sizeof(civilian_function);
        memcpy(game_base,code,size);
        int (__cdecl *entry)(int)=(void *)game_base;
        FlushInstructionCache(GetCurrentProcess(),game_base,size);
        assert(entry(37)==37);
        assert(!detour(0,code,4,hook_probe,(void **)&probe_original));
        assert(!detour(0,code,10,hook_probe,(void **)&probe_original));
        game_base[0]^=1;assert(!detour(0,code,n,hook_probe,(void **)&probe_original));game_base[0]^=1;
        assert(detour(0,code,n,hook_probe,(void **)&probe_original));
        assert(entry(37)==47 && probe_original(37)==37);
        assert(!memcmp(game_base+n,code+n,size-n));
        assert(VirtualFree(probe_original,0,MEM_RELEASE));probe_original=NULL;
    }
    assert(VirtualFree(game_base,0,MEM_RELEASE));game_base=saved_base;
}

static unsigned shot_calls;
static void __cdecl shot_probe(int player,float x,float y)
{assert(player>=-1 && player<=2 && x==12.5f && y==-3);shot_calls++;}
static void check_shot_feedback(void)
{
    const BYTE ray[]={0x83,0xEC,0x1C,0x8B,0x44,0x24,0x20};
    const BYTE normal[]={0xE8,0xDF,0x16,0xFF,0xFF},special[]={0xE8,0x9F,0x13,0xFF,0xFF};
    assert(!native_shot_profile(game_base));
    memcpy(game_base+0x6110,ray,sizeof(ray));memcpy(game_base+0x14A2C,normal,sizeof(normal));
    memcpy(game_base+0x14D6C,special,sizeof(special));assert(native_shot_profile(game_base));
    const unsigned offsets[]={0x6110,0x14A2C,0x14D6C},sizes[]={7,5,5};
    for(unsigned k=0;k<3;k++)for(unsigned b=0;b<sizes[k];b++) {
        game_base[offsets[k]+b]^=1;assert(!native_shot_profile(game_base));game_base[offsets[k]+b]^=1;
    }
    native_shot_ray_original=shot_probe;*(DWORD *)(game_base+0x5C8E98)=6;
    for(unsigned mode=0;mode<2;mode++)for(int p=0;p<2;p++) {
        *(DWORD *)(game_base+0x5CA08C)=mode;native_shot_ray(p,12.5f,-3);
        assert(shot_events[p]==mode+1);
    }
    *(short *)(game_base+0x5A1A00)=1;native_shot_ray(0,12.5f,-3);
    *(short *)(game_base+0x5A1A00)=0;*(DWORD *)(game_base+0x5C8E98)=5;native_shot_ray(1,12.5f,-3);
    assert(shot_events[0]==2 && shot_events[1]==2);
    *(DWORD *)(game_base+0x5C8E98)=6;native_shot_ray(-1,12.5f,-3);native_shot_ray(2,12.5f,-3);
    assert(shot_calls==8 && shot_events[0]==2 && shot_events[1]==2);
    shot_events[0]=UINT32_MAX;native_shot_ray(0,12.5f,-3);assert(!shot_events[0]);
}

static unsigned civilian_calls;
static DWORD civilian_flags;
static void __cdecl civilian_probe(BYTE *actor)
{civilian_calls++;civilian_flags=actor?*(DWORD *)(actor+0x34):0;}
static void check_rescue_protection(void)
{
    const BYTE entry[]={0x83,0xEC,0x18,0x53,0x56};
    const BYTE death[]={0x8B,0x4E,0x34,0xF7,0xC1,0x00,0x00,0x00,0x04};
    const BYTE shot[]={0xF6,0xC1,0x08};
    assert(!install_rescue_protection() && !game_base[0x8A920]);
    memcpy(game_base+0x8A920,entry,sizeof(entry));
    memcpy(game_base+0x8AADC,death,sizeof(death));
    memcpy(game_base+0x8AB8F,shot,sizeof(shot));
    const unsigned offsets[]={0x8A920,0x8AADC,0x8AB8F},sizes[]={sizeof(entry),sizeof(death),sizeof(shot)};
    for(unsigned k=0;k<3;k++)for(unsigned b=0;b<sizes[k];b++) {
        game_base[offsets[k]+b]^=1;
        assert(!install_rescue_protection());
        if(k)assert(!memcmp(game_base+0x8A920,entry,sizeof(entry)));
        game_base[offsets[k]+b]^=1;
    }
    assert(install_rescue_protection() && game_base[0x8A920]==0xE9);
    assert(VirtualFree(native_civilian_original,0,MEM_RELEASE));
    memcpy(game_base+0x8A920,entry,sizeof(entry));
    native_civilian_original=civilian_probe;rescue_protection_ready=1;
    DWORD actor[32],expected[32];
    const unsigned tasks[]={0x8A920,0x8B140,0x20000};
    for(unsigned task=0;task<3;task++)for(unsigned player=0;player<2;player++)for(unsigned dead=0;dead<2;dead++) {
        memset(actor,0xA5,sizeof(actor));actor[0]=(DWORD)(uintptr_t)(game_base+tasks[task]);
        DWORD flags=0x80018001u|(dead?0x4000000u:0)|(2u<<player)|8u;
        for(unsigned mode=0;mode<4;mode++) {
            actor[0x34/4]=flags;memcpy(expected,actor,sizeof(actor));
            frame->cheat_flags=mode?HOTD_CHEAT_RESCUE_NPCS:0;frame->cheat_tick=GetTickCount();
            if(mode==2)frame->cheat_tick-=500;
            rescue_protection_ready=mode!=3;
            unsigned calls=civilian_calls;native_civilian((BYTE *)actor);
            if(task<2 && mode==1)expected[0x34/4]&=~0xEu;
            assert(civilian_calls==calls+1 && civilian_flags==expected[0x34/4]);
            assert(!memcmp(actor,expected,sizeof(actor)));
        }
    }
    rescue_protection_ready=1;frame->cheat_tick=0;
    actor[0]=(DWORD)(uintptr_t)(game_base+0x8A920);actor[0x34/4]=0xFu;
    native_civilian((BYTE *)actor);assert(civilian_flags==0xFu);
    frame->cheat_tick=GetTickCount();native_civilian(NULL);assert(!civilian_flags);
    request(0);
}

int main(void)
{
    game_base=calloc(1,0x5CA200);frame=calloc(1,sizeof(*frame));assert(game_base && frame);
    check_detours();
    check_shot_feedback();
    check_rescue_protection();
    /* Refuse both hooks before modifying either unsupported entry. */
    assert(!install_invincibility() && !game_base[0x15300] && !game_base[0x15430]);
    native_damage_original=damage_probe;native_damage_forced_original=forced_probe;invincibility_ready=1;
    for(unsigned p=0;p<2;p++) {
        *(short *)(game_base+0x5A5C66+p*0x130)=3;
        request(HOTD_CHEAT_RESCUE_NPCS);native_damage(p,1,9);
        assert(damage_calls[p]==1 && *(short *)(game_base+0x5A5C66+p*0x130)==2);
        *(short *)(game_base+0x5A5CD0+p*0x130)=0;
        request(HOTD_CHEAT_INVINCIBILITY);native_damage(p,1,9);native_damage_forced(p,1,9,1,-1);
        assert(damage_calls[p]==1 && !forced_calls[p]);
        assert(*(short *)(game_base+0x5A5C66+p*0x130)==2 && !*(short *)(game_base+0x5A5CD0+p*0x130));
        frame->cheat_tick=GetTickCount()-500;native_damage_forced(p,1,9,1,-1);
        assert(damage_calls[p]==2 && forced_calls[p]==1 && *(short *)(game_base+0x5A5C66+p*0x130)==1);
        request(HOTD_CHEAT_INVINCIBILITY);invincibility_ready=0;native_damage(p,1,9);invincibility_ready=1;
        assert(damage_calls[p]==3 && !*(short *)(game_base+0x5A5C66+p*0x130));
    }
    request(0);
    memcpy(game_base+0x6E1F,"\x8B\x0D\x70\x8E\x9C\x00\x83\xF9\x01",9);
    memcpy(game_base+0x6E74,"\x8B\x15\x70\x8E\x9C\x00\x8B\x44\x24\x08\x83\xFA\x01",13);
    memcpy(game_base+0x6ED0,"\x8B\x0D\x70\x8E\x9C\x00\xB8\x01\x00\x00\x00",11);
    assert(native_continues_profile(game_base));
    const unsigned checks[]={0x6E1F,0x6E74,0x6ED0};
    for(unsigned i=0;i<3;i++) {
        game_base[checks[i]]^=1;assert(!native_continues_profile(game_base));game_base[checks[i]]^=1;
    }
    continues_supported=1;cheats_supported=1;
    DWORD *freeplay=(DWORD *)(game_base+0x5C8E70);
    *freeplay=7;*(DWORD *)(game_base+0x5C8E60)=2;*(DWORD *)(game_base+0x5C8E68)=3;
    request(HOTD_CHEAT_CONTINUES);assert(*freeplay==1);
    for(unsigned p=0;p<2;p++)assert(!game_base[0x5C9FD8+p*0x7C] && !*(short *)(game_base+0x5A5C66+p*0x130));
    assert(*(DWORD *)(game_base+0x5C8E60)==2 && *(DWORD *)(game_base+0x5C8E68)==3);
    request(HOTD_CHEAT_CONTINUES|HOTD_CHEAT_LIVES);assert(*freeplay==1 && game_base[0x5C9FD8]);
    request(HOTD_CHEAT_CONTINUES);assert(*freeplay==1 && !game_base[0x5C9FD8]);
    *freeplay=0;request(HOTD_CHEAT_CONTINUES);assert(*freeplay==1); /* Native reinitialization. */
    frame->cheat_tick=GetTickCount()-500;native_cheats(requested_cheats());assert(*freeplay==7);
    *freeplay=0;request(HOTD_CHEAT_CONTINUES);request(0);assert(!*freeplay);
    frame->cheat_tick=GetTickCount()-500;native_cheats(0);assert(!*freeplay);
    *freeplay=1;request(0);assert(!*freeplay); /* Native free play must not defeat explicit off. */
    request(HOTD_CHEAT_CONTINUES);request(0);assert(!*freeplay);
    frame->cheat_tick=GetTickCount()-500;native_cheats(0);assert(*freeplay==1);
    *freeplay=0;continues_supported=0;request(HOTD_CHEAT_CONTINUES);assert(!*freeplay);
    frame->cheat_flags=~HOTD_CHEAT_MASK;assert(!requested_cheats());
    free(frame);frame=NULL;assert(!requested_cheats());
    free(game_base);
    puts("Protection: both damage paths/hands, stale and unsupported pass-through, independent free play and restoration passed.");
    return 0;
}
