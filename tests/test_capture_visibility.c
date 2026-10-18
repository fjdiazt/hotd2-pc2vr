/* Actual bridge policy with fake native submissions and D3D7 visibility. No game starts. */
#include <assert.h>
#include "../src/bridge.c"

static DWORD sphere_result,sphere_seen;
static HRESULT sphere_hr=DD_OK;
static unsigned sphere_calls,original_submits;
static int nested_draw;
static BYTE other_record[HOTD_MODEL_RECORD_SIZE];
static HRESULT WINAPI fake_spheres(IDirect3DDevice7 *self,D3DVECTOR *centres,D3DVALUE *radii,
                                  DWORD n,DWORD flags,DWORD *out)
{
    (void)self;(void)centres;(void)radii;(void)flags;sphere_calls++;
    for(DWORD i=0;i<n;i++)out[i]=sphere_result;
    return sphere_hr;
}
static HRESULT WINAPI fake_transform(IDirect3DDevice7 *self,D3DTRANSFORMSTATETYPE state,D3DMATRIX *out)
{
    (void)self;(void)state;memset(out,0,sizeof(*out));out->_11=out->_22=out->_33=out->_44=1;
    return DD_OK;
}
static HRESULT WINAPI fake_viewport(IDirect3DDevice7 *self,D3DVIEWPORT7 *out)
{(void)self;memset(out,0,sizeof(*out));out->dwWidth=640;out->dwHeight=480;return DD_OK;}
static HRESULT WINAPI fake_state(IDirect3DDevice7 *self,D3DRENDERSTATETYPE state,DWORD *out)
{
    (void)self;
    *out=state==D3DRENDERSTATE_CULLMODE?D3DCULL_CW:
        state==D3DRENDERSTATE_ZENABLE || state==D3DRENDERSTATE_ZWRITEENABLE;
    return DD_OK;
}
static HRESULT WINAPI fake_stage(IDirect3DDevice7 *self,DWORD stage,D3DTEXTURESTAGESTATETYPE state,DWORD *out)
{(void)self;(void)stage;(void)state;*out=1;return DD_OK;}
static HRESULT WINAPI fake_texture(IDirect3DDevice7 *self,DWORD stage,IDirectDrawSurface7 **out)
{(void)self;(void)stage;*out=NULL;return DD_OK;}
static HRESULT WINAPI fake_material(IDirect3DDevice7 *self,D3DMATERIAL7 *out)
{(void)self;memset(out,0,sizeof(*out));out->diffuse.r=out->diffuse.g=out->diffuse.b=out->diffuse.a=1;return DD_OK;}

static int fake_model(const BYTE *record,int pass)
{
    D3DVECTOR centre={0};D3DVALUE radius=1;DWORD visible;
    float mesh[3][8]={{-1,-1,-5,0,0,1,0,0},{1,-1,-5,0,0,1,1,0},{0,1,-5,0,0,1,0,1}};
    (void)record;(void)pass;
    assert(SUCCEEDED(IDirect3DDevice7_ComputeSphereVisibility(active_device,&centre,&radius,1,0,&visible)));
    sphere_seen=visible;
    if(!(visible&0x00FFF000u))capture_draw(active_device,D3DPT_TRIANGLELIST,0x112,mesh,3,NULL,0);
    return 7;
}
static int __cdecl normal_model(const BYTE *record,int pass)
{
    if(nested_draw) {
        DWORD saved=capture_visibility_planes;
        nested_draw=0;
        assert(native_model_alt(other_record,pass)==7);
        assert(capture_visibility_planes==saved);
    }
    return fake_model(record,pass);
}
static int __cdecl alternate_model(const BYTE *record,int pass)
{return fake_model(record,pass);}
static void __cdecl submit_model(const BYTE *record)
{original_submits++;assert(native_model(record,0)==7);}
static void __cdecl submit_alternate(const BYTE *record,float alpha)
{original_submits++;assert(alpha==.5f);assert(native_model_alt(record,0)==7);}

