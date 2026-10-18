/* Count real lighting work and compare captured topology/bytes; never starts the game. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/lighting.h"
static unsigned lighting_calls;
static void counted_light_vertex(const HotdLighting *s,const float *p,const float *n,
    DWORD diffuse,DWORD specular,DWORD fvf,HotdLitVertex *out)
{lighting_calls++;hotd_light_vertex(s,p,n,diffuse,specular,fvf,out);}
#define hotd_light_vertex counted_light_vertex
#include "../src/bridge.c"
#undef hotd_light_vertex

static DWORD material_colour=0xFF80C040,stage_address=D3DTADDRESS_WRAP;
static IDirect3DDevice7 device;
static FILE *capture_output;
static unsigned full_strip_calls;
static HRESULT WINAPI fake_transform(IDirect3DDevice7 *self,D3DTRANSFORMSTATETYPE state,D3DMATRIX *out)
{
    (void)self;memset(out,0,sizeof(*out));out->_11=out->_22=out->_33=out->_44=1;
    if(state==D3DTRANSFORMSTATE_PROJECTION){out->_34=1;out->_44=0;}
    return DD_OK;
}
static HRESULT WINAPI fake_viewport(IDirect3DDevice7 *self,D3DVIEWPORT7 *out)
{(void)self;memset(out,0,sizeof(*out));out->dwWidth=640;out->dwHeight=480;return DD_OK;}
static HRESULT WINAPI fake_state(IDirect3DDevice7 *self,D3DRENDERSTATETYPE state,DWORD *out)
{
    (void)self;*out=0;
    switch(state) {
    case D3DRENDERSTATE_ZENABLE:case D3DRENDERSTATE_ZWRITEENABLE:
    case D3DRENDERSTATE_LIGHTING:case D3DRENDERSTATE_SPECULARENABLE:
    case D3DRENDERSTATE_LOCALVIEWER:case D3DRENDERSTATE_FOGENABLE:*out=1;break;
    case D3DRENDERSTATE_CULLMODE:*out=D3DCULL_CW;break;
    case D3DRENDERSTATE_FOGTABLEMODE:*out=D3DFOG_LINEAR;break;
    case D3DRENDERSTATE_FOGEND:{float end=100;memcpy(out,&end,4);break;}
    case D3DRENDERSTATE_FOGCOLOR:*out=0xFF102030;break;
    default:break;
    }
    return DD_OK;
}
static HRESULT WINAPI fake_stage(IDirect3DDevice7 *self,DWORD stage,D3DTEXTURESTAGESTATETYPE state,DWORD *out)
{(void)self;(void)stage;*out=state==D3DTSS_ADDRESSU || state==D3DTSS_ADDRESSV?stage_address:D3DTOP_SELECTARG1;return DD_OK;}
static HRESULT WINAPI fake_texture(IDirect3DDevice7 *self,DWORD stage,IDirectDrawSurface7 **out)
{(void)self;(void)stage;*out=NULL;return DD_OK;}
static HRESULT WINAPI fake_material(IDirect3DDevice7 *self,D3DMATERIAL7 *out)
{
    (void)self;memset(out,0,sizeof(*out));out->diffuse=hotd_light_unpack(material_colour);
    out->specular=hotd_light_unpack(0xFF101010);out->power=4;return DD_OK;
}
static HRESULT WINAPI fake_light_enabled(IDirect3DDevice7 *self,DWORD index,BOOL *out)
{(void)self;*out=index==0;return DD_OK;}
static HRESULT WINAPI fake_light(IDirect3DDevice7 *self,DWORD index,D3DLIGHT7 *out)
{
    (void)self;assert(index==0);memset(out,0,sizeof(*out));out->dltType=D3DLIGHT_DIRECTIONAL;
    out->dvDirection.z=1;out->dcvDiffuse.r=out->dcvDiffuse.g=out->dcvDiffuse.b=.8f;
    out->dcvSpecular=out->dcvDiffuse;return DD_OK;
}

static void run_capture(D3DPRIMITIVETYPE type,DWORD fvf,const void *data,unsigned n,const WORD *indices,unsigned ni,
    const WORD *expected,unsigned expected_vertices,unsigned expected_dropped,unsigned expected_lighting,int hud)
{
    count=hud_count=dropped=0;lighting_calls=0;
    capture_draw(&device,type,fvf,data,n,indices,ni);
    unsigned actual=count+hud_count;
    Vc2Vertex *captured=hud?hud_vertices:vertices;
    assert(actual==expected_vertices && dropped==expected_dropped);
    assert(hud ? count==0 : hud_count==0);
    if(n==100 && type==D3DPT_TRIANGLESTRIP)full_strip_calls=lighting_calls;
    assert(lighting_calls==expected_lighting);
    if(capture_output) {
        assert(fwrite(&actual,sizeof(actual),1,capture_output)==1);
        assert(fwrite(&dropped,sizeof(dropped),1,capture_output)==1);
        assert(fwrite(captured,sizeof(*captured),actual,capture_output)==actual);
    }
    if(expected) {
        DrawStyle style=read_draw_style(&device,fvf);
        unsigned stride=hotd_fvf_stride(fvf);
        static const float corners[4][3]={{-10,7.5f,-20},{10,7.5f,-20},{-10,-7.5f,-20},{10,-7.5f,-20}};
        for(unsigned i=0;i<actual;i++) {
            Vc2Vertex reference;
            assert(convert_vertex((const BYTE *)data+expected[i]*stride,fvf,NULL,style,&reference));
            if(hud) {
                reference.x=corners[expected[i]][0];reference.y=corners[expected[i]][1];reference.z=corners[expected[i]][2];
                reference.depth=1;
                reference.style=(reference.style&~(HOTD_WORLD|HOTD_SHOT|HOTD_SHOT_GROUP_MASK|HOTD_CULL_MASK))|HOTD_NO_ZWRITE;
            }
            assert(!memcmp(captured+i,&reference,sizeof(reference)));
        }
    }
}

int main(int argc,char **argv)
{
    IDirect3DDevice7Vtbl vt={.GetTransform=fake_transform,.GetViewport=fake_viewport,.GetRenderState=fake_state,
        .GetTextureStageState=fake_stage,.GetTexture=fake_texture,.GetMaterial=fake_material,
        .GetLightEnable=fake_light_enabled,.GetLight=fake_light};
    float mesh[100][8];
    const WORD strip_expected[]={0,1,2,2,1,3},fan_expected[]={0,1,2,0,2,3};
    const WORD permuted[]={2,0,3,1},permuted_expected[]={2,0,3,3,0,1};
    const WORD repeated[]={0,1,1,2,3},repeated_expected[]={0,1,1,1,1,2,1,2,3};
    const WORD bad_then_good[]={0,1,99,0,1,2},good[]={0,1,2};
    const WORD invalid_strip[]={0,1,99,2,3};
    const WORD two_lists[]={0,1,2,3,4,5};
    const WORD repeated_list[]={0,1,2,0,1,2};
    device.lpVtbl=&vt;frame=calloc(1,sizeof(*frame));assert(frame);frame->units_per_metre=10;
    if(argc>1){capture_output=fopen(argv[1],"wb");assert(capture_output);}
    for(unsigned i=0;i<100;i++) {
        mesh[i][0]=(float)(i%10)-5;mesh[i][1]=(float)(i/10)-5;mesh[i][2]=5+i*.01f;
        mesh[i][3]=mesh[i][4]=0;mesh[i][5]=-1;mesh[i][6]=i*.02f;mesh[i][7]=i*.03f;
    }
    capture_kind=HOTD_WORLD;
    run_capture(D3DPT_TRIANGLESTRIP,0x112,mesh,100,NULL,0,NULL,294,0,100,0);
    run_capture(D3DPT_TRIANGLEFAN,0x112,mesh,100,NULL,0,NULL,294,0,100,0);
    run_capture(D3DPT_TRIANGLESTRIP,0x112,mesh,4,NULL,0,strip_expected,6,0,4,0);
    run_capture(D3DPT_TRIANGLEFAN,0x112,mesh,4,NULL,0,fan_expected,6,0,4,0);
    run_capture(D3DPT_TRIANGLESTRIP,0x112,mesh,4,permuted,4,permuted_expected,6,0,4,0);
    run_capture(D3DPT_TRIANGLESTRIP,0x112,mesh,4,repeated,5,repeated_expected,9,0,4,0);
    run_capture(D3DPT_TRIANGLELIST,0x112,mesh,6,NULL,0,two_lists,6,0,6,0);
    run_capture(D3DPT_TRIANGLELIST,0x112,mesh,3,repeated_list,6,repeated_list,6,0,3,0);
    run_capture(D3DPT_TRIANGLELIST,0x112,mesh,3,bad_then_good,6,good,3,1,3,0);
    run_capture(D3DPT_TRIANGLESTRIP,0x112,mesh,4,invalid_strip,5,NULL,0,3,2,0);
    mesh[1][0]=NAN;
    run_capture(D3DPT_TRIANGLESTRIP,0x112,mesh,4,NULL,0,NULL,0,2,2,0);
    mesh[1][0]=-4;
    /* No cache can cross draw boundaries, material/sampler changes or capture provenance. */
    material_colour=0xFF20A0E0;stage_address=D3DTADDRESS_MIRROR;mesh[0][2]=9;
    run_capture(D3DPT_TRIANGLESTRIP,0x112,mesh,4,NULL,0,strip_expected,6,0,4,0);
    capture_kind=HOTD_SHOT|(3u<<26);stage_address=D3DTADDRESS_CLAMP;
    run_capture(D3DPT_TRIANGLESTRIP,0x112,mesh,4,NULL,0,strip_expected,6,0,4,0);
    float hud[4][8]={{0,0,.5f,1,0,0,0,0},{640,0,.5f,1,0,0,1,0},
        {0,480,.5f,1,0,0,0,1},{640,480,.5f,1,0,0,1,1}};
    for(unsigned i=0;i<4;i++){DWORD color=0x80402010+i;memcpy(hud[i]+4,&color,4);}
    capture_kind=0;
    run_capture(D3DPT_TRIANGLESTRIP,0x1C4,hud,4,NULL,0,strip_expected,6,0,4,1);
    run_capture(D3DPT_TRIANGLEFAN,0x1C4,hud,4,permuted,4,(WORD[]){2,0,3,2,3,1},6,0,4,1);
    count=hud_count=dropped=0;lighting_calls=0;fvf_count=0;
    capture_draw(NULL,D3DPT_TRIANGLELIST,0x112,NULL,3,NULL,0);
    capture_draw(NULL,D3DPT_TRIANGLELIST,0x112,mesh,0,NULL,0);
    capture_draw(NULL,D3DPT_TRIANGLELIST,0,mesh,3,NULL,0);
    assert(!count && !hud_count && !dropped && !lighting_calls && !fvf_count);
    if(capture_output)assert(!fclose(capture_output));
    free(frame);
    printf("Draw-local vertex reuse: 100-vertex strip uses %u lighting calls; topology, failures, provenance and HUD checks passed.\n",full_strip_calls);
    return 0;
}
