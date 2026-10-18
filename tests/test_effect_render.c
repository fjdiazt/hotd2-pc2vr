/* Exercise actual D3D11 shaders offscreen with WARP; no game/OpenXR/window. */
#include <assert.h>
#define main viewer_application_main
#include "../src/hotd2vr.c"
#undef main

static ID3D11Texture2D *target,*staging;
static ID3D11RenderTargetView *rtv;
static ID3D11DepthStencilView *dsv;
static int reverse_winding;
static float sample_u=.25f,sample_v=.25f,texture_span=1.0f/4096;
static unsigned sample_address;
static void check_effect(DWORD colour,unsigned style,int textured,int expected,int sharp)
{
    float matrix[16],clear[]={.4f,.4f,.4f,1};
    D3D11_MAPPED_SUBRESOURCE mapped;
    unsigned i;
    g_cfgHudSharp=sharp;g_ovStart=sharp?0:3;g_localCount=3;g_noClear=1;
    memset(g_local,0,3*sizeof(*g_local));
    for(i=0;i<3;i++) {
        g_local[i].x=i==2?3.0f:-1.0f;g_local[i].y=i==1?3.0f:-1.0f;g_local[i].z=.5f;
        g_local[i].colour=colour;g_local[i].style=style;
        g_local[i].address=sample_address;
        if(textured) {
            g_local[i].u=sample_u;g_local[i].v=sample_v;
            g_local[i].atlas_x=g_local[i].atlas_y=.5f/4096;
            g_local[i].atlas_w=g_local[i].atlas_h=texture_span;
        }
    }
    if(reverse_winding) {
        Vc2Vertex swap=g_local[1];g_local[1]=g_local[2];g_local[2]=swap;
    }
    if((style&HOTD_CULL_MASK) && !textured) {
        float ray_origin[3]={0,0,1},ray_direction[3]={0,0,-1},distance;
        g_unitsPerMetre=10;
        for(i=0;i<3;i++)g_local[i].style|=HOTD_WORLD;
        assert(!!rayVsScene(ray_origin,ray_direction,&distance)==(expected==255));
    }
    uploadVerts();mIdent(matrix);
    ID3D11DeviceContext_ClearRenderTargetView(g_ctx,rtv,clear);
    ID3D11DeviceContext_ClearDepthStencilView(g_ctx,dsv,D3D11_CLEAR_DEPTH,0,0);
    drawWith(rtv,dsv,8,8,matrix,NULL);
    ID3D11DeviceContext_CopyResource(g_ctx,(ID3D11Resource *)staging,(ID3D11Resource *)target);
    assert(SUCCEEDED(ID3D11DeviceContext_Map(g_ctx,(ID3D11Resource *)staging,0,D3D11_MAP_READ,0,&mapped)));
    unsigned char *pixel=(unsigned char *)mapped.pData+4*mapped.RowPitch+4*4;
    for(i=0;i<3;i++)assert(abs((int)pixel[i]-expected)<=2);
    ID3D11DeviceContext_Unmap(g_ctx,(ID3D11Resource *)staging,0);
}

int main(void)
{
    D3D_FEATURE_LEVEL level;
    ID3D11Texture2D *depth;
    D3D11_TEXTURE2D_DESC desc={0};
    setvbuf(stdout,NULL,_IONBF,0);
    g_cfgDepth=2;g_gammaOut=1;
    assert(SUCCEEDED(D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_WARP,NULL,0,NULL,0,D3D11_SDK_VERSION,&g_dev,&level,&g_ctx)));
    makePipeline();
    assert(g_psOv);
    desc.Width=desc.Height=8;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    assert(SUCCEEDED(ID3D11Device_CreateTexture2D(g_dev,&desc,NULL,&target)));
    assert(SUCCEEDED(ID3D11Device_CreateRenderTargetView(g_dev,(ID3D11Resource *)target,NULL,&rtv)));
    desc.Format=DXGI_FORMAT_D32_FLOAT;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    assert(SUCCEEDED(ID3D11Device_CreateTexture2D(g_dev,&desc,NULL,&depth)));
    assert(SUCCEEDED(ID3D11Device_CreateDepthStencilView(g_dev,(ID3D11Resource *)depth,NULL,&dsv)));
    ID3D11Texture2D_Release(depth);
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    assert(SUCCEEDED(ID3D11Device_CreateTexture2D(g_dev,&desc,NULL,&staging)));
    g_atlPix[0]=g_atlPix[1]=g_atlPix[4096]=g_atlPix[4097]=0x80FFFFFF;
    g_atlFresh=1;uploadAtlasTex();
    for(int sharp=0;sharp<2;sharp++) {
        check_effect(0x80000000,hotd_style(1,5,6,1,1,0,8,0),0,51,sharp);
        check_effect(0x00FFFFFF,hotd_style(1,5,6,1,1,0,8,0),0,102,sharp);
        check_effect(0x80FFFFFF,hotd_style(1,5,6,1,1,0,8,0),1,140,sharp);
        check_effect(0x80FFFFFF,hotd_style(1,5,6,1,0,0,8,0),1,179,sharp);
        check_effect(0x80FFFFFF,hotd_style(1,5,6,1,1,1,5,127),1,102,sharp);
        check_effect(0x80FFFFFF,hotd_style(1,5,5,0,1,0,8,0),1,90,sharp);
        for(reverse_winding=0;reverse_winding<2;reverse_winding++) {
            check_effect(0xFFFFFFFF,1u<<22,0,255,sharp); /* D3DCULL_NONE */
            check_effect(0xFFFFFFFF,2u<<22,0,reverse_winding?255:102,sharp); /* D3DCULL_CW */
            check_effect(0xFFFFFFFF,3u<<22,0,reverse_winding?102:255,sharp); /* D3DCULL_CCW */
        }
        reverse_winding=0;
    }
    /* Positive/negative repeats and exact seams, independently addressed on U and V. */
    const struct {float uv;int expected[3];} cases[]={
        {-1.0f/3,{128,64,0}},{4.0f/3,{64,128,255}},{-1,{0,255,0}},
        {1,{0,255,255}},{1.0f/3,{64,64,64}}};
    const unsigned gray[]={0,64,128,255};
    texture_span=3.0f/4096;
    for(unsigned axis=0;axis<2;axis++) {
        for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)
            g_atlPix[y*4096+x]=0xFF000000u|gray[axis?y:x]*0x010101u;
        g_atlFresh=1;uploadAtlasTex();
        for(unsigned u=1;u<=3;u++)for(unsigned v=1;v<=3;v++) {
            sample_address=hotd_address(u,v);
            for(unsigned c=0;c<sizeof(cases)/sizeof(*cases);c++)for(int sharp=0;sharp<2;sharp++) {
                sample_u=axis?.33333333f:cases[c].uv;
                sample_v=axis?cases[c].uv:.33333333f;
                check_effect(0xFFFFFFFF,0,1,cases[c].expected[(axis?v:u)-1],sharp);
            }
        }
    }
    ID3D11RenderTargetView_Release(rtv);ID3D11DepthStencilView_Release(dsv);
    ID3D11Texture2D_Release(target);ID3D11Texture2D_Release(staging);releaseGraphics();
    puts("Effect rendering: shadows, opacity, alpha test, blending, culling and U/V wrap/mirror/clamp passed (world/sharp).");
    return 0;
}
