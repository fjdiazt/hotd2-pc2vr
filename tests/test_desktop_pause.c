/* Real offscreen Direct3D 7 replay, including the bridge's native EndScene hook. */
#include <assert.h>
#include "../src/bridge.c"

static IDirectDraw7 *ddraw;
static IDirectDrawSurface7 *target,*zbuffer,*test_texture;
static IDirect3D7 *d3d;
static IDirect3DDevice7 *device;
static D3DMATRIX identity;
static DDPIXELFORMAT zformat;
static int missing_texture;
static HRESULT CALLBACK choose_z(DDPIXELFORMAT *format,void *unused)
{
    (void)unused;
    if(!zformat.dwSize && format->dwFlags&DDPF_ZBUFFER)zformat=*format;
    return D3DENUMRET_OK;
}
static IDirectDrawSurface7 *lookup(const Vc2Vertex *v)
{(void)v;return missing_texture?NULL:test_texture;}
static DWORD sample(unsigned x,unsigned y)
{
    DDSURFACEDESC2 desc={.dwSize=sizeof(desc)};
    assert(SUCCEEDED(IDirectDrawSurface7_Lock(target,NULL,&desc,DDLOCK_WAIT|DDLOCK_READONLY,NULL)));
    DWORD pixel=0;
    memcpy(&pixel,(BYTE *)desc.lpSurface+y*desc.lPitch+x*(desc.ddpfPixelFormat.dwRGBBitCount/8),desc.ddpfPixelFormat.dwRGBBitCount/8);
    pixel=pixel_bgra(pixel,&desc.ddpfPixelFormat,NULL)&0xFFFFFFu;
    assert(SUCCEEDED(IDirectDrawSurface7_Unlock(target,NULL)));
    return pixel;
}
static void triangle(unsigned at,DWORD colour,float depth)
{
    for(unsigned i=0;i<3;i++) {
        Vc2Vertex *v=frame->verts+at+i;memset(v,0,sizeof(*v));
        v->x=i==1?3:-1;v->y=i==2?3:-1;v->z=-depth;
        v->colour=colour;v->fog=1;
    }
    frame->vertex_count=at+3;
}
static void render(void)
{
    D3DMATRIX before,after;
    D3DVIEWPORT7 native_viewport={.dwX=3,.dwY=4,.dwWidth=20,.dwHeight=24,.dvMaxZ=1},after_viewport;
    DWORD value;
    before=identity;before._41=17;
    assert(SUCCEEDED(IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_WORLD,&before)));
    assert(SUCCEEDED(IDirect3DDevice7_SetViewport(device,&native_viewport)));
    assert(SUCCEEDED(IDirect3DDevice7_SetRenderState(device,D3DRENDERSTATE_ZWRITEENABLE,FALSE)));
    assert(SUCCEEDED(IDirect3DDevice7_SetTextureStageState(device,0,D3DTSS_ADDRESSU,D3DTADDRESS_MIRROR)));
    assert(SUCCEEDED(IDirect3DDevice7_SetTexture(device,0,test_texture)));
    assert(SUCCEEDED(IDirect3DDevice7_BeginScene(device)));
    HRESULT hr=hotd_pause_render(device,frame,&pause_projection,&pause_viewport,pause_raster,lookup);
    assert(missing_texture?FAILED(hr):SUCCEEDED(hr));
    assert(SUCCEEDED(IDirect3DDevice7_EndScene(device)));
    assert(SUCCEEDED(IDirect3DDevice7_GetTransform(device,D3DTRANSFORMSTATE_WORLD,&after)));
    assert(!memcmp(&before,&after,sizeof(before)));
    assert(SUCCEEDED(IDirect3DDevice7_GetViewport(device,&after_viewport)) &&
        !memcmp(&native_viewport,&after_viewport,sizeof(native_viewport)));
    assert(SUCCEEDED(IDirect3DDevice7_GetRenderState(device,D3DRENDERSTATE_ZWRITEENABLE,&value)) && !value);
    assert(SUCCEEDED(IDirect3DDevice7_GetTextureStageState(device,0,D3DTSS_ADDRESSU,&value)) && value==D3DTADDRESS_MIRROR);
    IDirectDrawSurface7 *surface=NULL;
    assert(SUCCEEDED(IDirect3DDevice7_GetTexture(device,0,&surface)) && surface==test_texture);
    IDirectDrawSurface7_Release(surface);
}
static unsigned presented;
static HRESULT WINAPI present_probe(void *self,DWORD flags)
{
    (void)self;assert(flags==17);presented++;
    DWORD pixel=sample(32,32);
    if(hotd_native_menu_visible(frame)) {
        assert(abs((int)((pixel>>16)&255)-128)<=1 && !(pixel&0xFF00) && abs((int)(pixel&255)-127)<=1);
        assert(frame->screen_w==64 && frame->screen_h==64);
        assert((frame->screen[32*64+32]&0xFFFFFF)==0x0000FF); /* Captured blue stays menu-free BGRA. */
    } else assert(pixel==0x0000FF);
    return 19;
}
static void check_native_menu(void)
{
    D3DVIEWPORT7 before={.dwX=3,.dwY=4,.dwWidth=20,.dwHeight=24,.dvMaxZ=1},after;
    DWORD value;
    active_device=device;original_begin=device->lpVtbl->BeginScene;original_present=present_probe;
    frame->menu_open=1;frame->menu_tick=GetTickCount();frame->menu_seq=2;
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)frame->menu_pixels[y][x]=0x800000FF;
    assert(SUCCEEDED(IDirect3DDevice7_SetViewport(device,&before)));
    assert(SUCCEEDED(IDirect3DDevice7_SetRenderState(device,D3DRENDERSTATE_ALPHABLENDENABLE,FALSE)));
    assert(SUCCEEDED(IDirect3DDevice7_SetTextureStageState(device,0,D3DTSS_ADDRESSU,D3DTADDRESS_MIRROR)));
    assert(SUCCEEDED(IDirect3DDevice7_SetTexture(device,0,test_texture)));
    count=hud_count=0; /* Overlay must never add captured geometry. */
    assert(SUCCEEDED(IDirect3DDevice7_Clear(device,0,NULL,D3DCLEAR_TARGET,0x0000FF,1,0)));
    assert(native_present(NULL,17)==19 && presented==1 && !count && !hud_count && !frame->vertex_count);
    assert(native_menu.sequence==2 && native_menu.texture);
    assert(SUCCEEDED(IDirect3DDevice7_GetViewport(device,&after)) && !memcmp(&before,&after,sizeof(before)));
    assert(SUCCEEDED(IDirect3DDevice7_GetRenderState(device,D3DRENDERSTATE_ALPHABLENDENABLE,&value)) && !value);
    assert(SUCCEEDED(IDirect3DDevice7_GetTextureStageState(device,0,D3DTSS_ADDRESSU,&value)) && value==D3DTADDRESS_MIRROR);
    IDirectDrawSurface7 *bound=NULL;
    assert(SUCCEEDED(IDirect3DDevice7_GetTexture(device,0,&bound)) && bound==test_texture);IDirectDrawSurface7_Release(bound);
    D3DVIEWPORT7 full={.dwWidth=64,.dwHeight=64,.dvMaxZ=1};
    assert(SUCCEEDED(IDirect3DDevice7_SetViewport(device,&full)));
    assert(SUCCEEDED(IDirect3DDevice7_Clear(device,0,NULL,D3DCLEAR_TARGET,0x0000FF,1,0)));
    assert(SUCCEEDED(IDirect3DDevice7_SetViewport(device,&before)));
    *(short *)(game_base+0x5A1A00)=1;
    unsigned captured=frame->screen_frame;
    assert(native_present(NULL,17)==19 && frame->screen_frame==captured); /* Pause still draws menu above retained capture. */
    *(short *)(game_base+0x5A1A00)=0;
    /* Unchanged pixels retain the uploaded texture; torn writes never replace it. */
    assert(SUCCEEDED(IDirect3DDevice7_BeginScene(device)));
    frame->menu_seq=3;assert(hotd_native_menu_draw(device,frame,&native_menu)==S_FALSE && native_menu.sequence==2);
    frame->menu_seq=4;frame->menu_pixels[0][0]=0xFF00FF00;
    assert(SUCCEEDED(hotd_native_menu_draw(device,frame,&native_menu)) && native_menu.sequence==4);
    assert(SUCCEEDED(IDirect3DDevice7_EndScene(device)));
    for(unsigned mode=0;mode<2;mode++) {
        frame->menu_open=mode?1:0;frame->menu_tick=GetTickCount()-501;
        assert(SUCCEEDED(IDirect3DDevice7_SetViewport(device,&full)));
        assert(SUCCEEDED(IDirect3DDevice7_Clear(device,0,NULL,D3DCLEAR_TARGET,0x0000FF,1,0)));
        assert(SUCCEEDED(IDirect3DDevice7_SetViewport(device,&before)));
        assert(native_present(NULL,17)==19);
    }
    hotd_native_menu_release(&native_menu);assert(!native_menu.texture && !native_menu.device);
}
int main(void)
{
    HWND window=CreateWindowA("STATIC","HOTD2 desktop pause test",WS_OVERLAPPEDWINDOW,0,0,80,80,NULL,NULL,NULL,NULL);
    assert(window);
    assert(SUCCEEDED(DirectDrawCreateEx(NULL,(void **)&ddraw,&IID_IDirectDraw7,NULL)));
    assert(SUCCEEDED(IDirectDraw7_SetCooperativeLevel(ddraw,window,DDSCL_NORMAL)));
    assert(SUCCEEDED(IDirectDraw7_QueryInterface(ddraw,&IID_IDirect3D7,(void **)&d3d)));
    DDSURFACEDESC2 desc={.dwSize=sizeof(desc),.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT,
        .dwWidth=64,.dwHeight=64,.ddsCaps={.dwCaps=DDSCAPS_OFFSCREENPLAIN|DDSCAPS_3DDEVICE}};
    assert(SUCCEEDED(IDirectDraw7_CreateSurface(ddraw,&desc,&target,NULL)));
    assert(SUCCEEDED(IDirect3D7_EnumZBufferFormats(d3d,&IID_IDirect3DHALDevice,choose_z,NULL)) && zformat.dwSize);
    desc.ddsCaps.dwCaps=DDSCAPS_ZBUFFER;desc.dwFlags|=DDSD_PIXELFORMAT;desc.ddpfPixelFormat=zformat;
    assert(SUCCEEDED(IDirectDraw7_CreateSurface(ddraw,&desc,&zbuffer,NULL)));
    assert(SUCCEEDED(IDirectDrawSurface7_AddAttachedSurface(target,zbuffer)));
    assert(SUCCEEDED(IDirect3D7_CreateDevice(d3d,&IID_IDirect3DHALDevice,target,&device)));
    desc.ddsCaps.dwCaps=DDSCAPS_TEXTURE;desc.dwWidth=desc.dwHeight=2;
    desc.ddpfPixelFormat=(DDPIXELFORMAT){.dwSize=sizeof(DDPIXELFORMAT),.dwFlags=DDPF_RGB|DDPF_ALPHAPIXELS,
        .dwRGBBitCount=32,.dwRBitMask=0xFF0000,.dwGBitMask=0xFF00,.dwBBitMask=255,.dwRGBAlphaBitMask=0xFF000000};
    assert(SUCCEEDED(IDirectDraw7_CreateSurface(ddraw,&desc,&test_texture,NULL)));
    DDSURFACEDESC2 locked={.dwSize=sizeof(locked)};
    assert(SUCCEEDED(IDirectDrawSurface7_Lock(test_texture,NULL,&locked,DDLOCK_WAIT,NULL)));
    for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)
        *(DWORD *)((BYTE *)locked.lpSurface+y*locked.lPitch+x*4)=x?0xFF00FF00:0xFFFF0000;
    assert(SUCCEEDED(IDirectDrawSurface7_Unlock(test_texture,NULL)));
    frame=calloc(1,sizeof(*frame));assert(frame);frame->units_per_metre=10;
    for(unsigned i=0;i<VC2_MAX_VERTS/3;i++)pause_raster[i]=(HotdPauseRaster){
        .zfunc=D3DCMP_LESSEQUAL,.z_enable=D3DZB_TRUE,.mag_filter=D3DTFG_LINEAR,.min_filter=D3DTFN_LINEAR,.mip_filter=D3DTFP_NONE};
    identity._11=identity._22=identity._33=identity._44=1;
    pause_projection=identity;pause_viewport=(D3DVIEWPORT7){.dwWidth=64,.dwHeight=64,.dvMaxZ=1};
    triangle(0,0xFF0000FF,.5f);render();assert(sample(32,32)==0xFF0000);
    /* Blending and alpha rejection preserve the previously rendered world. */
    triangle(3,0x8000FF00,.4f);
    for(unsigned i=3;i<6;i++)frame->verts[i].style=hotd_style(1,5,6,0,1,0,8,0);
    render();DWORD blended=sample(32,32);
    assert(abs((int)((blended>>16)&255)-127)<=1 && abs((int)((blended>>8)&255)-128)<=1 && !(blended&255));
    for(unsigned i=3;i<6;i++)frame->verts[i].style=hotd_style(1,5,6,0,1,1,5,200);
    render();assert(sample(32,32)==0xFF0000);
    /* Depth testing retains the nearer red triangle. */
    triangle(3,0xFF00FF00,.8f);render();assert(sample(32,32)==0xFF0000);
    /* Materials can deliberately compare greater/equal instead of less/equal. */
    pause_raster[1].zfunc=D3DCMP_GREATER;render();assert(sample(32,32)==0x00FF00);
    pause_raster[1].zfunc=D3DCMP_EQUAL;render();assert(sample(32,32)==0xFF0000);
    pause_raster[1].zfunc=D3DCMP_LESSEQUAL;
    /* A retained HUD triangle uses its original panel coordinates over the world. */
    triangle(3,0xFFFF0000,20);
    for(unsigned i=3;i<6;i++){frame->verts[i].x*=10;frame->verts[i].y*=7.5f;frame->verts[i].depth=1;}
    render();assert(sample(32,32)==0x0000FF);
    triangle(0,0xFFFFFFFF,.5f);
    for(unsigned i=0;i<3;i++) {
        frame->verts[i].atlas_w=frame->verts[i].atlas_h=1;
        frame->verts[i].u=2.25f;frame->verts[i].v=.25f;
    }
    render();assert(sample(32,32)==0xFF0000); /* Native wrap, rather than clamping UVs into an atlas. */
    for(unsigned i=0;i<3;i++)frame->verts[i].address=hotd_address(D3DTADDRESS_CLAMP,D3DTADDRESS_CLAMP);
    render();assert(sample(32,32)==0x00FF00);
    /* A failed replay still restores the caller's device state and texture binding. */
    missing_texture=1;render();missing_texture=0;
    triangle(0,0xFF000000,.5f);
    for(unsigned i=0;i<3;i++){frame->verts[i].specular=0x000000FF;frame->verts[i].fog_colour=0x0100FF00;frame->verts[i].fog=.5f;}
    render();DWORD fogged=sample(32,32);
    assert(abs((int)((fogged>>16)&255)-128)<=1 && abs((int)((fogged>>8)&255)-128)<=1 && !(fogged&255));
    /* The hook replays only a native gameplay pause, and stops on resume. */
    assert(SUCCEEDED(IDirect3DDevice7_SetViewport(device,&pause_viewport)));
    game_base=calloc(1,0x600000);assert(game_base);input_ready=1;original_end=device->lpVtbl->EndScene;
    textures[0]=(Texture){.surface=test_texture};texture_count=1;
    triangle(0,0xFF0000FF,.5f);*(short *)(game_base+0x5A1A00)=1;
    for(unsigned ready=0;ready<2;ready++) {
        pause_scene_ready=ready;
        assert(SUCCEEDED(IDirect3DDevice7_Clear(device,0,NULL,D3DCLEAR_TARGET,0x0000FF,1,0)));
        assert(SUCCEEDED(IDirect3DDevice7_BeginScene(device)));assert(SUCCEEDED(end_scene(device)));
        assert(sample(32,32)==(ready?0xFF0000:0x0000FF));
    }
    *(short *)(game_base+0x5A1A00)=0;
    assert(SUCCEEDED(IDirect3DDevice7_Clear(device,0,NULL,D3DCLEAR_TARGET,0x0000FF,1,0)));
    assert(SUCCEEDED(IDirect3DDevice7_BeginScene(device)));assert(SUCCEEDED(end_scene(device)));
    assert(sample(32,32)==0x0000FF);
    check_native_menu();
    IDirect3DDevice7_Release(device);IDirect3D7_Release(d3d);
    IDirectDrawSurface7_Release(test_texture);IDirectDrawSurface7_Release(zbuffer);
    IDirectDrawSurface7_Release(target);IDirectDraw7_Release(ddraw);DestroyWindow(window);
    free(frame);free(game_base);
    puts("Desktop pause/menu: real D3D7 replay and alpha composition, restored state, pause, capture isolation and stale/torn menu checks passed.");
    return 0;
}
