/* HOTD2's Direct3D7 geometry and DirectInput bridge. No game files are patched. */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0700
#include <windows.h>
#include <d3d.h>
#include <dinput.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "hotd_share.h"
#include "game_profile.h"
#include "game_input.h"
#include "texture_atlas.h"
#include "lighting.h"
#include "capture_visibility.h"

static FILE *logfile;
static Vc2Frame *frame;
static HANDLE mapping;
static HWND game_window;
static unsigned count, calls, dropped, frame_no, atlas_dirty;
static Vc2Vertex vertices[VC2_MAX_VERTS];
#define HOTD_MAX_HUD_VERTS (4096*6)
static Vc2Vertex hud_vertices[HOTD_MAX_HUD_VERTS];
static unsigned hud_count;
static HotdLens lens;
static unsigned lens_valid;
static unsigned capture_kind,shot_group;
static unsigned capture_skip;
static unsigned capture_model;
static DWORD capture_visibility_planes;
static HotdStageRecords stage_records;
static int visibility_hooks_ready,visibility_ready;
static float shot_origins[64][3];
static D3DMATRIX world, view, projection;
static D3DVIEWPORT7 viewport;
static DWORD fvf_logged[32], fvf_count;
static BYTE *game_base;
static int input_ready;
static DWORD aim_seq, aim_tick;
static unsigned buttons[2];
static int cheats_supported;
static unsigned cheat_owned;
static BYTE saved_cheats[2][2];
static int continues_supported,invincibility_ready;
static DWORD saved_freeplay;
static HotdRapid rapid_fire[2];
static char ini_path[MAX_PATH];
static IDirect3DDevice7 *active_device;
static void publish_frame(void);
static int (WINAPI *original_message_box)(HWND,LPCSTR,LPCSTR,UINT);

static void logline(const char *fmt, ...)
{
    va_list ap;
    if (!logfile) return;
    va_start(ap, fmt); vfprintf(logfile, fmt, ap); va_end(ap);
    fputc('\n', logfile); fflush(logfile);
}

static int WINAPI device_message_box(HWND hwnd,LPCSTR text,LPCSTR caption,UINT flags)
{
    if(hotd_quit_prompt(text,caption,flags)) {
        logline("native quit confirmation auto-accepted");
        return IDYES;
    }
    if(hotd_device_prompt(text,caption,flags)) {
        logline("native renderer auto-selected: %s",text);
        return IDYES;
    }
    return original_message_box(hwnd,text,caption,flags);
}

static int replace_pointer(void **slot, void *hook, void **original)
{
    DWORD old, unused;
    if (*slot == hook) return 1;
    if (*original && *original != *slot) { logline("different COM implementation; hook refused"); return 0; }
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) return 0;
    *original = *slot;
    InterlockedExchangePointer(slot, hook);
    VirtualProtect(slot, sizeof(*slot), old, &unused);
    return 1;
}

static int write_code(BYTE *at, const BYTE *bytes, unsigned n)
{
    DWORD old, unused;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(at, bytes, n);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    VirtualProtect(at, n, old, &unused);
    return 1;
}

static int active_input(void)
{
    DWORD seq;
    if (!frame || !input_ready) return 0;
    seq = frame->aim_seq;
    if (seq != aim_seq) { aim_seq = seq; aim_tick = GetTickCount(); }
    return aim_tick && GetTickCount()-aim_tick < 500;
}

static int native_cheat_profile(const BYTE *base)
{
    static const BYTE life[]={0x8A,0x0C,0x85,0xD8,0x9F,0x9C,0x00};
    static const BYTE ammo[]={0x8A,0x51,0x79},ammo_original[]={0x8A,0x42,0x79};
    return !memcmp(base+0x15342,life,sizeof(life)) && !memcmp(base+0x15464,life,sizeof(life)) &&
        !memcmp(base+0x149D9,ammo,sizeof(ammo)) && !memcmp(base+0x14C47,ammo_original,sizeof(ammo_original));
}

static int native_continues_profile(const BYTE *base)
{
    static const BYTE shared[]={0x8B,0x0D,0x70,0x8E,0x9C,0x00,0x83,0xF9,0x01};
    static const BYTE separate[]={0x8B,0x15,0x70,0x8E,0x9C,0x00,0x8B,0x44,0x24,0x08,0x83,0xFA,0x01};
    static const BYTE available[]={0x8B,0x0D,0x70,0x8E,0x9C,0x00,0xB8,0x01,0x00,0x00,0x00};
    return !memcmp(base+0x6E1F,shared,sizeof(shared)) &&
        !memcmp(base+0x6E74,separate,sizeof(separate)) && !memcmp(base+0x6ED0,available,sizeof(available));
}
static unsigned requested_cheats(void)
{
    return frame && (DWORD)(GetTickCount()-frame->cheat_tick)<500 ? frame->cheat_flags&HOTD_CHEAT_MASK:0;
}
static void (__cdecl *native_damage_original)(int,int,int);
static void (__cdecl *native_damage_forced_original)(int,int,int,int,int);
static void __cdecl native_damage(int player,int hit,int kind)
{
    if(!invincibility_ready || !(requested_cheats()&HOTD_CHEAT_INVINCIBILITY))
        native_damage_original(player,hit,kind);
}
static void __cdecl native_damage_forced(int player,int hit,int kind,int force,int timer)
{
    if(!invincibility_ready || !(requested_cheats()&HOTD_CHEAT_INVINCIBILITY))
        native_damage_forced_original(player,hit,kind,force,timer);
}

static void native_cheats(unsigned requested)
{
    const unsigned bits[]={HOTD_CHEAT_LIVES,HOTD_CHEAT_AMMO};
    requested&=(cheats_supported ? HOTD_CHEAT_LIVES|HOTD_CHEAT_AMMO:0) |
        (continues_supported ? HOTD_CHEAT_CONTINUES:0);
    for(unsigned kind=0;kind<2;kind++)for(unsigned p=0;p<2;p++) {
        BYTE *flag=game_base+0x5C9FD8+p*0x7C+kind;
        if(requested&bits[kind]) {
            if(!(cheat_owned&bits[kind]))saved_cheats[p][kind]=*flag;
            *flag=1; /* Use native cheats, including item weapons and civilian penalties. */
        } else if(cheat_owned&bits[kind])*flag=saved_cheats[p][kind];
    }
    DWORD *freeplay=(DWORD *)(game_base+0x5C8E70);
    if(requested&HOTD_CHEAT_CONTINUES) {
        if(!(cheat_owned&HOTD_CHEAT_CONTINUES))saved_freeplay=*freeplay;
        *freeplay=1; /* Native free play admits starts/continues without spending either credit pool. */
    } else if(cheat_owned&HOTD_CHEAT_CONTINUES)*freeplay=saved_freeplay;
    if(requested!=cheat_owned)logline("native cheats: ammo=%u lives=%u continues=%u",
        !!(requested&HOTD_CHEAT_AMMO),!!(requested&HOTD_CHEAT_LIVES),!!(requested&HOTD_CHEAT_CONTINUES));
    cheat_owned=requested;
}

static void update_input(void)
{
    int active = active_input();
    buttons[0] = active ? frame->aim_buttons : 0;
    buttons[1] = active && frame->akimbo ? frame->aim2_buttons : 0;
    if(active && frame->akimbo && (buttons[0]&VC2_BTN_PAIR_START) && ((buttons[0]|buttons[1])&VC2_BTN_START)) {
        buttons[0]|=VC2_BTN_START;buttons[1]|=VC2_BTN_START;
    }
    if (active) {
        unsigned p;
        for (p=0;p<(frame->akimbo ? 2u:1u);p++) {
            float sx = p ? frame->aim2_x : frame->aim_x;
            float sy = p ? frame->aim2_y : frame->aim_y;
            volatile short *axes = (volatile short *)(game_base+0x5C8FC4+p*0x28);
            if (!isfinite(sx) || !isfinite(sy)) continue;
            /* VR can aim beyond the flat viewport; only the native WORD range limits it. */
            axes[0] = (short)lroundf(fmaxf(-32768,fminf(32767,sx-320)));
            axes[1] = (short)lroundf(fmaxf(-32768,fminf(32767,240-sy)));
        }
    }
}

