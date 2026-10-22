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
static float srgb_encode(float value)
{return value<=.0031308f?12.92f*value:1.055f*powf(value,1/2.4f)-.055f;}
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
    assert(vrMenuShaders());
}

static void check_menu_alpha(float x,float y,const int expected[3])
{
    TexVtx quad[]={{-1,1,.5f,0,0},{1,1,.5f,0,0},{-1,-1,.5f,0,0},
                  {-1,-1,.5f,0,0},{1,1,.5f,0,0},{1,-1,.5f,0,0}};
    for(unsigned i=0;i<6;i++){quad[i].u=(x+.5f)/640;quad[i].v=(y+.5f)/480;}
    D3D11_MAPPED_SUBRESOURCE mapped;
    assert(SUCCEEDED(ID3D11DeviceContext_Map(g_ctx,(ID3D11Resource *)g_scrVB,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)));
    memcpy(mapped.pData,quad,sizeof(quad));ID3D11DeviceContext_Unmap(g_ctx,(ID3D11Resource *)g_scrVB,0);
    float identity[16],clear[]={.4f,.4f,.4f,1};mIdent(identity);
    assert(SUCCEEDED(ID3D11DeviceContext_Map(g_ctx,(ID3D11Resource *)g_cb,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)));
    memcpy(mapped.pData,identity,sizeof(identity));ID3D11DeviceContext_Unmap(g_ctx,(ID3D11Resource *)g_cb,0);
    ID3D11DeviceContext_VSSetConstantBuffers(g_ctx,0,1,&g_cb);
    ID3D11DeviceContext_OMSetRenderTargets(g_ctx,1,&rtv,NULL);
    ID3D11DeviceContext_ClearRenderTargetView(g_ctx,rtv,clear);
    g_vrMenuEnabled=g_vrMenuOpen=g_vrMenuDirty=1;g_vrMenuPage=-1;g_vrMenuRow=0;
    vrMenuDraw();
    ID3D11BlendState *blend=NULL;ID3D11DeviceContext_OMGetBlendState(g_ctx,&blend,NULL,NULL);assert(!blend);
    ID3D11DeviceContext_CopyResource(g_ctx,(ID3D11Resource *)staging,(ID3D11Resource *)target);
    assert(SUCCEEDED(ID3D11DeviceContext_Map(g_ctx,(ID3D11Resource *)staging,0,D3D11_MAP_READ,0,&mapped)));
    BYTE *pixel=(BYTE *)mapped.pData+4*mapped.RowPitch+4*4;
    for(unsigned c=0;c<3;c++)assert(abs(pixel[c]-expected[c])<=2);
    ID3D11DeviceContext_Unmap(g_ctx,(ID3D11Resource *)staging,0);
    g_vrMenuEnabled=g_vrMenuOpen=0;
}

