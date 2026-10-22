/* One-shot diagnostics for the verified chapter-4 bat models; no render-state writes. */
#ifndef HOTD_BAT_TRACE_H
#define HOTD_BAT_TRACE_H

typedef struct {
    uint32_t frame,model,object,pass,primitive,fvf,input_total,output_total;
    uint32_t input_count,output_count,first_vertex,texture,truncated;
    BYTE native_record[116];
    D3DMATRIX projection;
    HotdLighting lighting;
    float input[256][8];
    Vc2Vertex output[768];
} HotdBatRecord;
typedef struct { unsigned model,object,pass;BYTE record[116]; } HotdBatScope;
static HotdBatScope bat_scope;
static struct {
    HotdBatRecord *records;
    DWORD *screens;
    unsigned count,frames,last_frame,done,screen_frames[4],screen_w[4],screen_h[4];
    char path[MAX_PATH-32];
} bat_trace;

static void bat_trace_init(void)
{
    if(!GetPrivateProfileIntA("bridge","trace_bats",0,ini_path))return;
    char *slash=strrchr(ini_path,'\\');
    if(!slash || slash-ini_path>MAX_PATH-80)return;
    snprintf(bat_trace.path,sizeof(bat_trace.path),"%.*s\\HOTD2BatTrace-%lu-%lu",
        (int)(slash-ini_path),ini_path,(unsigned long)GetCurrentProcessId(),(unsigned long)GetTickCount());
    bat_trace.records=calloc(256,sizeof(HotdBatRecord));
    bat_trace.screens=calloc(4*640*480,sizeof(DWORD));
    if(!bat_trace.records || !bat_trace.screens) {
        free(bat_trace.records);free(bat_trace.screens);bat_trace.records=NULL;bat_trace.screens=NULL;
        logline("bat trace allocation failed; diagnostics disabled");return;
    }
    logline("bat trace armed: chapter=4 scene=0 frames=4 stride=15 draws<=256 path=%s",bat_trace.path);
}
static int bat_trace_location(void)
{
    return *(DWORD *)(game_base+0x5C8E98)==6 && !*(DWORD *)(game_base+0x5CA08C) &&
        *(short *)(game_base+0x5A1A08)==3 && !*(short *)(game_base+0x5A2BC0) &&
        !*(short *)(game_base+0x5A1A00);
}
static HotdBatScope bat_trace_scope(const BYTE *record,int pass)
{
    HotdBatScope scope={0};DWORD model;
    if(!bat_trace.records || !bat_trace_location())return scope;
#ifdef HOTD_BAT_BOT
    if(!hotd_bat_bot_ready())return scope;
#endif
    memcpy(&model,record+12,4);
    /* These slots are verified against zabat/zabat_wing in the supported executable. */
    for(unsigned id=0x1B01;id<=0x1B09;id++)if(model && model==*(DWORD *)(game_base+0x5A66A4+id*16)) {
        scope.model=id;scope.object=(DWORD)(uintptr_t)record;scope.pass=pass;
        memcpy(scope.record,record,sizeof(scope.record));break;
    }
    return scope;
}
static void bat_trace_draw(IDirect3DDevice7 *device,D3DPRIMITIVETYPE primitive,DWORD fvf,
    const void *input,unsigned input_count,const Vc2Vertex *output,unsigned output_count,unsigned first)
{
    unsigned current=frame_no+1;
    if(!bat_trace.records || !bat_scope.model || fvf!=0x112 || bat_trace.count==256)return;
    if(current!=bat_trace.last_frame) {
        if(bat_trace.frames==4 || (bat_trace.frames && current-bat_trace.last_frame<15))return;
        bat_trace.last_frame=current;bat_trace.frames++;
    }
    HotdBatRecord *r=bat_trace.records+bat_trace.count++;
    r->frame=current;r->model=bat_scope.model;r->object=bat_scope.object;r->pass=bat_scope.pass;
    r->primitive=primitive;r->fvf=fvf;r->input_total=input_count;r->output_total=output_count;r->first_vertex=first;
    r->input_count=input_count<256?input_count:256;r->output_count=output_count<768?output_count:768;
    r->truncated=r->input_count!=input_count || r->output_count!=output_count;
    memcpy(r->native_record,bat_scope.record,sizeof(r->native_record));
    if(!output_count)read_camera(device);
    r->projection=projection;
    if(!output_count || (frame->render_flags&HOTD_RENDER_UNLIT))read_lighting(device,fvf);
    r->lighting=draw_lighting;
    IDirectDrawSurface7 *surface=NULL;
    if(SUCCEEDED(IDirect3DDevice7_GetTexture(device,0,&surface)) && surface) {
        r->texture=(DWORD)(uintptr_t)surface;IDirectDrawSurface7_Release(surface);
    }
    memcpy(r->input,input,r->input_count*sizeof(r->input[0]));
    memcpy(r->output,output,r->output_count*sizeof(r->output[0]));
}
static int bat_trace_write(void)
{
    char path[MAX_PATH];FILE *file;int ok;
    snprintf(path,sizeof(path),"%s.bin",bat_trace.path);file=fopen(path,"wb");if(!file)return 0;
    ok=fwrite(bat_trace.records,sizeof(HotdBatRecord),bat_trace.count,file)==bat_trace.count;
    if(fclose(file))ok=0;
    if(!ok)return 0;
    snprintf(path,sizeof(path),"%s.json",bat_trace.path);file=fopen(path,"w");if(!file)return 0;
    ok=fprintf(file,"{\"version\":1,\"stage\":4,\"scene\":0,\"count\":%u,\"frames\":%u,\"record_size\":%u,"
        "\"offsets\":{\"native_record\":%u,\"projection\":%u,\"lighting\":%u,\"input\":%u,\"output\":%u},"
        "\"lighting_offsets\":{\"material\":%u,\"world\":%u,\"view\":%u,\"lights\":%u,\"light_count\":%u,"
        "\"ambient\":%u,\"normal_matrix\":%u},\"light_size\":%u}\n",
        bat_trace.count,bat_trace.frames,(unsigned)sizeof(HotdBatRecord),
        (unsigned)offsetof(HotdBatRecord,native_record),(unsigned)offsetof(HotdBatRecord,projection),
        (unsigned)offsetof(HotdBatRecord,lighting),(unsigned)offsetof(HotdBatRecord,input),(unsigned)offsetof(HotdBatRecord,output),
        (unsigned)offsetof(HotdLighting,material),(unsigned)offsetof(HotdLighting,world),(unsigned)offsetof(HotdLighting,view),
        (unsigned)offsetof(HotdLighting,lights),(unsigned)offsetof(HotdLighting,light_count),
        (unsigned)offsetof(HotdLighting,ambient),(unsigned)offsetof(HotdLighting,normal_matrix),(unsigned)sizeof(D3DLIGHT7))>0;
    if(fclose(file))ok=0;
    for(unsigned i=0;i<bat_trace.frames && ok;i++)if(bat_trace.screen_w[i]) {
        BITMAPFILEHEADER header={0};BITMAPINFOHEADER info={0};
        unsigned pixels=bat_trace.screen_w[i]*bat_trace.screen_h[i];
        header.bfType=0x4D42;header.bfOffBits=sizeof(header)+sizeof(info);header.bfSize=header.bfOffBits+pixels*4;
        info.biSize=sizeof(info);info.biWidth=bat_trace.screen_w[i];info.biHeight=-(LONG)bat_trace.screen_h[i];
        info.biPlanes=1;info.biBitCount=32;
        snprintf(path,sizeof(path),"%s-frame-%u.bmp",bat_trace.path,bat_trace.screen_frames[i]);
        file=fopen(path,"wb");if(!file){ok=0;break;}
        ok=fwrite(&header,sizeof(header),1,file)==1 && fwrite(&info,sizeof(info),1,file)==1 &&
            fwrite(bat_trace.screens+i*640*480,4,pixels,file)==pixels;
        if(fclose(file))ok=0;
    }
    return ok;
}
static void bat_trace_frame(int screen_fresh)
{
    if(!bat_trace.records || !bat_trace.count)return;
    if(frame->frame==bat_trace.last_frame && screen_fresh && frame->screen_ok &&
       frame->screen_w && frame->screen_h && frame->screen_w<=640 && frame->screen_h<=480) {
        unsigned i=bat_trace.frames-1;bat_trace.screen_frames[i]=frame->frame;
        bat_trace.screen_w[i]=frame->screen_w;bat_trace.screen_h[i]=frame->screen_h;
        memcpy(bat_trace.screens+i*640*480,frame->screen,frame->screen_w*frame->screen_h*4);
    }
    if(bat_trace.frames==4 || bat_trace.count==256 || !bat_trace_location()) {
        int ok=bat_trace_write();
        logline("bat trace %s: frames=%u draws=%u path=%s",ok?"saved":"write failed",bat_trace.frames,bat_trace.count,bat_trace.path);
        free(bat_trace.records);free(bat_trace.screens);bat_trace.records=NULL;bat_trace.screens=NULL;bat_trace.done=1;
    }
}
#endif
