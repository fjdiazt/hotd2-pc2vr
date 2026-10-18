/* Composite viewer menu pixels after capture; never submit them to scene capture. */
#ifndef HOTD_NATIVE_MENU_H
#define HOTD_NATIVE_MENU_H
typedef struct {
    IDirect3DDevice7 *device;
    IDirectDrawSurface7 *texture;
    DWORD sequence,pixels[480][640];
} HotdNativeMenu;

static int hotd_native_menu_visible(const Vc2Frame *source)
{
    return source && source->menu_open==1 && source->menu_tick &&
        (DWORD)(GetTickCount()-source->menu_tick)<500;
}
static void hotd_native_menu_release(HotdNativeMenu *menu)
{
    if(menu->texture)IDirectDrawSurface7_Release(menu->texture);
    menu->texture=NULL;menu->device=NULL;menu->sequence=0;
}
/* Caller owns BeginScene/EndScene; all modified device state is restored. */
static HRESULT hotd_native_menu_draw(IDirect3DDevice7 *device,const Vc2Frame *source,HotdNativeMenu *menu)
{
    typedef struct {float x,y,z,rhw;DWORD colour;float u,v;} Vertex;
    IDirectDrawSurface7 *target=NULL;
    DDSURFACEDESC2 desc={.dwSize=sizeof(desc)},locked={.dwSize=sizeof(locked)};
    D3DVIEWPORT7 saved_viewport,viewport;
    DWORD state=0,sequence;
    HRESULT hr,restored;
    if(!hotd_native_menu_visible(source))return S_FALSE;
    sequence=source->menu_seq;if(sequence&1)return S_FALSE;
    if(menu->device!=device)hotd_native_menu_release(menu);
    if(!menu->texture) {
        IUnknown *owner=NULL;IDirectDraw7 *draw=NULL;
        hr=IDirect3DDevice7_GetRenderTarget(device,&target);if(FAILED(hr))return hr;
        hr=IDirectDrawSurface7_GetDDInterface(target,(void **)&owner);
        IDirectDrawSurface7_Release(target);target=NULL;
        if(FAILED(hr))return hr;
        hr=owner->lpVtbl->QueryInterface(owner,&IID_IDirectDraw7,(void **)&draw);owner->lpVtbl->Release(owner);
        if(FAILED(hr))return hr;
        desc.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;
        desc.dwWidth=1024;desc.dwHeight=512;desc.ddsCaps.dwCaps=DDSCAPS_TEXTURE;
        desc.ddsCaps.dwCaps2=DDSCAPS2_TEXTUREMANAGE;
        desc.ddpfPixelFormat=(DDPIXELFORMAT){.dwSize=sizeof(DDPIXELFORMAT),.dwFlags=DDPF_RGB|DDPF_ALPHAPIXELS,
            .dwRGBBitCount=32,.dwRBitMask=0xFF0000,.dwGBitMask=0xFF00,.dwBBitMask=255,.dwRGBAlphaBitMask=0xFF000000};
        hr=IDirectDraw7_CreateSurface(draw,&desc,&menu->texture,NULL);IDirectDraw7_Release(draw);
        if(FAILED(hr))return hr;
        menu->device=device;menu->sequence=0;
    }
    if(IDirectDrawSurface7_IsLost(menu->texture)==DDERR_SURFACELOST) {
        hr=IDirectDrawSurface7_Restore(menu->texture);if(FAILED(hr))return hr;
        menu->sequence=0;
    }
    if(!menu->sequence || menu->sequence!=sequence) {
        MemoryBarrier();memcpy(menu->pixels,source->menu_pixels,sizeof(menu->pixels));MemoryBarrier();
        if(sequence!=source->menu_seq || !hotd_native_menu_visible(source))return S_FALSE;
        hr=IDirectDrawSurface7_Lock(menu->texture,NULL,&locked,DDLOCK_WAIT,NULL);if(FAILED(hr))return hr;
        for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)
            ((DWORD *)((BYTE *)locked.lpSurface+y*locked.lPitch))[x]=hotd_pause_argb(menu->pixels[y][x]);
        hr=IDirectDrawSurface7_Unlock(menu->texture,NULL);if(FAILED(hr))return hr;
        menu->sequence=sequence;
    }
    hr=IDirect3DDevice7_GetRenderTarget(device,&target);if(FAILED(hr))return hr;
    desc.dwSize=sizeof(desc);hr=IDirectDrawSurface7_GetSurfaceDesc(target,&desc);IDirectDrawSurface7_Release(target);
    if(FAILED(hr))return hr;
    hr=IDirect3DDevice7_GetViewport(device,&saved_viewport);if(FAILED(hr))return hr;
    hr=IDirect3DDevice7_CreateStateBlock(device,D3DSBT_ALL,&state);if(FAILED(hr))return hr;
