/* Actual bridge callbacks and native-style deferred queues. No game starts. */
#include <assert.h>
#include "../src/bridge.c"
static DWORD sphere_result,sphere_seen;
static HRESULT sphere_hr=DD_OK;
static unsigned immediate_models,immediate_sprites,submit_calls,append_calls;
static int nested_draw;
static int deferred_result=1,last_submit_result;
static BYTE other_record[HOTD_MODEL_RECORD_SIZE];
static HRESULT WINAPI fake_spheres(IDirect3DDevice7 *self,D3DVECTOR *centres,D3DVALUE *radii,DWORD n,DWORD flags,DWORD *out)
{(void)self;(void)centres;(void)radii;(void)flags;for(DWORD i=0;i<n;i++)out[i]=sphere_result;return sphere_hr;}
static HRESULT WINAPI fake_transform(IDirect3DDevice7 *self,D3DTRANSFORMSTATETYPE state,D3DMATRIX *out)
{(void)self;(void)state;memset(out,0,sizeof(*out));out->_11=out->_22=out->_33=out->_44=1;return DD_OK;}
static HRESULT WINAPI fake_viewport(IDirect3DDevice7 *self,D3DVIEWPORT7 *out)
{(void)self;memset(out,0,sizeof(*out));out->dwWidth=640;out->dwHeight=480;return DD_OK;}
static HRESULT WINAPI fake_state(IDirect3DDevice7 *self,D3DRENDERSTATETYPE state,DWORD *out)
{(void)self;*out=state==D3DRENDERSTATE_CULLMODE?D3DCULL_CW:state==D3DRENDERSTATE_ZENABLE || state==D3DRENDERSTATE_ZWRITEENABLE;return DD_OK;}
static HRESULT WINAPI fake_stage(IDirect3DDevice7 *self,DWORD stage,D3DTEXTURESTAGESTATETYPE state,DWORD *out)
{(void)self;(void)stage;(void)state;*out=1;return DD_OK;}
static HRESULT WINAPI fake_texture(IDirect3DDevice7 *self,DWORD stage,IDirectDrawSurface7 **out)
{(void)self;(void)stage;*out=NULL;return DD_OK;}
static HRESULT WINAPI fake_material(IDirect3DDevice7 *self,D3DMATERIAL7 *out)
{(void)self;memset(out,0,sizeof(*out));out->diffuse.r=out->diffuse.g=out->diffuse.b=out->diffuse.a=1;return DD_OK;}
static HRESULT WINAPI fake_begin(IDirect3DDevice7 *self){(void)self;return DD_OK;}
static HRESULT WINAPI no_target(IDirect3DDevice7 *self,IDirectDrawSurface7 **out){(void)self;(void)out;return E_FAIL;}
static int fake_model(const BYTE *record,int pass)
{
    D3DVECTOR centre={0};D3DVALUE radius=1;DWORD visible;
    float mesh[3][8]={{-1,-1,-5,0,0,1,0,0},{1,-1,-5,0,0,1,1,0},{0,1,-5,0,0,1,0,1}};
    (void)record;if(!pass)immediate_models++;
    assert(SUCCEEDED(IDirect3DDevice7_ComputeSphereVisibility(active_device,&centre,&radius,1,0,&visible)));
    sphere_seen=visible;
    if(!(visible&0x00FFF000u))capture_draw(active_device,D3DPT_TRIANGLELIST,0x112,mesh,3,NULL,0);
    return pass?0:deferred_result; /* Native callers test only AL. */
}
static int __cdecl normal_model(const BYTE *record,int pass)
{
    if(nested_draw){DWORD saved=capture_visibility_planes;nested_draw=0;native_model_alt(other_record,pass);assert(capture_visibility_planes==saved);}
    return fake_model(record,pass);
}
static int __cdecl alternate_model(const BYTE *record,int pass){return fake_model(record,pass);}
static void fake_append(const BYTE *record,unsigned size,int alternate,float alpha)
{
    HotdNativeQueue q=native_capture_queue();BYTE *copy=(BYTE *)q.cursor;
    /* Native appends are unguarded; a missed bridge guard corrupts this fixture too. */
    append_calls++;memcpy(copy,record,size);*(DWORD *)copy|=0x40000000;*(float *)(copy+4)=77;
    if(alternate)*(float *)(copy+0x10)=alpha;
    *(DWORD *)(q.list+q.count*4)=(DWORD)(ULONG_PTR)copy;
    *(DWORD *)(game_base+0x3E78C0)=(DWORD)(ULONG_PTR)(copy+size);*(DWORD *)(game_base+0x3E793C)=q.count+1;
}
static void __cdecl submit_model(const BYTE *record)
{submit_calls++;if((*(const DWORD *)record&0x80000000u)||(BYTE)(last_submit_result=native_model(record,0)))fake_append(record,HOTD_MODEL_RECORD_SIZE,0,0);}
static void __cdecl submit_alternate(const BYTE *record,float alpha)
{submit_calls++;if((*(const DWORD *)record&0x80000000u)||(BYTE)(last_submit_result=native_model_alt(record,0)))fake_append(record,HOTD_MODEL_RECORD_SIZE,1,alpha);}
static void __cdecl submit_sprite(const BYTE *record)
{submit_calls++;if(*(const DWORD *)record&0x80000000u)fake_append(record,0x3C,0,0);else immediate_sprites++;}
static void __cdecl passthrough_submit(const BYTE *record){(void)record;submit_calls++;}
static void __cdecl passthrough_alt(const BYTE *record,float alpha){(void)record;(void)alpha;submit_calls++;}
static void reset_queue(unsigned capacity)
{
    uintptr_t list=(uintptr_t)game_base+0x3E814C;
    *(DWORD *)(game_base+0x3E78C4)=(DWORD)list;*(DWORD *)(game_base+0x3E78C8)=capacity;
    *(DWORD *)(game_base+0x3E793C)=0;*(DWORD *)(game_base+0x3E78C0)=(DWORD)(list+4*((capacity+3)&~3u));
}
static BYTE *queued(unsigned index){return (BYTE *)(ULONG_PTR)(*(DWORD *)(*(DWORD *)(game_base+0x3E78C4)+index*4));}
static void reset_capture(DWORD flags)
{
    frame->render_flags=flags;frame->units_per_metre=10;count=calls=dropped=hud_count=0;visibility_flags_latched=0;
    memset(&stage_records,0,sizeof(stage_records));visibility_queue_drops=0;
    memset(&gfile_records,0,sizeof(gfile_records));
    immediate_models=immediate_sprites=submit_calls=append_calls=0;
    assert(!capture_model && !capture_visibility_planes && !visibility_submit_record);reset_queue(1024);
}
static void test_signatures(void)
{
    static const unsigned offsets[]={0xA7F81,0xA84C1,0xA7E50,0xA8390,0xA7A40,0x137B,0x13C7,0x140E,0x6310,0xA7A10};
    static const unsigned lengths[]={8,8,5,5,5,10,10,10,9,11};
    static const BYTE mesh[]={0xF7,0x44,0x24,0x18,0,0xF0,0xFF,0};
    static const BYTE submit[]={0x56,0x8B,0x74,0x24,0x08};
    static const BYTE gates[3][10]={{0xE8,0x90,0x4F,0,0,0x83,0xC4,0x10,0x85,0xC0},{0xE8,0x44,0x4F,0,0,0x83,0xC4,0x10,0x85,0xC0},{0xE8,0xFD,0x4E,0,0,0x83,0xC4,0x10,0x85,0xC0}};
    static const BYTE sphere[]={0x83,0xEC,0x38,0xD9,0x05,0xCC,0x49,0x4C,0};
    static const BYTE queue[]={0xA1,0xC8,0x78,0x7E,0,0x8B,0x0D,0xC4,0x78,0x7E,0};
    assert(!install_capture_visibility() && !visibility_hooks_ready && !visibility_ready);
    memcpy(game_base+offsets[0],mesh,sizeof(mesh));memcpy(game_base+offsets[1],mesh,sizeof(mesh));
    for(unsigned i=2;i<5;i++)memcpy(game_base+offsets[i],submit,sizeof(submit));
    for(unsigned i=0;i<3;i++)memcpy(game_base+offsets[5+i],gates[i],sizeof(gates[i]));
    memcpy(game_base+offsets[8],sphere,sizeof(sphere));memcpy(game_base+offsets[9],queue,sizeof(queue));
    assert(hotd_visibility_profile(game_base));
    for(unsigned i=0;i<10;i++)for(unsigned b=0;b<lengths[i];b++){game_base[offsets[i]+b]^=1;assert(!hotd_visibility_profile(game_base));game_base[offsets[i]+b]^=1;}
}
static void test_queue_bounds(void)
{
    uintptr_t list=(uintptr_t)game_base+0x3E814C;HotdNativeQueue q={list,list+16,0,4},bad;
    assert(hotd_native_queue_room(q,list));q.count=4;q.cursor+=4*HOTD_MODEL_RECORD_SIZE;
    assert(hotd_native_queue_valid(q,list)&&!hotd_native_queue_room(q,list));q.count=3;q.cursor=list+16+3*0x3C;
    assert(hotd_native_queue_room(q,list));
    bad=q;bad.list+=4;assert(!hotd_native_queue_valid(bad,list));
    bad=q;bad.capacity=0;assert(!hotd_native_queue_valid(bad,list));
    bad=q;bad.capacity=1025;assert(!hotd_native_queue_valid(bad,list));
    bad=q;bad.count=5;assert(!hotd_native_queue_valid(bad,list));
    bad=q;bad.cursor=list+16;assert(!hotd_native_queue_valid(bad,list));
    bad=q;bad.cursor+=1;assert(!hotd_native_queue_valid(bad,list));
    bad=q;bad.cursor=list+16+3*HOTD_MODEL_RECORD_SIZE+4;assert(!hotd_native_queue_valid(bad,list));
    bad=(HotdNativeQueue){UINTPTR_MAX-3,0,0,4};assert(!hotd_native_queue_valid(bad,bad.list));
}
static void test_gfile_capture(BYTE *record,DWORD *task)
{
    const unsigned handlers[]={0x37AC0,0x24900};
    const BYTE signatures[2][16]={
        {0x8B,0x44,0x24,0x04,0x83,0xEC,0x4C,0x53,0x8B,0x98,0x0C,0x13,0,0,0x55,0x8B},
        {0x83,0xEC,0x44,0x53,0x55,0x8B,0x6C,0x24,0x50,0x33,0xDB,0x56,0x57,0x66,0x8B,0x8D}};
    assert(!native_gfile_profile());
    for(unsigned h=0;h<2;h++)memcpy(game_base+handlers[h],signatures[h],16);
    assert(native_gfile_profile());
    for(unsigned h=0;h<2;h++)for(unsigned b=0;b<16;b++) {
        game_base[handlers[h]+b]^=1;assert(!native_gfile_profile());game_base[handlers[h]+b]^=1;
    }
    gfile_ready=1;sphere_result=0;deferred_result=1;
    for(unsigned h=0;h<2;h++)for(unsigned alt=0;alt<2;alt++) {
        BYTE alias[HOTD_MODEL_RECORD_SIZE];reset_capture(HOTD_RENDER_NATIVE_MESH_CULLING);
        task[0]=(DWORD)(ULONG_PTR)(game_base+handlers[h]);
        if(alt)native_submit_alt(record,.5f);else native_submit(record);
        assert(count==3 && (vertices[0].address&HOTD_GFILE) && !(vertices[0].style&HOTD_WORLD));
        assert(gfile_records.count==1 && !capture_gfile && !gfile_immediate);
        BYTE *copy=queued(0);memcpy(alias,copy,sizeof(alias));task[0]=0x1234;count=0;
        native_model(alias,1);assert(count==3 && !(vertices[0].address&HOTD_GFILE) && (vertices[0].style&HOTD_WORLD));
        assert(gfile_records.count==1);count=0;
        if(alt)native_model_alt(copy,1);else native_model(copy,1);
        assert(count==3 && (vertices[0].address&HOTD_GFILE) && !(vertices[0].style&HOTD_WORLD));
        assert(!gfile_records.count && vertices[0].z==5); /* Transport keeps native geometry and native depth state. */
        assert(!(vertices[0].style&HOTD_NO_ZWRITE));count=0;
        native_model(copy,1);assert(!(vertices[0].address&HOTD_GFILE));
    }
    reset_capture(0);task[0]=(DWORD)(ULONG_PTR)(game_base+handlers[0]);nested_draw=1;
    native_submit(record);assert(count==6 && !(vertices[0].address&HOTD_GFILE) && (vertices[3].address&HOTD_GFILE));
    assert(!capture_gfile && !gfile_immediate);
    BYTE *copy=queued(0);*(DWORD *)copy^=0x04000000;count=0;
    native_model(copy,1);assert(!(vertices[0].address&HOTD_GFILE) && !gfile_records.count);
    reset_capture(0);gfile_ready=0;native_submit(record);
    assert(!(vertices[0].address&HOTD_GFILE) && !gfile_records.count);
    gfile_ready=1;task[0]=0x1234;reset_capture(0);native_submit(record);
    assert(!(vertices[0].address&HOTD_GFILE) && !gfile_records.count);
    task[0]=(DWORD)(ULONG_PTR)(game_base+handlers[0]);native_submit(record);assert(gfile_records.count==1);
    publish_frame();assert(!gfile_records.count);gfile_ready=0;
}
int main(void)
{
    IDirect3DDevice7Vtbl vt={.ComputeSphereVisibility=sphere_visibility,.GetTransform=fake_transform,.GetViewport=fake_viewport,
        .GetRenderState=fake_state,.GetTextureStageState=fake_stage,.GetTexture=fake_texture,.GetMaterial=fake_material,.GetRenderTarget=no_target};
    IDirect3DDevice7 device={&vt};BYTE record[HOTD_MODEL_RECORD_SIZE]={0},actor[HOTD_MODEL_RECORD_SIZE],*copy;DWORD task[3]={0};
    game_base=VirtualAlloc(NULL,0x5C9000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    frame=VirtualAlloc(NULL,sizeof(*frame),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(game_base&&frame);
    active_device=&device;original_sphere_visibility=fake_spheres;native_model_original=normal_model;native_model_alt_original=alternate_model;
    native_submit_original=submit_model;native_submit_alt_original=submit_alternate;native_sprite_submit_original=submit_sprite;original_begin=fake_begin;
    test_signatures();test_queue_bounds();visibility_ready=1;
    *(DWORD *)(record+0xC)=0x12345678;*(DWORD *)(other_record+0xC)=0x87654321;
    *(DWORD **)(game_base+0x3DF8B0)=task;task[0]=(DWORD)(ULONG_PTR)(game_base+0x1260);
    reset_capture(HOTD_RENDER_NATIVE_MESH_CULLING);sphere_result=D3DSTATUS_CLIPINTERSECTIONLEFT;
    assert(native_model(record,0)==1&&sphere_seen==sphere_result&&count==0);reset_capture(0);
    assert(native_model(record,0)==1&&sphere_seen==0&&count==3);
    assert((vertices[0].style&HOTD_WORLD)&&vertices[0].z==5);
    assert((vertices[0].style&HOTD_CULL_MASK)==((DWORD)D3DCULL_CW<<HOTD_CULL_SHIFT));
    assert(native_model_alt(record,1)==0&&sphere_seen==sphere_result&&count==3);
    for(unsigned bit=12;bit<24;bit++){sphere_result=1u<<bit;count=0;native_model(record,0);assert(sphere_seen==(bit<16?0:sphere_result)&&count==(bit<16?3u:0u));}
    {D3DVECTOR centre={0};D3DVALUE radius=1;DWORD visible;sphere_result=0x80000000|D3DSTATUS_CLIPINTERSECTIONLEFT;
        native_model(record,0);assert(sphere_seen==0x80000000);
        assert(SUCCEEDED(sphere_visibility(&device,&centre,&radius,1,0,&visible))&&visible==sphere_result);
        capture_model=1;capture_visibility_planes=0x3F000;sphere_hr=E_FAIL;
        assert(FAILED(sphere_visibility(&device,&centre,&radius,1,0,&visible))&&visible==sphere_result);
        sphere_hr=DD_OK;capture_model=0;capture_visibility_planes=0;}
    visibility_ready=0;sphere_result=D3DSTATUS_CLIPINTERSECTIONLEFT;native_model(record,0);assert(sphere_seen==sphere_result);visibility_ready=1;
    /* Equal actor values, including changed lighting keys, cannot borrow stage provenance. */
    reset_capture(HOTD_RENDER_EXPAND_WORLD|HOTD_RENDER_NATIVE_MESH_CULLING);sphere_result=D3DSTATUS_CLIPINTERSECTIONBACK;
    native_submit(record);assert(count==3&&stage_records.count==1&&stage_records.accepted==1);copy=queued(0);
    assert(copy!=record&&*(DWORD *)copy!=*(DWORD *)record&&hotd_stage_record(&stage_records,copy));
    memcpy(actor,copy,sizeof(actor));task[0]=0x1234;native_submit(actor);
    assert(sphere_seen==sphere_result&&count==3&&stage_records.count==1);
    *(DWORD *)actor^=0x04000000;native_model(actor,1);assert(sphere_seen==sphere_result&&stage_records.count==1);
    native_model(copy,1);assert(sphere_seen==0&&count==6&&stage_records.count==0);
    native_model(copy,1);assert(sphere_seen==sphere_result&&count==6);
    reset_capture(HOTD_RENDER_EXPAND_WORLD|HOTD_RENDER_NATIVE_MESH_CULLING);task[0]=(DWORD)(ULONG_PTR)(game_base+0x1260);
    native_submit_alt(record,.5f);copy=queued(0);assert(*(float *)(copy+0x10)==.5f&&stage_records.count==1&&count==3);
    native_model_alt(copy,1);assert(sphere_seen==0&&count==6&&!stage_records.count);
    for(unsigned bit=12;bit<24;bit++){reset_capture(HOTD_RENDER_EXPAND_WORLD|HOTD_RENDER_NATIVE_MESH_CULLING);sphere_result=1u<<bit;
        native_submit_alt(record,.5f);count=0;native_model_alt(queued(0),1);assert(sphere_seen==(bit<18?0:sphere_result)&&count==(bit<18?3u:0u));}
    reset_capture(HOTD_RENDER_EXPAND_WORLD|HOTD_RENDER_NATIVE_MESH_CULLING);sphere_result=D3DSTATUS_CLIPINTERSECTIONBACK;
    native_submit(record);copy=queued(0);*(DWORD *)copy^=0x04000000;native_model(copy,1);assert(sphere_seen==sphere_result&&!stage_records.count);
    *(DWORD *)copy^=0x04000000;native_model(copy,1);assert(sphere_seen==sphere_result);
    nested_draw=1;native_submit(record);assert(!capture_model&&!capture_visibility_planes);
    /* Overflow rejects newcomers but preserves accepted transparent copies. */
    {BYTE records[HOTD_STAGE_RECORDS+1][HOTD_MODEL_RECORD_SIZE];reset_capture(HOTD_RENDER_EXPAND_WORLD|HOTD_RENDER_NATIVE_MESH_CULLING);
        memset(records,0,sizeof(records));for(unsigned i=0;i<HOTD_STAGE_RECORDS;i++){*(DWORD *)(records[i]+0xC)=i+1;assert(hotd_remember_stage(&stage_records,records[i]));}
        assert(!hotd_remember_stage(&stage_records,records[HOTD_STAGE_RECORDS])&&stage_records.overflow);
        native_model(records[0],1);assert(sphere_seen==0&&stage_records.count==HOTD_STAGE_RECORDS-1);
        native_model(records[HOTD_STAGE_RECORDS],1);assert(sphere_seen==sphere_result);assert(!native_stage_visibility(0,1,0x1380));}
    /* Later actor/model/ALT/sprite appends cannot overwrite the pointer table. */
    reset_capture(HOTD_RENDER_EXPAND_WORLD|HOTD_RENDER_NATIVE_MESH_CULLING);reset_queue(4);
    for(unsigned i=0;i<3;i++)fake_append(record,HOTD_MODEL_RECORD_SIZE,0,0);
    native_submit(record);copy=queued(3);assert(native_capture_queue().count==4&&stage_records.count==1);
    {HotdNativeQueue full=native_capture_queue();BYTE first[HOTD_MODEL_RECORD_SIZE];memcpy(first,queued(0),sizeof(first));task[0]=0x1234;
        unsigned im=immediate_models,app=append_calls;native_submit(record);native_submit_alt(record,.5f);
        assert(immediate_models==im+2&&append_calls==app&&native_capture_queue().cursor==full.cursor);
        *(DWORD *)actor=0x80000000;native_submit(actor);native_submit_alt(actor,.5f);native_sprite_submit(actor);
        assert(immediate_models==im+2&&append_calls==app&&visibility_queue_drops==5);
        *(DWORD *)actor=0;native_sprite_submit(actor);assert(immediate_sprites==1);
        assert(!memcmp(first,queued(0),sizeof(first))&&native_capture_queue().count==4);}
    {
        unsigned drops=visibility_queue_drops,appends=append_calls;
        deferred_result=0x100;native_submit(record);assert(last_submit_result==0x100&&visibility_queue_drops==drops&&append_calls==appends);
        native_submit_alt(record,.5f);assert(last_submit_result==0x100&&visibility_queue_drops==drops&&append_calls==appends);
        deferred_result=0x101;native_submit(record);assert(last_submit_result==0&&visibility_queue_drops==drops+1&&append_calls==appends);
        native_submit_alt(record,.5f);assert(last_submit_result==0&&visibility_queue_drops==drops+2&&append_calls==appends);
        deferred_result=1;
    }
    native_model(copy,1);assert(sphere_seen==0&&!stage_records.count);
    for(unsigned broken=0;broken<6;broken++){reset_capture(HOTD_RENDER_EXPAND_WORLD|HOTD_RENDER_NATIVE_MESH_CULLING);
        if(broken==0)*(DWORD *)(game_base+0x3E78C8)=0;
        if(broken==1)*(DWORD *)(game_base+0x3E78C8)=1025;
        if(broken==2)*(DWORD *)(game_base+0x3E793C)=1025;
        if(broken==3)*(DWORD *)(game_base+0x3E78C0)+=1;
        if(broken==4)*(DWORD *)(game_base+0x3E78C4)+=4;
        if(broken==5)*(DWORD *)(game_base+0x3E78C0)-=4;
        *(DWORD *)actor=0x80000000;native_submit(actor);native_submit_alt(actor,.5f);native_sprite_submit(actor);
        assert(!submit_calls&&!append_calls&&visibility_queue_drops==3);
        *(DWORD *)actor=0;native_submit(actor);native_submit_alt(actor,.5f);native_sprite_submit(actor);
        assert(immediate_models==2&&immediate_sprites==1&&!append_calls&&visibility_queue_drops==5);assert(!native_stage_visibility(0,1,0x1380));}
    /* World-off does not read queue metadata at all, even when its page is inaccessible. */
    reset_capture(0);
    {DWORD old,unused;assert(VirtualProtect(game_base+0x3E7000,0x1000,PAGE_NOACCESS,&old));
        native_model(record,0);assert(sphere_seen==sphere_result);
        native_submit_original=passthrough_submit;native_submit_alt_original=passthrough_alt;native_sprite_submit_original=passthrough_submit;
        *(DWORD *)actor=0x80000000;native_submit(actor);native_submit_alt(actor,.5f);native_sprite_submit(actor);assert(submit_calls==3&&!visibility_queue_drops);
        assert(VirtualProtect(game_base+0x3E7000,0x1000,old,&unused));native_submit_original=submit_model;
        native_submit_alt_original=submit_alternate;native_sprite_submit_original=submit_sprite;}
    /* Policy remains coherent across a frame's opaque and transparent passes. */
    reset_capture(HOTD_RENDER_EXPAND_WORLD|HOTD_RENDER_NATIVE_MESH_CULLING);task[0]=(DWORD)(ULONG_PTR)(game_base+0x1260);
    begin_scene(&device);frame->render_flags=HOTD_RENDER_NATIVE_MESH_CULLING;begin_scene(&device);
    native_submit(record);copy=queued(0);native_model(copy,1);assert(sphere_seen==0&&stage_records.accepted==1);
    game_window=GetDesktopWindow();publish_frame();assert(!visibility_flags_latched&&!stage_records.count);
    begin_scene(&device);native_model(record,0);assert(sphere_seen==sphere_result);visibility_flags_latched=0;
    /* Sustained pressure logs transitions and 300-frame summaries, not every frame. */
    reset_capture(0);frame_no=visibility_pressure=visibility_last_pressure_log=0;logfile=tmpfile();assert(logfile);
    for(unsigned f=0;f<1000;f++){stage_records.overflow=1;visibility_queue_drops=1;publish_frame();}
    publish_frame();rewind(logfile);
    {char line[1024];unsigned lines=0;while(fgets(line,sizeof(line),logfile))lines++;assert(lines==5);}
    fclose(logfile);logfile=NULL;
    reset_capture(0);frame_no=visibility_pressure=visibility_last_pressure_log=0;logfile=tmpfile();assert(logfile);
    for(unsigned f=0;f<1000;f++){stage_records.overflow=f&1;visibility_queue_drops=f&1;publish_frame();}
    rewind(logfile);
    {
        char line[1024];unsigned lines=0,previous=0,current;
        while(fgets(line,sizeof(line),logfile)) {
            assert(sscanf(line,"frame=%u",&current)==1);
            if(previous)assert(current%300==0 || current-previous>=60);
            previous=current;lines++;
        }
        assert(lines>4 && lines<=20); /* Useful pressure reports, bounded despite 999 transitions. */
    }
    fclose(logfile);logfile=NULL;
    /* Source task and effect identities keep actors, tracers, shots and black overlays native. */
    reset_capture(HOTD_RENDER_EXPAND_WORLD|HOTD_RENDER_NATIVE_MESH_CULLING);task[0]=0x1234;native_submit(record);
    assert(sphere_seen==sphere_result&&!stage_records.count);task[0]=(DWORD)(ULONG_PTR)(game_base+0x1260);
    *(unsigned short *)(game_base+0x179F78)=100;*(DWORD *)(game_base+0x5A66A4+100*16)=*(DWORD *)(record+0xC);
    *(float *)(record+0x34+14*4)=-1;native_submit(record);assert(sphere_seen==sphere_result);
    *(float *)(record+0x34+14*4)=-3;native_submit(record);assert(sphere_seen==sphere_result);
    *(DWORD *)(game_base+0x5A66A4+100*16)=0;*(DWORD *)(game_base+0x5A66A4+0x93E*16)=*(DWORD *)(record+0xC);
    native_submit(record);assert(sphere_seen==sphere_result);
    reset_capture(HOTD_RENDER_EXPAND_WORLD);
    for(unsigned i=0;i<3;i++)assert(native_stage_visibility(0,1,(uintptr_t[]){0x1380,0x13CC,0x1413}[i]));
    assert(!native_stage_visibility(0,1,0x1381)&&!native_stage_visibility(0,0,0x1380)&&!native_stage_visibility(0,NAN,0x1380));
    frame->render_flags=0;assert(!native_stage_visibility(0,1,0x1380)&&native_stage_visibility(1,1,0x1381));
    test_gfile_capture(record,task);
    VirtualFree(frame,0,MEM_RELEASE);VirtualFree(game_base,0,MEM_RELEASE);
    puts("Capture visibility, G's File immediate/deferred provenance, signature and queue-bound checks passed.");return 0;
}
