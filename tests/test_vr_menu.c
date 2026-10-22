/* Real settings, input isolation, restart staging and atomic INI persistence. */
#include <assert.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static char config_test_exe[MAX_PATH];
static DWORD config_test_module(HMODULE module,char *path,DWORD size)
{
    if(!module && config_test_exe[0]) {
        assert(strlen(config_test_exe)<size);
        lstrcpynA(path,config_test_exe,size);return (DWORD)strlen(path);
    }
    return GetModuleFileNameA(module,path,size);
}
#define GetModuleFileNameA config_test_module
#define main viewer_application_main
#include "../src/hotd2vr.c"
#undef main
#undef GetModuleFileNameA

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
static void check_config_defaults(void)
{
    char temp[MAX_PATH],directory[MAX_PATH];
    assert(GetTempPathA(sizeof(temp),temp) && GetTempFileNameA(temp,"h2c",0,directory));
    assert(DeleteFileA(directory) && CreateDirectoryA(directory,NULL));
    snprintf(config_test_exe,sizeof(config_test_exe),"%s\\viewer.exe",directory);
    loadCfg();vrMenuInit();
    assert(!g_cfgHudSharp);
    assert(!g_cfgAutoZoom && !g_cfgScope && !g_cfgHudZoom);
    assert(g_cfgDepth==2 && !g_cfgDepthBias && g_cfgSuper==100 && g_cfgAkimbo && g_cfgExpandWorld);
    assert(strstr(g_cfgGun,"gun.obj"));
    for(unsigned g=0;g<6;g++)for(unsigned r=0;r<vrMenuCounts[g];r++)
        assert(vrMenuItems[g][r].value==vrMenuItems[g][r].defaults);
    assert(WritePrivateProfileStringA("vr","hudsharp","1",g_vrIniPath));
    loadCfg();vrMenuInit();assert(g_cfgHudSharp && vrMenuSave());
    assert(GetPrivateProfileIntA("vr","hudsharp",-1,g_vrIniPath)==1);
    vrMenuResetDefaults();assert(!g_cfgHudSharp && vrMenuSave());
    assert(GetPrivateProfileIntA("vr","hudsharp",-1,g_vrIniPath)==-1);
    loadCfg();assert(!g_cfgHudSharp);
    const char *keys[]={"autozoom","scope","zoom","zoomspeed","autozoomat","zoomsmooth",
        "scopesize","scopedist","scopemount","scopeglass","hudzoom"};
    const char *values[]={"1","1","300","6","40","60","15","30","0","32","1"};
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);i++)
        assert(WritePrivateProfileStringA("vr",keys[i],values[i],g_vrIniPath));
    loadCfg();
    assert(g_cfgAutoZoom && g_cfgScope && g_cfgHudZoom && g_cfgZoomX==300 && g_cfgZoomSpd==6);
    assert(g_cfgAutoAt==40 && g_cfgZoomSmooth==60 && g_cfgScopeDeg==15 && g_cfgScopeDist==30);
    assert(!g_cfgScopeMount && g_cfgScopeGlass==32);
    vrMenuInit();vrMenuResetDefaults();assert(vrMenuSave());
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);i++)
        assert(GetPrivateProfileIntA("vr",keys[i],-1,g_vrIniPath)==atoi(values[i]));
    assert(DeleteFileA(g_vrIniPath));
    WIN32_FIND_DATAA found;char pattern[MAX_PATH],backup[MAX_PATH];
    snprintf(pattern,sizeof(pattern),"%s\\*",directory);HANDLE search=FindFirstFileA(pattern,&found);
    assert(search!=INVALID_HANDLE_VALUE);
    do {if(!(found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
        snprintf(backup,sizeof(backup),"%s\\%s",directory,found.cFileName);assert(DeleteFileA(backup));
    }}while(FindNextFileA(search,&found));FindClose(search);
    loadCfg();assert(RemoveDirectoryA(directory));config_test_exe[0]=0;
}
static void check_menu_layout(void)
{
    vrMenuInit();
    const char *groups[]={"GAME SETTINGS","DISPLAY","RENDERING","AUDIO","CONTROLS","CHEATS"};
    const char *keys[6][11]={
        {"skip_splash","continue_game"},
        {"brightness","gamma","swap_player_hud","window","vrwindow","windowed"},
        {"worldscale","units_per_metre","supersample","msaa","hudsharp","native_lighting","expand_meshes","expand_world","depth","depthbias","background"},
        {"master_volume","music_volume","effects_volume"},
        {"akimbo","haptic","arcade_gun_colors","laser","crosshair","gunscale","reloadms","aimreach","focusgame"},
        {"infinite_ammo","infinite_lives","invincibility","infinite_continues","rapid_fire","rapid_fire_rate"}
    };
    const unsigned counts[]={2,6,11,3,9,6};
    assert(vrMenuRootCount==12);
    g_vrMenuEnabled=g_vrMenuOpen=1;g_vrMenuArmed=g_vrMenuBlock=g_vrMenuPrevious=0;
    for(unsigned g=0;g<6;g++) {
        g_vrMenuPage=-1;g_vrMenuRow=MENU_GROUP_FIRST+g;
        assert(!strcmp(vrMenuRootLabels[g_vrMenuRow],groups[g]));
        press(VC2_BTN_START);assert(g_vrMenuPage==(int)g && g_vrMenuRow==0);
        assert(vrMenuCounts[g]==counts[g]);
        for(unsigned row=0;row<counts[g];row++) {
            assert(!strcmp(vrMenuItems[g][g_vrMenuRow].key,keys[g][row]));
            press(VC2_BTN_DOWN);
        }
        assert(g_vrMenuRow==0);press(VC2_BTN_UP);assert(g_vrMenuRow==(int)counts[g]-1);
        press(VC2_BTN_BACK);assert(g_vrMenuPage==-1 && g_vrMenuRow==(int)(MENU_GROUP_FIRST+g));
    }
    assert(vrMenuItems[MENU_RENDERING][1].value==10 && (vrMenuItems[MENU_RENDERING][1].flags&MENU_RESTART_GAME));
    BYTE glyphs[2][sizeof(g_vrMenuFont['M'])];
    memcpy(glyphs[0],g_vrMenuFont['M'],sizeof(glyphs[0]));memcpy(glyphs[1],g_vrMenuFont['P'],sizeof(glyphs[1]));
    memset(g_vrMenuFont['M'],255,sizeof(glyphs[0]));memset(g_vrMenuFont['P'],255,sizeof(glyphs[1]));
    for(int group=-1;group<6;group++) {
        g_vrMenuPage=group;unsigned count=group<0?vrMenuRootCount:vrMenuCounts[group],perPage=group<0?8:6;
        for(unsigned row=0;row<count;row++) {
            g_vrMenuRow=row;vrMenuCanvas();
            assert(g_vrMenuPixels[48][440]==0x8017191D);
            assert((g_vrMenuPixels[72][440]==0xFF989898)==(count>perPage));
            assert((g_vrMenuPixels[356][440]==0xFF989898)==(count>perPage));
        }
    }
    memcpy(g_vrMenuFont['M'],glyphs[0],sizeof(glyphs[0]));memcpy(g_vrMenuFont['P'],glyphs[1],sizeof(glyphs[1]));
    g_vrMenuPage=-1;g_vrMenuRow=g_vrMenuOpen=g_vrMenuPrevious=0;
}
int main(void)
{
    check_menu_layout();check_config_defaults();
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
    VrMenuItem *arcade=&vrMenuItems[MENU_CONTROLS][2];
    assert(!strcmp(arcade->label,"Arcade gun colors") && !arcade->flags && !g_cfgArcadeGunColors);
    assert(vrMenuSet(arcade,1) && g_cfgArcadeGunColors && vrMenuSave());
    assert(GetPrivateProfileIntA("vr","arcade_gun_colors",0,g_vrIniPath)==1);
    g_previewOnly=1;assert(!vrMenuSet(arcade,0) && g_cfgArcadeGunColors);g_previewOnly=0;
    assert(vrMenuSet(arcade,0) && !g_cfgArcadeGunColors && vrMenuSave());
    assert(GetPrivateProfileIntA("vr","arcade_gun_colors",-1,g_vrIniPath)==-1);
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
    vrMenuChange(&vrMenuItems[MENU_GAME_SETTINGS][1],1);
    assert(g_cfgContinueGame && g_frame->checkpoint_autosave==2 && g_cfgContinueEdited);
    assert(GetPrivateProfileIntA("vr","continue_game",-1,g_vrIniPath)==-1);
    int font=vrMenuLoadFont();
    if(font) {
        image("build/menu-root.bmp");
        const char *names[]={"game-settings","display","rendering","audio","controls","cheats"};
        for(unsigned group=0;group<MENU_GROUP_COUNT;group++)for(unsigned row=0;row<vrMenuCounts[group];row+=6) {
            char path[96];g_vrMenuPage=group;g_vrMenuRow=row;
            snprintf(path,sizeof(path),"build/menu-%s-page%u.bmp",names[group],row/6+1);image(path);
        }
        g_vrMenuPage=-1;g_vrMenuRow=8;image("build/menu-root-page2.bmp");g_vrMenuRow=0;
    }
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
    vrMenuCanvas();assert(g_vrMenuPixels[479][639]==0x8017191D);
    vrMenuRect(0,0,32,32,0x80FFFFFF);
    memset(g_vrMenuFont['?'][0][0],255,4);g_vrMenuFont['?'][0][0][3]=128;
    vrMenuText(0,0,"?",1,0xFFFFFFFF);assert(g_vrMenuPixels[0][0]==0xC0FFFFFF);
    for(int i=0;i<MENU_GROUP_FIRST+MENU_RENDERING;i++)press(VC2_BTN_DOWN);
    press(VC2_BTN_START);assert(g_vrMenuPage==MENU_RENDERING);
    int original=vrMenuItems[MENU_RENDERING][0].value;
    press(VC2_BTN_RIGHT);assert(vrMenuItems[MENU_RENDERING][0].value==original+5 && g_cfgWorldScale==1.30f);
    assert(vrMenuControl(VC2_BTN_RIGHT)==1 && vrMenuControl(VC2_BTN_RIGHT)==1);
    assert(vrMenuItems[MENU_RENDERING][0].value==original+10); /* Held direction changes only once. */
    assert(vrMenuControl(0)==1);
    for(int i=0;i<6;i++)press(VC2_BTN_DOWN);
    assert(g_vrMenuRow==6);if(font)image("build/menu-settings-page2.bmp");
    press(VC2_BTN_BACK);assert(g_vrMenuPage==-1);
    for(int i=0;i<MENU_CHEATS-MENU_RENDERING;i++)press(VC2_BTN_DOWN);
    press(VC2_BTN_START);assert(g_vrMenuPage==MENU_CHEATS);
    press(VC2_BTN_START);assert(vrMenuItems[MENU_CHEATS][0].value==1 && g_cheatFlags==HOTD_CHEAT_AMMO);
    assert(g_frame->cheat_flags==HOTD_CHEAT_AMMO);
    press(VC2_BTN_START);assert(vrMenuItems[MENU_CHEATS][0].value==0);
    assert(!g_cheatFlags && !g_frame->cheat_flags);
    if(font)image("build/menu-cheats.bmp");
    vrMenuChange(&vrMenuItems[MENU_AUDIO][0],-1);
    assert(g_cfgMasterVolume==95 && g_frame->audio_levels==hotd_pack_audio(95,100,100));
    int msaa=g_cfgMsaa;vrMenuChange(&vrMenuItems[MENU_RENDERING][3],1);
    assert(vrMenuItems[MENU_RENDERING][3].value==8 && g_cfgMsaa==msaa);
    vrMenuChange(&vrMenuItems[MENU_GAME_SETTINGS][0],-1);assert(vrMenuItems[MENU_GAME_SETTINGS][0].value==0);
    g_previewOnly=1;vrMenuChange(&vrMenuItems[MENU_CHEATS][0],1);
    assert(!g_cheatFlags && !vrMenuItems[MENU_CHEATS][0].value);
    assert(vrMenuSave() && GetPrivateProfileIntA("vr","master_volume",100,g_vrIniPath)==100);
    g_previewOnly=0;
    g_nativePaused=1;assert(g_vrMenuOpen);
    assert(vrMenuControl(0)==1);
    HANDLE lock=CreateFileA(g_vrIniPath,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    assert(lock!=INVALID_HANDLE_VALUE);
    assert(vrMenuControl(MENU_TOGGLE)==1 && g_vrMenuOpen && g_vrMenuError);
    assert(vrMenuItems[MENU_AUDIO][0].initial==100 && g_cfgMasterVolume==95);
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
    vrMenuChange(&vrMenuItems[MENU_AUDIO][1],-1);g_nativePaused=1;vrMenuControl(0);
    g_nativePaused=0;vrMenuControl(0);assert(g_vrMenuOpen);
    press(MENU_TOGGLE);assert(!g_vrMenuOpen && !g_nativePaused);
    assert(GetPrivateProfileIntA("vr","music_volume",0,g_vrIniPath)==95);
    /* Missing INI can be created; unsigned endpoints cannot wrap. */
    assert(DeleteFileA(g_vrIniPath));
    for(int i=0;i<25;i++)vrMenuChange(&vrMenuItems[MENU_AUDIO][0],-1);
    assert(!g_cfgMasterVolume && vrMenuSave());
    assert(GetPrivateProfileIntA("vr","master_volume",-1,g_vrIniPath)==0);
    for(int i=0;i<5;i++)vrMenuChange(&vrMenuItems[MENU_CHEATS][i],1);
    assert(vrMenuSave() && readCheats(g_vrIniPath)==g_cheatFlags && g_cheatFlags);
    for(int i=0;i<5;i++)vrMenuChange(&vrMenuItems[MENU_CHEATS][i],-1);
    assert(vrMenuSave() && !readCheats(g_vrIniPath) && !g_cheatFlags);
    D3D_FEATURE_LEVEL level;
    assert(SUCCEEDED(D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_WARP,NULL,0,NULL,0,D3D11_SDK_VERSION,&g_dev,&level,&g_ctx)));
    vrMenuChange(&vrMenuItems[MENU_DISPLAY][0],1);assert(g_cfgBrightness==95 && g_ps && g_psOv && g_psTex);
    vrMenuChange(&vrMenuItems[MENU_DISPLAY][1],-1);assert(g_cfgGamma==215 && g_gammaOut==2.15f);
    /* Root action restores defaults by removing overrides, including disk values hidden by CLI overrides. */
    assert(WritePrivateProfileStringA("vr","laser","0",g_vrIniPath));
    assert(WritePrivateProfileStringA("vr","gunmodel","custom.obj",g_vrIniPath));
    assert(WritePrivateProfileStringA("other","unknown","keep",g_vrIniPath));
    g_cfgLaser=1;g_cfgMsaa=2;g_cfgArcadeGunColors=1;vrMenuInit();
    assert(vrMenuSet(&vrMenuItems[MENU_RENDERING][0],200));
    assert(vrMenuSet(&vrMenuItems[MENU_DISPLAY][2],0) && !g_cfgSwapPlayerHud);
    assert(vrMenuSet(&vrMenuItems[MENU_CHEATS][0],1));
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
    assert(g_cfgMsaa==2 && vrMenuItems[MENU_RENDERING][3].value==4); /* Resource setting waits for restart. */
    assert(!g_cfgArcadeGunColors);
    assert(GetPrivateProfileIntA("vr","laser",-1,g_vrIniPath)==0); /* Save only on close. */
    if(font)image("build/menu-reset-defaults.bmp");
    char shipped[MAX_PATH];assert(GetFullPathNameA("HOTD2VR.ini",sizeof(shipped),shipped,NULL));
    assert(GetFileAttributesA(shipped)!=INVALID_FILE_ATTRIBUTES);
    for(unsigned g=0;g<6;g++)for(unsigned r=0;r<vrMenuCounts[g];r++) {
        VrMenuItem *item=&vrMenuItems[g][r];
        assert(item->value==item->defaults && item->defaults>=item->min && item->defaults<=item->max);
        GetPrivateProfileStringA(item->kind==MENU_BRIDGE?"bridge":"vr",item->key,"",value,sizeof(value),shipped);
        assert(!value[0]);
    }
    press(MENU_TOGGLE);assert(!g_vrMenuOpen && !g_vrMenuResetPending && g_nativePaused);
    assert(GetPrivateProfileIntA("vr","laser",-1,g_vrIniPath)==-1); /* Reset removes untouched disk overrides too. */
    assert(GetPrivateProfileIntA("vr","msaa",-1,g_vrIniPath)==-1 && !readCheats(g_vrIniPath));
    GetPrivateProfileStringA("vr","gunmodel","",value,sizeof(value),g_vrIniPath);assert(!strcmp(value,"custom.obj"));
    GetPrivateProfileStringA("other","unknown","",value,sizeof(value),g_vrIniPath);assert(!strcmp(value,"keep"));
    for(unsigned g=0;g<6;g++)for(unsigned r=0;r<vrMenuCounts[g];r++) {
        VrMenuItem *item=&vrMenuItems[g][r];
        GetPrivateProfileStringA(item->kind==MENU_BRIDGE?"bridge":"vr",item->key,"",value,sizeof(value),g_vrIniPath);
        assert(!value[0]);
    }
    /* Return uses the bridge command, saves edits first, and closes only on success. */
    g_vrMenuOpen=1;g_vrMenuPage=-1;g_vrMenuRow=MENU_QUIT-1;
    g_vrMenuBlock=g_vrMenuArmed=0;g_vrMenuPrevious=0;
    assert(!strcmp(vrMenuRootLabels[g_vrMenuRow],"RETURN TO TITLE"));
    if(font)image("build/menu-return-to-title.bmp");
    unsigned request=g_frame->checkpoint_request;
    g_previewOnly=1;press(VC2_BTN_START);g_previewOnly=0;
    assert(g_frame->checkpoint_request==request && g_vrMenuOpen);
    assert(vrMenuSet(&vrMenuItems[MENU_DISPLAY][0],95));
    lock=CreateFileA(g_vrIniPath,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);assert(lock!=INVALID_HANDLE_VALUE);
    press(VC2_BTN_START);assert(g_frame->checkpoint_request==request && g_vrMenuOpen && g_vrMenuError);
    CloseHandle(lock);assert(vrMenuSet(&vrMenuItems[MENU_DISPLAY][0],90));
    press(VC2_BTN_START);assert(g_frame->checkpoint_request==request+1 && g_frame->checkpoint_action==3);
    assert(g_vrMenuOpen && !strcmp(g_vrMenuStatus,"RETURNING TO TITLE"));
    press(VC2_BTN_START);assert(g_frame->checkpoint_request==request+1);
    g_frame->checkpoint_response=request+1;g_frame->checkpoint_result=HOTD_SAVE_UNAVAILABLE;vrMenuCheckpointPoll();
    assert(g_vrMenuOpen && !strcmp(g_vrMenuStatus,"RETURN UNAVAILABLE   TRY AGAIN"));
    press(VC2_BTN_START);assert(g_frame->checkpoint_request==request+2);
    g_frame->checkpoint_response=request+2;g_frame->checkpoint_result=HOTD_SAVE_OK;
    g_frame->checkpoint_stage=g_frame->checkpoint_scene=0;vrMenuCheckpointPoll();
    assert(!g_vrMenuOpen && !g_vrMenuRequest && g_vrMenuBlock);
    assert(!strcmp(g_vrMenuStatus,"RETURNED TO TITLE"));
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
    assert(vrMenuSet(&vrMenuItems[MENU_DISPLAY][0],95));
    lock=CreateFileA(g_vrIniPath,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);assert(lock!=INVALID_HANDLE_VALUE);
    press(VC2_BTN_START);assert(g_vrMenuOpen && g_vrMenuError);
    assert(GetPrivateProfileIntA("vr","brightness",-1,g_vrIniPath)==-1);
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
    puts("Settings menu: live values/shaders, restart staging, stick toggle, unchanged pause, paging, preview guards, reset defaults, page counters, acknowledged title return, saved native quit and save/backup/retry passed.");
    return 0;
}
