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

static FILE *logfile;
static Vc2Frame *frame;
static HANDLE mapping;
static HWND game_window;
static unsigned count, calls, dropped, frame_no, atlas_dirty;
static Vc2Vertex vertices[VC2_MAX_VERTS];
static HotdLens lens;
static unsigned lens_valid;
static D3DMATRIX world, view, projection;
static D3DVIEWPORT7 viewport;
static DWORD fvf_logged[32], fvf_count;
static BYTE *game_base;
static int input_ready;
static DWORD aim_seq, aim_tick;
static unsigned buttons[2];
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

static int relax_mesh_culling(BYTE *at)
{
    static const BYTE signature[]={0xF7,0x44,0x24,0x18,0x00,0xF0,0xFF,0x00};
    const BYTE side_planes=0;
    if(memcmp(at,signature,sizeof(signature)))return 0;
    /* Mdl_Draw: retain near/far/custom planes; submit meshes outside the flat screen edges. */
    return write_code(at+5,&side_planes,1);
}

static int active_input(void)
{
    DWORD seq;
    if (!frame || !input_ready) return 0;
    seq = frame->aim_seq;
    if (seq != aim_seq) { aim_seq = seq; aim_tick = GetTickCount(); }
    return aim_tick && GetTickCount()-aim_tick < 500;
}

