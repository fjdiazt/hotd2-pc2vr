/* Real settings, input isolation, restart staging and atomic INI persistence. */
#include <assert.h>
#define COBJMACROS
#include <windows.h>
static int keyboard_fixture;
static SHORT keyboard_keys[256];
static DWORD keyboard_owner;
static SHORT WINAPI menu_key_state(int key)
{return keyboard_fixture?keyboard_keys[key]:GetAsyncKeyState(key);}
static HWND WINAPI menu_foreground(void)
{return keyboard_fixture?(HWND)(ULONG_PTR)1:GetForegroundWindow();}
static DWORD WINAPI menu_window_owner(HWND window,LPDWORD owner)
{if(!keyboard_fixture)return GetWindowThreadProcessId(window,owner);*owner=keyboard_owner;return 1;}
#define GetAsyncKeyState menu_key_state
#define GetForegroundWindow menu_foreground
#define GetWindowThreadProcessId menu_window_owner
#define main viewer_application_main
#include "../src/hotd2vr.c"
#undef main

static void press(unsigned buttons)
{assert(vrMenuControl(buttons)==1);assert(vrMenuControl(0)==1);}
static void image(const char *path)
{
    vrMenuCanvas();
    BITMAPFILEHEADER file={0};BITMAPINFOHEADER info={0};
    file.bfType=0x4D42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+sizeof(g_vrMenuPixels);
    info.biSize=sizeof(info);info.biWidth=640;info.biHeight=-480;info.biPlanes=1;info.biBitCount=32;
    FILE *out=fopen(path,"wb");assert(out);
    assert(fwrite(&file,sizeof(file),1,out)==1 && fwrite(&info,sizeof(info),1,out)==1);
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++) {
        DWORD rgba=g_vrMenuPixels[y][x],bgra=(rgba&0xFF00FF00)|((rgba&255)<<16)|((rgba>>16)&255);
        assert(fwrite(&bgra,4,1,out)==1);
    }
    assert(!fclose(out));
}
static void check_keyboard(void)
{
    keyboard_fixture=1;keyboard_owner=GetCurrentProcessId();
    g_vrMenuEnabled=1;g_vrMenuOpen=g_vrMenuBlock=g_vrMenuArmed=0;g_vrMenuPrevious=0;
    keyboard_keys[VK_F8]=(SHORT)0x8000;
    vrMenuKeyboard();assert(g_vrMenuOpen && g_vrMenuOwnsInput && (g_frame->aim_buttons&VC2_BTN_MENU));
    vrMenuKeyboard();assert(g_vrMenuOpen); /* Held toggle does not bounce. */
    memset(keyboard_keys,0,sizeof(keyboard_keys));vrMenuKeyboard();
    keyboard_keys[VK_DOWN]=(SHORT)0x8000;vrMenuKeyboard();vrMenuKeyboard();assert(g_vrMenuRow==1);
    memset(keyboard_keys,0,sizeof(keyboard_keys));vrMenuKeyboard();
    g_vrMenuRow=MENU_GROUP_FIRST;
    keyboard_keys[VK_RETURN]=(SHORT)0x8000;vrMenuKeyboard();assert(g_vrMenuPage==0);
    memset(keyboard_keys,0,sizeof(keyboard_keys));vrMenuKeyboard();
    keyboard_keys[VK_RCONTROL]=(SHORT)0x8000;vrMenuKeyboard();assert(g_vrMenuPage==-1 && g_vrMenuOpen);
    vrMenuKeyboard();assert(g_vrMenuOpen); /* Held Back cannot close the parent page too. */
    memset(keyboard_keys,0,sizeof(keyboard_keys));vrMenuKeyboard();
    keyboard_keys[VK_RCONTROL]=(SHORT)0x8000;vrMenuKeyboard();assert(!g_vrMenuOpen && g_vrMenuOwnsInput);
    memset(keyboard_keys,0,sizeof(keyboard_keys));vrMenuKeyboard();vrMenuKeyboard();
    assert(!g_vrMenuOwnsInput && !(g_frame->aim_buttons&VC2_BTN_MENU));
    keyboard_keys[VK_F8]=(SHORT)0x8000;
    keyboard_owner=123456;g_frame->game_pid=123457;
    vrMenuKeyboard();assert(!g_vrMenuOpen); /* Other applications cannot toggle or navigate. */
    keyboard_owner=g_frame->game_pid;vrMenuKeyboard();assert(g_vrMenuOpen); /* Native window focus. */
    memset(keyboard_keys,0,sizeof(keyboard_keys));vrMenuKeyboard();
    keyboard_keys[VK_TAB]=(SHORT)0x8000;assert(vrMenuKeyboardInput()==VC2_BTN_START);
    keyboard_keys[VK_TAB]=0;keyboard_keys[VK_LCONTROL]=(SHORT)0x8000;assert(vrMenuKeyboardInput()==MENU_KEY_BACK);
    keyboard_keys[VK_LCONTROL]=0;keyboard_keys[VK_ESCAPE]=(SHORT)0x8000;vrMenuKeyboard();assert(!g_vrMenuOpen);
    memset(keyboard_keys,0,sizeof(keyboard_keys));vrMenuKeyboard();vrMenuKeyboard();
    g_previewOnly=1;keyboard_keys[VK_F8]=(SHORT)0x8000;assert(!vrMenuKeyboardInput());
    keyboard_owner=GetCurrentProcessId();assert(vrMenuKeyboardInput()==MENU_TOGGLE);
    g_previewOnly=0;g_vrMenuEnabled=0;assert(!vrMenuKeyboardInput());g_vrMenuEnabled=1;
    g_frame->game_pid=0;keyboard_fixture=0;memset(keyboard_keys,0,sizeof(keyboard_keys));
    g_vrMenuPage=-1;g_vrMenuRow=0;g_vrMenuPrevious=0;
}
int main(void)
{
    char temp[MAX_PATH],directory[MAX_PATH],pattern[MAX_PATH],file[MAX_PATH];
    assert(GetTempPathA(sizeof(temp),temp));
    assert(GetTempFileNameA(temp,"h2t",0,directory));
    assert(DeleteFileA(directory) && CreateDirectoryA(directory,NULL));
    snprintf(g_vrIniPath,sizeof(g_vrIniPath),"%s\\HOTD2VR.ini",directory);
    g_cheatFlags=0;g_cfgLaser=0; /* Untouched CLI override must not replace disk laser=1. */
    vrMenuInit();assert(vrMenuSave());
    assert(GetFileAttributesA(g_vrIniPath)==INVALID_FILE_ATTRIBUTES); /* No edits, no write. */
    FILE *ini=fopen(g_vrIniPath,"wb");assert(ini);
    fputs("; keep this comment\r\n[vr]\r\nlaser=1\r\nworldscale=1.25\r\n[other]\r\nunknown=preserved\r\n",ini);
    assert(!fclose(ini));
    g_frame=calloc(1,sizeof(*g_frame));assert(g_frame);
    check_keyboard();
    g_vrMenuEnabled=1;assert(vrMenuControl(0)==0 && !g_vrMenuOpen);
    g_nativePaused=1;assert(vrMenuControl(0)==0 && !g_vrMenuOpen);
    g_nativePaused=0;assert(vrMenuControl(0)==0);
    assert(vrMenuControl(MENU_TOGGLE)==1 && g_vrMenuOpen);
    assert(vrMenuControl(MENU_TOGGLE)==1 && g_vrMenuOpen);
    assert(vrMenuControl(0)==1);
    press(VC2_BTN_PAUSE);press(VC2_BTN_BACK);assert(g_vrMenuOpen && g_vrMenuPage==-1);
    /* First two entries call the bridge, show progress, and report only acknowledged results. */
    assert(!strcmp(vrMenuRootLabels[0],"SAVE GAME") && !strcmp(vrMenuRootLabels[1],"LOAD GAME"));
    g_previewOnly=1;press(VC2_BTN_START);assert(!g_frame->checkpoint_request);g_previewOnly=0;
    press(VC2_BTN_START);assert(g_frame->checkpoint_request==1 && g_vrMenuRequest==1);
    assert(!strcmp(g_vrMenuStatus,"SAVING GAME") && !g_cfgContinueGame);
    press(VC2_BTN_START);assert(g_frame->checkpoint_request==1); /* No duplicate outstanding command. */
    g_frame->checkpoint_result=HOTD_SAVE_OK;g_frame->checkpoint_stage=1;g_frame->checkpoint_scene=4;
    g_frame->checkpoint_response=1;vrMenuCheckpointPoll();
    assert(!g_vrMenuRequest && !strcmp(g_vrMenuStatus,"SAVED A PRELUDE SCENE 4"));
    press(VC2_BTN_DOWN);press(VC2_BTN_START);
    assert(g_frame->checkpoint_action==HOTD_CHECKPOINT_LOAD && !strcmp(g_vrMenuStatus,"LOADING GAME"));
    g_frame->checkpoint_result=HOTD_SAVE_INVALID;g_frame->checkpoint_response=2;vrMenuCheckpointPoll();
    assert(!strcmp(g_vrMenuStatus,"NO VALID SAVED GAME"));
    press(VC2_BTN_START);g_frame->checkpoint_result=HOTD_SAVE_OK;g_frame->checkpoint_stage=2;g_frame->checkpoint_scene=8;
    g_frame->checkpoint_response=3;vrMenuCheckpointPoll();assert(!strcmp(g_vrMenuStatus,"LOADED MUDDY SCENE 8"));
    press(VC2_BTN_START);g_vrMenuRequestTick=GetTickCount()-45001;vrMenuCheckpointPoll();
    assert(!g_vrMenuRequest && strstr(g_vrMenuStatus,"UNKNOWN"));
    press(VC2_BTN_START);assert(g_frame->checkpoint_request==4); /* Timeout cannot release bridge ownership. */
    g_frame->checkpoint_response=4;g_vrMenuRow=0;g_vrMenuError=NULL;g_vrMenuStatus[0]=0;
    /* Autosave updates heartbeat live; configuration persists through existing atomic save. */
    vrMenuChange(&vrMenuItems[5][6],1);
    assert(g_cfgContinueGame && g_frame->checkpoint_autosave==2 && g_cfgContinueEdited);
    assert(GetPrivateProfileIntA("vr","continue_game",-1,g_vrIniPath)==-1);
    int font=vrMenuLoadFont();if(font)image("build/menu-root.bmp");
    /* Pre-action captions show current Save location and independent saved Load destination. */
    g_frame->checkpoint_current_stage=3;g_frame->checkpoint_current_scene=7;
    g_frame->checkpoint_saved_stage=2;g_frame->checkpoint_saved_scene=8;
    vrMenuDestination();assert(!strcmp(g_vrMenuDestination,"SAVE STAGE 3 DARKNESS SCENE 7"));
    if(font)image("build/menu-save-destination.bmp");
    g_vrMenuRow=MENU_LOAD_GAME;vrMenuDestination();
    assert(!strcmp(g_vrMenuDestination,"LOAD STAGE 2 MUDDY SCENE 8"));
    if(font)image("build/menu-load-destination.bmp");
    g_frame->checkpoint_saved_stage=0;vrMenuDestination();assert(!strcmp(g_vrMenuDestination,"NO SAVED GAME"));
    g_vrMenuRow=MENU_SAVE_GAME;g_frame->checkpoint_current_stage=0;vrMenuDestination();
    assert(strstr(g_vrMenuDestination,"UNAVAILABLE"));
    g_frame->checkpoint_current_stage=9;vrMenuDestination();assert(strstr(g_vrMenuDestination,"UNAVAILABLE"));
    g_frame->seq=1;vrMenuDestination();assert(!strcmp(g_vrMenuDestination,"WAITING FOR GAME LOCATION"));
    g_frame->seq=2;g_frame->checkpoint_current_stage=3;g_frame->checkpoint_current_scene=9;
    g_vrMenuDirty=0;vrMenuDestination();assert(g_vrMenuDirty && strstr(g_vrMenuDestination,"SCENE 9"));
    assert(g_frame->checkpoint_request==4 && g_frame->checkpoint_response==4);
    /* Stage select follows Load; browse stage, then actual scene IDs before issuing a restart. */
    assert(!strcmp(vrMenuRootLabels[2],"STAGE SELECT"));
    g_frame->checkpoint_stage_scenes[2][0]=(1u<<0)|(1u<<7)|(1u<<9)|(1u<<14);
    g_frame->checkpoint_stage_scenes[2][1]=(1u<<3)|(1u<<7)|(1u<<8)|(1u<<31);
    g_frame->checkpoint_stage_scenes[2][2]=1u;g_frame->checkpoint_stage_scenes[2][7]=1u<<31;
    g_frame->checkpoint_stage_scenes[5][7]=1u<<31;
    g_vrMenuRow=MENU_STAGE_SELECT;press(VC2_BTN_START);
    assert(g_vrMenuPage==MENU_STAGES && g_vrMenuRow==2 && g_frame->checkpoint_request==4);
    if(font)image("build/menu-stage-select.bmp");
    assert(vrMenuControl(VC2_BTN_START)==1 && vrMenuControl(VC2_BTN_START)==1 && vrMenuControl(0)==1);
    assert(g_vrMenuPage==MENU_SCENES && g_vrMenuStage==3 && g_vrMenuRow==1 && vrMenuCount()==10);
    assert(g_vrMenuScenes[0]==0 && g_vrMenuScenes[4]==35 && g_frame->checkpoint_request==4);
    press(VC2_BTN_UP);assert(g_vrMenuRow==0);press(VC2_BTN_DOWN);press(VC2_BTN_DOWN);
    assert(g_vrMenuScenes[g_vrMenuRow]==9);if(font)image("build/menu-scene-select.bmp");
    for(int i=0;i<4;i++)press(VC2_BTN_DOWN);
    assert(g_vrMenuRow==6 && g_vrMenuScenes[g_vrMenuRow]==40);if(font)image("build/menu-scene-select-page2.bmp");
    for(int i=0;i<4;i++)press(VC2_BTN_UP);
    g_previewOnly=1;press(VC2_BTN_START);g_previewOnly=0;
    assert(g_frame->checkpoint_request==4 && !g_frame->checkpoint_target_stage);
    press(VC2_BTN_START);assert(g_frame->checkpoint_request==5 && g_frame->checkpoint_action==HOTD_CHECKPOINT_SELECT);
    assert(g_frame->checkpoint_target_stage==3 && g_frame->checkpoint_target_scene==9);
    assert(!strcmp(g_vrMenuStatus,"STARTING SELECTED SCENE"));
    press(VC2_BTN_START);assert(g_frame->checkpoint_request==5);
    g_frame->checkpoint_response=5;g_frame->checkpoint_result=HOTD_SAVE_OK;
    g_frame->checkpoint_stage=3;g_frame->checkpoint_scene=9;vrMenuCheckpointPoll();
    assert(!strcmp(g_vrMenuStatus,"LOADED DARKNESS SCENE 9"));
    press(VC2_BTN_BACK);assert(g_vrMenuPage==MENU_STAGES && g_vrMenuRow==2);
    for(int i=0;i<3;i++)press(VC2_BTN_DOWN);press(VC2_BTN_START);
    assert(g_vrMenuPage==MENU_SCENES && vrMenuCount()==1 && g_vrMenuScenes[0]==255);
    press(VC2_BTN_BACK);press(VC2_BTN_UP);press(VC2_BTN_START);
    assert(g_vrMenuPage==MENU_STAGES && !strcmp(g_vrMenuError,"NO AVAILABLE SCENES"));
    press(VC2_BTN_BACK);assert(g_vrMenuPage==-1 && g_vrMenuRow==MENU_STAGE_SELECT && !g_vrMenuError);
    g_vrMenuRow=0;g_vrMenuError=NULL;
    vrMenuCanvas();assert(g_vrMenuPixels[479][639]==0x8017191D);
    vrMenuRect(0,0,32,32,0x80FFFFFF);
    memset(g_vrMenuFont['?'][0][0],255,4);g_vrMenuFont['?'][0][0][3]=128;
    vrMenuText(0,0,"?",1,0xFFFFFFFF);assert(g_vrMenuPixels[0][0]==0xC0FFFFFF);
    for(int i=0;i<MENU_GROUP_FIRST;i++)press(VC2_BTN_DOWN);
    press(VC2_BTN_START);assert(g_vrMenuPage==0);
    int original=vrMenuItems[0][0].value;
    press(VC2_BTN_RIGHT);assert(vrMenuItems[0][0].value==original+5 && g_cfgWorldScale==1.30f);
    assert(vrMenuControl(VC2_BTN_RIGHT)==1 && vrMenuControl(VC2_BTN_RIGHT)==1);
    assert(vrMenuItems[0][0].value==original+10); /* Held direction changes only once. */
    assert(vrMenuControl(0)==1);
    for(int i=0;i<6;i++)press(VC2_BTN_DOWN);
    assert(g_vrMenuRow==6);if(font)image("build/menu-settings-page2.bmp");
    press(VC2_BTN_BACK);assert(g_vrMenuPage==-1);
    for(int i=0;i<4;i++)press(VC2_BTN_DOWN);
    press(VC2_BTN_START);assert(g_vrMenuPage==4);
    press(VC2_BTN_START);assert(vrMenuItems[4][0].value==1 && g_cheatFlags==HOTD_CHEAT_AMMO);
    assert(g_frame->cheat_flags==HOTD_CHEAT_AMMO);
    press(VC2_BTN_START);assert(vrMenuItems[4][0].value==0);
    assert(!g_cheatFlags && !g_frame->cheat_flags);
    if(font)image("build/menu-cheats.bmp");
    vrMenuChange(&vrMenuItems[2][0],-1);
    assert(g_cfgMasterVolume==95 && g_frame->audio_levels==hotd_pack_audio(95,100,100));
    int msaa=g_cfgMsaa;vrMenuChange(&vrMenuItems[1][3],1);
    assert(vrMenuItems[1][3].value==8 && g_cfgMsaa==msaa);
    vrMenuChange(&vrMenuItems[5][2],-1);assert(vrMenuItems[5][2].value==0);
    g_previewOnly=1;vrMenuChange(&vrMenuItems[4][0],1);
    assert(!g_cheatFlags && !vrMenuItems[4][0].value);
    assert(vrMenuSave() && GetPrivateProfileIntA("vr","master_volume",100,g_vrIniPath)==100);
    g_previewOnly=0;
    g_nativePaused=1;assert(g_vrMenuOpen);
    assert(vrMenuControl(0)==1);
    HANDLE lock=CreateFileA(g_vrIniPath,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    assert(lock!=INVALID_HANDLE_VALUE);
    assert(vrMenuControl(MENU_TOGGLE)==1 && g_vrMenuOpen && g_vrMenuError);
    assert(vrMenuItems[2][0].initial==100 && g_cfgMasterVolume==95);
    assert(GetPrivateProfileIntA("vr","master_volume",100,g_vrIniPath)==100);
    CloseHandle(lock);assert(vrMenuControl(0)==1);
    assert(vrMenuControl(MENU_TOGGLE)==1 && !g_vrMenuOpen && g_nativePaused);
    assert(GetPrivateProfileIntA("vr","master_volume",0,g_vrIniPath)==95);
    assert(GetPrivateProfileIntA("vr","continue_game",0,g_vrIniPath)==1);
    assert(GetPrivateProfileIntA("vr","msaa",0,g_vrIniPath)==8 && g_cfgMsaa==msaa);
    assert(GetPrivateProfileIntA("bridge","skip_splash",1,g_vrIniPath)==0);
    assert(GetPrivateProfileIntA("vr","laser",0,g_vrIniPath)==1);
    assert(GetPrivateProfileIntA("vr","infinite_ammo",-1,g_vrIniPath)==-1);
    char value[64];GetPrivateProfileStringA("vr","worldscale","",value,sizeof(value),g_vrIniPath);
    assert(!strcmp(value,"1.35"));
    GetPrivateProfileStringA("other","unknown","",value,sizeof(value),g_vrIniPath);
    assert(!strcmp(value,"preserved"));
    ini=fopen(g_vrIniPath,"rb");assert(ini);assert(fgets(value,sizeof(value),ini) && strstr(value,"keep this comment"));fclose(ini);
    WIN32_FIND_DATAA found;snprintf(pattern,sizeof(pattern),"%s.menu-*.bak",g_vrIniPath);
    HANDLE search=FindFirstFileA(pattern,&found);assert(search!=INVALID_HANDLE_VALUE);FindClose(search);
    snprintf(file,sizeof(file),"%s\\%s",directory,found.cFileName);
    assert(GetPrivateProfileIntA("vr","master_volume",100,file)==100);
    assert(GetPrivateProfileIntA("vr","laser",0,file)==1);
    WIN32_FILE_ATTRIBUTE_DATA before,after;
    assert(GetFileAttributesExA(g_vrIniPath,GetFileExInfoStandard,&before));
    assert(vrMenuSave() && GetFileAttributesExA(g_vrIniPath,GetFileExInfoStandard,&after));
    assert(!CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime));
    assert(vrMenuControl(MENU_TOGGLE)==1 && vrMenuControl(0)==1);
    assert(vrMenuControl(0)==0 && !g_cheatFlags);
    /* Native pause/resume cannot open or close this independent overlay. */
    g_nativePaused=0;assert(vrMenuControl(0)==0 && !g_vrMenuOpen);
    press(MENU_TOGGLE);assert(g_vrMenuOpen);
    vrMenuChange(&vrMenuItems[2][1],-1);g_nativePaused=1;vrMenuControl(0);
    g_nativePaused=0;vrMenuControl(0);assert(g_vrMenuOpen);
    press(MENU_TOGGLE);assert(!g_vrMenuOpen && !g_nativePaused);
    assert(GetPrivateProfileIntA("vr","music_volume",0,g_vrIniPath)==95);
    /* Missing INI can be created; unsigned endpoints cannot wrap. */
    assert(DeleteFileA(g_vrIniPath));
    for(int i=0;i<25;i++)vrMenuChange(&vrMenuItems[2][0],-1);
    assert(!g_cfgMasterVolume && vrMenuSave());
    assert(GetPrivateProfileIntA("vr","master_volume",-1,g_vrIniPath)==0);
    for(int i=0;i<5;i++)vrMenuChange(&vrMenuItems[4][i],1);
    assert(vrMenuSave() && readCheats(g_vrIniPath)==g_cheatFlags && g_cheatFlags);
    for(int i=0;i<5;i++)vrMenuChange(&vrMenuItems[4][i],-1);
    assert(vrMenuSave() && !readCheats(g_vrIniPath) && !g_cheatFlags);
    D3D_FEATURE_LEVEL level;
    assert(SUCCEEDED(D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_WARP,NULL,0,NULL,0,D3D11_SDK_VERSION,&g_dev,&level,&g_ctx)));
    vrMenuChange(&vrMenuItems[0][1],1);assert(g_cfgBrightness==95 && g_ps && g_psOv && g_psTex);
    vrMenuChange(&vrMenuItems[0][2],-1);assert(g_cfgGamma==215 && g_gammaOut==2.15f);
    /* Root action resets shipped defaults, including disk values hidden by CLI overrides. */
    assert(WritePrivateProfileStringA("vr","laser","0",g_vrIniPath));
    assert(WritePrivateProfileStringA("vr","gunmodel","custom.obj",g_vrIniPath));
    assert(WritePrivateProfileStringA("other","unknown","keep",g_vrIniPath));
    g_cfgLaser=1;g_cfgMsaa=2;vrMenuInit();
    assert(vrMenuSet(&vrMenuItems[0][0],200));
    assert(vrMenuSet(&vrMenuItems[0][7],0) && !g_cfgSwapPlayerHud);
    assert(vrMenuSet(&vrMenuItems[4][0],1));
    g_vrMenuOpen=1;g_vrMenuPage=-1;g_vrMenuRow=MENU_RESET-1;
    g_vrMenuBlock=g_vrMenuArmed=0;g_vrMenuPrevious=0;g_nativePaused=1;
    press(VC2_BTN_DOWN);assert(g_vrMenuRow==MENU_RESET && !strcmp(vrMenuRootLabels[MENU_RESET],"RESET DEFAULTS"));
    g_previewOnly=1;press(VC2_BTN_START);
    assert(g_cfgWorldScale==2 && g_cheatFlags==HOTD_CHEAT_AMMO && !g_vrMenuResetPending);
    g_previewOnly=0;press(VC2_BTN_START);
    assert(g_vrMenuPage==-1 && g_vrMenuRow==MENU_RESET && g_vrMenuOpen && g_nativePaused);
    assert(g_vrMenuResetPending && g_cfgWorldScale==1.25f && g_cfgBrightness==90 && g_cfgGamma==220);
    assert(g_cfgSwapPlayerHud);
    assert(!g_cheatFlags && !g_frame->cheat_flags && g_frame->audio_levels==hotd_pack_audio(100,100,100));
    assert(g_cfgMsaa==2 && vrMenuItems[1][3].value==4); /* Resource setting waits for restart. */
    assert(GetPrivateProfileIntA("vr","laser",-1,g_vrIniPath)==0); /* Save only on close. */
    if(font)image("build/menu-reset-defaults.bmp");
    char shipped[MAX_PATH];assert(GetFullPathNameA("HOTD2VR.ini",sizeof(shipped),shipped,NULL));
    assert(GetFileAttributesA(shipped)!=INVALID_FILE_ATTRIBUTES);
    for(unsigned g=0;g<6;g++)for(unsigned r=0;r<vrMenuCounts[g];r++) {
        VrMenuItem *item=&vrMenuItems[g][r];
        assert(item->value==item->defaults && item->defaults>=item->min && item->defaults<=item->max);
        GetPrivateProfileStringA(item->kind==MENU_BRIDGE?"bridge":"vr",item->key,"",value,sizeof(value),shipped);
        if(value[0])assert(item->value==(item->kind==MENU_SCALE?(int)lround(strtod(value,NULL)*100):atoi(value)));
    }
    press(MENU_TOGGLE);assert(!g_vrMenuOpen && !g_vrMenuResetPending && g_nativePaused);
    assert(GetPrivateProfileIntA("vr","laser",-1,g_vrIniPath)==1); /* Reset overrides untouched CLI values too. */
    assert(GetPrivateProfileIntA("vr","msaa",-1,g_vrIniPath)==4 && !readCheats(g_vrIniPath));
    GetPrivateProfileStringA("vr","gunmodel","",value,sizeof(value),g_vrIniPath);assert(!strcmp(value,"custom.obj"));
    GetPrivateProfileStringA("other","unknown","",value,sizeof(value),g_vrIniPath);assert(!strcmp(value,"keep"));
    for(unsigned g=0;g<6;g++)for(unsigned r=0;r<vrMenuCounts[g];r++) {
        VrMenuItem *item=&vrMenuItems[g][r];
        GetPrivateProfileStringA(item->kind==MENU_BRIDGE?"bridge":"vr",item->key,"",value,sizeof(value),g_vrIniPath);
        assert(value[0] && item->defaults==(item->kind==MENU_SCALE?(int)lround(strtod(value,NULL)*100):atoi(value)));
    }
    /* Quit targets only the connected window and requires a successful save. */
    HWND gameWindow=CreateWindowExA(0,"STATIC","Menu quit fixture",WS_POPUP,0,0,1,1,NULL,NULL,GetModuleHandleA(NULL),NULL);
    assert(gameWindow);
    MSG message;
    g_vrMenuOpen=1;g_vrMenuPage=-1;g_vrMenuRow=MENU_QUIT-1;
    g_vrMenuBlock=g_vrMenuArmed=0;g_vrMenuPrevious=0;
    press(VC2_BTN_DOWN);assert(g_vrMenuRow==MENU_QUIT && !strcmp(vrMenuRootLabels[MENU_QUIT],"QUIT GAME"));
    if(font)image("build/menu-quit-game.bmp");
    g_frame->game_hwnd=(DWORD)(ULONG_PTR)gameWindow;g_frame->game_pid=GetCurrentProcessId();
    g_previewOnly=1;press(VC2_BTN_START);g_previewOnly=0;
    assert(g_vrMenuOpen && !PeekMessageA(&message,gameWindow,WM_CLOSE,WM_CLOSE,PM_REMOVE));
    g_frame->game_pid++;
    press(VC2_BTN_START);assert(g_vrMenuError && !PeekMessageA(&message,gameWindow,WM_CLOSE,WM_CLOSE,PM_REMOVE));
    g_frame->game_pid=GetCurrentProcessId();
    assert(vrMenuSet(&vrMenuItems[0][1],95));
    lock=CreateFileA(g_vrIniPath,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);assert(lock!=INVALID_HANDLE_VALUE);
    press(VC2_BTN_START);assert(g_vrMenuOpen && g_vrMenuError);
    assert(GetPrivateProfileIntA("vr","brightness",-1,g_vrIniPath)==90);
    assert(!PeekMessageA(&message,gameWindow,WM_CLOSE,WM_CLOSE,PM_REMOVE));CloseHandle(lock);
    press(VC2_BTN_START);assert(g_vrMenuOpen && g_nativePaused);
    assert(GetPrivateProfileIntA("vr","brightness",-1,g_vrIniPath)==95);
    assert(PeekMessageA(&message,gameWindow,WM_CLOSE,WM_CLOSE,PM_REMOVE) && !message.wParam && !message.lParam);
    assert(!PeekMessageA(&message,gameWindow,WM_CLOSE,WM_CLOSE,PM_REMOVE));
    assert(DestroyWindow(gameWindow));g_frame->game_hwnd=g_frame->game_pid=0;
    releaseGraphics();free(g_frame);g_frame=NULL;
    snprintf(pattern,sizeof(pattern),"%s\\*",directory);search=FindFirstFileA(pattern,&found);
    assert(search!=INVALID_HANDLE_VALUE);
    do {if(!(found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
        snprintf(file,sizeof(file),"%s\\%s",directory,found.cFileName);assert(DeleteFileA(file));
    }}while(FindNextFileA(search,&found));FindClose(search);assert(RemoveDirectoryA(directory));
    puts("Settings menu: live values/shaders, restart staging, stick toggle, unchanged pause, paging, preview guards, reset defaults, saved native quit and save/backup/retry passed.");
    return 0;
}
