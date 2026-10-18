/* Exercise the actual bridge with fake COM surfaces; no game or renderer starts. */
#include <assert.h>
#include "../src/bridge.c"

typedef struct { IDirectDrawSurface7 iface; unsigned refs; } FakeSurface;
static FakeSurface surfaces[1600],*selected;
static DWORD pixels[128*128];
static DWORD texture_flags=DDPF_RGB,texture_bits=32,texture_alpha;
static HRESULT lock_result=DD_OK;
static unsigned prompt_fallbacks;
static DWORD native_cull=1;
static DWORD native_z_enable=1;
static int lighting_fixture;
static DWORD fixture_lighting=1;
static unsigned light_queries;
static DWORD native_address_u=D3DTADDRESS_WRAP,native_address_v=D3DTADDRESS_WRAP;
static HRESULT WINAPI get_transform(IDirect3DDevice7 *self,D3DTRANSFORMSTATETYPE state,D3DMATRIX *out)
{
    (void)self;memset(out,0,sizeof(*out));out->_11=out->_22=out->_33=out->_44=1;
    if(state==D3DTRANSFORMSTATE_PROJECTION){out->_34=1;out->_44=0;}
    return DD_OK;
}
static HRESULT WINAPI get_viewport(IDirect3DDevice7 *self,D3DVIEWPORT7 *out)
{(void)self;memset(out,0,sizeof(*out));out->dwWidth=640;out->dwHeight=480;return DD_OK;}
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
    if(FAILED(lock_result))return lock_result;
    desc->dwWidth=desc->dwHeight=128;desc->lPitch=128*4;desc->lpSurface=pixels;
    desc->ddpfPixelFormat.dwFlags=texture_flags;desc->ddpfPixelFormat.dwRGBBitCount=texture_bits;
    desc->ddpfPixelFormat.dwRGBAlphaBitMask=texture_alpha;
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
    case D3DRENDERSTATE_ZENABLE:*out=native_z_enable;break;
    case D3DRENDERSTATE_ALPHABLENDENABLE:case D3DRENDERSTATE_ZWRITEENABLE:
    case D3DRENDERSTATE_ALPHATESTENABLE:case D3DRENDERSTATE_LIGHTING:*out=1;break;
    case D3DRENDERSTATE_SRCBLEND:case D3DRENDERSTATE_ALPHAFUNC:*out=5;break;
    case D3DRENDERSTATE_DESTBLEND:*out=6;break;
    case D3DRENDERSTATE_ALPHAREF:*out=127;break;
    case D3DRENDERSTATE_CULLMODE:*out=native_cull;break;
    default:break;
    }
    if(lighting_fixture)switch(state) {
    case D3DRENDERSTATE_LIGHTING:*out=fixture_lighting;break;
    case D3DRENDERSTATE_AMBIENT:*out=0xFF333333;break;
    case D3DRENDERSTATE_SPECULARENABLE:case D3DRENDERSTATE_LOCALVIEWER:
    case D3DRENDERSTATE_FOGENABLE:*out=1;break;
    case D3DRENDERSTATE_FOGTABLEMODE:*out=D3DFOG_LINEAR;break;
    case D3DRENDERSTATE_FOGEND:{float end=2;memcpy(out,&end,4);break;}
    case D3DRENDERSTATE_FOGCOLOR:*out=0x00112233;break;
    default:break;
    }
    return DD_OK;
}
static HRESULT WINAPI get_light_enable(IDirect3DDevice7 *self,DWORD index,BOOL *out)
{(void)self;light_queries++;*out=lighting_fixture && index==15;return DD_OK;}
static HRESULT WINAPI get_light(IDirect3DDevice7 *self,DWORD index,D3DLIGHT7 *out)
{
    (void)self;assert(index==15);memset(out,0,sizeof(*out));out->dltType=D3DLIGHT_DIRECTIONAL;
    out->dvDirection.z=1;out->dcvDiffuse.r=out->dcvDiffuse.g=out->dcvDiffuse.b=.5f;
    out->dcvSpecular.r=out->dcvSpecular.g=out->dcvSpecular.b=.25f;return DD_OK;
}
static HRESULT WINAPI get_stage_state(IDirect3DDevice7 *self,DWORD stage,D3DTEXTURESTAGESTATETYPE state,DWORD *out)
{
    (void)self;(void)stage;
    *out=state==D3DTSS_ADDRESSU?native_address_u:state==D3DTSS_ADDRESSV?native_address_v:
        state==D3DTSS_ALPHAOP?D3DTOP_MODULATE:D3DTA_TEXTURE;
    return DD_OK;
}
static HRESULT WINAPI get_material(IDirect3DDevice7 *self,D3DMATERIAL7 *out)
{
    (void)self;memset(out,0,sizeof(*out));out->diffuse.a=.5f;
    if(lighting_fixture) {
        out->diffuse.r=out->diffuse.g=out->diffuse.b=.5f;
        out->ambient.r=out->ambient.g=out->ambient.b=.25f;
        out->emissive.r=out->emissive.g=out->emissive.b=.125f;
        out->specular.r=out->specular.g=out->specular.b=.5f;out->power=8;
    }
    return DD_OK;
}
static unsigned model_draws;
static int __cdecl draw_test_model(const BYTE *record,int pass)
{
    float mesh[3][8]={{-1,-1,5,0,0,1,0,0},{1,-1,5,0,0,1,1,0},{0,1,5,0,0,1,0,1}};
    (void)record;(void)pass;model_draws++;
    capture_draw(active_device,D3DPT_TRIANGLELIST,0x112,mesh,3,NULL,0);
    return 7;
}
static int __cdecl draw_test_tl_model(const BYTE *record,int pass)
{
    float mesh[3][8]={{0,0,.9f,.1f,0,0,0,0},{640,0,.9f,.1f,0,0,1,0},{0,480,.9f,.1f,0,0,0,1}};
    (void)record;(void)pass;assert(capture_model && !capture_kind);
    capture_draw(active_device,D3DPT_TRIANGLELIST,0x1C4,mesh,3,NULL,0);
    return 11;
}

