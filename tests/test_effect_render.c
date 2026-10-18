/* Exercise actual D3D11 shaders offscreen with WARP; no game/OpenXR/window. */
#include <assert.h>
#define main viewer_application_main
#include "../src/hotd2vr.c"
#undef main

static ID3D11Texture2D *target,*staging;
static ID3D11RenderTargetView *rtv;
static ID3D11DepthStencilView *dsv;
static int reverse_winding;
static float background_depth;
static float sample_u=.25f,sample_v=.25f,texture_span=1.0f/4096;
static unsigned sample_address,sample_specular,sample_fog_colour;
static float sample_fog=1,sample_depth;
static int vary_fog;
static int expected_rgb[3]={-1,-1,-1};
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
        g_local[i].depth=sample_depth;
        g_local[i].specular=sample_specular;g_local[i].fog_colour=sample_fog_colour;
        g_local[i].fog=vary_fog?(i? -2.0f:2.0f):sample_fog;
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
    ID3D11DeviceContext_ClearDepthStencilView(g_ctx,dsv,D3D11_CLEAR_DEPTH,background_depth,0);
    drawWith(rtv,dsv,8,8,matrix,NULL);
    ID3D11DeviceContext_CopyResource(g_ctx,(ID3D11Resource *)staging,(ID3D11Resource *)target);
    assert(SUCCEEDED(ID3D11DeviceContext_Map(g_ctx,(ID3D11Resource *)staging,0,D3D11_MAP_READ,0,&mapped)));
    unsigned char *pixel=(unsigned char *)mapped.pData+4*mapped.RowPitch+4*4;
    for(i=0;i<3;i++)assert(abs((int)pixel[i]-(expected_rgb[i]<0?expected:expected_rgb[i]))<=2);
    ID3D11DeviceContext_Unmap(g_ctx,(ID3D11Resource *)staging,0);
}

static void brightness_shaders(int brightness,float gamma)
{
    g_cfgBrightness=brightness;g_gammaOut=gamma;
    for(int sharp=0;sharp<2;sharp++) {
        ID3DBlob *blob=NULL,*error=NULL;
        ID3D11PixelShader **shader=sharp?&g_psOv:&g_ps;
        HRESULT result=compileG(sharp?SRC_OV:SRC,"ps","ps_4_0",&blob,&error);
        if(FAILED(result) && error)fprintf(stderr,"%s\n",(const char *)ID3D10Blob_GetBufferPointer(error));
        assert(SUCCEEDED(result));
        ID3D11PixelShader_Release(*shader);*shader=NULL;
        assert(SUCCEEDED(ID3D11Device_CreatePixelShader(g_dev,ID3D10Blob_GetBufferPointer(blob),
            ID3D10Blob_GetBufferSize(blob),NULL,shader)));
        ID3D10Blob_Release(blob);if(error)ID3D10Blob_Release(error);
    }
}