static void update_input(void)
{
    int active = active_input();
    buttons[0] = active ? frame->aim_buttons : 0;
    buttons[1] = active && frame->akimbo ? frame->aim2_buttons : 0;
    if (active) {
        unsigned p;
        for (p=0;p<(frame->akimbo ? 2u:1u);p++) {
            float sx = p ? frame->aim2_x : frame->aim_x;
            float sy = p ? frame->aim2_y : frame->aim_y;
            volatile short *axes = (volatile short *)(game_base+0x5C8FC4+p*0x28);
            if (!isfinite(sx) || !isfinite(sy)) continue;
            if(sx<0) sx=0;
            if(sx>640) sx=640;
            if(sy<0) sy=0;
            if(sy>480) sy=480;
            axes[0] = (short)lroundf(sx-320);
            axes[1] = (short)lroundf(240-sy);
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
    update_input();
    for(p=0;p<2;p++) {
        if(active_input() && (!p || frame->akimbo)) {
            HotdGunInput *gun=(HotdGunInput *)(game_base+0x5C8FC0+p*0x28);
            unsigned cancel_pressed=(buttons[p]&VC2_BTN_BACK)&~previous[p];
            unsigned action=hotd_apply_gun_buttons(gun,buttons[p],previous+p);
            previous[p]|=buttons[p]&VC2_BTN_BACK;
            if(cancel_pressed)action=VC2_BTN_RELOAD;
            if(action==VC2_BTN_RELOAD) {
                int accepted=native_reload(p,gun);
                logline("native reload P%u: queued=%d ammo_before=%d phase=%lu status=%d control=%u",p+1,accepted,
                    *(short *)(game_base+0x5A5C7C+p*0x130),*(DWORD *)(game_base+0x5C8E98),
                    *(short *)(game_base+0x5A5C62+p*0x130),game_base[0x5A5D85+p*0x130]);
            }
            if(action==VC2_BTN_FIRE)
                logline("native trigger P%u: valid=%d fire=%d ammo=%d",p+1,gun->valid,gun->fire,
                    *(short *)(game_base+0x5A5C7C+p*0x130));
        } else previous[p]=0;
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
        if(*(DWORD *)(game_base+0x5C8E98)==4) {
            unsigned nav=(buttons[0]|buttons[1])&(VC2_BTN_UP|VC2_BTN_DOWN);
            if(nav==VC2_BTN_UP)keys[DIK_UP]=0x80;
            if(nav==VC2_BTN_DOWN)keys[DIK_DOWN]=0x80;
        }
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

typedef struct { DWORD colour,style; } DrawStyle;
static DrawStyle read_draw_style(IDirect3DDevice7 *device,DWORD fvf)
{
    DWORD blend=0,src=2,dst=1,zwrite=1,test=0,func=8,ref=0,alphaop=2,alphaarg=2;
    DWORD lighting=0,colorvertex=0,matsrc=0,cull=D3DCULL_NONE;
    D3DMATERIAL7 material;
    DrawStyle style={0xFFFFFFFF,0};
#define RS(name,out) IDirect3DDevice7_GetRenderState(device,D3DRENDERSTATE_##name,&out)
    RS(ALPHABLENDENABLE,blend);RS(SRCBLEND,src);RS(DESTBLEND,dst);RS(ZWRITEENABLE,zwrite);
    RS(ALPHATESTENABLE,test);RS(ALPHAFUNC,func);RS(ALPHAREF,ref);
    RS(LIGHTING,lighting);RS(COLORVERTEX,colorvertex);RS(DIFFUSEMATERIALSOURCE,matsrc);
    RS(CULLMODE,cull);
#undef RS
    IDirect3DDevice7_GetTextureStageState(device,0,D3DTSS_ALPHAOP,&alphaop);
    IDirect3DDevice7_GetTextureStageState(device,0,D3DTSS_ALPHAARG1,&alphaarg);
    if((fvf&D3DFVF_POSITION_MASK)!=D3DFVF_XYZRHW && lighting &&
       (!colorvertex || matsrc==D3DMCS_MATERIAL || !(fvf&D3DFVF_DIFFUSE)) &&
       SUCCEEDED(IDirect3DDevice7_GetMaterial(device,&material)))
        style.colour=hotd_rgba(material.diffuse.r,material.diffuse.g,material.diffuse.b,material.diffuse.a);
    style.style=hotd_style(blend,src,dst,zwrite,alphaop==D3DTOP_MODULATE ||
        (alphaop==D3DTOP_SELECTARG1 && (alphaarg&D3DTA_SELECTMASK)==D3DTA_DIFFUSE),test,func,ref) |
        ((cull&3u)<<HOTD_CULL_SHIFT);
    return style;
}

static int convert_vertex(const BYTE *v,DWORD fvf,Texture *tex,DrawStyle style,Vc2Vertex *out)
{
    unsigned pos=fvf&D3DFVF_POSITION_MASK,offset;
    DWORD colour=0xFFFFFFFF;
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
    if(fvf&D3DFVF_SPECULAR)offset+=4;
    out->colour=(fvf&D3DFVF_DIFFUSE) ? (colour&0xFF00FF00u)|((colour&255)<<16)|((colour>>16)&255):style.colour;
    out->style=style.style;
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
    if((fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW) {
        const float *v=data;
        if(fabsf(v[2]-0.1f)<0.00001f && fabsf(v[3]-0.5f)<0.00001f)return;
    }
    IDirect3DDevice7_GetRenderState(device,D3DRENDERSTATE_ZENABLE,&z_enable);
    /* Native 2D remains on the captured panel; never inflate screen-space HUD into the world. */
    if(!z_enable)return;
    texture=capture_texture(device);
    trace_draw_state(device,fvf,texture,data);
    style=read_draw_style(device,fvf);
    for(i=0;i+2<total;i+=(type==D3DPT_TRIANGLELIST?3:1)) {
        unsigned q[3]={i,i+1,i+2},k;
        Vc2Vertex tri[3];
        if(type==D3DPT_TRIANGLEFAN){q[0]=0;q[1]=i+1;q[2]=i+2;}
        else if(type==D3DPT_TRIANGLESTRIP && (i&1)){q[0]=i+1;q[1]=i;}
        else if(type!=D3DPT_TRIANGLELIST && type!=D3DPT_TRIANGLESTRIP)break;
        if(count+3>VC2_MAX_VERTS){dropped++;break;}
        for(k=0;k<3;k++) {
            unsigned idx=indices?indices[q[k]]:q[k];
            if(idx>=n || !convert_vertex((const BYTE *)data+idx*stride,fvf,texture,style,tri+k))break;
        }
        if(k!=3){dropped++;continue;}
        memcpy(vertices+count,tri,sizeof(tri));count+=3;
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

static int detour9(DWORD rva,const BYTE signature[9],void *hook,void **original)
{
    BYTE *at=game_base+rva,*trampoline,patch[9];DWORD jump;
    if(memcmp(at,signature,9))return 0;
    trampoline=VirtualAlloc(NULL,14,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    if(!trampoline)return 0;
    memcpy(trampoline,at,9);trampoline[9]=0xE9;
    jump=(DWORD)((at+9)-(trampoline+14));memcpy(trampoline+10,&jump,4);
    memset(patch,0x90,sizeof(patch));patch[0]=0xE9;
    jump=(DWORD)((BYTE *)hook-(at+5));memcpy(patch+1,&jump,4);
    *original=trampoline;
    if(!write_code(at,patch,sizeof(patch))){VirtualFree(trampoline,0,MEM_RELEASE);*original=NULL;return 0;}
    return 1;
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
static void publish_frame(void)
{
    if(!frame || !active_device)return;
    frame_no++;
    if(!IsWindow(game_window))EnumWindows(find_game_window,0);
    capture_screen(active_device);
    if(!atlas_dirty)frame->seq++;
    MemoryBarrier();
    memcpy(frame->verts,vertices,count*sizeof(vertices[0]));
    frame->vertex_count=count;frame->frame=frame_no;
    frame->reserved[0]=count;frame->reserved[1]=0;frame->reserved[2]=count+1;
    frame->lens=lens;frame->lens_valid=lens_valid;
    frame->draw_calls=calls;frame->dropped_triangles=dropped;frame->texture_count=texture_count;
    frame->heartbeat=GetTickCount();frame->game_hwnd=(DWORD)(ULONG_PTR)game_window;
    if(atlas_dirty){frame->atlas_frame++;MemoryBarrier();frame->atlas_seq++;atlas_dirty=0;}
    MemoryBarrier();frame->seq++;
    if(frame_no==1 || frame_no%300==0 || texture_failures)logline("frame=%u draws=%u vertices=%u dropped=%u textures=%u evictions=%u texture_failures=%u lens=%u screen=%u",
        frame_no,calls,count,dropped,texture_count,texture_evictions,texture_failures,lens_valid,frame->screen_ok);
    texture_failures=0;
    count=calls=dropped=0;
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
    logline("native mesh screen-edge culling: %s",relax_mesh_culling(game_base+0xA7F81)?"relaxed":"unsupported; unchanged");
    if(input_ready) {
        static const BYTE draw_signature[]={0x8B,0x54,0x24,0x04,0xA1,0x74,0xEB,0x7D,0x00};
        static const BYTE input_signature[]={0x83,0xEC,0x20,0x53,0x55,0x56,0x57,0x33,0xFF};
        if(!detour9(0xCBB0,input_signature,native_input,(void **)&native_input_original) ||
           !detour9(0xA59C0,draw_signature,native_strip,(void **)&native_strip_original) ||
           !detour9(0xA5A00,draw_signature,native_tl,(void **)&native_tl_original) ||
           !detour9(0xA5A40,draw_signature,native_list,(void **)&native_list_original)) {
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