static DWORD (__cdecl *native_input_original)(void);
static int native_reload(unsigned player,HotdGunInput *gun)
{
    BYTE *profile=game_base+0x5A5D84+player*0x130;
    if(*(DWORD *)(game_base+0x5C8E98)!=6 ||
       *(short *)(game_base+0x5A5C62+player*0x130)!=5)return 0;
    if(profile[1]==1) {
        gun->valid=0;gun->fire=1; /* Native gun path: off-screen trigger means reload. */
        return 1;
    }
    if(profile[1]==0) {
        unsigned scheme=profile[0]==1 ? 2u:profile[0]==2 ? 3u:1u;
        DWORD mask=*(DWORD *)(game_base+0x5C9F6C+player*0x7C+scheme*20);
        *(DWORD *)(game_base+0x5C9028)|=mask;
        return mask!=0;
    }
    return 0;
}

static DWORD __cdecl native_input(void)
{
    static unsigned previous[2];
    unsigned p;
    DWORD result=native_input_original();
    unsigned requested=requested_cheats();
    update_input();
    native_cheats(requested);
    for(p=0;p<2;p++) {
        if(active_input() && (!p || frame->akimbo)) {
            HotdGunInput *gun=(HotdGunInput *)(game_base+0x5C8FC0+p*0x28);
            unsigned cancel_pressed=(buttons[p]&VC2_BTN_BACK)&~previous[p];
            unsigned action=hotd_apply_gun_buttons(gun,buttons[p],previous+p);
            previous[p]|=buttons[p]&VC2_BTN_BACK;
            if(cancel_pressed)action=VC2_BTN_RELOAD;
            if(hotd_rapid_fire(rapid_fire+p,
                (buttons[p]&VC2_BTN_FIRE) && !(buttons[p]&VC2_BTN_RELOAD) && !cancel_pressed,
                (requested&HOTD_CHEAT_RAPID) && *(DWORD *)(game_base+0x5C8E98)==6 &&
                *(short *)(game_base+0x5A5C62+p*0x130)==5 && *(DWORD *)(game_base+0x5C8E00) &&
                !*(short *)(game_base+0x5A1A00),frame->rapid_fire_rate,GetTickCount())) {
                gun->fire=1;action=VC2_BTN_FIRE;
            }
            if(action==VC2_BTN_RELOAD) {
                int accepted=native_reload(p,gun);
                logline("native reload P%u: queued=%d ammo_before=%d phase=%lu status=%d control=%u",p+1,accepted,
                    *(short *)(game_base+0x5A5C7C+p*0x130),*(DWORD *)(game_base+0x5C8E98),
                    *(short *)(game_base+0x5A5C62+p*0x130),game_base[0x5A5D85+p*0x130]);
            }
            if(action==VC2_BTN_FIRE)
                logline("native trigger P%u: valid=%d fire=%d ammo=%d",p+1,gun->valid,gun->fire,
                    *(short *)(game_base+0x5A5C7C+p*0x130));
        } else {previous[p]=0;rapid_fire[p].active=0;}
    }
    return result;
}

typedef HRESULT (WINAPI *InputCreate)(HINSTANCE,DWORD,REFIID,void **,IUnknown *);
typedef HRESULT (WINAPI *CreateInputDevice)(void *,REFGUID,void **,IUnknown *);
typedef HRESULT (WINAPI *CreateInputDeviceEx)(void *,REFGUID,REFIID,void **,IUnknown *);
typedef HRESULT (WINAPI *GetDeviceState)(void *,DWORD,void *);
static InputCreate original_input_create;
static CreateInputDevice original_device_create;
static CreateInputDeviceEx original_device_create_ex;
static GetDeviceState original_keyboard_state;

static HRESULT WINAPI keyboard_state(void *self,DWORD size,void *out)
{
    HRESULT hr = original_keyboard_state(self,size,out);
    update_input();
    if(FAILED(hr) && size==256 && active_input()) {
        memset(out,0,size);hr=DI_OK;
    }
    if (SUCCEEDED(hr) && size==256 && active_input()) {
        BYTE *keys = out;
        if(buttons[0]&VC2_BTN_START) keys[DIK_RETURN]=0x80;
        if(buttons[1]&VC2_BTN_START) keys[DIK_TAB]=0x80;
        if(buttons[0]&VC2_BTN_BACK) keys[DIK_RCONTROL]=0x80;
        if(buttons[1]&VC2_BTN_BACK) keys[DIK_LCONTROL]=0x80;
        unsigned nav=(buttons[0]|buttons[1])&(VC2_BTN_UP|VC2_BTN_DOWN);
        if(nav==VC2_BTN_UP)keys[DIK_UP]=0x80;
        if(nav==VC2_BTN_DOWN)keys[DIK_DOWN]=0x80;
        nav=(buttons[0]|buttons[1])&(VC2_BTN_LEFT|VC2_BTN_RIGHT);
        if(nav==VC2_BTN_LEFT)keys[DIK_LEFT]=0x80;
        if(nav==VC2_BTN_RIGHT)keys[DIK_RIGHT]=0x80;
    }
    return hr;
}

static void hook_keyboard(REFGUID guid,void *device)
{
    if (IsEqualGUID(guid,&GUID_SysKeyboard)) {
        void **vt = *(void ***)device;
        logline("keyboard GetDeviceState hook: %d",replace_pointer(vt+9,keyboard_state,(void **)&original_keyboard_state));
    }
}
static HRESULT WINAPI create_input_device(void *self,REFGUID guid,void **out,IUnknown *outer)
{
    HRESULT hr=original_device_create(self,guid,out,outer);
    if(SUCCEEDED(hr) && *out) hook_keyboard(guid,*out);
    return hr;
}
static HRESULT WINAPI create_input_device_ex(void *self,REFGUID guid,REFIID iid,void **out,IUnknown *outer)
{
    HRESULT hr=original_device_create_ex(self,guid,iid,out,outer);
    if(SUCCEEDED(hr) && *out) hook_keyboard(guid,*out);
    return hr;
}
static HRESULT WINAPI input_create(HINSTANCE instance,DWORD version,REFIID iid,void **out,IUnknown *outer)
{
    HRESULT hr=original_input_create(instance,version,iid,out,outer);
    if(SUCCEEDED(hr) && *out && IsEqualGUID(iid,&IID_IDirectInput7A)) {
        void **vt=*(void ***)*out;
        replace_pointer(vt+3,create_input_device,(void **)&original_device_create);
        replace_pointer(vt+9,create_input_device_ex,(void **)&original_device_create_ex);
        logline("DirectInput7 hooked");
    } else logline("DirectInputCreateEx hr=%08lx iid=%08lx",(unsigned long)hr,(unsigned long)iid->Data1);
    return hr;
}

typedef struct { IDirectDrawSurface7 *surface; unsigned x,y,w,h,last_used; } Texture;
static Texture textures[2048];
static HotdAtlas atlas_space;
static unsigned texture_count, texture_evictions, texture_failures;
_Static_assert(VC2_ATLAS_W==HOTD_ATLAS_SIDE*HOTD_ATLAS_CELL && VC2_ATLAS_H==VC2_ATLAS_W,
    "Atlas allocator dimensions");

static int evict_texture(void)
{
    unsigned i,victim=texture_count;
    for(i=0;i<texture_count;i++)if(textures[i].last_used!=frame_no &&
        (victim==texture_count || textures[i].last_used<textures[victim].last_used))victim=i;
    if(victim==texture_count)return 0; /* Never evict a tile referenced by this frame. */
    Texture *t=textures+victim;
    hotd_atlas_free(&atlas_space,t->x-1,t->y-1,t->w+2,t->h+2);
    IDirectDrawSurface7_Release(t->surface);
    textures[victim]=textures[--texture_count];
    texture_evictions++;
    return 1;
}

static BYTE channel(DWORD value,DWORD mask)
{
    unsigned shift=0;
    if(!mask) return 255;
    while(!(mask&1)) {mask>>=1;shift++;}
    return (BYTE)(((value>>shift)&mask)*255u/mask);
}
static DWORD read_pixel(const BYTE *row,unsigned x,unsigned bytes)
{
    DWORD v=0;
    memcpy(&v,row+x*bytes,bytes);
    return v;
}

