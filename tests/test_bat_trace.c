/* Bounded diagnostics against isolated memory; no game or OpenXR session. */
#include <assert.h>
#include "../src/bridge.c"

static HRESULT WINAPI no_texture(IDirect3DDevice7 *self,DWORD stage,IDirectDrawSurface7 **out)
{(void)self;(void)stage;*out=NULL;return DD_OK;}
static unsigned diagnostic_queries;
static HRESULT WINAPI diagnostic_state(IDirect3DDevice7 *self,D3DRENDERSTATETYPE state,DWORD *out)
{(void)self;(void)state;diagnostic_queries++;*out=0;return DD_OK;}
static HRESULT WINAPI diagnostic_stage(IDirect3DDevice7 *self,DWORD stage,D3DTEXTURESTAGESTATETYPE state,DWORD *out)
{(void)self;(void)stage;(void)state;diagnostic_queries++;*out=0;return DD_OK;}
static HRESULT WINAPI diagnostic_material(IDirect3DDevice7 *self,D3DMATERIAL7 *out)
{(void)self;diagnostic_queries++;memset(out,0,sizeof(*out));return DD_OK;}

int main(void)
{
    IDirect3DDevice7Vtbl diagnostic_vt={.GetRenderState=diagnostic_state,
        .GetTextureStageState=diagnostic_stage,.GetMaterial=diagnostic_material};
    IDirect3DDevice7 diagnostic_device={.lpVtbl=&diagnostic_vt};float point[8]={0};
    trace_draw_state(&diagnostic_device,0x112,NULL,point);assert(!diagnostic_queries);
    trace_render_states=1;trace_draw_state(&diagnostic_device,0x112,NULL,point);
    assert(diagnostic_queries==17);trace_render_states=0;
    trace_draw_state(&diagnostic_device,0x112,NULL,point);assert(diagnostic_queries==17);
    char temporary[MAX_PATH],directory[MAX_PATH],path[MAX_PATH];
    assert(GetTempPathA(sizeof(temporary),temporary));
    assert(GetTempFileNameA(temporary,"bat",0,directory));
    assert(DeleteFileA(directory) && CreateDirectoryA(directory,NULL));
    assert(strlen(directory)<MAX_PATH-12);strcpy(ini_path,directory);strcat(ini_path,"\\trace.ini");
    bat_trace_init();assert(!bat_trace.records);
    assert(WritePrivateProfileStringA("bridge","trace_bats","1",ini_path));
    bat_trace_init();assert(bat_trace.records);
    game_base=calloc(1,0x600000);frame=calloc(1,sizeof(*frame));assert(game_base && frame);
    BYTE record[116]={0};DWORD model=0x12345678;
    memcpy(record+12,&model,4);memcpy(game_base+0x5A66A4+0x1B02*16,&model,4);
    assert(!bat_trace_scope(record,1).model);
    *(DWORD *)(game_base+0x5C8E98)=6;*(short *)(game_base+0x5A1A08)=3;
    assert(bat_trace_scope(record,1).model==0x1B02);
    *(short *)(game_base+0x5A1A00)=1;assert(!bat_trace_scope(record,1).model);
    *(short *)(game_base+0x5A1A00)=0;*(short *)(game_base+0x5A2BC0)=1;
    assert(!bat_trace_scope(record,1).model);*(short *)(game_base+0x5A2BC0)=0;
    IDirect3DDevice7Vtbl vt={.GetTexture=no_texture};IDirect3DDevice7 device={.lpVtbl=&vt};
    float input[300][8]={0};Vc2Vertex output[900]={0};
    input[0][0]=12;output[0].x=34;draw_lighting.material.power=8;
    char prefix[MAX_PATH-32];strcpy(prefix,bat_trace.path);
    for(unsigned sample=0;sample<4;sample++) {
        frame_no=sample*15;bat_scope=bat_trace_scope(record,1);
        bat_trace_draw(&device,D3DPT_TRIANGLESTRIP,0x112,input,300,output,900,17);
        assert(bat_trace.count==sample+1 && bat_trace.frames==sample+1);
        assert(input[0][0]==12 && output[0].x==34);
        HotdBatRecord *r=bat_trace.records+sample;
        assert(r->model==0x1B02 && r->input_count==256 && r->output_count==768 && r->truncated);
        assert(r->input[0][0]==12 && r->output[0].x==34 && r->lighting.material.power==8);
        frame_no++;bat_trace_draw(&device,D3DPT_TRIANGLESTRIP,0x112,input,3,output,3,0);
        assert(bat_trace.count==sample+1);frame->frame=frame_no;
        frame->screen_frame=999;frame->screen_ok=1;frame->screen_w=frame->screen_h=1;frame->screen[0]=0xFF123456;
        bat_trace_frame(1);
    }
    assert(!bat_trace.records && bat_trace.done);
    char fixture[MAX_PATH],*slash;assert(GetModuleFileNameA(NULL,fixture,sizeof(fixture)));
    slash=strrchr(fixture,'\\');assert(slash && slash-fixture<MAX_PATH-32);
    snprintf(path,sizeof(path),"%s.bin",prefix);FILE *file=fopen(path,"rb");assert(file);
    assert(!fseek(file,0,SEEK_END) && ftell(file)==4*(long)sizeof(HotdBatRecord));assert(!fclose(file));
    strcpy(slash+1,"bat-trace-fixture.bin");assert(CopyFileA(path,fixture,FALSE) && DeleteFileA(path));
    snprintf(path,sizeof(path),"%s.json",prefix);file=fopen(path,"rb");assert(file);
    char metadata[4096]={0};assert(fread(metadata,1,sizeof(metadata)-1,file)>0 && !fclose(file));
    assert(strstr(metadata,"\"count\":4") && strstr(metadata,"\"stage\":4"));
    strcpy(slash+1,"bat-trace-fixture.json");assert(CopyFileA(path,fixture,FALSE) && DeleteFileA(path));
    for(unsigned sample=0;sample<4;sample++) {
        snprintf(path,sizeof(path),"%s-frame-%u.bmp",prefix,sample*15+1);file=fopen(path,"rb");assert(file);
        DWORD pixel;assert(!fseek(file,sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER),SEEK_SET));
        assert(fread(&pixel,4,1,file)==1 && pixel==0xFF123456 && !fclose(file));assert(DeleteFileA(path));
    }
    assert(DeleteFileA(ini_path) && RemoveDirectoryA(directory));free(game_base);free(frame);
    puts("Bat trace: opt-in/stage/pause guards, sampling/copy bounds, unchanged inputs and one-shot files passed.");
    return 0;
}