static void check_arcade_guns(void)
{
    gunLoad();assert(g_gunVB && g_cbGunColor && g_vsGun && g_psGun && g_dssGun);
    ID3D11Buffer_Release(g_gunVB);g_gunVB=NULL;
    Vc2Vertex vertices[3]={0};
    for(unsigned i=0;i<3;i++) {
        vertices[i].x=i==2?3:-1;vertices[i].y=i==1?3:-1;vertices[i].z=.5f;
        vertices[i].colour=0xFF804020;vertices[i].u=vertices[i].v=.5f/4096;
    }
    D3D11_BUFFER_DESC buffer={0};D3D11_SUBRESOURCE_DATA data={0};
    buffer.ByteWidth=sizeof(vertices);buffer.Usage=D3D11_USAGE_DEFAULT;buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    data.pSysMem=vertices;
    assert(SUCCEEDED(ID3D11Device_CreateBuffer(g_dev,&buffer,&data,&g_gunVB)));g_gunCount=3;
    g_gunPose.orientation.w=g_gun2Pose.orientation.w=1;
    ID3D11DeviceContext_RSSetState(g_ctx,g_rs);
    ID3D11DeviceContext_IASetPrimitiveTopology(g_ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const BYTE expected[2][3]={{222,36,43},{29,75,143}},textured[]={32,64,128};
    BYTE flat[2][2]={0};
    float identity[16],clear[4]={0};mIdent(identity);
    ID3D11Texture2D *srgbTarget=NULL;ID3D11RenderTargetView *srgbRtv=NULL;
    D3D11_TEXTURE2D_DESC desc={0};desc.Width=desc.Height=8;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    assert(SUCCEEDED(ID3D11Device_CreateTexture2D(g_dev,&desc,NULL,&srgbTarget)));
    assert(SUCCEEDED(ID3D11Device_CreateRenderTargetView(g_dev,(ID3D11Resource *)srgbTarget,NULL,&srgbRtv)));
    for(unsigned output=0;output<2;output++)for(unsigned brightness=0;brightness<2;brightness++) {
    g_cfgBrightness=brightness?50:100;
    for(unsigned face=0;face<2;face++) {
    for(unsigned i=0;i<3;i++)vertices[i].z=.5f-(face?.08f:0)*(vertices[i].x+vertices[i].y);
    ID3D11DeviceContext_UpdateSubresource(g_ctx,(ID3D11Resource *)g_gunVB,0,NULL,vertices,0,0);
    for(unsigned gamma=0;gamma<2;gamma++)for(unsigned player=0;player<2;player++)for(unsigned mode=0;mode<5;mode++) {
        g_gammaOut=gamma?2.2f:1;
        g_gunValid=!player;g_gun2Valid=player;
        g_cfgArcadeGunColors=mode==1 || mode==2;
        g_gunSRV=mode==2 || mode==4?NULL:g_atlSRV;
        for(unsigned i=0;i<3;i++)vertices[i].u=mode==4?-1:.5f/4096;
        ID3D11DeviceContext_UpdateSubresource(g_ctx,(ID3D11Resource *)g_gunVB,0,NULL,vertices,0,0);
        ID3D11RenderTargetView *gunRtv=output?srgbRtv:rtv;
        ID3D11DeviceContext_OMSetRenderTargets(g_ctx,1,&gunRtv,dsv);
        ID3D11DeviceContext_OMSetBlendState(g_ctx,NULL,NULL,0xFFFFFFFFu);
        D3D11_VIEWPORT viewport={0,0,8,8,0,1};ID3D11DeviceContext_RSSetViewports(g_ctx,1,&viewport);
        ID3D11DeviceContext_ClearRenderTargetView(g_ctx,gunRtv,clear);
        ID3D11DeviceContext_ClearDepthStencilView(g_ctx,dsv,D3D11_CLEAR_DEPTH,0,0);
        drawGun(identity);
        ID3D11ShaderResourceView *bound=NULL;ID3D11DeviceContext_PSGetShaderResources(g_ctx,0,1,&bound);
        assert(bound==(g_cfgArcadeGunColors?NULL:g_gunSRV));if(bound)ID3D11ShaderResourceView_Release(bound);
        ID3D11DeviceContext_CopyResource(g_ctx,(ID3D11Resource *)staging,(ID3D11Resource *)(output?srgbTarget:target));
        D3D11_MAPPED_SUBRESOURCE mapped;
        assert(SUCCEEDED(ID3D11DeviceContext_Map(g_ctx,(ID3D11Resource *)staging,0,D3D11_MAP_READ,0,&mapped)));
        for(unsigned y=2;y<6;y++)for(unsigned x=2;x<6;x++) {
            const BYTE *pixel=(BYTE *)mapped.pData+y*mapped.RowPitch+x*4;
            for(unsigned c=0;c<3;c++) {
                if(g_cfgArcadeGunColors) {
                    float base=expected[player][c]/255.0f;
                    float low=powf(base*.4f*g_cfgBrightness/100,g_gammaOut);
                    float high=powf((base*.75f+.005f)*g_cfgBrightness/100,g_gammaOut);
                    if(output){low=srgb_encode(low);high=srgb_encode(high);}
                    assert(pixel[c]>=low*255-1 && pixel[c]<=high*255+1);
                } else {
                    float value=powf(textured[c]/255.0f*g_cfgBrightness/100,g_gammaOut);
                    if(output)value=srgb_encode(value);
                    assert(abs(pixel[c]-(int)lroundf(value*255))<=1);
                }
            }
            if(g_cfgArcadeGunColors) {
                unsigned channel=player?2:0;
                if(!face)flat[gamma][player]=pixel[channel];
                else assert(pixel[channel]>flat[gamma][player]);
            }
            assert(pixel[3]==255);
        }
        ID3D11DeviceContext_Unmap(g_ctx,(ID3D11Resource *)staging,0);
    }
    }
    }
    ID3D11RenderTargetView_Release(srgbRtv);ID3D11Texture2D_Release(srgbTarget);
    g_cfgBrightness=100;
    g_gunSRV=NULL;g_gunValid=g_gun2Valid=g_cfgArcadeGunColors=0;g_gammaOut=1;
    puts("Guns: UNORM/sRGB gamma and live brightness, P1/P2 face contrast, texture bypass and restoration passed.");
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
    check_arcade_guns();
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
    brightness_shaders(100,1);
    memset(g_vrMenuFont['V'][0][0],255,4);
    const int background[]={65,63,62},text[]={232,232,232};
    check_menu_alpha(630,470,background); /* 50% dark panel over 40% grey scene. */
    check_menu_alpha(42,24,text);         /* Title glyph remains opaque. */
    ID3D11RenderTargetView_Release(rtv);ID3D11DepthStencilView_Release(dsv);
    ID3D11Texture2D_Release(target);ID3D11Texture2D_Release(staging);releaseGraphics();
    puts("Effect rendering: world/sharp effects, brightness, 50% menu background and opaque text passed.");
    return 0;
}