static void test_signatures(void)
{
    static const unsigned offsets[]={0xA7F81,0xA84C1,0xA7E50,0xA8390,0x137B,0x13C7,0x140E,0x6310};
    static const unsigned lengths[]={8,8,5,5,10,10,10,9};
    static const BYTE mesh[]={0xF7,0x44,0x24,0x18,0x00,0xF0,0xFF,0x00};
    static const BYTE submit[]={0x56,0x8B,0x74,0x24,0x08};
    static const BYTE gates[3][10]={{0xE8,0x90,0x4F,0,0,0x83,0xC4,0x10,0x85,0xC0},
        {0xE8,0x44,0x4F,0,0,0x83,0xC4,0x10,0x85,0xC0},{0xE8,0xFD,0x4E,0,0,0x83,0xC4,0x10,0x85,0xC0}};
    static const BYTE sphere[]={0x83,0xEC,0x38,0xD9,0x05,0xCC,0x49,0x4C,0x00};
    assert(!hotd_visibility_profile(game_base));
    assert(!install_capture_visibility() && !visibility_hooks_ready && !visibility_ready);
    memcpy(game_base+offsets[0],mesh,sizeof(mesh));memcpy(game_base+offsets[1],mesh,sizeof(mesh));
    memcpy(game_base+offsets[2],submit,sizeof(submit));memcpy(game_base+offsets[3],submit,sizeof(submit));
    for(unsigned i=0;i<3;i++)memcpy(game_base+offsets[4+i],gates[i],sizeof(gates[i]));
    memcpy(game_base+offsets[7],sphere,sizeof(sphere));
    assert(hotd_visibility_profile(game_base));
    for(unsigned i=0;i<8;i++)for(unsigned b=0;b<lengths[i];b++) {
        game_base[offsets[i]+b]^=1;
        assert(!hotd_visibility_profile(game_base));
        game_base[offsets[i]+b]^=1;
    }
}
static void reset_capture(DWORD flags)
{
    frame->render_flags=flags;
    count=calls=dropped=hud_count=0;
    stage_records.count=stage_records.overflow=0;
    assert(!capture_model && !capture_visibility_planes);
}

