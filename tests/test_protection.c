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
    BYTE *saved_base=game_base;
    game_base=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(game_base);
    for(unsigned n=5;n<=9;n+=4) {
        const BYTE *code=n==5?short_function:long_function;
        unsigned size=n==5?sizeof(short_function):sizeof(long_function);
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

int main(void)
{
    game_base=calloc(1,0x5CA200);frame=calloc(1,sizeof(*frame));assert(game_base && frame);
    check_detours();
    /* Refuse both hooks before modifying either unsupported entry. */
    assert(!install_invincibility() && !game_base[0x15300] && !game_base[0x15430]);
    native_damage_original=damage_probe;native_damage_forced_original=forced_probe;invincibility_ready=1;
    for(unsigned p=0;p<2;p++) {
        *(short *)(game_base+0x5A5C66+p*0x130)=3;
        request(0);native_damage(p,1,9);
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
