/* Exercise the actual bridge with fake COM surfaces; no game or renderer starts. */
#include <assert.h>
#include "../src/bridge.c"

typedef struct { IDirectDrawSurface7 iface; unsigned refs; } FakeSurface;
static FakeSurface surfaces[1600],*selected;
static DWORD pixels[128*128];
static unsigned prompt_fallbacks;
static int WINAPI fallback_prompt(HWND hwnd,LPCSTR text,LPCSTR caption,UINT flags)
{(void)hwnd;(void)text;(void)caption;(void)flags;prompt_fallbacks++;return IDNO;}
static ULONG WINAPI release_surface(IDirectDrawSurface7 *self)
{
    FakeSurface *surface=(FakeSurface *)self;
    assert(surface->refs>1);
    return --surface->refs;
}
static HRESULT WINAPI lock_surface(IDirectDrawSurface7 *self,RECT *rect,DDSURFACEDESC2 *desc,DWORD flags,HANDLE event)
{
    (void)self;(void)rect;(void)flags;(void)event;
    desc->dwWidth=desc->dwHeight=128;desc->lPitch=128*4;desc->lpSurface=pixels;
    desc->ddpfPixelFormat.dwFlags=DDPF_RGB;desc->ddpfPixelFormat.dwRGBBitCount=32;
    desc->ddpfPixelFormat.dwRBitMask=0xFF0000;desc->ddpfPixelFormat.dwGBitMask=0xFF00;desc->ddpfPixelFormat.dwBBitMask=255;
    return DD_OK;
}
static HRESULT WINAPI unlock_surface(IDirectDrawSurface7 *self,RECT *rect)
{(void)self;(void)rect;return DD_OK;}
static HRESULT WINAPI no_color_key(IDirectDrawSurface7 *self,DWORD flags,DDCOLORKEY *key)
{(void)self;(void)flags;(void)key;return DDERR_NOCOLORKEY;}
static HRESULT WINAPI get_texture(IDirect3DDevice7 *self,DWORD stage,IDirectDrawSurface7 **out)
{(void)self;(void)stage;selected->refs++;*out=&selected->iface;return DD_OK;}
static HRESULT WINAPI no_render_target(IDirect3DDevice7 *self,IDirectDrawSurface7 **out)
{(void)self;(void)out;return E_FAIL;}
static HRESULT WINAPI get_render_state(IDirect3DDevice7 *self,D3DRENDERSTATETYPE state,DWORD *out)
{
    (void)self;*out=0;
    switch(state) {
    case D3DRENDERSTATE_ALPHABLENDENABLE:case D3DRENDERSTATE_ZWRITEENABLE:
    case D3DRENDERSTATE_ALPHATESTENABLE:case D3DRENDERSTATE_LIGHTING:*out=1;break;
    case D3DRENDERSTATE_SRCBLEND:case D3DRENDERSTATE_ALPHAFUNC:*out=5;break;
    case D3DRENDERSTATE_DESTBLEND:*out=6;break;
    case D3DRENDERSTATE_ALPHAREF:*out=127;break;
    default:break;
    }
    return DD_OK;
}
static HRESULT WINAPI get_stage_state(IDirect3DDevice7 *self,DWORD stage,D3DTEXTURESTAGESTATETYPE state,DWORD *out)
{(void)self;(void)stage;*out=state==D3DTSS_ALPHAOP?D3DTOP_MODULATE:D3DTA_TEXTURE;return DD_OK;}
static HRESULT WINAPI get_material(IDirect3DDevice7 *self,D3DMATERIAL7 *out)
{(void)self;memset(out,0,sizeof(*out));out->diffuse.a=.5f;return DD_OK;}

int main(void)
{
    IDirectDrawSurface7Vtbl surface_vt={.Release=release_surface,.Lock=lock_surface,
        .Unlock=unlock_surface,.GetColorKey=no_color_key};
    IDirect3DDevice7Vtbl device_vt={.GetTexture=get_texture,.GetRenderTarget=no_render_target,
        .GetRenderState=get_render_state,.GetTextureStageState=get_stage_state,.GetMaterial=get_material};
    IDirect3DDevice7 device={&device_vt};
    unsigned i,j,held=0;
    original_message_box=fallback_prompt;
    assert(device_message_box(NULL,"Are you sure you want to quit?","House of the Dead2",0x24)==IDYES);
    assert(!prompt_fallbacks);
    assert(device_message_box(NULL,"NVIDIA device will be used. \n","Attention",0x34)==IDYES);
    assert(!prompt_fallbacks);
    assert(device_message_box(NULL,"Graphics error","House of the Dead2",0x24)==IDNO);
    assert(device_message_box(NULL,"Are you sure you want to quit?","Other game",0x24)==IDNO);
    assert(device_message_box(NULL,"Are you sure you want to quit?","House of the Dead2",0x20)==IDNO);
    assert(device_message_box(NULL,NULL,"House of the Dead2",0x24)==IDNO);
    assert(device_message_box(NULL,"Are you sure you want to quit?",NULL,0x24)==IDNO);
    assert(prompt_fallbacks==5);
    frame=calloc(1,sizeof(*frame));assert(frame);
    game_window=GetDesktopWindow();active_device=&device;
    DrawStyle style=read_draw_style(&device,0x112);
    Vc2Vertex vertex;
    float source[]={0,0,1,0,0,1,0,0};
    world._11=world._22=world._33=world._44=1;
    view._11=view._22=view._33=view._44=1;
    assert(convert_vertex((const BYTE *)source,0x112,NULL,style,&vertex));
    assert(vertex.colour==0x80000000u && (vertex.style&255)==0x65);
    assert((vertex.style&HOTD_DIFFUSE_ALPHA) && (vertex.style&HOTD_ALPHA_TEST));
    for(i=0;i<128*128;i++)pixels[i]=0x123456;
    for(i=0;i<1600;i++){surfaces[i].iface.lpVtbl=&surface_vt;surfaces[i].refs=1;}
    for(i=0;i<20;i++) {
        for(j=0;j<80;j++) {
            selected=surfaces+i*80+j;
            Texture *t=capture_texture(&device);
            assert(t && t->last_used==frame_no && selected->refs==2);
            assert(frame->atlas[t->y*VC2_ATLAS_W+t->x]==0xFF123456);
        }
        for(j=0;j<80;j++)assert(surfaces[i*80+j].refs==2); /* Current frame cannot lose a tile. */
        assert((frame->seq&1) && (frame->atlas_seq&1));
        if(i==0)assert(!evict_texture()); /* All cached tiles still in use. */
        publish_frame();
        assert(!(frame->seq&1) && !(frame->atlas_seq&1));
    }
    assert(texture_evictions>0 && !texture_failures && texture_count<=784);
    for(i=0;i<1600;i++)held+=surfaces[i].refs==2;
    assert(held==texture_count);
    while(evict_texture());
    for(i=0;i<1600;i++)assert(surfaces[i].refs==1);
    assert(!texture_count);
    free(frame);
    puts("Texture cache: 1600 surfaces, eviction, pinned live tiles and COM release passed.");
    return 0;
}