static DWORD pixel_bgra(DWORD value,const DDPIXELFORMAT *pf,const PALETTEENTRY *palette)
{
    if(pf->dwFlags&DDPF_PALETTEINDEXED8) {
        const PALETTEENTRY *p=palette+(value&255);
        return 0xFF000000u | ((DWORD)p->peRed<<16) | ((DWORD)p->peGreen<<8) | p->peBlue;
    }
    return ((DWORD)((pf->dwFlags&DDPF_ALPHAPIXELS) ? channel(value,pf->dwRGBAlphaBitMask):255)<<24) |
        ((DWORD)channel(value,pf->dwRBitMask)<<16) | ((DWORD)channel(value,pf->dwGBitMask)<<8) |
        channel(value,pf->dwBBitMask);
}

static int opaque_bitmap_alpha(IDirectDrawSurface7 *surface)
{
    DDSURFACEDESC2 desc={0};unsigned x,y;
    desc.dwSize=sizeof(desc);
    if(FAILED(IDirectDrawSurface7_Lock(surface,NULL,&desc,DDLOCK_WAIT,NULL)))return 0;
    int repaired=desc.ddpfPixelFormat.dwRGBBitCount==32 &&
        (desc.ddpfPixelFormat.dwFlags&(DDPF_RGB|DDPF_ALPHAPIXELS))==(DDPF_RGB|DDPF_ALPHAPIXELS) &&
        desc.ddpfPixelFormat.dwRGBAlphaBitMask==0xFF000000;
    if(repaired)for(y=0;y<desc.dwHeight;y++) {
        DWORD *row=(DWORD *)((BYTE *)desc.lpSurface+(LONG)y*desc.lPitch);
        for(x=0;x<desc.dwWidth;x++)row[x]|=0xFF000000;
    }
    IDirectDrawSurface7_Unlock(surface,NULL);
    return repaired;
}

static Texture *capture_texture(IDirect3DDevice7 *device)
{
    IDirectDrawSurface7 *surface=NULL;
    IDirectDrawPalette *pal=NULL;
    PALETTEENTRY entries[256];
    DDSURFACEDESC2 desc;
    DDCOLORKEY key;
    Texture *t;
    unsigned i,x,y,bytes,tx,ty;
    int has_key;
    if(FAILED(IDirect3DDevice7_GetTexture(device,0,&surface)) || !surface) return NULL;
    for(i=0;i<texture_count;i++) if(textures[i].surface==surface) {
        textures[i].last_used=frame_no;
        IDirectDrawSurface7_Release(surface); return textures+i;
    }
    if(texture_count>=2048 && !evict_texture()) {texture_failures++;IDirectDrawSurface7_Release(surface);return NULL;}
    memset(&desc,0,sizeof(desc));desc.dwSize=sizeof(desc);
    if(FAILED(IDirectDrawSurface7_Lock(surface,NULL,&desc,DDLOCK_WAIT|DDLOCK_READONLY,NULL))) {
        texture_failures++;
        IDirectDrawSurface7_Release(surface);return NULL;
    }
    bytes=desc.ddpfPixelFormat.dwRGBBitCount/8;
    if(!(desc.ddpfPixelFormat.dwFlags&(DDPF_RGB|DDPF_PALETTEINDEXED8)) || bytes<1 || bytes>4 ||
       !desc.dwWidth || !desc.dwHeight || desc.dwWidth+2>VC2_ATLAS_W || desc.dwHeight+2>VC2_ATLAS_H) goto fail;
    memset(entries,0,sizeof(entries));
    if(desc.ddpfPixelFormat.dwFlags&DDPF_PALETTEINDEXED8) {
        if(FAILED(IDirectDrawSurface7_GetPalette(surface,&pal))) goto fail;
        IDirectDrawPalette_GetEntries(pal,0,0,256,entries);
        IDirectDrawPalette_Release(pal);
    }
    while(!hotd_atlas_alloc(&atlas_space,desc.dwWidth+2,desc.dwHeight+2,&tx,&ty)) {
        if(!evict_texture())goto fail;
    }
    has_key=SUCCEEDED(IDirectDrawSurface7_GetColorKey(surface,DDCKEY_SRCBLT,&key));
    t=textures+texture_count++;
    t->surface=surface;t->x=tx+1;t->y=ty+1;t->w=desc.dwWidth;t->h=desc.dwHeight;t->last_used=frame_no;
    /* Reused tiles and their geometry must become visible together. */
    if(!atlas_dirty) {frame->seq++;frame->atlas_seq++;MemoryBarrier();atlas_dirty=1;}
    for(y=0;y<t->h;y++) {
        const BYTE *row=(const BYTE *)desc.lpSurface+(LONG)y*desc.lPitch;
        for(x=0;x<t->w;x++) {
            DWORD raw=read_pixel(row,x,bytes),color=pixel_bgra(raw,&desc.ddpfPixelFormat,entries);
            if(has_key && raw>=key.dwColorSpaceLowValue && raw<=key.dwColorSpaceHighValue) color&=0xFFFFFF;
            frame->atlas[(t->y+y)*VC2_ATLAS_W+t->x+x]=color;
        }
    }
    /* Duplicate edge texels so linear filtering cannot bleed into another tile. */
    for(y=0;y<t->h;y++) {
        uint32_t *row=frame->atlas+(t->y+y)*VC2_ATLAS_W;
        row[t->x-1]=row[t->x];row[t->x+t->w]=row[t->x+t->w-1];
    }
    memcpy(frame->atlas+(t->y-1)*VC2_ATLAS_W+t->x-1,
           frame->atlas+t->y*VC2_ATLAS_W+t->x-1,(t->w+2)*4);
    memcpy(frame->atlas+(t->y+t->h)*VC2_ATLAS_W+t->x-1,
           frame->atlas+(t->y+t->h-1)*VC2_ATLAS_W+t->x-1,(t->w+2)*4);
    IDirectDrawSurface7_Unlock(surface,NULL);
    if(texture_count<=10)logline("texture %u: %ux%u bits=%lu flags=%08lx",texture_count,t->w,t->h,
        (unsigned long)desc.ddpfPixelFormat.dwRGBBitCount,(unsigned long)desc.ddpfPixelFormat.dwFlags);
    /* shortcut: cached textures are immutable; add dirty tracking if a game surface changes in place. */
    return t; /* Keep one reference so reused COM addresses cannot select stale textures. */
fail:
    texture_failures++;
    IDirectDrawSurface7_Unlock(surface,NULL);IDirectDrawSurface7_Release(surface);return NULL;
}

static void transform_point(const float in[3],const D3DMATRIX *matrix,float out[3])
{
    const float *m=(const float *)matrix;
    unsigned j;
    for(j=0;j<3;j++)out[j]=in[0]*m[j]+in[1]*m[4+j]+in[2]*m[8+j]+m[12+j];
}

static void read_camera(IDirect3DDevice7 *device)
{
    IDirect3DDevice7_GetTransform(device,D3DTRANSFORMSTATE_WORLD,&world);
    IDirect3DDevice7_GetTransform(device,D3DTRANSFORMSTATE_VIEW,&view);
    IDirect3DDevice7_GetTransform(device,D3DTRANSFORMSTATE_PROJECTION,&projection);
    IDirect3DDevice7_GetViewport(device,&viewport);
    if(projection._34!=0 && isfinite(projection._11) && projection._11>0 && projection._22>0) {
        lens.fx=projection._11*320;lens.fy=projection._22*240;
        lens.cx=320;lens.cy=240;lens_valid=1;
    }
}

