/* Actual checkpoint callbacks and atomic files; no game or viewer starts. */
#include <assert.h>
#include "../src/bridge.c"
static unsigned loads,starts;
static int loaded_mode,started_scene;
static void *expected_out;
static unsigned events;
static void __cdecl fake_event(void)
{
    events++;*(short *)(game_base+0x5A2BB0)=1;*(DWORD *)(game_base+0x5C7108)=0x12345678;
}
static void __cdecl fake_load(void)
{
    loads++;loaded_mode=*(short *)(game_base+0x5A1A08);
}
static void __cdecl fake_start(void **out,int mode)
{
    assert(out==&expected_out && mode==0);
    starts++;started_scene=*(short *)(game_base+0x5A2BC0);
    checkpoint_event();
}
static void scripts(int stage,int count)
{
    DWORD *list=(DWORD *)(game_base+0x577400),*variants=(DWORD *)(game_base+0x577900);
    *(DWORD *)(game_base+0x577200+(stage-1)*4)=(DWORD)(uintptr_t)list;
    for(int i=0;i<count;i++)list[i]=(DWORD)(uintptr_t)variants;
    list[count]=0xFFFFFFFFu;variants[0]=variants[1]=(DWORD)(uintptr_t)(game_base+0x577A00);
    *(DWORD *)(game_base+0x577A00)=0x4D;
}
static void start_arcade(void)
{
    *(short *)(game_base+0x5A1A08)=0;
    checkpoint_load();
    /* Native Game6_Start resets scene after its table-load call. */
    *(short *)(game_base+0x5A2BC0)=0;
    checkpoint_start(&expected_out,0);
}
static void disk_tables(char *directory)
{
    char file[MAX_PATH+64],evt[MAX_PATH+16];DWORD common[6]={0},stage[70]={0};
    assert(GetTempFileNameA(directory,"hcs",0,file));assert(DeleteFileA(file) && CreateDirectoryA(file,NULL));
    strcpy(directory,file);snprintf(evt,sizeof(evt),"%s\\evt",directory);assert(CreateDirectoryA(evt,NULL));
    common[0]=common[1]=0x0CEB5A00; /* Relocates to the stage table at 0x977400. */
    for(int i=0;i<20;i++)stage[i]=0x0CEB5B00;
    stage[20]=0xFFFFFFFFu;stage[64]=stage[65]=0x0CEB5B10;stage[68]=0x4D;
    snprintf(file,sizeof(file),"%s\\comevtbl.bin",evt);FILE *out=fopen(file,"wb");assert(out);
    assert(fwrite(common,sizeof(common),1,out)==1 && !fclose(out));
    for(int i=1;i<=2;i++) {
        snprintf(file,sizeof(file),"%s\\st%devtbl.bin",evt,i);out=fopen(file,"wb");assert(out);
        assert(fwrite(stage,sizeof(stage),1,out)==1 && !fclose(out));
    }
}
int main(void)
{
    char temporary[MAX_PATH],path[MAX_PATH];HotdCheckpoint point;
    assert(GetTempPathA(MAX_PATH,temporary) && GetTempFileNameA(temporary,"hcp",0,path));
    assert(!hotd_checkpoint_read(path,&point));
    for(int stage=1;stage<=6;stage++) {
        point=(HotdCheckpoint){stage,hotd_checkpoint_first_scene(stage)};
        assert(hotd_checkpoint_write(path,point));
        HotdCheckpoint read;assert(hotd_checkpoint_read(path,&read) && read.stage==stage && read.scene==point.scene);
    }
    assert(!hotd_checkpoint_write(path,(HotdCheckpoint){7,0}));
    assert(hotd_checkpoint_read(path,&point) && point.stage==6);
    HANDLE locked=CreateFileA(path,GENERIC_READ,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    assert(locked!=INVALID_HANDLE_VALUE && !hotd_checkpoint_write(path,(HotdCheckpoint){2,3}));
    CloseHandle(locked);assert(hotd_checkpoint_read(path,&point) && point.stage==6);
    FILE *invalid=fopen(path,"w");assert(invalid);fputs("[checkpoint]\nversion=2\nstage=6\nscene=0\n",invalid);fclose(invalid);
    assert(!hotd_checkpoint_read(path,&point));
    invalid=fopen(path,"w");assert(invalid);fputs("[checkpoint]\nversion=1\nstage=2bad\nscene=0\n",invalid);fclose(invalid);
    assert(!hotd_checkpoint_read(path,&point));
    assert(hotd_checkpoint_write(path,(HotdCheckpoint){4,2}));
    game_base=calloc(1,0x600000);assert(game_base);
    strcpy(checkpoint_path,path);checkpoint_load_original=fake_load;checkpoint_start_original=fake_start;checkpoint_event_original=fake_event;
    *(DWORD *)(game_base+0x5C8E98)=6;
    checkpoint_ready=checkpoint_continue=1;
    for(int stage=1;stage<=6;stage++)scripts(stage,20);
    checkpoint_stage=3;checkpoint_scene=-1;start_arcade();assert(loaded_mode==2 && started_scene==7);
    assert(*(short *)(game_base+0x5A2BB0)==1 && *(DWORD *)(game_base+0x5C7108)==0x12345678);
    checkpoint_stage=6;checkpoint_scene=4;start_arcade();assert(loaded_mode==5 && started_scene==4);
    assert(*(short *)(game_base+0x5A2BB0)==0 && *(DWORD *)(game_base+0x5C7108)==(DWORD)(uintptr_t)(game_base+0x577A00));
    assert(!checkpoint_bootstrap);
    checkpoint_stage=0;checkpoint_scene=-1;start_arcade();assert(loaded_mode==3 && started_scene==2);
    checkpoint_continue=0;start_arcade();assert(loaded_mode==0 && started_scene==0);
    checkpoint_continue=1;checkpoint_stage=5;checkpoint_scene=20;unsigned previous=loads;
    start_arcade();assert(loads==previous+2 && loaded_mode==0 && started_scene==0 && !checkpoint_armed);
    checkpoint_stage=3;checkpoint_scene=7;
    for(unsigned type=1;type<=3;type++) {
        *(DWORD *)(game_base+0x5CA08C)=type;start_arcade();assert(loaded_mode==0 && started_scene==0);
    }
    *(DWORD *)(game_base+0x5CA08C)=0;
    /* Save only active Arcade gameplay, and only after a stage/scene change. */
    checkpoint_stage=0;*(DWORD *)(game_base+0x5C8E98)=6;*(DWORD *)(game_base+0x5C90A4)=2;
    *(short *)(game_base+0x5A1A08)=1;*(short *)(game_base+0x5A2BC0)=3;
    checkpoint_continue=0;checkpoint_record(3);assert(hotd_checkpoint_read(path,&point) && point.stage==4);
    checkpoint_continue=1;checkpoint_record(0);assert(hotd_checkpoint_read(path,&point) && point.stage==4);
    for(unsigned phase=3;phase<=5;phase++) {
        *(DWORD *)(game_base+0x5C8E98)=phase;checkpoint_record(3);assert(hotd_checkpoint_read(path,&point) && point.stage==4);
    }
    *(DWORD *)(game_base+0x5C8E98)=6;
    *(DWORD *)(game_base+0x5CA08C)=3;checkpoint_record(3);assert(hotd_checkpoint_read(path,&point) && point.stage==4);
    *(DWORD *)(game_base+0x5CA08C)=0;
    *(DWORD *)(game_base+0x5C90A4)=3;checkpoint_record(3);assert(hotd_checkpoint_read(path,&point) && point.stage==4);
    *(DWORD *)(game_base+0x5C90A4)=2;checkpoint_record(3);
    assert(hotd_checkpoint_read(path,&point) && point.stage==2 && point.scene==3);
    /* Repeated frames must not write again. */
    assert(DeleteFileA(path));checkpoint_record(3);assert(GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES);
    *(short *)(game_base+0x5A2BC0)=4;checkpoint_record(1);
    assert(hotd_checkpoint_read(path,&point) && point.scene==4);
    locked=CreateFileA(path,GENERIC_READ,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    assert(locked!=INVALID_HANDLE_VALUE);
    *(short *)(game_base+0x5A2BC0)=5;checkpoint_record(3);assert(checkpoint_retry);
    CloseHandle(locked);checkpoint_record(3);
    assert(hotd_checkpoint_read(path,&point) && point.scene==4);
    checkpoint_retry=GetTickCount()-1;checkpoint_record(3);
    assert(hotd_checkpoint_read(path,&point) && point.scene==5 && !checkpoint_retry);
    *(short *)(game_base+0x5A2BC0)=255;checkpoint_record(3);
    assert(hotd_checkpoint_read(path,&point) && point.scene==5);
    assert(!checkpoint_scene_available((HotdCheckpoint){0,0}));
    *(DWORD *)(game_base+0x577200)=0xFFFFFFFCu;assert(!checkpoint_scene_available((HotdCheckpoint){1,2}));
    assert(starts==8 && events==starts);
    /* Manual saves work with autosave/hooks off, including a paused gameplay scene. */
    frame=calloc(1,sizeof(*frame));assert(frame);
    checkpoint_continue=checkpoint_ready=0;
    *(short *)(game_base+0x5A1A00)=1;*(short *)(game_base+0x5A2BC0)=6;
    frame->checkpoint_request=1;checkpoint_manual(1);
    assert(frame->checkpoint_response==1 && frame->checkpoint_result==HOTD_SAVE_OK);
    assert(frame->checkpoint_stage==2 && frame->checkpoint_scene==6);
    assert(hotd_checkpoint_read(path,&point) && point.scene==6);
    checkpoint_refresh_saved();checkpoint_locations(1);
    assert(frame->checkpoint_current_stage==2 && frame->checkpoint_current_scene==6);
    assert(frame->checkpoint_saved_stage==2 && frame->checkpoint_saved_scene==6);
    assert(frame->checkpoint_request==1 && frame->checkpoint_response==1); /* Previews never issue commands. */
    checkpoint_locations(0);assert(!frame->checkpoint_current_stage && frame->checkpoint_saved_stage==2);
    assert(DeleteFileA(path));checkpoint_manual(1);assert(GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES);
    checkpoint_preview_tick=GetTickCount()-1001;checkpoint_refresh_saved();checkpoint_locations(1);
    assert(!frame->checkpoint_saved_stage && frame->checkpoint_current_stage==2);
    assert(hotd_checkpoint_write(path,point));
    frame->checkpoint_request=2;checkpoint_manual(0);
    assert(frame->checkpoint_response==2 && frame->checkpoint_result==HOTD_SAVE_UNAVAILABLE);
    assert(hotd_checkpoint_read(path,&point) && point.scene==6);
    locked=CreateFileA(path,GENERIC_READ,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    assert(locked!=INVALID_HANDLE_VALUE);
    *(short *)(game_base+0x5A2BC0)=7;frame->checkpoint_request=3;checkpoint_manual(1);
    assert(frame->checkpoint_response==3 && frame->checkpoint_result==HOTD_SAVE_IO_ERROR);
    CloseHandle(locked);assert(hotd_checkpoint_read(path,&point) && point.scene==6);
    *(DWORD *)(game_base+0x5C8E98)=4;frame->checkpoint_request=4;checkpoint_manual(1);
    assert(frame->checkpoint_response==4 && frame->checkpoint_result==HOTD_SAVE_UNAVAILABLE);
    assert(hotd_checkpoint_read(path,&point) && point.scene==6);
    /* Live autosave overrides expire with the viewer heartbeat. */
    frame->cheat_tick=GetTickCount();frame->checkpoint_autosave=2;
    assert(checkpoint_auto_enabled());frame->checkpoint_autosave=1;checkpoint_continue=1;
    assert(!checkpoint_auto_enabled());frame->cheat_tick=GetTickCount()-1000;assert(checkpoint_auto_enabled());
    checkpoint_continue=0;frame->checkpoint_autosave=0;
    /* Real disk preflight, pending ACK, paused load, explicit-start precedence and new game. */
    char directory[MAX_PATH],fixture[MAX_PATH+32];strcpy(directory,temporary);disk_tables(directory);
    strcpy(checkpoint_game_directory,directory);
    assert(hotd_checkpoint_file_scene(directory,(HotdCheckpoint){2,6}));
    assert(!hotd_checkpoint_file_scene(directory,(HotdCheckpoint){2,20}));
    assert(!hotd_checkpoint_file_scene(directory,(HotdCheckpoint){3,0}));
    checkpoint_ready=1;checkpoint_stage=6;checkpoint_scene=4;
    *(DWORD *)(game_base+0x5C8E98)=6;*(int *)(game_base+0x3C17A0)=-1;
    frame->checkpoint_action=HOTD_CHECKPOINT_LOAD;frame->checkpoint_request=5;
    checkpoint_manual(1);assert(checkpoint_pending==5 && frame->checkpoint_response==4);
    assert(*(int *)(game_base+0x3C17A0)==6 && checkpoint_unpause);
    assert(native_pause()==1 && !checkpoint_unpause); /* Native pause routine performs the actual resume. */
    assert(*(short *)(game_base+0x5A5C62)==0 && *(short *)(game_base+0x5A5C62+0x130)==9);
    checkpoint_manual(1);assert(frame->checkpoint_response==4); /* Old scene cannot ACK. */
    *(int *)(game_base+0x3C17A0)=-1;start_arcade();
    assert(loaded_mode==1 && started_scene==6 && checkpoint_entered);
    checkpoint_manual(1);assert(!checkpoint_pending && frame->checkpoint_response==5 && frame->checkpoint_result==HOTD_SAVE_OK);
    assert(frame->checkpoint_stage==2 && frame->checkpoint_scene==6);
    assert(hotd_checkpoint_write(path,(HotdCheckpoint){2,255}));
    frame->checkpoint_request=6;checkpoint_manual(1);
    assert(frame->checkpoint_response==6 && frame->checkpoint_result==HOTD_SAVE_INVALID && *(int *)(game_base+0x3C17A0)==-1);
    assert(hotd_checkpoint_write(path,(HotdCheckpoint){2,6}));scripts(1,20);
    frame->checkpoint_request=7;frame->checkpoint_action=HOTD_CHECKPOINT_NEW;checkpoint_manual(3);
    assert(checkpoint_pending==7 && !checkpoint_entered);
    *(int *)(game_base+0x3C17A0)=-1;start_arcade();checkpoint_manual(3);
    assert(frame->checkpoint_response==7 && frame->checkpoint_result==HOTD_SAVE_OK && frame->checkpoint_stage==1 && !frame->checkpoint_scene);
    assert(hotd_checkpoint_read(path,&point) && point.stage==2 && point.scene==6); /* No deletion/overwrite. */
    *(DWORD *)(game_base+0x5C8E98)=3;frame->akimbo=1;
    frame->checkpoint_request=8;frame->checkpoint_action=HOTD_CHECKPOINT_LOAD;checkpoint_manual(0);
    assert(checkpoint_pending==8);checkpoint_deadline=GetTickCount()-1;checkpoint_manual(1);
    assert(*(short *)(game_base+0x5A5C62)==0 && *(short *)(game_base+0x5A5C62+0x130)==0);
    assert(!checkpoint_pending && frame->checkpoint_response==8 && frame->checkpoint_result==HOTD_SAVE_LOAD_FAILED);
    for(int i=0;i<3;i++) {
        if(i)snprintf(fixture,sizeof(fixture),"%s\\evt\\st%devtbl.bin",directory,i);
        else snprintf(fixture,sizeof(fixture),"%s\\evt\\comevtbl.bin",directory);
        assert(DeleteFileA(fixture));
    }
    snprintf(fixture,sizeof(fixture),"%s\\evt",directory);assert(RemoveDirectoryA(fixture) && RemoveDirectoryA(directory));
    free(frame);frame=NULL;
    /* All three pass-through entry hooks validate before any instruction changes. */
    strcpy(ini_path,path);checkpoint_ready=checkpoint_continue=checkpoint_stage=0;checkpoint_scene=-1;
    assert(!install_checkpoints() && !checkpoint_ready);
    assert(SetEnvironmentVariableW(L"HOTD2VR_START_STAGE",L"7"));assert(!install_checkpoints());
    assert(SetEnvironmentVariableW(L"HOTD2VR_START_STAGE",L"2"));
    memcpy(game_base+0x5FF0A,(BYTE[]){0xE8,0x51,0x32,0xFB,0xFF},5);
    BYTE unchanged[5];memcpy(unchanged,game_base+0x5FF0A,5);
    assert(!install_checkpoints() && !memcmp(unchanged,game_base+0x5FF0A,5));
    memcpy(game_base+0x5FFD4,(BYTE[]){0xE8,0x57,0,0,0},5);
    assert(!install_checkpoints() && !memcmp(unchanged,game_base+0x5FF0A,5));
    memcpy(game_base+0x60710,(BYTE[]){0xE8,0xAB,0xE4,0xFF,0xFF},5);
    assert(install_checkpoints() && checkpoint_ready && memcmp(unchanged,game_base+0x5FF0A,5));
    assert(SetEnvironmentVariableW(L"HOTD2VR_START_STAGE",NULL));
    assert(SetEnvironmentVariableW(L"HOTD2VR_CONTINUE_GAME",L"0"));
    checkpoint_stage=0;checkpoint_scene=-1;checkpoint_ready=0;
    assert(WritePrivateProfileStringA("vr","continue_game","1",ini_path));
    memcpy(game_base+0x5FF0A,(BYTE[]){0xE8,0x51,0x32,0xFB,0xFF},5);
    memcpy(game_base+0x5FFD4,(BYTE[]){0xE8,0x57,0,0,0},5);
    memcpy(game_base+0x60710,(BYTE[]){0xE8,0xAB,0xE4,0xFF,0xFF},5);
    assert(install_checkpoints() && checkpoint_ready && !checkpoint_continue);
    assert(SetEnvironmentVariableW(L"HOTD2VR_CONTINUE_GAME",NULL));
    assert(!checkpoint_continue);free(game_base);assert(DeleteFileA(path));
    puts("Checkpoints: stage/scene selection, Arcade guards, strict files, atomic replacement, autosave changes and fallback passed.");
    return 0;
}