int main(void)
{
    const BYTE cull_code[]={0xF7,0x44,0x24,0x18,0x00,0xF0,0xFF,0x00};
    BYTE *code=VirtualAlloc(NULL,sizeof(cull_code),MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    DWORD cull_mask;
    assert(code);memcpy(code,cull_code,sizeof(cull_code));
    assert(relax_mesh_culling(code));memcpy(&cull_mask,code+4,4);
    assert(!(cull_mask&(D3DSTATUS_CLIPINTERSECTIONLEFT|D3DSTATUS_CLIPINTERSECTIONRIGHT|
        D3DSTATUS_CLIPINTERSECTIONTOP|D3DSTATUS_CLIPINTERSECTIONBOTTOM)));
    assert((cull_mask&D3DSTATUS_CLIPINTERSECTIONFRONT) && (cull_mask&D3DSTATUS_CLIPINTERSECTIONBACK));
    assert((cull_mask&D3DSTATUS_CLIPINTERSECTIONGEN0) && (cull_mask&D3DSTATUS_CLIPINTERSECTIONGEN5));
    for(unsigned b=0;b<sizeof(cull_code);b++)if(b!=5)assert(code[b]==cull_code[b]);
    memcpy(code,cull_code,sizeof(cull_code));code[3]^=1;
    assert(!relax_mesh_culling(code) && code[5]==0xF0);
    VirtualFree(code,0,MEM_RELEASE);
    IDirectDrawSurface7Vtbl surface_vt={.Release=release_surface,.Lock=lock_surface,
        .Unlock=unlock_surface,.GetColorKey=no_color_key};
    IDirect3DDevice7Vtbl device_vt={.GetTexture=get_texture,.GetRenderTarget=no_render_target,
        .GetRenderState=get_render_state,.GetTextureStageState=get_stage_state,.GetMaterial=get_material,
        .GetTransform=get_transform,.GetViewport=get_viewport,.GetLightEnable=get_light_enable,.GetLight=get_light};
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
    for(native_cull=D3DCULL_NONE;native_cull<=D3DCULL_CCW;native_cull++) {
        DrawStyle facing=read_draw_style(&device,0x112);
        assert(((facing.style>>22)&3u)==native_cull);
    }
    Vc2Vertex vertex;
    float source[]={0,0,1,0,0,1,0,0};
    world._11=world._22=world._33=world._44=1;
    view._11=view._22=view._33=view._44=1;
    assert(convert_vertex((const BYTE *)source,0x112,NULL,style,&vertex));
    assert(vertex.colour==0x80000000u && (vertex.style&255)==0x65);
    capture_kind=HOTD_WORLD;
    assert(convert_vertex((const BYTE *)source,0x112,NULL,style,&vertex) && (vertex.style&HOTD_WORLD));
    capture_kind=HOTD_SHOT|(3u<<26);
    assert(convert_vertex((const BYTE *)source,0x112,NULL,style,&vertex) &&
        (vertex.style&(HOTD_WORLD|HOTD_SHOT|HOTD_SHOT_GROUP_MASK))==capture_kind);
    capture_kind=0;
    assert((vertex.style&HOTD_DIFFUSE_ALPHA) && (vertex.style&HOTD_ALPHA_TEST));
    /* Exercise live state/material/light capture, including sparse highest native light slot. */
    lighting_fixture=1;light_queries=0;source[5]=-1;
    DrawStyle lit_style=read_draw_style(&device,0x112);
    assert(light_queries==16 && draw_lighting.light_count==1);
    assert(convert_vertex((const BYTE *)source,0x112,NULL,lit_style,&vertex));
    assert(vertex.colour==0x806C6C6Cu && vertex.specular==0x00202020u);
    assert(vertex.fog_colour==0x01332211u && fabsf(vertex.fog-.5f)<.0001f);
    frame->render_flags=HOTD_RENDER_UNLIT;light_queries=0;
    DrawStyle unlit_style=read_draw_style(&device,0x112);
    assert(!light_queries && !unlit_style.lighting); /* Off skips light queries and normal preparation. */
    assert(convert_vertex((const BYTE *)source,0x112,NULL,unlit_style,&vertex));
    assert(vertex.colour==0x80808080u && !vertex.specular && !vertex.fog_colour && vertex.fog==1);
    frame->render_flags=0;lit_style=read_draw_style(&device,0x112);
    assert(convert_vertex((const BYTE *)source,0x112,NULL,lit_style,&vertex));
    assert(vertex.colour==0x806C6C6Cu && vertex.specular==0x00202020u && vertex.fog_colour==0x01332211u);
    float coloured[9]={0,0,1,0,0,-1,0,0,0};DWORD red=0xFFFF0000;
    memcpy(coloured+6,&red,4);lit_style=read_draw_style(&device,0x152);
    assert(convert_vertex((const BYTE *)coloured,0x152,NULL,lit_style,&vertex));
    assert(vertex.colour==0x806C6C6Cu); /* Material source wins over vertex red. */
    fixture_lighting=0;lit_style=read_draw_style(&device,0x152);
    assert(convert_vertex((const BYTE *)coloured,0x152,NULL,lit_style,&vertex));
    assert(vertex.colour==0xFF0000FFu && !vertex.specular);
    fixture_lighting=1;lighting_fixture=0;source[5]=1;
    source[6]=-1.0f/3;source[7]=4.0f/3;
    for(native_address_u=1;native_address_u<=3;native_address_u++)
        for(native_address_v=1;native_address_v<=3;native_address_v++) {
            DrawStyle addressing=read_draw_style(&device,0x112);
            assert(addressing.address==(native_address_u-1)+4*(native_address_v-1));
            assert(convert_vertex((const BYTE *)source,0x112,NULL,addressing,&vertex));
            assert(vertex.address==addressing.address);
        }
    native_address_u=native_address_v=D3DTADDRESS_WRAP;
    assert(!hotd_address(0,99));
    for(i=0;i<128*128;i++)pixels[i]=0x123456;
    for(i=0;i<1600;i++){surfaces[i].iface.lpVtbl=&surface_vt;surfaces[i].refs=1;}
    texture_flags=DDPF_RGB|DDPF_ALPHAPIXELS;texture_alpha=0xFF000000;
    assert(opaque_bitmap_alpha(&surfaces[0].iface));
    for(i=0;i<128*128;i++)assert(pixels[i]==0xFF123456);
    pixels[0]=0x00123456;texture_bits=16;
    assert(!opaque_bitmap_alpha(&surfaces[0].iface) && pixels[0]==0x00123456);
    texture_bits=32;texture_flags=DDPF_RGB;
    assert(!opaque_bitmap_alpha(&surfaces[0].iface) && pixels[0]==0x00123456);
    texture_flags|=DDPF_ALPHAPIXELS;lock_result=E_FAIL;
    assert(!opaque_bitmap_alpha(&surfaces[0].iface) && pixels[0]==0x00123456);
    lock_result=DD_OK;texture_flags=DDPF_RGB;texture_alpha=0;
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
    /* Exercise actual HUD capture/publication after world geometry. */
    game_base=calloc(1,0x5CA200);assert(game_base);frame->units_per_metre=10;
    *(DWORD *)(game_base+0x5C8E98)=6;selected=surfaces;native_cull=D3DCULL_NONE;
    float hud[3][8]={{0,0,.1f,.5f,0,0,0,0},{640,0,.1f,.5f,0,0,1,0},{0,480,.1f,.5f,0,0,0,1}};
    for(i=0;i<3;i++){DWORD colour=0x80FFFFFF;memcpy(hud[i]+4,&colour,4);}
    float mesh[3][8]={{-1,-1,5,0,0,1,0,0},{1,-1,5,0,0,1,1,0},{0,1,5,0,0,1,0,1}};
    capture_kind=HOTD_WORLD;capture_draw(&device,D3DPT_TRIANGLELIST,0x112,mesh,3,NULL,0);
    capture_kind=0;capture_draw(&device,D3DPT_TRIANGLELIST,0x1C4,hud,3,NULL,0);
    publish_frame();
    assert(frame->vertex_count==6 && frame->reserved[2]==4);
    assert(frame->verts[0].z==-5 && (frame->verts[0].style&HOTD_WORLD));
    assert(frame->verts[3].x==-10 && frame->verts[3].y==7.5f && frame->verts[3].z==-20);
    assert(frame->verts[4].x==10 && frame->verts[5].y==-7.5f);
    assert(frame->verts[3].depth==1 && frame->verts[3].colour==0x80FFFFFF);
    assert(!(frame->verts[3].style&(HOTD_WORLD|HOTD_SHOT|HOTD_CULL_MASK)));
    /* UI survives every mode and native sort depth, including maps/rankings/options. */
    const float ui_depths[]={.9f,.98f,.9999f,1,2,21,45,101,120,200};
    for(unsigned phase=0;phase<=16;phase++)for(unsigned sprite=0;sprite<sizeof(ui_depths)/sizeof(*ui_depths);sprite++) {
        *(DWORD *)(game_base+0x5C8E98)=phase;
        for(i=0;i<3;i++){hud[i][2]=.9f;hud[i][3]=1.0f/ui_depths[sprite];}
        capture_kind=HOTD_WORLD;capture_draw(&device,D3DPT_TRIANGLELIST,0x112,mesh,3,NULL,0);
        capture_kind=0;capture_draw(&device,D3DPT_TRIANGLELIST,0x1C4,hud,3,NULL,0);publish_frame();
        assert(frame->vertex_count==6 && frame->reserved[2]==4);
        assert(frame->verts[0].z==-5 && (frame->verts[0].style&HOTD_WORLD));
        assert(frame->verts[3].z==-20 && frame->verts[3].depth==1);
        assert(!(frame->verts[3].style&(HOTD_WORLD|HOTD_SHOT|HOTD_CULL_MASK)));
    }
    *(DWORD *)(game_base+0x5C8E98)=6;
    for(i=0;i<3;i++){hud[i][2]=.9f;hud[i][3]=1;}
    capture_draw(&device,D3DPT_TRIANGLELIST,0x1C4,hud,3,NULL,0);publish_frame();
    assert(frame->vertex_count==3 && frame->verts[0].z==-20); /* Real HUD's rhw=1 path. */
    for(i=0;i<3;i++)hud[i][3]=1.0f/.98f;
    capture_draw(&device,D3DPT_TRIANGLELIST,0x1C4,hud,3,NULL,0);publish_frame();
    assert(frame->vertex_count==3 && frame->reserved[2]==1 && frame->verts[0].z==-20);
    /* Omit only native letterbox instances; desktop draws, pause and fades survive. */
    BYTE record[0x74]={0};float matrix[16]={0};DWORD bar=0x12345678;
    native_z_enable=1;matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;matrix[14]=-1;
    memcpy(record+0xC,&bar,4);memcpy(game_base+0x5A66A4+0x93E*16,&bar,4);
    const float bar_positions[]={.35f,-.35f,.4f,-.4f,.45f,-.45f};
    unsigned expected_draws=model_draws;
    for(unsigned phase=0;phase<=16;phase++)for(i=0;i<sizeof(bar_positions)/sizeof(*bar_positions);i++) {
        *(DWORD *)(game_base+0x5C8E98)=phase;
        matrix[13]=bar_positions[i];memcpy(record+0x34,matrix,64);
        assert(native_letterbox(record));
        assert(native_model_draw(record,0,draw_test_model)==7 && !capture_skip && !capture_kind);
        publish_frame();assert(!frame->vertex_count && model_draws==++expected_draws);
    }
    matrix[13]=0;matrix[14]=-.0005f;memcpy(record+0x34,matrix,64);
    assert(!native_letterbox(record)); /* Pause dimming. */
    native_model_draw(record,0,draw_test_model);publish_frame();assert(frame->vertex_count==3);
    matrix[5]=8;matrix[14]=-1;memcpy(record+0x34,matrix,64);
    assert(!native_letterbox(record)); /* Full-screen blackout. */
    matrix[5]=1;matrix[13]=.35f;memcpy(record+0x34,matrix,64);
    *(DWORD *)(game_base+0x5C8E98)=6;record[0xC]^=1;assert(!native_letterbox(record));
    for(i=0;i<3;i++)hud[i][3]=.1f;
    capture_kind=HOTD_WORLD;
    capture_draw(&device,D3DPT_TRIANGLELIST,0x1C4,hud,3,NULL,0);publish_frame();
    assert(frame->vertex_count==3 && frame->reserved[2]==4 && frame->verts[0].z==-10);
    assert(frame->verts[0].style&HOTD_WORLD); /* Pretransformed model geometry stays world geometry. */
    capture_kind=HOTD_SHOT|(3u<<26);
    capture_draw(&device,D3DPT_TRIANGLELIST,0x1C4,hud,3,NULL,0);publish_frame();
    assert(frame->vertex_count==3 && frame->reserved[2]==4 && frame->verts[0].z==-10);
    assert((frame->verts[0].style&(HOTD_SHOT|HOTD_SHOT_GROUP_MASK))==capture_kind);
    capture_kind=0;
    /* Traveling tracers deliberately have no collision tag, but remain model geometry. */
    memcpy(record+0xC,&bar,4);matrix[13]=0;matrix[14]=-5;memcpy(record+0x34,matrix,64);
    *(WORD *)(game_base+0x179F78)=373;memcpy(game_base+0x5A66A4+373*16,&bar,4);
    assert(native_model_draw(record,0,draw_test_tl_model)==11 && !capture_model && !capture_kind);
    publish_frame();assert(frame->vertex_count==3 && frame->reserved[2]==4 && frame->verts[0].z==-10);
    native_z_enable=0;
    for(i=0;i<3;i++)hud[i][3]=3;
    capture_draw(&device,D3DPT_TRIANGLELIST,0x1C4,hud,3,NULL,0);publish_frame();
    assert(frame->vertex_count==3 && frame->reserved[2]==1 && frame->verts[0].z==-20);
    while(evict_texture());
    free(game_base);free(frame);
    puts("Texture cache: 1600 surfaces, eviction, pinned live tiles and COM release passed.");
    return 0;
}