static HotdLighting draw_lighting;
typedef struct { DWORD colour,style,address,fog_colour; const HotdLighting *lighting; } DrawStyle;
static DWORD read_state(IDirect3DDevice7 *device,D3DRENDERSTATETYPE state,DWORD fallback)
{
    DWORD value=fallback;
    if(FAILED(IDirect3DDevice7_GetRenderState(device,state,&value)))return fallback;
    return value;
}
static float read_float_state(IDirect3DDevice7 *device,D3DRENDERSTATETYPE state,float fallback)
{
    DWORD bits;memcpy(&bits,&fallback,4);bits=read_state(device,state,bits);
    memcpy(&fallback,&bits,4);return fallback;
}
static void read_lighting(IDirect3DDevice7 *device,DWORD fvf)
{
    HotdLighting *s=&draw_lighting;
    memset(s,0,sizeof(*s));s->world=world;s->view=view;
    s->material.diffuse.r=s->material.diffuse.g=s->material.diffuse.b=s->material.diffuse.a=1;
    if((fvf&D3DFVF_POSITION_MASK)!=D3DFVF_XYZRHW)IDirect3DDevice7_GetMaterial(device,&s->material);
#define READ(field,state,value) s->field=read_state(device,D3DRENDERSTATE_##state,value)
    READ(lighting,LIGHTING,1);READ(colorvertex,COLORVERTEX,1);
    READ(diffuse_source,DIFFUSEMATERIALSOURCE,D3DMCS_COLOR1);READ(ambient_source,AMBIENTMATERIALSOURCE,D3DMCS_MATERIAL);
    READ(specular_source,SPECULARMATERIALSOURCE,D3DMCS_COLOR2);READ(emissive_source,EMISSIVEMATERIALSOURCE,D3DMCS_MATERIAL);
    READ(ambient,AMBIENT,0);READ(specular_enable,SPECULARENABLE,0);READ(local_viewer,LOCALVIEWER,1);
    READ(normalize_normals,NORMALIZENORMALS,0);READ(fog_enable,FOGENABLE,0);
    READ(fog_vertex_mode,FOGVERTEXMODE,D3DFOG_NONE);READ(fog_table_mode,FOGTABLEMODE,D3DFOG_NONE);
    READ(range_fog,RANGEFOGENABLE,0);
#undef READ
    s->fog_start=read_float_state(device,D3DRENDERSTATE_FOGSTART,0);
    s->fog_end=read_float_state(device,D3DRENDERSTATE_FOGEND,1);
    s->fog_density=read_float_state(device,D3DRENDERSTATE_FOGDENSITY,1);
    if(s->lighting && (fvf&D3DFVF_POSITION_MASK)!=D3DFVF_XYZRHW && device->lpVtbl->GetLightEnable && device->lpVtbl->GetLight)
        for(DWORD i=0;i<16;i++) {
            BOOL enabled=FALSE;D3DLIGHT7 light;
            if(SUCCEEDED(IDirect3DDevice7_GetLightEnable(device,i,&enabled)) && enabled &&
               SUCCEEDED(IDirect3DDevice7_GetLight(device,i,&light)))s->lights[s->light_count++]=light;
        }
    hotd_lighting_prepare(s);
}
static DrawStyle read_draw_style(IDirect3DDevice7 *device,DWORD fvf)
{
    DWORD blend=0,src=2,dst=1,zwrite=1,test=0,func=8,ref=0,alphaop=2,alphaarg=2;
    DWORD cull=D3DCULL_NONE;
    DWORD address_u=D3DTADDRESS_WRAP,address_v=D3DTADDRESS_WRAP;
    DrawStyle style={.colour=0xFFFFFFFF};
#define RS(name,out) IDirect3DDevice7_GetRenderState(device,D3DRENDERSTATE_##name,&out)
    RS(ALPHABLENDENABLE,blend);RS(SRCBLEND,src);RS(DESTBLEND,dst);RS(ZWRITEENABLE,zwrite);
    RS(ALPHATESTENABLE,test);RS(ALPHAFUNC,func);RS(ALPHAREF,ref);

    RS(CULLMODE,cull);
#undef RS
    IDirect3DDevice7_GetTextureStageState(device,0,D3DTSS_ALPHAOP,&alphaop);
    IDirect3DDevice7_GetTextureStageState(device,0,D3DTSS_ALPHAARG1,&alphaarg);
    IDirect3DDevice7_GetTextureStageState(device,0,D3DTSS_ADDRESSU,&address_u);
    IDirect3DDevice7_GetTextureStageState(device,0,D3DTSS_ADDRESSV,&address_v);
    style.address=hotd_address(address_u,address_v);
    if(!frame || !(frame->render_flags&HOTD_RENDER_UNLIT)) {
        read_lighting(device,fvf);style.lighting=&draw_lighting;
        DWORD fog=read_state(device,D3DRENDERSTATE_FOGCOLOR,0);
        style.fog_colour=(fog&0xFF00u)|((fog&255u)<<16)|((fog>>16)&255u);
    } else if((fvf&D3DFVF_POSITION_MASK)!=D3DFVF_XYZRHW && read_state(device,D3DRENDERSTATE_LIGHTING,0)) {
        DWORD colorvertex=read_state(device,D3DRENDERSTATE_COLORVERTEX,0);
        DWORD source=read_state(device,D3DRENDERSTATE_DIFFUSEMATERIALSOURCE,0);
        D3DMATERIAL7 material;
        if((!colorvertex || source==D3DMCS_MATERIAL || !(fvf&D3DFVF_DIFFUSE)) &&
           SUCCEEDED(IDirect3DDevice7_GetMaterial(device,&material)))
            style.colour=hotd_rgba(material.diffuse.r,material.diffuse.g,material.diffuse.b,material.diffuse.a);
    }
    style.style=hotd_style(blend,src,dst,zwrite,alphaop==D3DTOP_MODULATE ||
        (alphaop==D3DTOP_SELECTARG1 && (alphaarg&D3DTA_SELECTMASK)==D3DTA_DIFFUSE),test,func,ref) |
        ((cull&3u)<<HOTD_CULL_SHIFT);
    return style;
}

static int convert_vertex(const BYTE *v,DWORD fvf,Texture *tex,DrawStyle style,Vc2Vertex *out)
{
    unsigned pos=fvf&D3DFVF_POSITION_MASK,offset;
    DWORD colour=0xFFFFFFFF,specular=0;
    float p[3],tmp[3];
    const float *f=(const float *)v;
    if(pos==D3DFVF_XYZRHW) {
        if(!lens_valid || !viewport.dwWidth || !viewport.dwHeight ||
           !hotd_unproject(&lens,(f[0]-viewport.dwX)*640/viewport.dwWidth,
               (f[1]-viewport.dwY)*480/viewport.dwHeight,f[3],p)) return 0;
        offset=16;
    } else if(pos==D3DFVF_XYZ) {
        transform_point(f,&world,tmp);transform_point(tmp,&view,p);p[2]=-p[2];offset=12;
    } else return 0;
    if(!isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2]))return 0;
    out->x=p[0];out->y=p[1];out->z=p[2];
    if(fvf&D3DFVF_NORMAL)offset+=12;
    if(fvf&0x20)offset+=4;
    if(fvf&D3DFVF_DIFFUSE){memcpy(&colour,v+offset,4);offset+=4;}
    if(fvf&D3DFVF_SPECULAR){memcpy(&specular,v+offset,4);offset+=4;}
    if(style.lighting) {
        HotdLitVertex lit;
        hotd_light_vertex(style.lighting,f,(fvf&D3DFVF_NORMAL)?f+(pos==D3DFVF_XYZRHW?4:3):NULL,colour,specular,fvf,&lit);
        out->colour=hotd_rgba(lit.diffuse[0],lit.diffuse[1],lit.diffuse[2],lit.diffuse[3]);
        out->specular=hotd_rgba(lit.specular[0],lit.specular[1],lit.specular[2],0);
        out->fog_colour=style.fog_colour|(lit.fog_mode<<24);out->fog=lit.fog;
    } else {
        out->colour=(fvf&D3DFVF_DIFFUSE) ? (colour&0xFF00FF00u)|((colour&255)<<16)|((colour>>16)&255):style.colour;
        out->specular=out->fog_colour=0;out->fog=1;
    }
    out->style=style.style|capture_kind;
    out->address=style.address;
    out->u=out->v=-1;out->depth=0;
    out->atlas_x=out->atlas_y=out->atlas_w=out->atlas_h=0;
    if(tex && (fvf&D3DFVF_TEXCOUNT_MASK)) {
        float uv[2];memcpy(uv,v+offset,8);
        if(!isfinite(uv[0])||!isfinite(uv[1]))return 0;
        out->u=uv[0];out->v=uv[1];
        out->atlas_x=(tex->x+0.5f)/VC2_ATLAS_W;
        out->atlas_y=(tex->y+0.5f)/VC2_ATLAS_H;
        out->atlas_w=(tex->w-1.0f)/VC2_ATLAS_W;
        out->atlas_h=(tex->h-1.0f)/VC2_ATLAS_H;
    }
    return 1;
}