#define MENU_CALL(call) do {hr=(call);if(FAILED(hr))goto restore;} while(0)
#define MENU_RS(name,value) MENU_CALL(IDirect3DDevice7_SetRenderState(device,D3DRENDERSTATE_##name,value))
#define MENU_TS(stage,name,value) MENU_CALL(IDirect3DDevice7_SetTextureStageState(device,stage,D3DTSS_##name,value))
    viewport=(D3DVIEWPORT7){.dwWidth=desc.dwWidth,.dwHeight=desc.dwHeight,.dvMaxZ=1};
    MENU_CALL(IDirect3DDevice7_SetViewport(device,&viewport));
    MENU_RS(ZENABLE,D3DZB_FALSE);MENU_RS(ZWRITEENABLE,FALSE);MENU_RS(STENCILENABLE,FALSE);
    MENU_RS(ALPHATESTENABLE,FALSE);MENU_RS(ALPHABLENDENABLE,TRUE);
    MENU_RS(SRCBLEND,D3DBLEND_SRCALPHA);MENU_RS(DESTBLEND,D3DBLEND_INVSRCALPHA);
    MENU_RS(FOGENABLE,FALSE);MENU_RS(LIGHTING,FALSE);MENU_RS(SPECULARENABLE,FALSE);
    MENU_RS(CULLMODE,D3DCULL_NONE);MENU_RS(FILLMODE,D3DFILL_SOLID);MENU_RS(COLORKEYENABLE,FALSE);
    MENU_RS(CLIPPLANEENABLE,0);MENU_RS(WRAP0,0);
    MENU_TS(0,COLOROP,D3DTOP_SELECTARG1);MENU_TS(0,COLORARG1,D3DTA_TEXTURE);
    MENU_TS(0,ALPHAOP,D3DTOP_SELECTARG1);MENU_TS(0,ALPHAARG1,D3DTA_TEXTURE);
    MENU_TS(0,TEXCOORDINDEX,0);MENU_TS(0,TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
    MENU_TS(0,ADDRESSU,D3DTADDRESS_CLAMP);MENU_TS(0,ADDRESSV,D3DTADDRESS_CLAMP);
    MENU_TS(0,MINFILTER,D3DTFN_POINT);MENU_TS(0,MAGFILTER,D3DTFG_POINT);MENU_TS(0,MIPFILTER,D3DTFP_NONE);
    MENU_TS(1,COLOROP,D3DTOP_DISABLE);
    MENU_CALL(IDirect3DDevice7_SetTexture(device,0,menu->texture));
    Vertex quad[4]={{-.5f,-.5f,0,1,0xFFFFFFFF,0,0},{desc.dwWidth-.5f,-.5f,0,1,0xFFFFFFFF,640.f/1024,0},
        {-.5f,desc.dwHeight-.5f,0,1,0xFFFFFFFF,0,480.f/512},{desc.dwWidth-.5f,desc.dwHeight-.5f,0,1,0xFFFFFFFF,640.f/1024,480.f/512}};
    MENU_CALL(IDirect3DDevice7_DrawPrimitive(device,D3DPT_TRIANGLESTRIP,D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1,quad,4,0));
restore:
    restored=IDirect3DDevice7_ApplyStateBlock(device,state);if(SUCCEEDED(hr))hr=restored;
    restored=IDirect3DDevice7_SetViewport(device,&saved_viewport);if(SUCCEEDED(hr))hr=restored;
    restored=IDirect3DDevice7_DeleteStateBlock(device,state);if(SUCCEEDED(hr))hr=restored;
#undef MENU_TS
#undef MENU_RS
#undef MENU_CALL
    return hr;
}
#endif