int main(void)
{
    IDirect3DDevice7Vtbl vt={.ComputeSphereVisibility=sphere_visibility,.GetTransform=fake_transform,
        .GetViewport=fake_viewport,.GetRenderState=fake_state,.GetTextureStageState=fake_stage,
        .GetTexture=fake_texture,.GetMaterial=fake_material};
    IDirect3DDevice7 device={&vt};
    BYTE record[HOTD_MODEL_RECORD_SIZE]={0},copy[HOTD_MODEL_RECORD_SIZE];
    DWORD task[3]={0};
    game_base=VirtualAlloc(NULL,0x5C9000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    frame=VirtualAlloc(NULL,sizeof(*frame),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(game_base && frame);
    active_device=&device;original_sphere_visibility=fake_spheres;
    native_model_original=normal_model;native_model_alt_original=alternate_model;
    native_submit_original=submit_model;native_submit_alt_original=submit_alternate;
    test_signatures();
    *(DWORD *)(record+0xC)=0x12345678;
    *(DWORD *)(other_record+0xC)=0x87654321;
    *(DWORD **)(game_base+0x3DF8B0)=task;task[0]=(DWORD)(ULONG_PTR)(game_base+0x1260);

    /* Native flags off: no sphere-result changes, including outside model capture. */
    visibility_ready=1;sphere_result=D3DSTATUS_CLIPINTERSECTIONLEFT;
    reset_capture(HOTD_RENDER_NATIVE_MESH_CULLING);
    assert(native_model(record,0)==7 && sphere_seen==sphere_result && count==0);
    reset_capture(0);
    assert(native_model(record,0)==7 && sphere_seen==0 && count==3);
    assert((vertices[0].style&HOTD_WORLD) && vertices[0].z==5);
    assert((vertices[0].style&HOTD_CULL_MASK)==((DWORD)D3DCULL_CW<<HOTD_CULL_SHIFT));
    assert(native_model_alt(record,1)==7 && sphere_seen==sphere_result && count==3);
    {
        D3DVECTOR centre={0};D3DVALUE radius=1;DWORD visible;
        assert(SUCCEEDED(sphere_visibility(&device,&centre,&radius,1,0,&visible)) && visible==sphere_result);
        capture_model=1;capture_visibility_planes=0x3F000;
        sphere_hr=E_FAIL;
        assert(FAILED(sphere_visibility(&device,&centre,&radius,1,0,&visible)) && visible==sphere_result);
        sphere_hr=DD_OK;capture_model=0;capture_visibility_planes=0;
    }
    for(unsigned bit=12;bit<24;bit++) {
        sphere_result=1u<<bit;count=0;
        assert(native_model(record,0)==7);
        assert(sphere_seen==(bit<16?0:sphere_result));
        assert(count==(bit<16?3u:0u));
    }
    sphere_result=0x80000000|D3DSTATUS_CLIPINTERSECTIONLEFT;
    assert(native_model(record,0)==7 && sphere_seen==0x80000000);
    visibility_ready=0;sphere_result=D3DSTATUS_CLIPINTERSECTIONLEFT;
    assert(native_model(record,0)==7 && sphere_seen==sphere_result);
    visibility_ready=1;

    /* Only known stage submissions authorize rear-camera plane removal. */
    reset_capture(HOTD_RENDER_EXPAND_WORLD|HOTD_RENDER_NATIVE_MESH_CULLING);
    sphere_result=D3DSTATUS_CLIPINTERSECTIONFRONT|D3DSTATUS_CLIPINTERSECTIONBACK;
    assert(native_model(record,0)==7 && sphere_seen==sphere_result);
    native_submit(record);
    assert(stage_records.count==1 && sphere_seen==0 && count==3 && original_submits==1);
    memcpy(copy,record,sizeof(copy));*(DWORD *)copy=0xE0000000;*(float *)(copy+4)=77;
    assert(native_model(copy,1)==7 && sphere_seen==0 && count==6);
    native_submit_alt(copy,.5f);
    assert(stage_records.count==2 && sphere_seen==0 && count==9 && original_submits==2);
    *(float *)(copy+0x10)=.5f;
    assert(native_model_alt(copy,1)==7 && sphere_seen==0 && count==12);
    for(unsigned bit=12;bit<24;bit++) {
        sphere_result=1u<<bit;count=0;
        assert(native_model_alt(copy,1)==7);
        assert(sphere_seen==(bit<18?0:sphere_result));
        assert(count==(bit<18?3u:0u));
    }
    sphere_result=D3DSTATUS_CLIPINTERSECTIONBACK;
    assert(native_model(other_record,0)==7 && sphere_seen==sphere_result);
    copy[0x34]^=1;
    assert(native_model(copy,1)==7 && sphere_seen==sphere_result);
    copy[0x34]^=1;
    nested_draw=1;
    assert(native_model(copy,1)==7 && sphere_seen==0);
    assert(!capture_model && !capture_visibility_planes);
    frame->render_flags=HOTD_RENDER_NATIVE_MESH_CULLING;
    assert(native_model(copy,1)==7 && sphere_seen==sphere_result);

    reset_capture(HOTD_RENDER_EXPAND_WORLD|HOTD_RENDER_NATIVE_MESH_CULLING);
    task[0]=0x1234;native_submit(record);
    assert(stage_records.count==0 && sphere_seen==sphere_result);
    task[0]=(DWORD)(ULONG_PTR)(game_base+0x1260);
    /* Shot effects, traveling tracers and black overlays cannot borrow stage tags. */
    *(unsigned short *)(game_base+0x179F78)=100;
    *(DWORD *)(game_base+0x5A66A4+100*16)=*(DWORD *)(record+0xC);
    *(float *)(record+0x34+14*4)=-1;native_submit(record);
    assert(sphere_seen==sphere_result && (capture_kind==0));
    *(float *)(record+0x34+14*4)=-3;native_submit(record);
    assert(sphere_seen==sphere_result);
    *(DWORD *)(game_base+0x5A66A4+100*16)=0;
    reset_capture(HOTD_RENDER_EXPAND_WORLD|HOTD_RENDER_NATIVE_MESH_CULLING);
    memset(record,0,sizeof(record));*(DWORD *)(record+0xC)=0x98765432;
    *(DWORD *)(game_base+0x5A66A4+0x93E*16)=*(DWORD *)(record+0xC);
    for(unsigned i=0;i<16;i++)((float *)(record+0x34))[i]=i%5==0?1:0;
    *(float *)(record+0x34+13*4)=.4f;*(float *)(record+0x34+14*4)=-1;
    native_submit(record);assert(sphere_seen==sphere_result && count==0);

    reset_capture(HOTD_RENDER_EXPAND_WORLD|HOTD_RENDER_NATIVE_MESH_CULLING);
    for(unsigned i=0;i<HOTD_STAGE_RECORDS;i++) {
        *(DWORD *)(other_record+0xC)=i+1;remember_stage_record(other_record);
    }
    assert(stage_records.count==HOTD_STAGE_RECORDS && !stage_records.overflow);
    *(DWORD *)(other_record+0xC)=1;remember_stage_record(other_record);
    assert(stage_records.count==HOTD_STAGE_RECORDS && !stage_records.overflow);
    *(DWORD *)(other_record+0xC)=999;remember_stage_record(other_record);
    assert(stage_records.overflow && !hotd_stage_record(&stage_records,other_record));
    *(DWORD *)(other_record+0xC)=1;
    assert(native_model(other_record,1)==7 && sphere_seen==sphere_result);
    assert(!native_stage_visibility(0,1,0x1380));
    reset_capture(HOTD_RENDER_EXPAND_WORLD);
    for(unsigned i=0;i<3;i++)assert(native_stage_visibility(0,1,(uintptr_t[]){0x1380,0x13CC,0x1413}[i]));
    assert(!native_stage_visibility(0,1,0x1381));
    assert(!native_stage_visibility(0,0,0x1380));
    assert(!native_stage_visibility(0,NAN,0x1380));
    frame->render_flags=0;assert(!native_stage_visibility(0,1,0x1380));
    assert(native_stage_visibility(1,1,0x1381));
    assert(sphere_calls>20);
    VirtualFree(frame,0,MEM_RELEASE);VirtualFree(game_base,0,MEM_RELEASE);
    puts("Capture visibility checks passed.");
    return 0;
}