static void trace_draw_state(IDirect3DDevice7 *device,DWORD fvf,Texture *tex,const void *data)
{
    static const D3DRENDERSTATETYPE rs[]={D3DRENDERSTATE_ALPHABLENDENABLE,D3DRENDERSTATE_SRCBLEND,
        D3DRENDERSTATE_DESTBLEND,D3DRENDERSTATE_ZWRITEENABLE,D3DRENDERSTATE_ALPHATESTENABLE,
        D3DRENDERSTATE_TEXTUREFACTOR,D3DRENDERSTATE_LIGHTING,D3DRENDERSTATE_COLORVERTEX,
        D3DRENDERSTATE_DIFFUSEMATERIALSOURCE,D3DRENDERSTATE_AMBIENT};
    static const D3DTEXTURESTAGESTATETYPE ts[]={D3DTSS_COLOROP,D3DTSS_COLORARG1,D3DTSS_COLORARG2,
        D3DTSS_ALPHAOP,D3DTSS_ALPHAARG1,D3DTSS_ALPHAARG2};
    static DWORD seen[32][19];
    static unsigned used;
    DWORD key[19]={0};D3DMATERIAL7 material={0};unsigned i;
    if(used==32 || frame_no%30)return;
    key[0]=fvf;key[1]=tex!=NULL;
    for(i=0;i<10;i++)IDirect3DDevice7_GetRenderState(device,rs[i],key+2+i);
    if(tex && !key[2])return;
    for(i=0;i<6;i++)IDirect3DDevice7_GetTextureStageState(device,0,ts[i],key+12+i);
    IDirect3DDevice7_GetMaterial(device,&material);memcpy(key+18,&material.diffuse.a,4);
    for(i=0;i<used;i++)if(!memcmp(key,seen[i],sizeof(key)))return;
    memcpy(seen[used++],key,sizeof(key));
    logline("render-state fvf=%lx tex=%lu blend=%lu src=%lu dst=%lu zwrite=%lu atest=%lu factor=%08lx lighting=%lu colorvertex=%lu matsrc=%lu ambient=%08lx colorop=%lu args=%lu,%lu alphaop=%lu args=%lu,%lu material=%g,%g,%g,%g first=%g,%g,%g",
        (unsigned long)fvf,(unsigned long)key[1],(unsigned long)key[2],(unsigned long)key[3],
        (unsigned long)key[4],(unsigned long)key[5],(unsigned long)key[6],(unsigned long)key[7],
        (unsigned long)key[8],(unsigned long)key[9],(unsigned long)key[10],(unsigned long)key[11],
        (unsigned long)key[12],(unsigned long)key[13],(unsigned long)key[14],(unsigned long)key[15],
        (unsigned long)key[16],(unsigned long)key[17],material.diffuse.r,material.diffuse.g,
        material.diffuse.b,material.diffuse.a,((const float *)data)[0],((const float *)data)[1],((const float *)data)[2]);
}

