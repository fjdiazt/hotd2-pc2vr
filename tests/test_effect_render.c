/* Exercise actual D3D11 shaders offscreen with WARP; no game/OpenXR/window. */
#include <assert.h>
#define main viewer_application_main
#include "../src/hotd2vr.c"
#undef main

static ID3D11Texture2D *target,*staging;
static ID3D11RenderTargetView *rtv;
static ID3D11DepthStencilView *dsv;
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
        if(textured) {
            g_local[i].u=g_local[i].v=.25f;
            g_local[i].atlas_x=g_local[i].atlas_y=.5f/4096;
            g_local[i].atlas_w=g_local[i].atlas_h=1.0f/4096;
        }
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
    }
    ID3D11RenderTargetView_Release(rtv);ID3D11DepthStencilView_Release(dsv);
    ID3D11Texture2D_Release(target);ID3D11Texture2D_Release(staging);releaseGraphics();
    puts("Effect shaders: black shadows, splash opacity, alpha test and native blend factors passed (world/sharp).");
    return 0;
}