int main(void)
{
    D3D_FEATURE_LEVEL level;
    ID3D11Texture2D *depth;
    D3D11_TEXTURE2D_DESC desc={0};
    setvbuf(stdout,NULL,_IONBF,0);
    g_cfgDepth=2;g_gammaOut=1;g_cfgBrightness=100;
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
    /* Secondary colour follows texture modulation; table fog follows saturated specular. */
    for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)g_atlPix[y*4096+x]=0xFF404040;
    g_atlFresh=1;uploadAtlasTex();
    for(int sharp=0;sharp<2;sharp++) {
        sample_specular=0x00404040;
        check_effect(0xFF808080,0,1,96,sharp);
        sample_fog_colour=1u<<24;sample_fog=.5f;
        check_effect(0xFF808080,0,1,48,sharp);
        check_effect(0xFFFFFFFF,0,0,128,sharp); /* Clamp highlight before fog. */
        sample_specular=0;sample_fog_colour=(1u<<24)|0x00FFFFFF;sample_fog=.25f;
        check_effect(0xFF000000,0,0,191,sharp);
        sample_fog_colour=0x010000FFu;sample_fog=.5f;
        expected_rgb[0]=128;expected_rgb[1]=expected_rgb[2]=0;
        check_effect(0xFF000000,0,0,0,sharp); /* Packed fog RGB reaches correct channels. */
        expected_rgb[0]=expected_rgb[1]=expected_rgb[2]=-1;
        sample_fog_colour=2u<<24;sample_fog=logf(2.0f);
        check_effect(0xFFFFFFFF,0,0,128,sharp);
        sample_fog_colour=3u<<24;sample_fog=sqrtf(logf(2.0f));
        check_effect(0xFFFFFFFF,0,0,128,sharp);
        sample_fog_colour=1u<<24;vary_fog=1;
        check_effect(0xFFFFFFFF,0,0,0,sharp); /* Clamp fog per pixel, not at triangle vertices. */
        vary_fog=0;sample_fog_colour=0;
        check_effect(0xFFFFFFFF,0,0,255,sharp);
    }
    for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)g_atlPix[y*4096+x]=0x80FFFFFF;
    g_atlFresh=1;uploadAtlasTex();
    background_depth=.9f;
    check_effect(0xFFFFFFFF,HOTD_NO_ZWRITE,0,255,1); /* HUD stays visible over nearer world depth. */
    g_scrVisible=g_scrHave=1;g_scrSRV=g_atlSRV;
    check_effect(0xFFFFFFFF,HOTD_NO_ZWRITE,0,102,1); /* Screen panel replaces HUD, not a second UI layer. */
    g_inScope=1;
    check_effect(0xFFFFFFFF,HOTD_NO_ZWRITE,0,255,1); /* Scope never draws the screen panel. */
    g_inScope=0;g_scrSRV=NULL;
    check_effect(0xFFFFFFFF,HOTD_NO_ZWRITE,0,255,1); /* Missing panel texture cannot hide UI. */
    g_scrVisible=g_scrHave=0;
    /* Positive/negative repeats and exact seams, independently addressed on U and V. */
    const struct {float uv;int expected[3];} cases[]={
        {-1.0f/3,{128,64,0}},{4.0f/3,{64,128,255}},{-1,{0,255,0}},
        {1,{0,255,255}},{1.0f/3,{64,64,64}}};
    const unsigned gray[]={0,64,128,255};
    background_depth=0;texture_span=3.0f/4096;
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
    /* Scene brightness follows native shading, precedes output gamma, and leaves HUD/aim depth neutral. */
    sample_address=0;sample_u=sample_v=.25f;texture_span=1.0f/4096;
    sample_specular=sample_fog_colour=0;sample_fog=1;
    for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)g_atlPix[y*4096+x]=0xFF808080;
    g_atlFresh=1;uploadAtlasTex();
    const int brightness[]={50,90,100,200};
    for(unsigned b=0;b<sizeof(brightness)/sizeof(*brightness);b++) {
        float gain=brightness[b]/100.0f;
        brightness_shaders(brightness[b],1);
        for(int sharp=0;sharp<2;sharp++) {
            check_effect(0xFF808080,0,0,(int)fminf(255,128*gain),sharp);
            check_effect(0xFF808080,0,1,(int)fminf(255,128*128/255.0f*gain),sharp);
            check_effect(0xFFFFFFFF,0,0,(int)fminf(255,255*gain),sharp);
            sample_specular=0x00404040;sample_fog_colour=1u<<24;sample_fog=.5f;
            check_effect(0xFF808080,0,1,(int)((128*128/255.0f+64)*.5f*gain),sharp);
            sample_specular=0;sample_fog_colour=0x01FFFFFF;
            check_effect(0xFF000000,0,0,(int)fminf(255,127.5f*gain),sharp);
            sample_fog_colour=0;sample_depth=1;
            check_effect(0xFF808080,0,0,128,sharp);
            check_effect(0xFF808080,0,1,64,sharp);
            sample_depth=0;
        }
    }
    brightness_shaders(50,2);
    for(int sharp=0;sharp<2;sharp++) {
        check_effect(0xFF808080,0,0,16,sharp);
        sample_specular=0x00404040;sample_fog_colour=1u<<24;sample_fog=.5f;
        check_effect(0xFF808080,0,1,4,sharp);
        sample_specular=sample_fog_colour=0;sample_depth=1;
        check_effect(0xFF808080,0,0,64,sharp);
        sample_depth=0;
    }
    ID3D11RenderTargetView_Release(rtv);ID3D11DepthStencilView_Release(dsv);
    ID3D11Texture2D_Release(target);ID3D11Texture2D_Release(staging);releaseGraphics();
    puts("Effect rendering: shadows, opacity, alpha test, blending, culling, secondary specular, fog, U/V wrap/mirror/clamp and scene brightness passed (world/sharp).");
    return 0;
}