static void capture_draw(IDirect3DDevice7 *device,D3DPRIMITIVETYPE type,DWORD fvf,
                         const void *data,DWORD n,const WORD *indices,DWORD ni)
{
    unsigned stride=hotd_fvf_stride(fvf),i,total=indices?ni:n;
    Texture *texture;
    DrawStyle style;
    DWORD z_enable=0;
    int hud=0;
    if(capture_skip)return;
    calls++;
    read_camera(device);
    for(i=0;i<fvf_count && fvf_logged[i]!=fvf;i++);
    if(i==fvf_count && fvf_count<32) {
        const float *v=data;
        fvf_logged[fvf_count++]=fvf;
        logline("draw fvf=%08lx stride=%u type=%u vertices=%lu first=%g,%g,%g,%g",
            (unsigned long)fvf,stride,type,(unsigned long)n,v[0],v[1],v[2],stride>=16?v[3]:0);
        logline("projection %g %g %g %g; viewport %lu %lu %lu %lu",projection._11,projection._22,
            projection._33,projection._34,(unsigned long)viewport.dwX,(unsigned long)viewport.dwY,
            (unsigned long)viewport.dwWidth,(unsigned long)viewport.dwHeight);
    }
    if(!frame || !data || !stride || total<3)return;
    /* Native sprites/bitmaps are screen-space UI at any sort depth, in every mode. */
    hud=(fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW && !capture_kind && !capture_model;
    IDirect3DDevice7_GetRenderState(device,D3DRENDERSTATE_ZENABLE,&z_enable);
    if(!z_enable && !hud)return;
    texture=capture_texture(device);
    trace_draw_state(device,fvf,texture,data);
    style=read_draw_style(device,fvf);
    if(!hud && (capture_kind&HOTD_SHOT)) {
        const float origin[3]={0};float tmp[3],*anchor=shot_origins[capture_kind>>26];
        transform_point(origin,&world,tmp);transform_point(tmp,&view,anchor);anchor[2]=-anchor[2];
    }
    for(i=0;i+2<total;i+=(type==D3DPT_TRIANGLELIST?3:1)) {
        unsigned q[3]={i,i+1,i+2},k;
        Vc2Vertex tri[3];
        if(type==D3DPT_TRIANGLEFAN){q[0]=0;q[1]=i+1;q[2]=i+2;}
        else if(type==D3DPT_TRIANGLESTRIP && (i&1)){q[0]=i+1;q[1]=i;}
        else if(type!=D3DPT_TRIANGLELIST && type!=D3DPT_TRIANGLESTRIP)break;
        if((hud?hud_count:count)+3>(hud?HOTD_MAX_HUD_VERTS:VC2_MAX_VERTS)){dropped++;break;}
        for(k=0;k<3;k++) {
            unsigned idx=indices?indices[q[k]]:q[k];
            if(idx>=n || !convert_vertex((const BYTE *)data+idx*stride,fvf,texture,style,tri+k))break;
            if(hud) {
                const float *v=(const float *)((const BYTE *)data+idx*stride);
                float sx=(v[0]-viewport.dwX)*640/viewport.dwWidth,sy=(v[1]-viewport.dwY)*480/viewport.dwHeight;
                tri[k].x=(sx/320-1)*HOTD_PANEL_HALF_WIDTH_M*frame->units_per_metre;
                tri[k].y=(1-sy/240)*HOTD_PANEL_HALF_HEIGHT_M*frame->units_per_metre;
                tri[k].z=-HOTD_PANEL_DISTANCE_M*frame->units_per_metre;
                tri[k].depth=1;
                tri[k].style=(tri[k].style&~(HOTD_WORLD|HOTD_SHOT|HOTD_SHOT_GROUP_MASK|HOTD_CULL_MASK))|HOTD_NO_ZWRITE;
            }
        }
        if(k!=3){dropped++;continue;}
        if(hud){memcpy(hud_vertices+hud_count,tri,sizeof(tri));hud_count+=3;}
        else {memcpy(vertices+count,tri,sizeof(tri));count+=3;}
    }
}

static void capture_screen(IDirect3DDevice7 *device)
{
    IDirectDrawSurface7 *surface=NULL;
    DDSURFACEDESC2 desc;
    unsigned w,h,x,y,bytes;
    if(FAILED(IDirect3DDevice7_GetRenderTarget(device,&surface)) || !surface)return;
    memset(&desc,0,sizeof(desc));desc.dwSize=sizeof(desc);
    if(FAILED(IDirectDrawSurface7_Lock(surface,NULL,&desc,DDLOCK_WAIT|DDLOCK_READONLY,NULL)))goto done;
    bytes=desc.ddpfPixelFormat.dwRGBBitCount/8;
    if(!(desc.ddpfPixelFormat.dwFlags&DDPF_RGB) || bytes<2 || bytes>4)goto unlock;
    w=desc.dwWidth;h=desc.dwHeight;
    if(w>VC2_SCREEN_MAX_W)w=VC2_SCREEN_MAX_W;
    if(h>VC2_SCREEN_MAX_H)h=VC2_SCREEN_MAX_H;
    frame->screen_seq++;MemoryBarrier();
    for(y=0;y<h;y++) {
        const BYTE *row=(const BYTE *)desc.lpSurface+(LONG)(y*desc.dwHeight/h)*desc.lPitch;
        for(x=0;x<w;x++)frame->screen[y*w+x]=pixel_bgra(read_pixel(row,x*desc.dwWidth/w,bytes),&desc.ddpfPixelFormat,NULL);
    }
    frame->screen_w=w;frame->screen_h=h;frame->screen_ok=1;frame->screen_frame++;
    MemoryBarrier();frame->screen_seq++;
unlock: IDirectDrawSurface7_Unlock(surface,NULL);
done: IDirectDrawSurface7_Release(surface);
}

typedef DWORD (__cdecl *NativeDraw)(const void *,DWORD);
typedef int (__cdecl *NativeModel)(const BYTE *,int);
static NativeModel native_model_original,native_model_alt_original;
typedef void (__cdecl *NativeSubmit)(const BYTE *);
typedef void (__cdecl *NativeSubmitAlt)(const BYTE *,float);
static NativeSubmit native_submit_original;
static NativeSubmitAlt native_submit_alt_original;
static int (__cdecl *native_visibility_original)(float,float,float,float);
static HRESULT (WINAPI *original_sphere_visibility)(IDirect3DDevice7 *,D3DVECTOR *,D3DVALUE *,DWORD,DWORD,DWORD *);

static int native_stage_task(void)
{
    const DWORD *task=*(const DWORD **)(game_base+0x3DF8B0);
    return task && task[0]==(DWORD)(ULONG_PTR)(game_base+0x1260);
}
static int remember_stage_record(const BYTE *record)
{
    DWORD model,black;
    memcpy(&model,record+0xC,sizeof(model));
    memcpy(&black,game_base+0x5A66A4+0x93E*16,sizeof(black));
    if(visibility_ready && frame && (frame->render_flags&HOTD_RENDER_EXPAND_WORLD) &&
       model && model!=black && native_stage_task())return hotd_remember_stage(&stage_records,record);
    return 0;
}
static void __cdecl native_submit(const BYTE *record)
{remember_stage_record(record);native_submit_original(record);}
static void __cdecl native_submit_alt(const BYTE *record,float alpha)
{
    if(remember_stage_record(record)) {
        BYTE deferred[HOTD_MODEL_RECORD_SIZE];
        memcpy(deferred,record,sizeof(deferred));memcpy(deferred+0x10,&alpha,sizeof(alpha));
        /* The ALT queue replaces its copy's alpha as well as sort flags/depth. */
        hotd_remember_stage(&stage_records,deferred);
    }
    native_submit_alt_original(record,alpha);
}

static int native_stage_visibility(int visible,float radius,uintptr_t caller)
{
    return visible || (visibility_ready && frame && (frame->render_flags&HOTD_RENDER_EXPAND_WORLD) &&
        !stage_records.overflow && stage_records.count<HOTD_STAGE_RECORDS && isfinite(radius) && radius>0 &&
        hotd_stage_visibility_caller(caller));
}
static int __cdecl native_visibility(float x,float y,float z,float radius)
{
    int visible=native_visibility_original(x,y,z,radius);
    return native_stage_visibility(visible,radius,(uintptr_t)__builtin_return_address(0)-(uintptr_t)game_base);
}
static HRESULT WINAPI sphere_visibility(IDirect3DDevice7 *self,D3DVECTOR *centres,D3DVALUE *radii,
                                       DWORD n,DWORD flags,DWORD *visibility)
{
    HRESULT hr=original_sphere_visibility(self,centres,radii,n,flags,visibility);
    if(SUCCEEDED(hr) && capture_model && capture_visibility_planes)
        for(DWORD i=0;i<n;i++)visibility[i]&=~capture_visibility_planes;
    return hr;
}
static unsigned native_model_kind(const BYTE *record)
{
    DWORD model;
    float z;
    memcpy(&model,record+0xC,4);memcpy(&z,record+0x34+14*4,4);
    /* Camera-plane flashes relocate; traveling tracers keep their depth but never block aim. */
    if(model)
        for(unsigned bank=0;bank<4;bank++) {
            unsigned first=*(unsigned short *)(game_base+0x179F78+bank*2);
            for(unsigned i=0;i<9;i++)
                if(model==*(DWORD *)(game_base+0x5A66A4+(first+i)*16))
                    return fabsf(fabsf(z)-1.0f)<.01f ? HOTD_SHOT|((shot_group++&63u)<<26):0;
        }
    return HOTD_WORLD;
}
static int native_letterbox(const BYTE *record)
{
    DWORD model,bar;
    float matrix[16];
    memcpy(&model,record+0xC,4);memcpy(&bar,game_base+0x5A66A4+0x93E*16,4);
    if(!bar || model!=bar)return 0;
    memcpy(matrix,record+0x34,sizeof(matrix));
    /* The same black model also supplies pause dimming and full-screen fades. */
    if(!(fabsf(matrix[13])>=.349f && fabsf(matrix[13])<=.451f) ||
       !(fabsf(fabsf(matrix[14])-1)<.0001f))return 0;
    for(unsigned i=0;i<16;i++)if(i!=13 && i!=14 &&
        !(fabsf(matrix[i]-(i%5==0?1.0f:0.0f))<.0001f))return 0;
    return 1;
}
static int native_model_draw(const BYTE *record,int pass,NativeModel original)
{
    unsigned previous=capture_kind,previous_skip=capture_skip,previous_model=capture_model;
    DWORD previous_planes=capture_visibility_planes;
    int result;
    capture_model=1;
    capture_skip|=native_letterbox(record);
    if(capture_skip && !previous_skip) {
        static unsigned logged;
        if(!logged++){logline("native letterbox bars omitted from VR capture");}
    }
    capture_kind=native_model_kind(record);
    capture_visibility_planes=visibility_ready?hotd_visibility_expand(frame?frame->render_flags:0,
        capture_kind==HOTD_WORLD && !capture_skip && hotd_stage_record(&stage_records,record),
        original==native_model_alt_original):0;
    result=original(record,pass);
    capture_kind=previous;capture_skip=previous_skip;capture_model=previous_model;
    capture_visibility_planes=previous_planes;
    return result;
}
static int __cdecl native_model(const BYTE *record,int pass)
{return native_model_draw(record,pass,native_model_original);}
static int __cdecl native_model_alt(const BYTE *record,int pass)
{return native_model_draw(record,pass,native_model_alt_original);}
static NativeDraw native_strip_original,native_list_original,native_tl_original;
static DWORD native_draw(const void *v,DWORD n,D3DPRIMITIVETYPE type,DWORD fvf,NativeDraw original)
{
    IDirect3DDevice7 *device=*(IDirect3DDevice7 **)(game_base+0x3DEB74);
    if(device){active_device=device;capture_draw(device,type,fvf,v,n,NULL,0);}
    return original(v,n);
}
static DWORD __cdecl native_strip(const void *v,DWORD n)
{return native_draw(v,n,D3DPT_TRIANGLESTRIP,0x112,native_strip_original);}
static DWORD __cdecl native_list(const void *v,DWORD n)
{return native_draw(v,n,D3DPT_TRIANGLELIST,0x112,native_list_original);}
static DWORD __cdecl native_tl(const void *v,DWORD n)
{return native_draw(v,n,D3DPT_TRIANGLESTRIP,0x1C4,native_tl_original);}

/* Callers supply whole, non-relative instructions (5-byte damage or 9-byte draw/input prologues). */
static int detour(DWORD rva,const BYTE *signature,unsigned n,void *hook,void **original)
{
    BYTE *at=game_base+rva,*trampoline,patch[9];DWORD jump;
    if(n<5 || n>sizeof(patch) || memcmp(at,signature,n))return 0;
    trampoline=VirtualAlloc(NULL,n+5,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    if(!trampoline)return 0;
    memcpy(trampoline,at,n);trampoline[n]=0xE9;
    jump=(DWORD)((at+n)-(trampoline+n+5));memcpy(trampoline+n+1,&jump,4);
    FlushInstructionCache(GetCurrentProcess(),trampoline,n+5);
    memset(patch,0x90,n);patch[0]=0xE9;
    jump=(DWORD)((BYTE *)hook-(at+5));memcpy(patch+1,&jump,4);
    *original=trampoline;
    if(!write_code(at,patch,n)){VirtualFree(trampoline,0,MEM_RELEASE);*original=NULL;return 0;}
    return 1;
}
static int install_invincibility(void)
{
    static const BYTE signature[]={0x56,0x8B,0x74,0x24,0x08}; /* push esi; mov esi,[esp+8] */
    if(!native_cheat_profile(game_base) || memcmp(game_base+0x15300,signature,sizeof(signature)) ||
       memcmp(game_base+0x15430,signature,sizeof(signature)))return 0;
    return detour(0x15300,signature,sizeof(signature),native_damage,(void **)&native_damage_original) &&
        detour(0x15430,signature,sizeof(signature),native_damage_forced,(void **)&native_damage_forced_original);
}

static int install_capture_visibility(void)
{
    static const BYTE submit[]={0x56,0x8B,0x74,0x24,0x08};
    static const BYTE sphere[]={0x83,0xEC,0x38,0xD9,0x05,0xCC,0x49,0x4C,0x00};
    /* Verify all call sites before any hook can enable expanded submission. */
    if(!hotd_visibility_profile(game_base))return 0;
    visibility_hooks_ready=detour(0xA7E50,submit,sizeof(submit),native_submit,(void **)&native_submit_original) &&
        detour(0xA8390,submit,sizeof(submit),native_submit_alt,(void **)&native_submit_alt_original) &&
        detour(0x6310,sphere,sizeof(sphere),native_visibility,(void **)&native_visibility_original);
    return visibility_hooks_ready;
}

static HRESULT (WINAPI *original_present)(void *,DWORD);
static HRESULT WINAPI native_present(void *self,DWORD flags)
{
    publish_frame();
    return original_present(self,flags);
}

static HRESULT (WINAPI *original_begin)(IDirect3DDevice7 *);
static HRESULT (WINAPI *original_end)(IDirect3DDevice7 *);
static HRESULT WINAPI begin_scene(IDirect3DDevice7 *self)
{
    active_device=self;update_input();return original_begin(self);
}
static HRESULT WINAPI end_scene(IDirect3DDevice7 *self)
{
    return original_end(self);
}
static BOOL CALLBACK find_game_window(HWND hwnd,LPARAM unused)
{
    DWORD pid=0;char title[128];RECT client,outer;
    (void)unused;
    GetWindowThreadProcessId(hwnd,&pid);
    if(pid!=GetCurrentProcessId())return TRUE;
    GetWindowTextA(hwnd,title,sizeof(title));
    if(strcmp(title,"The House of the Dead 2"))return TRUE;
    game_window=hwnd;
    GetClientRect(hwnd,&client);GetWindowRect(hwnd,&outer);
    if(client.right!=640 || client.bottom!=480)
        SetWindowPos(hwnd,NULL,0,0,outer.right-outer.left+640-client.right,
            outer.bottom-outer.top+480-client.bottom,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    return FALSE;
}
static unsigned native_active_players(void)
{
    unsigned active=0;
    if(input_ready)for(unsigned p=0;p<2;p++)
        if(hotd_player_active(*(DWORD *)(game_base+0x5C8E98),
            *(short *)(game_base+0x5A5C62+p*0x130)))active|=1u<<p;
    return active;
}
static void publish_frame(void)
{
    if(!frame || !active_device)return;
    frame_no++;
    if(!IsWindow(game_window))EnumWindows(find_game_window,0);
    capture_screen(active_device);
    if(!atlas_dirty)frame->seq++;
    MemoryBarrier();
    memcpy(frame->verts,vertices,count*sizeof(vertices[0]));
    unsigned hud_n=hud_count<VC2_MAX_VERTS-count?hud_count:VC2_MAX_VERTS-count;
    dropped+=(hud_count-hud_n)/3;
    memcpy(frame->verts+count,hud_vertices,hud_n*sizeof(hud_vertices[0]));
    frame->vertex_count=count+hud_n;frame->frame=frame_no;
    frame->reserved[0]=count;frame->reserved[1]=0;frame->reserved[2]=count+1;
    frame->lens=lens;frame->lens_valid=lens_valid;
    frame->draw_calls=calls;frame->dropped_triangles=dropped;frame->texture_count=texture_count;
    frame->active_players=native_active_players();
    memcpy(frame->shot_origins,shot_origins,sizeof(shot_origins));
    frame->heartbeat=GetTickCount();frame->game_hwnd=(DWORD)(ULONG_PTR)game_window;
    if(atlas_dirty){frame->atlas_frame++;MemoryBarrier();frame->atlas_seq++;atlas_dirty=0;}
    MemoryBarrier();frame->seq++;
    if(frame_no==1 || frame_no%300==0 || texture_failures || stage_records.overflow)logline("frame=%u draws=%u vertices=%u dropped=%u textures=%u evictions=%u texture_failures=%u lens=%u screen=%u stage_records=%u stage_overflow=%u",
        frame_no,calls,count,dropped,texture_count,texture_evictions,texture_failures,lens_valid,frame->screen_ok,
        stage_records.count,stage_records.overflow);
    texture_failures=0;
    count=hud_count=calls=dropped=shot_group=0;
    stage_records.count=stage_records.overflow=0;
}
static HRESULT (WINAPI *original_create_device)(IDirect3D7 *,REFCLSID,IDirectDrawSurface7 *,IDirect3DDevice7 **);
static HRESULT WINAPI create_device(IDirect3D7 *self,REFCLSID cls,IDirectDrawSurface7 *target,IDirect3DDevice7 **out)
{
    HRESULT hr=original_create_device(self,cls,target,out);
    logline("CreateDevice hr=%08lx",(unsigned long)hr);
    if(SUCCEEDED(hr) && *out) {
        IDirect3DDevice7Vtbl *vt=(IDirect3DDevice7Vtbl *)(*out)->lpVtbl;
#define HOOK(member,fun,original) replace_pointer((void **)&vt->member,fun,(void **)&original)
        HOOK(BeginScene,begin_scene,original_begin);HOOK(EndScene,end_scene,original_end);
        visibility_ready=visibility_hooks_ready && HOOK(ComputeSphereVisibility,sphere_visibility,original_sphere_visibility);
        logline("native capture visibility policy: %s",visibility_ready?"ready":"unavailable; native culling retained");
#undef HOOK
    }
    return hr;
}

static HRESULT (WINAPI *original_query)(IDirectDraw7 *,REFIID,void **);
static HRESULT (WINAPI *original_coop)(IDirectDraw7 *,HWND,DWORD);
static HRESULT WINAPI query_interface(IDirectDraw7 *self,REFIID iid,void **out)
{
    HRESULT hr=original_query(self,iid,out);
    if(SUCCEEDED(hr) && *out && IsEqualGUID(iid,&IID_IDirect3D7)) {
        IDirect3D7Vtbl *vt=(IDirect3D7Vtbl *)((IDirect3D7 *)*out)->lpVtbl;
        logline("Direct3D7 CreateDevice hook: %d",replace_pointer((void **)&vt->CreateDevice,create_device,(void **)&original_create_device));
    }
    return hr;
}
static HRESULT WINAPI cooperative_level(IDirectDraw7 *self,HWND hwnd,DWORD flags)
{
    if(hwnd)game_window=hwnd;
    logline("SetCooperativeLevel hwnd=%p flags=%08lx",hwnd,(unsigned long)flags);
    return original_coop(self,hwnd,flags);
}
static HRESULT (WINAPI *original_release_dc)(IDirectDrawSurface7 *,HDC);
static HRESULT WINAPI release_bitmap_dc(IDirectDrawSurface7 *self,HDC dc)
{
    DWORD caller=(DWORD)(uintptr_t)__builtin_return_address(0)-(DWORD)(uintptr_t)game_base;
    HRESULT hr=original_release_dc(self,dc);
    /* GDI BitBlt clears alpha; these two native loaders upload opaque BMP artwork. */
    if(SUCCEEDED(hr) && hotd_bitmap_dc_caller(caller))
        logline("native bitmap upload: caller=%lx opaque32=%d",(unsigned long)caller,opaque_bitmap_alpha(self));
    return hr;
}
static HRESULT (WINAPI *original_create_surface)(IDirectDraw7 *,DDSURFACEDESC2 *,IDirectDrawSurface7 **,IUnknown *);
static HRESULT WINAPI create_surface(IDirectDraw7 *self,DDSURFACEDESC2 *desc,IDirectDrawSurface7 **out,IUnknown *outer)
{
    HRESULT hr=original_create_surface(self,desc,out,outer);
    if(SUCCEEDED(hr) && *out) {
        IDirectDrawSurface7Vtbl *vt=(IDirectDrawSurface7Vtbl *)(*out)->lpVtbl;
        replace_pointer((void **)&vt->ReleaseDC,release_bitmap_dc,(void **)&original_release_dc);
    }
    return hr;
}
static HRESULT (WINAPI *original_ddraw_create)(GUID *,void **,REFIID,IUnknown *);
static HRESULT WINAPI ddraw_create(GUID *guid,void **out,REFIID iid,IUnknown *outer)
{
    HRESULT hr=original_ddraw_create(guid,out,iid,outer);
    logline("DirectDrawCreateEx hr=%08lx iid=%08lx",(unsigned long)hr,(unsigned long)iid->Data1);
    if(SUCCEEDED(hr) && *out && IsEqualGUID(iid,&IID_IDirectDraw7)) {
        IDirectDraw7Vtbl *vt=(IDirectDraw7Vtbl *)((IDirectDraw7 *)*out)->lpVtbl;
        replace_pointer((void **)&vt->QueryInterface,query_interface,(void **)&original_query);
        replace_pointer((void **)&vt->SetCooperativeLevel,cooperative_level,(void **)&original_coop);
        replace_pointer((void **)&vt->CreateSurface,create_surface,(void **)&original_create_surface);
    }
    return hr;
}

static int hook_import(const char *dll,const char *name,void *hook,void **original)
{
    IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER *)game_base;
    IMAGE_NT_HEADERS32 *nt=(IMAGE_NT_HEADERS32 *)(game_base+dos->e_lfanew);
    DWORD rva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *imp=(IMAGE_IMPORT_DESCRIPTOR *)(game_base+rva);
    if(!rva)return 0;
    for(;imp->Name;imp++)if(!_stricmp((char *)game_base+imp->Name,dll)) {
        IMAGE_THUNK_DATA32 *names=(IMAGE_THUNK_DATA32 *)(game_base+imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA32 *iat=(IMAGE_THUNK_DATA32 *)(game_base+imp->FirstThunk);
        if(!imp->OriginalFirstThunk)return 0;
        for(;names->u1.AddressOfData;names++,iat++)if(!IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) {
            IMAGE_IMPORT_BY_NAME *entry=(IMAGE_IMPORT_BY_NAME *)(game_base+names->u1.AddressOfData);
            if(!strcmp((char *)entry->Name,name))return replace_pointer((void **)&iat->u1.Function,hook,original);
        }
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void *reserved)
{
    char path[MAX_PATH],*slash;
    static const BYTE xsig[]={0x66,0x89,0x46,0x04};
    static const BYTE ysig[]={0x66,0x89,0x46,0x06};
    (void)reserved;
    if(reason!=DLL_PROCESS_ATTACH)return TRUE;
    DisableThreadLibraryCalls(module);
    GetModuleFileNameA(module,path,MAX_PATH);slash=strrchr(path,'\\');if(!slash)return FALSE;
    strcpy(slash+1,"HOTD2Bridge.log");logfile=fopen(path,"w");
    strcpy(slash+1,"HOTD2VR.ini");strcpy(ini_path,path);
    game_base=(BYTE *)GetModuleHandleA(NULL);
    mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(Vc2Frame),VC2_SHARE_NAME);
    if(!mapping || GetLastError()==ERROR_ALREADY_EXISTS){logline("bridge mapping unavailable or another session active");return FALSE;}
    frame=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Vc2Frame));
    if(!frame)return FALSE;
    memset(frame,0,sizeof(*frame));frame->magic=VC2_SHARE_MAGIC;frame->version=VC2_SHARE_VERSION;
    frame->game_pid=GetCurrentProcessId();
    frame->render_flags=GetPrivateProfileIntA("vr","native_lighting",1,ini_path)?0:HOTD_RENDER_UNLIT;
    if(!GetPrivateProfileIntA("vr","expand_meshes",1,ini_path))frame->render_flags|=HOTD_RENDER_NATIVE_MESH_CULLING;
    if(GetPrivateProfileIntA("vr","expand_world",0,ini_path))frame->render_flags|=HOTD_RENDER_EXPAND_WORLD;
    logline("native lighting: %s",frame->render_flags&HOTD_RENDER_UNLIT?"off":"on");
    frame->units_per_metre=(float)GetPrivateProfileIntA("bridge","units_per_metre",10,ini_path);
    if(frame->units_per_metre<1 || frame->units_per_metre>100000)frame->units_per_metre=10;
    /* Refuse input writes unless all signature sites and the PE image size match. */
    {
        IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER *)game_base;
        IMAGE_NT_HEADERS32 *nt=(IMAGE_NT_HEADERS32 *)(game_base+dos->e_lfanew);
        input_ready=nt->OptionalHeader.SizeOfImage>=0x5C9000 &&
            !memcmp(game_base+0xD1AD,xsig,4) && !memcmp(game_base+0xD1B7,ysig,4);
    }
    frame->input_supported=input_ready;
    logline("HOTD2 bridge pid=%lu input_profile=%d",(unsigned long)GetCurrentProcessId(),input_ready);
    if(!input_ready){logline("unsupported executable layout; injection refused");return FALSE;}
    cheats_supported=native_cheat_profile(game_base);
    logline("native cheat profile: %s",cheats_supported?"verified":"unsupported; native flags unchanged");
    continues_supported=native_continues_profile(game_base);
    logline("native continues profile: %s",continues_supported?"verified":"unsupported; free play unchanged");
    invincibility_ready=install_invincibility();
    logline("native invincibility hooks: %s",invincibility_ready?"ready":"unavailable; damage unchanged");
    logline("native capture visibility hooks: %s",install_capture_visibility()?"ready":"unavailable; native culling retained");
    if(input_ready) {
        static const BYTE draw_signature[]={0x8B,0x54,0x24,0x04,0xA1,0x74,0xEB,0x7D,0x00};
        static const BYTE input_signature[]={0x83,0xEC,0x20,0x53,0x55,0x56,0x57,0x33,0xFF};
        static const BYTE model_signature[]={0x83,0xEC,0x1C,0xA1,0x74,0xEB,0x7D,0x00,0x53};
        if(!detour(0xCBB0,input_signature,sizeof(input_signature),native_input,(void **)&native_input_original) ||
           !detour(0xA7EF0,model_signature,sizeof(model_signature),native_model,(void **)&native_model_original) ||
           !detour(0xA8440,model_signature,sizeof(model_signature),native_model_alt,(void **)&native_model_alt_original) ||
           !detour(0xA59C0,draw_signature,sizeof(draw_signature),native_strip,(void **)&native_strip_original) ||
           !detour(0xA5A00,draw_signature,sizeof(draw_signature),native_tl,(void **)&native_tl_original) ||
           !detour(0xA5A40,draw_signature,sizeof(draw_signature),native_list,(void **)&native_list_original)) {
            logline("native submission/input hook failed");return FALSE;
        }
        logline("native polygon submission hooks ready");
        void **slot=(void **)(game_base+0x172304);
        if(*slot==game_base+0xBA3FF)
            logline("native frame presentation hook: %d",replace_pointer(slot,native_present,(void **)&original_present));
        else {logline("unsupported frame presentation layout");return FALSE;}
    }
    if(input_ready)logline("native device-selection hook: %d",
        hook_import("USER32.dll","MessageBoxA",device_message_box,(void **)&original_message_box));
    if(GetPrivateProfileIntA("bridge","windowed",1,ini_path)) {
        static const BYTE mode_signature[]={0x6A,0x0B,0xA3,0x28,0xE6,0x7D,0x00};
        const BYTE windowed_mode=7; /* Native table: 640x480, desktop-compatible 32-bit, windowed. */
        if(input_ready && !memcmp(game_base+0x9E461,mode_signature,sizeof(mode_signature)))
            logline("native windowed mode: %s",write_code(game_base+0x9E462,&windowed_mode,1)?"selected":"write failed");
        else logline("native windowed mode: unsupported executable; unchanged");
    }
    if(input_ready && GetPrivateProfileIntA("bridge","background",1,ini_path)) {
        static const BYTE activate_signature[]={0x83,0xFB,0x01,0x74,0x1C,0x83,0xFB,0x02};
        const BYTE jump=0xEB;
        if(!memcmp(game_base+0xA66E4,activate_signature,sizeof(activate_signature)))
            logline("background rendering: %s",write_code(game_base+0xA66E7,&jump,1)?"enabled":"write failed");
        else logline("background rendering: unsupported executable; unchanged");
    }
    logline("DirectInput import hook: %d",hook_import("DINPUT.dll","DirectInputCreateEx",input_create,(void **)&original_input_create));
    if(!hook_import("DDRAW.dll","DirectDrawCreateEx",ddraw_create,(void **)&original_ddraw_create)) {
        logline("DirectDraw import hook failed");return FALSE;
    }
    logline("DirectDraw import hooked");
    return TRUE;
}
