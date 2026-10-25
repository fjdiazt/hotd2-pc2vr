/* Actual bridge callbacks, fake native sprite queue/device. No game starts. */
#include <assert.h>
#include "../src/bridge.c"
static unsigned native_bitmaps,native_submits,native_sprites,suppressed;
static BYTE request[HOTD_SPRITE_RECORD_SIZE],other[HOTD_SPRITE_RECORD_SIZE];
static int nested_bitmap,nested_sprite,append_bad;
static HRESULT WINAPI transform(IDirect3DDevice7 *self,D3DTRANSFORMSTATETYPE state,D3DMATRIX *out)
{(void)self;memset(out,0,sizeof(*out));out->_11=out->_22=out->_33=out->_44=1;
 if(state==D3DTRANSFORMSTATE_PROJECTION){out->_34=1;out->_44=0;}return DD_OK;}
static HRESULT WINAPI get_viewport(IDirect3DDevice7 *self,D3DVIEWPORT7 *out)
{(void)self;memset(out,0,sizeof(*out));out->dwWidth=640;out->dwHeight=480;return DD_OK;}
static HRESULT WINAPI state(IDirect3DDevice7 *self,D3DRENDERSTATETYPE key,DWORD *out)
{(void)self;*out=key==D3DRENDERSTATE_ZENABLE || key==D3DRENDERSTATE_ZWRITEENABLE;return DD_OK;}
static HRESULT WINAPI stage(IDirect3DDevice7 *self,DWORD n,D3DTEXTURESTAGESTATETYPE key,DWORD *out)
{(void)self;(void)n;(void)key;*out=1;return DD_OK;}
static HRESULT WINAPI texture(IDirect3DDevice7 *self,DWORD n,IDirectDrawSurface7 **out)
{(void)self;(void)n;*out=NULL;return DD_OK;}
static HRESULT WINAPI material(IDirect3DDevice7 *self,D3DMATERIAL7 *out)
{(void)self;memset(out,0,sizeof(*out));out->diffuse.r=out->diffuse.g=out->diffuse.b=out->diffuse.a=1;return DD_OK;}
static HRESULT WINAPI no_target(IDirect3DDevice7 *self,IDirectDrawSurface7 **out)
{(void)self;(void)out;return E_FAIL;}
static void __cdecl sprite(const BYTE *record)
{
    float quad[4][8]={{300,200,0.5f,1,0,0,0,0},{340,200,0.5f,1,0,0,1,0},
                       {300,240,0.5f,1,0,0,0,1},{340,240,0.5f,1,0,0,1,1}};
    (void)record;native_sprites++;suppressed+=capture_skip!=0;
    capture_draw(active_device,D3DPT_TRIANGLESTRIP,0x1C4,quad,4,NULL,0);
    if(nested_sprite && capture_skip){nested_sprite=0;native_sprite_draw(other);assert(capture_skip);}
}
static void __cdecl submit(const BYTE *record)
{
    native_submits++;
    if(*(DWORD *)record&0x80000000u) {
        HotdNativeQueue q=native_capture_queue();BYTE *copy=(BYTE *)q.cursor;
        memcpy(copy,record,HOTD_SPRITE_RECORD_SIZE);
        *(DWORD *)copy=(*(DWORD *)copy&0xBFFFFFFFu)|0x20000000;
        *(DWORD *)(q.list+4*q.count)=(DWORD)(ULONG_PTR)copy;
        *(DWORD *)(game_base+0x3E78C0)=(DWORD)(ULONG_PTR)(copy+HOTD_SPRITE_RECORD_SIZE+(append_bad?4:0));
        *(DWORD *)(game_base+0x3E793C)=q.count+1;
    }else native_sprite_draw(record);
}
static void __cdecl bitmap(const BYTE *definition)
{
    native_bitmaps++;
    if(nested_bitmap){nested_bitmap=0;native_bitmap_draw(definition,0x88081);assert(crosshair_definition==definition);}
    native_sprite_submit(request);
}
static BYTE *queued(unsigned i)
{HotdNativeQueue q=native_capture_queue();return (BYTE *)(ULONG_PTR)*(DWORD *)(q.list+4*i);}
static BYTE *definition(unsigned p,unsigned style)
{
    BYTE *def=game_base+0x5A5D44+p*0x130;
    *(DWORD *)def=*(WORD *)(game_base+0x179F58+2*(p*4+style));return def;
}
static void reset(void)
{
    uintptr_t list=(uintptr_t)game_base+0x3E814C;
    *(DWORD *)(game_base+0x3E78C4)=(DWORD)list;*(DWORD *)(game_base+0x3E78C8)=1024;
    *(DWORD *)(game_base+0x3E793C)=0;*(DWORD *)(game_base+0x3E78C0)=(DWORD)(list+4096);
    memset(&crosshair_records,0,sizeof(crosshair_records));memset(request,0,sizeof(request));memset(other,0,sizeof(other));
    native_bitmaps=native_submits=native_sprites=suppressed=hud_count=count=calls=dropped=0;
    memset(player_hud_records,0,sizeof(player_hud_records));player_hud_owner=capture_player_hud=0;
    player_hud_immediate=NULL;player_hud_immediate_group=0;player_hud_ready=0;
    crosshair_ready=1;crosshair_definition=crosshair_immediate=NULL;capture_skip=crosshair_draw_depth=0;
    nested_bitmap=nested_sprite=append_bad=0;
}
static const BYTE *hud_task_seen;
static void __cdecl hud_ammo(const BYTE *task)
{hud_task_seen=task;native_sprite_submit(request);}
static DWORD __cdecl hud_lives(const BYTE *task)
{hud_task_seen=task;native_sprite_submit(request);return 37;}
static int hud_player_seen;
static void __cdecl hud_credits(int player)
{hud_player_seen=player;native_sprite_submit(request);native_sprite_submit(request);}
static void test_player_hud(void)
{
    const BYTE ammo[]={0x83,0xEC,0x14,0x8B,0x44,0x24,0x18},lives[]={0x8B,0x44,0x24,4,0x56,0x57};
    const BYTE credits[]={0x83,0x3D,0x98,0x8E,0x9C,0,6};
    BYTE task[0x38]={0},alias[HOTD_SPRITE_RECORD_SIZE],saved[HOTD_SPRITE_RECORD_SIZE];
    assert(!hotd_player_hud_profile(game_base));
    memcpy(game_base+0x177D0,ammo,sizeof(ammo));memcpy(game_base+0x174A0,lives,sizeof(lives));
    assert(!hotd_player_hud_profile(game_base));memcpy(game_base+0x6CE0,credits,sizeof(credits));
    assert(hotd_player_hud_profile(game_base));
    const unsigned addresses[]={0x177D0,0x174A0,0x6CE0},sizes[]={7,6,7};
    for(unsigned h=0;h<3;h++)for(unsigned b=0;b<sizes[h];b++) {
        game_base[addresses[h]+b]^=1;assert(!hotd_player_hud_profile(game_base));game_base[addresses[h]+b]^=1;
    }
    native_hud_ammo_original=hud_ammo;native_hud_lives_original=hud_lives;
    native_hud_credits_original=hud_credits;
    for(unsigned kind=0;kind<2;kind++)for(unsigned p=0;p<2;p++)for(unsigned deferred=0;deferred<2;deferred++) {
        reset();player_hud_ready=1;*(DWORD *)(task+0x34)=p;*(DWORD *)request=deferred?0x80000000:0;
        if(kind)assert(native_hud_lives(task)==37);else native_hud_ammo(task);
        assert(hud_task_seen==task && !player_hud_owner && !capture_player_hud);
        if(deferred)native_sprite_draw(queued(0));
        assert(native_submits==1 && native_sprites==1 && hud_count==6 && !suppressed);
        for(unsigned v=0;v<6;v++)assert((hud_vertices[v].address&HOTD_PLAYER_HUD_MASK)==((kind*2+p+1)<<HOTD_PLAYER_HUD_SHIFT));
        native_sprite_draw(other);assert(!(hud_vertices[6].address&HOTD_PLAYER_HUD_MASK));
    }
    /* Join prompt and credit digits share the player row, immediate or queued. */
    for(int p=0;p<2;p++)for(unsigned deferred=0;deferred<2;deferred++) {
        reset();player_hud_ready=1;*(DWORD *)(game_base+0x5C8E98)=6;
        *(DWORD *)request=deferred?0x80000000:0;native_hud_credits(p);
        assert(hud_player_seen==p && !player_hud_owner && !capture_player_hud);
        if(deferred){native_sprite_draw(queued(0));native_sprite_draw(queued(1));}
        assert(native_submits==2 && native_sprites==2 && hud_count==12 && !suppressed);
        for(unsigned v=0;v<12;v++)assert((hud_vertices[v].address&HOTD_PLAYER_HUD_MASK)==((p+5u)<<HOTD_PLAYER_HUD_SHIFT));
        native_sprite_draw(other);assert(!(hud_vertices[12].address&HOTD_PLAYER_HUD_MASK));
    }
    for(unsigned mode=0;mode<12;mode++)if(mode!=6) {
        reset();player_hud_ready=1;*(DWORD *)(game_base+0x5C8E98)=mode;native_hud_credits(1);
        assert(hud_player_seen==1 && !(hud_vertices[0].address&HOTD_PLAYER_HUD_MASK));
    }
    for(int p=-1;p<=2;p+=3) {
        reset();player_hud_ready=1;*(DWORD *)(game_base+0x5C8E98)=6;native_hud_credits(p);
        assert(hud_player_seen==p && !(hud_vertices[0].address&HOTD_PLAYER_HUD_MASK));
    }
    reset();native_hud_credits(0);assert(!(hud_vertices[0].address&HOTD_PLAYER_HUD_MASK));
    reset();player_hud_ready=1;*(DWORD *)request=0x80000000;native_hud_credits(1);
    BYTE *credit_copy=queued(0);publish_frame();native_sprite_draw(credit_copy);
    assert(!(hud_vertices[0].address&HOTD_PLAYER_HUD_MASK));
    reset();player_hud_ready=1;*(DWORD *)(task+0x34)=0;*(DWORD *)request=0x80000000;
    native_hud_ammo(task);memcpy(alias,queued(0),sizeof(alias));memcpy(saved,queued(0),sizeof(saved));
    native_sprite_draw(alias);assert(!(hud_vertices[0].address&HOTD_PLAYER_HUD_MASK));
    queued(0)[20]^=1;native_sprite_draw(queued(0));memcpy(queued(0),saved,sizeof(saved));native_sprite_draw(queued(0));
    assert(!(hud_vertices[6].address&HOTD_PLAYER_HUD_MASK) && !(hud_vertices[12].address&HOTD_PLAYER_HUD_MASK));
    reset();player_hud_ready=1;*(DWORD *)(task+0x34)=2;native_hud_ammo(task);
    assert(!(hud_vertices[0].address&HOTD_PLAYER_HUD_MASK));
    reset();player_hud_ready=0;*(DWORD *)(task+0x34)=0;native_hud_ammo(task);
    assert(!(hud_vertices[0].address&HOTD_PLAYER_HUD_MASK));
    reset();player_hud_ready=1;*(DWORD *)request=0x80000000;native_hud_ammo(task);
    BYTE *copy=queued(0);publish_frame();native_sprite_draw(copy);
    assert(!(hud_vertices[0].address&HOTD_PLAYER_HUD_MASK));
}
static void test_profile(void)
{
    static const BYTE bitmap_sig[]={0x8B,0x44,0x24,4,0x83,0xEC,0x38,0x8B,8};
    static const BYTE sprite_sig[]={0x81,0xEC,0xB0,0,0,0,0x53,0x55,0x56},submit_sig[]={0x56,0x8B,0x74,0x24,8};
    static const BYTE call1[]={0xE8,0x49,0x5B,0,0,0x83,0xC4,4},call2[]={0xE8,0x7A,0x5C,0,0,0x83,0xC4,0x10};
    static const WORD ids[]={0xAA8,0xAAA,0xAAB,0xAAC,0xAA9,0xAAD,0xAAE,0xAAF};
    static const unsigned offsets[]={0x1C630,0xA7AB0,0xA7A40,0x16AE2,0x169B1,0x179F58},sizes[]={9,9,5,8,8,16};
    assert(!hotd_crosshair_profile(game_base));assert(!install_crosshair_capture(0));
    memcpy(game_base+offsets[0],bitmap_sig,9);memcpy(game_base+offsets[1],sprite_sig,9);
    memcpy(game_base+offsets[2],submit_sig,5);memcpy(game_base+offsets[3],call1,8);
    memcpy(game_base+offsets[4],call2,8);memcpy(game_base+offsets[5],ids,16);
    assert(hotd_crosshair_profile(game_base));
    for(unsigned i=0;i<6;i++)for(unsigned b=0;b<sizes[i];b++) {
        game_base[offsets[i]+b]^=1;assert(!hotd_crosshair_profile(game_base));game_base[offsets[i]+b]^=1;
    }
}
int main(void)
{
    IDirect3DDevice7Vtbl vt={.GetTransform=transform,.GetViewport=get_viewport,.GetRenderState=state,
        .GetTextureStageState=stage,.GetTexture=texture,.GetMaterial=material,.GetRenderTarget=no_target};
    IDirect3DDevice7 device={&vt};BYTE preview[40]={0},alias[HOTD_SPRITE_RECORD_SIZE],saved[HOTD_SPRITE_RECORD_SIZE];
    game_base=VirtualAlloc(NULL,0x600000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    frame=VirtualAlloc(NULL,sizeof(*frame),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(game_base&&frame);
    frame->units_per_metre=10;active_device=&device;game_window=GetDesktopWindow();
    native_bitmap_original=bitmap;native_sprite_submit_original=submit;native_sprite_draw_original=sprite;
    test_profile();
    /* Both native gameplay/end callers, both players, all styles, immediate/deferred. */
    for(unsigned p=0;p<2;p++)for(unsigned s=0;s<4;s++)for(unsigned defer=0;defer<2;defer++)for(unsigned end=0;end<2;end++) {
        reset();*(DWORD *)request=defer?0x80000000:0;
        native_bitmap_draw(definition(p,s),end?0x169B6:0x16AE7);
        if(defer)native_sprite_draw(queued(0));
        assert(native_bitmaps==1&&native_submits==1&&native_sprites==1&&suppressed==1&&hud_count==0);
        assert(!crosshair_definition&&!crosshair_immediate&&!capture_skip&&!crosshair_draw_depth);
    }
    /* Shared artwork, Options caller, wrong definition/player ID, unsupported profile all stay. */
    reset();memcpy(preview,definition(1,0),4);native_bitmap_draw(preview,0x16AE7);
    native_bitmap_draw(definition(1,0),0x88081);*(DWORD *)definition(0,0)=0xAA9;
    native_bitmap_draw(game_base+0x5A5D44,0x16AE7);*(DWORD *)definition(1,0)=0xFFFF;
    native_bitmap_draw(game_base+0x5A5E74,0x16AE7);crosshair_ready=0;
    native_bitmap_draw(definition(1,3),0x16AE7);
    assert(native_sprites==5&&suppressed==0&&hud_count==30);
    /* Unrelated nested draws restore outer suppression and retain other inherited skip. */
    reset();nested_bitmap=1;nested_sprite=1;native_bitmap_draw(definition(1,0),0x16AE7);
    assert(native_bitmaps==2&&native_sprites==3&&suppressed==1&&hud_count==12&&!capture_skip);
    reset();capture_skip=4;nested_sprite=1;native_bitmap_draw(definition(0,0),0x16AE7);
    assert(capture_skip==4&&suppressed==2&&hud_count==0);
    /* Exact post-copy identity/address: equal art elsewhere, edited/recycled records cannot inherit. */
    reset();*(DWORD *)request=0x80000000;native_bitmap_draw(definition(1,0),0x16AE7);
    memcpy(alias,queued(0),sizeof(alias));native_sprite_draw(alias);assert(hud_count==6);
    memcpy(saved,queued(0),sizeof(saved));queued(0)[20]^=1;native_sprite_draw(queued(0));
    memcpy(queued(0),saved,sizeof(saved));native_sprite_draw(queued(0));assert(hud_count==18&&suppressed==0);
    for(unsigned byte=0;byte<HOTD_SPRITE_RECORD_SIZE;byte++) {
        reset();*(DWORD *)request=0x80000000;native_bitmap_draw(definition(1,0),0x16AE7);
        memcpy(saved,queued(0),sizeof(saved));queued(0)[byte]^=1;native_sprite_draw(queued(0));
        memcpy(queued(0),saved,sizeof(saved));native_sprite_draw(queued(0));assert(!suppressed&&hud_count==12);
    }
    reset();*(DWORD *)request=0x80000000;native_bitmap_draw(definition(0,0),0x16AE7);
    native_sprite_draw(queued(0));native_sprite_draw(queued(0));assert(suppressed==1&&hud_count==6);
    /* Saturation keeps accepted tags; overflow visible. Consumed holes never duplicate recycled addresses. */
    reset();*(DWORD *)request=0x80000000;
    for(unsigned i=0;i<HOTD_SPRITE_RECORDS+2;i++)native_bitmap_draw(definition(1,i%4),0x16AE7);
    for(unsigned i=0;i<HOTD_SPRITE_RECORDS+2;i++)native_sprite_draw(queued(i));
    assert(suppressed==HOTD_SPRITE_RECORDS&&hud_count==12);
    hotd_remember_sprite(&crosshair_records,queued(0));hotd_remember_sprite(&crosshair_records,queued(1));
    assert(hotd_take_sprite(&crosshair_records,queued(0)));hotd_remember_sprite(&crosshair_records,queued(1));
    assert(hotd_take_sprite(&crosshair_records,queued(1))&&!hotd_take_sprite(&crosshair_records,queued(1)));
    /* Bad append metadata cannot earn ownership; publication clears outstanding tags. */
    reset();append_bad=1;*(DWORD *)request=0x80000000;native_bitmap_draw(definition(1,0),0x16AE7);
    native_sprite_draw(queued(0));assert(!suppressed&&hud_count==6);
    reset();*(DWORD *)request=0x80000000;native_bitmap_draw(definition(1,0),0x16AE7);
    BYTE *copy=queued(0);publish_frame();native_sprite_draw(copy);assert(!suppressed&&hud_count==6);
    test_player_hud();
    puts("Sprite capture: both crosshairs and player HUD, native calls, scoped ownership, queue aliases/profile/reset passed");
    return 0;
}
