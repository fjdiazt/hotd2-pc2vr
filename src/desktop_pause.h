/* Replay the viewer's retained geometry through the game's Direct3D 7 device. */
#ifndef HOTD_DESKTOP_PAUSE_H
#define HOTD_DESKTOP_PAUSE_H
#include "hotd_share.h"

typedef IDirectDrawSurface7 *(*HotdPauseTexture)(const Vc2Vertex *);
typedef struct { float x,y,z; DWORD colour,specular; float u,v; } HotdPauseVertex;
typedef struct { BYTE zfunc,z_enable,color_key,mag_filter,min_filter,mip_filter; } HotdPauseRaster;

static DWORD hotd_pause_argb(DWORD rgba)
{return (rgba&0xFF00FF00u)|((rgba&255u)<<16)|((rgba>>16)&255u);}

static int hotd_pause_same_draw(const Vc2Vertex *a,const Vc2Vertex *b)
{
    return a->style==b->style && a->address==b->address && a->fog_colour==b->fog_colour &&
        a->depth==b->depth && a->atlas_x==b->atlas_x && a->atlas_y==b->atlas_y &&
        a->atlas_w==b->atlas_w && a->atlas_h==b->atlas_h;
}

/* Called inside the native scene, after its pause UI. No framebuffer copies. */
static HRESULT hotd_pause_render(IDirect3DDevice7 *device,const Vc2Frame *scene,
    const D3DMATRIX *projection,const D3DVIEWPORT7 *viewport,const HotdPauseRaster *raster,HotdPauseTexture texture)
{
    HotdPauseVertex batch[1536];
    D3DMATRIX saved[3],identity={0},hud={0};
    D3DVIEWPORT7 saved_viewport;
    const D3DTRANSFORMSTATETYPE transforms[]={D3DTRANSFORMSTATE_WORLD,D3DTRANSFORMSTATE_VIEW,D3DTRANSFORMSTATE_PROJECTION};
    DWORD state=0;
    HRESULT hr,restored;
    if(!scene || !scene->vertex_count || scene->vertex_count>VC2_MAX_VERTS || scene->vertex_count%3 ||
       !isfinite(scene->units_per_metre) || scene->units_per_metre<=0)return S_FALSE;
    for(unsigned i=0;i<3;i++) {
        hr=IDirect3DDevice7_GetTransform(device,transforms[i],saved+i);
        if(FAILED(hr))return hr;
    }
    hr=IDirect3DDevice7_GetViewport(device,&saved_viewport);if(FAILED(hr))return hr;
    hr=IDirect3DDevice7_CreateStateBlock(device,D3DSBT_ALL,&state);if(FAILED(hr))return hr;
#define PAUSE_CALL(call) do { hr=(call);if(FAILED(hr))goto restore; } while(0)
#define PAUSE_RS(name,value) PAUSE_CALL(IDirect3DDevice7_SetRenderState(device,D3DRENDERSTATE_##name,value))
#define PAUSE_TS(stage,name,value) PAUSE_CALL(IDirect3DDevice7_SetTextureStageState(device,stage,D3DTSS_##name,value))
    identity._11=identity._22=identity._33=identity._44=1;
    hud._11=1/(HOTD_PANEL_HALF_WIDTH_M*scene->units_per_metre);
    hud._22=1/(HOTD_PANEL_HALF_HEIGHT_M*scene->units_per_metre);hud._44=1;
    PAUSE_CALL(IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_WORLD,&identity));
    PAUSE_CALL(IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_VIEW,&identity));
    PAUSE_CALL(IDirect3DDevice7_SetViewport(device,(D3DVIEWPORT7 *)viewport));
    PAUSE_RS(LIGHTING,FALSE);PAUSE_RS(COLORVERTEX,TRUE);PAUSE_RS(SPECULARENABLE,TRUE);
    PAUSE_RS(FOGTABLEMODE,D3DFOG_NONE);PAUSE_RS(FOGVERTEXMODE,D3DFOG_NONE);
    PAUSE_RS(CLIPPING,TRUE);PAUSE_RS(CLIPPLANEENABLE,0);PAUSE_RS(STENCILENABLE,FALSE);
    PAUSE_RS(FILLMODE,D3DFILL_SOLID);PAUSE_RS(SHADEMODE,D3DSHADE_GOURAUD);
    PAUSE_RS(ZBIAS,0);PAUSE_RS(WRAP0,0);
    PAUSE_RS(TEXTUREPERSPECTIVE,TRUE);
    PAUSE_TS(1,COLOROP,D3DTOP_DISABLE);
    PAUSE_TS(0,TEXCOORDINDEX,0);PAUSE_TS(0,TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
    PAUSE_TS(0,COLORARG1,D3DTA_TEXTURE);PAUSE_TS(0,COLORARG2,D3DTA_DIFFUSE);
    PAUSE_TS(0,ALPHAARG1,D3DTA_TEXTURE);PAUSE_TS(0,ALPHAARG2,D3DTA_DIFFUSE);
    PAUSE_CALL(IDirect3DDevice7_Clear(device,0,NULL,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));
    for(unsigned start=0;start<scene->vertex_count;) {
        const Vc2Vertex *v=scene->verts+start;
        const HotdPauseRaster *r=raster+start/3;
        unsigned end=start+3,style=v->style,overlay=v->depth>=1;
        IDirectDrawSurface7 *surface=v->atlas_w>0?texture(v):NULL;
        if(v->atlas_w>0 && !surface){hr=E_FAIL;goto restore;}
        while(end<scene->vertex_count && end-start+3<=sizeof(batch)/sizeof(batch[0]) &&
              hotd_pause_same_draw(v,scene->verts+end) && !memcmp(r,raster+end/3,sizeof(*r)))end+=3;
        PAUSE_CALL(IDirect3DDevice7_SetTransform(device,D3DTRANSFORMSTATE_PROJECTION,
            (D3DMATRIX *)(overlay?&hud:projection)));
        PAUSE_CALL(IDirect3DDevice7_SetTexture(device,0,surface));
        PAUSE_TS(0,COLOROP,surface?D3DTOP_MODULATE:D3DTOP_SELECTARG2);
        PAUSE_TS(0,ALPHAOP,!surface?D3DTOP_SELECTARG2:
            style&HOTD_DIFFUSE_ALPHA?D3DTOP_MODULATE:D3DTOP_SELECTARG1);
        PAUSE_TS(0,ADDRESSU,1+(v->address&3u));PAUSE_TS(0,ADDRESSV,1+((v->address>>2)&3u));
        PAUSE_TS(0,MAGFILTER,r->mag_filter);PAUSE_TS(0,MINFILTER,r->min_filter);PAUSE_TS(0,MIPFILTER,r->mip_filter);
        PAUSE_RS(COLORKEYENABLE,r->color_key);PAUSE_RS(ZFUNC,r->zfunc);
        PAUSE_RS(ZENABLE,overlay?D3DZB_FALSE:r->z_enable);PAUSE_RS(ZWRITEENABLE,!overlay && !(style&HOTD_NO_ZWRITE));
        DWORD cull=(style&HOTD_CULL_MASK)>>HOTD_CULL_SHIFT;
        PAUSE_RS(CULLMODE,overlay || !cull?D3DCULL_NONE:cull);
        PAUSE_RS(ALPHABLENDENABLE,(style&255u)!=0);
        PAUSE_RS(SRCBLEND,style&15u?style&15u:D3DBLEND_ONE);
        PAUSE_RS(DESTBLEND,(style>>4)&15u?(style>>4)&15u:D3DBLEND_ZERO);
        PAUSE_RS(ALPHATESTENABLE,(style&HOTD_ALPHA_TEST)!=0);
        PAUSE_RS(ALPHAFUNC,(style>>11)&7u?(style>>11)&7u:D3DCMP_ALWAYS);
        PAUSE_RS(ALPHAREF,(style>>14)&255u);
        PAUSE_RS(FOGENABLE,(v->fog_colour>>24)!=0);PAUSE_RS(FOGCOLOR,hotd_pause_argb(v->fog_colour)&0xFFFFFFu);
        for(unsigned i=start;i<end;i++) {
            const Vc2Vertex *in=scene->verts+i;
            HotdPauseVertex *out=batch+i-start;
            unsigned mode=in->fog_colour>>24;
            float fog=mode==2?expf(-in->fog):mode==3?expf(-in->fog*in->fog):mode?in->fog:1;
            out->x=in->x;out->y=in->y;out->z=-in->z;
            out->colour=hotd_pause_argb(in->colour);
            out->specular=(hotd_pause_argb(in->specular)&0xFFFFFFu)|(hotd_unorm(fog)<<24);
            out->u=in->u;out->v=in->v;
        }
        PAUSE_CALL(IDirect3DDevice7_DrawPrimitive(device,D3DPT_TRIANGLELIST,
            D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_SPECULAR|D3DFVF_TEX1,batch,end-start,0));
        start=end;
    }
restore:
    restored=IDirect3DDevice7_ApplyStateBlock(device,state);
    if(SUCCEEDED(hr))hr=restored;
    for(unsigned i=0;i<3;i++) {
        restored=IDirect3DDevice7_SetTransform(device,transforms[i],saved+i);
        if(SUCCEEDED(hr))hr=restored;
    }
    restored=IDirect3DDevice7_SetViewport(device,&saved_viewport);if(SUCCEEDED(hr))hr=restored;
    restored=IDirect3DDevice7_DeleteStateBlock(device,state);if(SUCCEEDED(hr))hr=restored;
#undef PAUSE_TS
#undef PAUSE_RS
#undef PAUSE_CALL
    return hr;
}
#endif
