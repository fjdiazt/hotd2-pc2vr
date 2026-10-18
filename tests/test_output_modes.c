/* Independent outputs and preview ownership; no game or OpenXR session. */
#include <assert.h>
#define main viewer_application_main
#include "../src/hotd2vr.c"
#undef main

typedef struct { Vc2Frame *shared;unsigned request,result; } SaveReply;
static DWORD WINAPI checkpoint_reply(void *parameter)
{
    SaveReply *reply=parameter;
    while(reply->shared->checkpoint_request!=reply->request)Sleep(1);
    reply->shared->checkpoint_result=reply->result;
    reply->shared->checkpoint_stage=2;reply->shared->checkpoint_scene=4;
    MemoryBarrier();InterlockedExchange((volatile LONG *)&reply->shared->checkpoint_response,reply->request);
    return 0;
}

int main(int argc,char **argv)
{
    for(int i=1;i<argc;i++)if(!strcmp(argv[i],"--preview-only")) {
        Sleep(INFINITE);return 0; /* Job ownership test child, no renderer. */
    }
    char dir[MAX_PATH],ini[MAX_PATH];
    assert(GetTempPathA(sizeof(dir),dir));
    assert(GetTempFileNameA(dir,"out",0,ini));
    readGraphicsFlags(ini);assert(!g_cfgWindow && g_cfgVrWindow);
    for(int window=0;window<2;window++)for(int vr=0;vr<2;vr++) {
        assert(WritePrivateProfileStringA("vr","window",window?"1":"0",ini));
        assert(WritePrivateProfileStringA("vr","vrwindow",vr?"1":"0",ini));
        readGraphicsFlags(ini);assert(g_cfgWindow==window && g_cfgVrWindow==vr);
        char *args[]={"viewer",window?"--window":"--no-window",vr?"--vrwindow":"--no-vrwindow","--stop-before-bridge"};
        assert(viewer_application_main(4,args)==2);
        assert(g_cfgWindow==window && g_cfgVrWindow==vr);
    }
    char *last[]={"viewer","--window","--vrwindow","--no-window","--no-vrwindow","--stop-before-bridge"};
    assert(viewer_application_main(6,last)==2 && !g_cfgWindow && !g_cfgVrWindow);
    assert(DeleteFileA(ini));

    /* Any accidental publication faults against this read-only frame. */
    g_frame=VirtualAlloc(NULL,sizeof(*g_frame),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(g_frame);g_frame->aim_seq=17;g_frame->aim2_seq=19;g_frame->cheat_tick=23;
    DWORD old;assert(VirtualProtect(g_frame,sizeof(*g_frame),PAGE_READONLY,&old));
    g_previewOnly=1;
    pushCheats();pushAim(1,2,VC2_BTN_FIRE);pushAim2(3,4,VC2_BTN_FIRE);
    assert(g_frame->aim_seq==17 && g_frame->aim2_seq==19 && g_frame->cheat_tick==23);
    assert(VirtualFree(g_frame,0,MEM_RELEASE));g_frame=NULL;g_previewOnly=0;

    Vc2Frame *shared=calloc(1,sizeof(*shared));assert(shared);
    shared->aim_seq=17;shared->cheat_tick=23;shared->cheat_flags=HOTD_CHEAT_AMMO;
    for(unsigned result=HOTD_SAVE_OK;result<=HOTD_SAVE_IO_ERROR;result++) {
        SaveReply reply={shared,shared->checkpoint_request+1,result};
        HANDLE worker=CreateThread(NULL,0,checkpoint_reply,&reply,0,NULL);assert(worker);
        assert(requestCheckpointSave(shared,2000)==(result==HOTD_SAVE_OK?0:1));
        assert(WaitForSingleObject(worker,2000)==WAIT_OBJECT_0);CloseHandle(worker);
        assert(shared->aim_seq==17 && shared->cheat_tick==23 && shared->cheat_flags==HOTD_CHEAT_AMMO);
    }
    assert(requestCheckpointSave(shared,1)==1);free(shared);

    DesktopPreview preview={0};
    assert(openDesktopPreview(&preview,GetCommandLineA()));
    HANDLE child=NULL;
    assert(DuplicateHandle(GetCurrentProcess(),preview.process,GetCurrentProcess(),&child,SYNCHRONIZE,FALSE,0));
    assert(WaitForSingleObject(child,100)==WAIT_TIMEOUT);
    closeDesktopPreview(&preview);
    assert(WaitForSingleObject(child,5000)==WAIT_OBJECT_0);
    assert(!preview.job && !preview.process);CloseHandle(child);
    closeDesktopPreview(&preview);
    puts("Outputs: four INI/CLI combinations, read-only controls and owned preview cleanup passed.");
    return 0;
}
