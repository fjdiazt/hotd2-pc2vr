/* Independent outputs and preview ownership; no game or OpenXR session. */
#include <assert.h>
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../src/startup.h"
static wchar_t output_test_launch_options[256];
static const char *output_test_prepare(const wchar_t *viewer,const wchar_t *game,const wchar_t *options,int launch,int font)
{
    (void)viewer;(void)game;(void)font;
    assert(launch);wcscpy(output_test_launch_options,options);return "fixture stopped before game launch";
}
#define hotdPrepareGame output_test_prepare
#define MessageBoxA(a,b,c,d) 0
static char output_test_share[96];
static HANDLE output_test_open(DWORD access,BOOL inherit,const char *name)
{(void)name;return OpenFileMappingA(access,inherit,output_test_share);}
#define OpenFileMappingA output_test_open
#define main viewer_application_main
#include "../src/hotd2vr.c"
#undef main
#undef OpenFileMappingA
#undef hotdPrepareGame
#undef MessageBoxA

static Vc2Frame *headless_frame;
static unsigned command_action,command_mode;
static DWORD WINAPI observe_command(void *unused)
{
    (void)unused;DWORD deadline=GetTickCount()+4000;
    while((LONG)(deadline-GetTickCount())>0) {
        unsigned request=headless_frame->checkpoint_request;
        if(request!=headless_frame->checkpoint_response) {
            MemoryBarrier();assert(headless_frame->checkpoint_action==command_action && headless_frame->checkpoint_target_mode==command_mode);
            if(command_action==HOTD_CHECKPOINT_SELECT)assert(headless_frame->checkpoint_target_stage==2 && headless_frame->checkpoint_target_scene==3);
            headless_frame->checkpoint_stage=2;headless_frame->checkpoint_scene=3;
            headless_frame->checkpoint_mode=command_mode;headless_frame->checkpoint_result=HOTD_SAVE_OK;
            MemoryBarrier();InterlockedExchange((volatile LONG *)&headless_frame->checkpoint_response,request);return 1;
        }
        Sleep(1);
    }
    return 0;
}
static DWORD WINAPI observe_headless(void *unused)
{
    (void)unused;
    DWORD deadline=GetTickCount()+4000;
    while((LONG)(deadline-GetTickCount())>0) {
        if(headless_frame->cheat_tick && (headless_frame->cheat_flags&HOTD_CHEAT_AMMO) &&
           (DWORD)(GetTickCount()-headless_frame->cheat_tick)<500)return 1;
        Sleep(5);
    }
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

    char *launch[]={"viewer","--no-menu","--no-graphics-hooks","--window","--vrwindow","--stereo",
        "--start-stage","4","--start-scene","0","--continue-game","--no-continue-game","--show-splash"};
    assert(viewer_application_main(sizeof(launch)/sizeof(*launch),launch)==1);
    assert(wcsstr(output_test_launch_options,L"--no-window --no-vrwindow"));
    assert(wcsstr(output_test_launch_options,L"--no-graphics-hooks"));
    assert(wcsstr(output_test_launch_options,L"--start-stage 4 --start-scene 0"));
    assert(wcsstr(output_test_launch_options,L"--no-continue-game"));
    assert(wcsstr(output_test_launch_options,L"--show-splash"));
    assert(!wcsstr(output_test_launch_options,L"--stereo"));

    /* Any accidental publication faults against this read-only frame. */
    g_frame=VirtualAlloc(NULL,sizeof(*g_frame),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(g_frame);g_frame->aim_seq=17;g_frame->aim2_seq=19;g_frame->cheat_tick=23;
    DWORD old;assert(VirtualProtect(g_frame,sizeof(*g_frame),PAGE_READONLY,&old));
    g_previewOnly=1;
    pushCheats();pushAim(1,2,VC2_BTN_FIRE);pushAim2(3,4,VC2_BTN_FIRE);vrMenuNativePublish();
    assert(g_frame->aim_seq==17 && g_frame->aim2_seq==19 && g_frame->cheat_tick==23);
    assert(VirtualFree(g_frame,0,MEM_RELEASE));g_frame=NULL;g_previewOnly=0;

    /* Real headless main against an isolated mapping, never the running game's bridge. */
    snprintf(output_test_share,sizeof(output_test_share),"Local\\HOTD2_Output_Test_%lu",GetCurrentProcessId());
    HANDLE mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(Vc2Frame),output_test_share);
    assert(mapping && GetLastError()!=ERROR_ALREADY_EXISTS);
    headless_frame=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Vc2Frame));assert(headless_frame);
    headless_frame->magic=VC2_SHARE_MAGIC;headless_frame->version=VC2_SHARE_VERSION;
    headless_frame->game_pid=GetCurrentProcessId();headless_frame->aim_seq=17;headless_frame->aim2_seq=19;
    HANDLE observer=CreateThread(NULL,0,observe_headless,NULL,0,NULL);assert(observer);
    char *headless[]={"viewer","--no-window","--no-vrwindow","--infinite-ammo","--seconds","1"};
    assert(viewer_application_main(6,headless)==0);
    assert(WaitForSingleObject(observer,5000)==WAIT_OBJECT_0);
    DWORD observed=0;assert(GetExitCodeThread(observer,&observed) && observed==1);CloseHandle(observer);
    assert(!g_dev && !g_ctx && !g_vrMenuEnabled);
    assert(headless_frame->aim_seq==17 && headless_frame->aim2_seq==19);
    assert(!headless_frame->cheat_flags && (DWORD)(GetTickCount()-headless_frame->cheat_tick)>=500);
    g_frame=NULL;g_map=NULL;g_gameProc=NULL;g_stopAt=0;
    headless_frame->checkpoint_game_phase=6;
    headless_frame->cheat_tick=123;headless_frame->cheat_flags=HOTD_CHEAT_LIVES;
    headless_frame->audio_levels=0x123456;headless_frame->aim_seq=17;headless_frame->aim2_seq=19;
    char *save[]={"viewer","--save"};
    char *load[]={"viewer","--game-mode","original","--load"};
    char *select[]={"viewer","--game-mode","original","--start-stage","2","--start-scene","3"};
    char **commands[]={save,load,select};int counts[]={2,4,7};
    for(unsigned command=0;command<3;command++) {
        command_action=command==0?HOTD_CHECKPOINT_SAVE:command==1?HOTD_CHECKPOINT_LOAD:HOTD_CHECKPOINT_SELECT;
        command_mode=command?1:0;
        observer=CreateThread(NULL,0,observe_command,NULL,0,NULL);assert(observer);
        assert(viewer_application_main(counts[command],commands[command])==0);
        assert(WaitForSingleObject(observer,5000)==WAIT_OBJECT_0);
        assert(GetExitCodeThread(observer,&observed) && observed==1);CloseHandle(observer);
        assert(headless_frame->cheat_tick==123 && headless_frame->cheat_flags==HOTD_CHEAT_LIVES);
        assert(headless_frame->audio_levels==0x123456 && headless_frame->aim_seq==17 && headless_frame->aim2_seq==19);
    }
    UnmapViewOfFile(headless_frame);CloseHandle(mapping);g_frame=NULL;g_map=NULL;g_gameProc=NULL;
    assert(viewer_application_main(2,save)==1); /* Never launch a new game just to save. */

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
