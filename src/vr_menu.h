/* Viewer settings use extracted native glyphs without changing native resource ownership. */
static int g_vrMenuOpen,g_vrMenuPage=-1,g_vrMenuRow;
static int g_vrMenuDirty=1,g_vrMenuArmed=1,g_vrMenuBlock,g_vrMenuResetPending;
static int g_vrMenuTextureDirty=1,g_vrMenuPublishedOpen=-1;
static unsigned g_vrMenuRevision,g_vrMenuPublishedRevision;
static unsigned g_vrMenuPrevious;
static BYTE g_vrMenuFont[128][32][32][4];
static DWORD g_vrMenuPixels[480][640];
static ID3D11Texture2D *g_vrMenuTexture;
static ID3D11ShaderResourceView *g_vrMenuSRV;
static ID3D11BlendState *g_vrMenuBlend;
enum { MENU_GAME_SETTINGS,MENU_DISPLAY,MENU_RENDERING,MENU_AUDIO,MENU_CONTROLS,MENU_CHEATS,MENU_GROUP_COUNT };
static const char *const vrMenuGroups[]={"GAME SETTINGS","DISPLAY","RENDERING","AUDIO","CONTROLS","CHEATS"};
static const char *const vrMenuGroupHelp[]={"GAME STARTUP AND SAVING","OUTPUTS AND PICTURE SETTINGS",
    "WORLD SCALE AND GRAPHICS QUALITY","GAME MUSIC AND EFFECTS VOLUME",
    "GUNS AIMING AND CONTROLLER FEEDBACK","OPTIONAL GAMEPLAY ASSISTS"};
enum { MENU_SAVE_GAME,MENU_LOAD_GAME,MENU_STAGE_SELECT,MENU_GROUP_FIRST,MENU_RESET=MENU_GROUP_FIRST+MENU_GROUP_COUNT,MENU_TITLE,MENU_QUIT };
static const char *const vrMenuRootLabels[]={"SAVE GAME","LOAD GAME","STAGE SELECT","GAME SETTINGS","DISPLAY","RENDERING","AUDIO","CONTROLS","CHEATS","RESET DEFAULTS","RETURN TO TITLE","QUIT GAME"};
enum { MENU_STAGES=MENU_GROUP_COUNT,MENU_SCENES,MENU_LOAD_MODES,MENU_STAGE_MODES };
static const unsigned vrMenuRootCount=sizeof(vrMenuRootLabels)/sizeof(vrMenuRootLabels[0]);
static unsigned g_vrMenuRequest,g_vrMenuAction;
static DWORD g_vrMenuRequestTick;
static char g_vrMenuStatus[96];
static char g_vrMenuDestination[96];
static const char *const vrMenuChapters[]={"A PRELUDE","MUDDY","DARKNESS","DESPAIR","DAWN","ORIGINAL SIN"};
static unsigned g_vrMenuStage=1,g_vrMenuScenes[256],g_vrMenuSceneCount;
static unsigned g_vrMenuMode;
enum { MENU_INT,MENU_UNSIGNED,MENU_SCALE,MENU_CHEAT,MENU_BRIDGE };
enum { MENU_RESTART_VIEWER=1,MENU_RESTART_GAME=2,MENU_SHADER=4,MENU_POWER2=8,MENU_CAPTURE=16 };
typedef struct {
    const char *label,*key,*help;void *target;int min,max,step,kind,flags;
    unsigned mask;int value,initial,defaults;
} VrMenuItem;
/* Built-in defaults; absent INI keys use the matching loadCfg defaults. */
#define MI(label,key,help,target,lo,hi,step,kind,flags,def) {label,key,help,target,lo,hi,step,kind,flags,0,0,0,def}
#define MB(label,key,help,target,def) MI(label,key,help,target,0,1,1,MENU_INT,0,def)
#define MC(label,key,help,mask) {label,key,help,NULL,0,1,1,MENU_CHEAT,0,mask,0,0,0}
static VrMenuItem vrMenuItems[MENU_GROUP_COUNT][11]={
    {MI("Skip startup logos","skip_splash","SKIP SEGA AND EMPIRE LOGOS",NULL,0,1,1,MENU_BRIDGE,MENU_RESTART_GAME,1),
     MB("Autosave","continue_game","SAVE AND RESUME SCENES",&g_cfgContinueGame,0)},
    {MI("Brightness percent","brightness","ADJUST SCENE AND GUN BRIGHTNESS",&g_cfgBrightness,25,400,5,MENU_INT,MENU_SHADER,90),
     MI("Gamma percent","gamma","ADJUST PICTURE TONES",&g_cfgGamma,0,300,5,MENU_INT,MENU_SHADER,220),
     MB("Swap player HUD","swap_player_hud","P1 HUD RIGHT   P2 HUD LEFT",&g_cfgSwapPlayerHud,1),
     MI("Desktop preview","window","SHOW A DESKTOP VR PREVIEW",&g_cfgWindow,0,1,1,MENU_INT,MENU_RESTART_GAME,0),
     MI("Headset output","vrwindow","SHOW THE GAME IN YOUR HEADSET",&g_cfgVrWindow,0,1,1,MENU_INT,MENU_RESTART_GAME,1),
     MI("Native windowed","windowed","RUN THE ORIGINAL GAME IN A WINDOW",NULL,0,1,1,MENU_BRIDGE,MENU_RESTART_GAME,1),
     MI("Focus game","focusgame","FOCUS GAME WINDOW AT STARTUP",&g_cfgFocus,0,1,1,MENU_INT,MENU_RESTART_VIEWER,1)},
    {MI("World scale percent","worldscale","CHANGE HOW LARGE THE WORLD FEELS",&g_cfgWorldScale,10,1000,5,MENU_SCALE,0,125),
     MI("Units per metre","units_per_metre","BASE GAME UNITS PER VR METRE",NULL,1,100000,1,MENU_BRIDGE,MENU_RESTART_GAME,10),
     MI("Resolution percent","supersample","CHANGE HEADSET IMAGE RESOLUTION",&g_cfgSuper,50,250,10,MENU_INT,MENU_RESTART_VIEWER,100),
     MI("MSAA","msaa","SMOOTH JAGGED EDGES",&g_cfgMsaa,1,8,1,MENU_INT,MENU_RESTART_VIEWER|MENU_POWER2,4),
     MB("Sharp HUD","hudsharp","MAKE HUD ICONS CRISPER",&g_cfgHudSharp,0),
     MI("Native lighting","native_lighting","USE GAME LIGHTS AND FOG",&g_cfgNativeLighting,0,1,1,MENU_INT,MENU_CAPTURE,1),
     MI("Expand meshes","expand_meshes","KEEP ACTORS VISIBLE AT VIEW EDGES",&g_cfgExpandMeshes,0,1,1,MENU_INT,MENU_CAPTURE,1),
     MI("Expand world","expand_world","EXPERIMENTAL WIDER SCENERY",&g_cfgExpandWorld,0,1,1,MENU_INT,MENU_CAPTURE,1),
     MI("Depth mode","depth","CONTROL WHICH SURFACES SHOW IN FRONT",&g_cfgDepth,0,2,1,MENU_INT,MENU_RESTART_VIEWER,2),
     MI("Depth bias","depthbias","SEPARATE OVERLAPPING SURFACES",&g_cfgDepthBias,0,200,1,MENU_INT,MENU_RESTART_VIEWER,0),
     MI("Background rendering","background","KEEP DRAWING WHEN GAME LOSES FOCUS",NULL,0,1,1,MENU_BRIDGE,MENU_RESTART_GAME,1)},
    {MI("Master volume","master_volume","SET OVERALL GAME VOLUME",&g_cfgMasterVolume,0,100,5,MENU_UNSIGNED,0,100),
     MI("Music volume","music_volume","SET MUSIC VOLUME",&g_cfgMusicVolume,0,100,5,MENU_UNSIGNED,0,100),
     MI("Effects volume","effects_volume","SET EFFECTS AND VOICE VOLUME",&g_cfgEffectsVolume,0,100,5,MENU_UNSIGNED,0,100)},
    {MB("Akimbo","akimbo","PLAY WITH TWO GUNS",&g_cfgAkimbo,1),
     MB("Arcade gun colors","arcade_gun_colors","USE RED AND BLUE GUN SHELLS",&g_cfgArcadeGunColors,0),
     MB("Laser","laser","SHOW AIMING BEAMS",&g_cfgLaser,1),
     MB("Crosshair","crosshair","SHOW AIM POINT MARKERS",&g_cfgCrosshair,1),
     MI("Reload duration ms","reloadms","RELOAD ANIMATION TIME",&g_cfgReloadMs,60,1000,20,MENU_INT,0,180),
     MB("Haptics","haptic","VIBRATE WHEN SHOTS FIRE",&g_cfgHaptic,1),
     MI("Gun scale percent","gunscale","RESIZE GUN MODELS",&g_cfgGunScale,10,1000,5,MENU_INT,MENU_RESTART_VIEWER,100),
     MI("Aim reach metres","aimreach","AIM DISTANCE OVER EMPTY SPACE",&g_cfgReach,1,100,1,MENU_INT,0,6)},
    {MC("Infinite ammo","infinite_ammo","SHOOT WITHOUT USING AMMO",HOTD_CHEAT_AMMO),
     MC("Infinite lives","infinite_lives","PREVENT LIFE LOSS",HOTD_CHEAT_LIVES),
     MC("Invincibility","invincibility","BLOCK DAMAGE AND HIT REACTIONS",HOTD_CHEAT_INVINCIBILITY),
     MC("Protect rescue NPCs","protect_rescue_npcs","BLOCK PLAYER DAMAGE TO RESCUE NPCS",HOTD_CHEAT_RESCUE_NPCS),
     MC("Infinite continues","infinite_continues","JOIN AND CONTINUE WITHOUT CREDITS",HOTD_CHEAT_CONTINUES),
     MC("Rapid fire","rapid_fire","REPEAT SHOTS WHILE HOLDING TRIGGER",HOTD_CHEAT_RAPID),
     MI("Shots per second","rapid_fire_rate","REQUESTED RAPID FIRE RATE",&g_cfgRapidRate,1,30,1,MENU_UNSIGNED,0,4)}
};
#undef MI
#undef MB
#undef MC
static const unsigned vrMenuCounts[]={2,7,11,3,8,7};
static const char *g_vrMenuError;
static int vrMenuClose(void);
#define MENU_TOGGLE 0x80000000u
#define MENU_KEY_BACK 0x40000000u

static unsigned vrMenuKeyboardInput(void)
{
    if(!g_vrMenuEnabled)return 0;
    DWORD owner=0;
    HWND foreground=GetForegroundWindow();
    if(!foreground || !GetWindowThreadProcessId(foreground,&owner) || !owner)return 0;
    if(owner!=GetCurrentProcessId() && (g_previewOnly || !g_frame || owner!=g_frame->game_pid))return 0;
    const int keys[]={VK_F8,VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,VK_RETURN,VK_TAB,VK_RCONTROL,VK_LCONTROL,VK_BACK,VK_ESCAPE};
    const unsigned actions[]={MENU_TOGGLE,VC2_BTN_UP,VC2_BTN_DOWN,VC2_BTN_LEFT,VC2_BTN_RIGHT,
        VC2_BTN_START,VC2_BTN_START,MENU_KEY_BACK,MENU_KEY_BACK,MENU_KEY_BACK,MENU_KEY_BACK};
    unsigned input=0;
    for(unsigned k=0;k<sizeof(keys)/sizeof(keys[0]);k++)if(GetAsyncKeyState(keys[k])&0x8000)input|=actions[k];
    return input;
}

static void vrMenuInit(void)
{
    if(g_frame && !g_cfgContinueEdited && (g_frame->checkpoint_autosave==1 || g_frame->checkpoint_autosave==2))
        g_cfgContinueGame=g_frame->checkpoint_autosave==2;
    for(unsigned group=0;group<MENU_GROUP_COUNT;group++)for(unsigned row=0;row<vrMenuCounts[group];row++) {
        VrMenuItem *item=&vrMenuItems[group][row];int value;
        switch(item->kind) {
        case MENU_SCALE:value=(int)lroundf(*(float *)item->target*100);break;
        case MENU_UNSIGNED:value=(int)*(unsigned *)item->target;break;
        case MENU_CHEAT:value=!!(g_cheatFlags&item->mask);break;
        case MENU_BRIDGE:value=(int)GetPrivateProfileIntA("bridge",item->key,item->defaults,g_vrIniPath);break;
        default:value=*(int *)item->target;break;
        }
        item->value=item->initial=value;
    }
    g_vrMenuResetPending=0;g_vrMenuError=NULL;g_vrMenuDirty=1;g_vrMenuStatus[0]=0;
}

static void vrMenuCheckpointPoll(void)
{
    if(g_frame && g_frame->checkpoint_waiting_items && g_vrMenuOpen)vrMenuClose();
    if(!g_vrMenuRequest || !g_frame)return;
    if(g_frame->checkpoint_response==g_vrMenuRequest) {
        MemoryBarrier();
        g_vrMenuError=NULL;
        unsigned result=g_frame->checkpoint_result,stage=g_frame->checkpoint_stage,scene=g_frame->checkpoint_scene;
        if(g_vrMenuAction==HOTD_CHECKPOINT_TITLE) {
            lstrcpyA(g_vrMenuStatus,result==HOTD_SAVE_OK?"RETURNED TO TITLE":
                result==HOTD_SAVE_UNAVAILABLE?"RETURN UNAVAILABLE   TRY AGAIN":"RETURN FAILED   TRY AGAIN");
            if(result==HOTD_SAVE_OK)vrMenuClose();
        } else if(result==HOTD_SAVE_OK && stage>=1 && stage<=6 && scene<=255)
            snprintf(g_vrMenuStatus,sizeof(g_vrMenuStatus),"%s %s %s SCENE %u",
                g_vrMenuAction==HOTD_CHECKPOINT_SAVE?"SAVED":g_vrMenuAction==HOTD_CHECKPOINT_NEW?"STARTED":"LOADED",
                hotd_checkpoint_mode_name(g_frame->checkpoint_mode),vrMenuChapters[stage-1],scene);
        else lstrcpyA(g_vrMenuStatus,result==HOTD_SAVE_UNAVAILABLE?"UNAVAILABLE   ACTIVE ARCADE OR ORIGINAL REQUIRED":
            result==HOTD_SAVE_IO_ERROR?"SAVE FAILED   PREVIOUS SAVE KEPT":
            result==HOTD_SAVE_INVALID?(g_vrMenuAction==HOTD_CHECKPOINT_SELECT?"INVALID STAGE OR SCENE":"NO VALID SAVED GAME"):"LOAD FAILED   TRY AGAIN");
        g_vrMenuRequest=0;g_vrMenuDirty=1;
    } else if(g_frame->checkpoint_waiting_items) {
        g_vrMenuRequestTick=GetTickCount();lstrcpyA(g_vrMenuStatus,"CHOOSE ITEMS IN THE GAME");
    } else if((DWORD)(GetTickCount()-g_vrMenuRequestTick)>45000) {
        lstrcpyA(g_vrMenuStatus,"GAME NOT RESPONDING   RESULT UNKNOWN");g_vrMenuRequest=0;g_vrMenuDirty=1;
    }
}
static void vrMenuCheckpoint(unsigned action,unsigned stage,unsigned scene)
{
    if(g_previewOnly){g_vrMenuError="READ ONLY PREVIEW";return;}
    if(!g_frame){g_vrMenuError="GAME UNAVAILABLE";return;}
    HANDLE commandLock=CreateMutexA(NULL,FALSE,VC2_SHARE_NAME "_Checkpoint");
    DWORD acquired=commandLock?WaitForSingleObject(commandLock,0):WAIT_FAILED;
    if(acquired!=WAIT_OBJECT_0 && acquired!=WAIT_ABANDONED) {
        if(commandLock)CloseHandle(commandLock);
        g_vrMenuError="SAVE OR LOAD STILL PENDING";return;
    }
    if(g_vrMenuRequest || g_frame->checkpoint_request!=g_frame->checkpoint_response) {
        ReleaseMutex(commandLock);CloseHandle(commandLock);g_vrMenuError="SAVE OR LOAD STILL PENDING";return;
    }
    g_vrMenuRequest=g_frame->checkpoint_request+1;if(!g_vrMenuRequest)g_vrMenuRequest=1;
    g_vrMenuAction=action;g_vrMenuRequestTick=GetTickCount();g_vrMenuError=NULL;
    lstrcpyA(g_vrMenuStatus,action==HOTD_CHECKPOINT_SAVE?"SAVING GAME":action==HOTD_CHECKPOINT_NEW?"STARTING NEW GAME":
        action==HOTD_CHECKPOINT_TITLE?"RETURNING TO TITLE":action==HOTD_CHECKPOINT_SELECT?"STARTING SELECTED SCENE":"LOADING GAME");
    g_frame->checkpoint_target_stage=stage;g_frame->checkpoint_target_scene=scene;
    g_frame->checkpoint_target_mode=g_vrMenuMode;
    g_frame->checkpoint_action=action;MemoryBarrier();
    InterlockedExchange((volatile LONG *)&g_frame->checkpoint_request,g_vrMenuRequest);
    ReleaseMutex(commandLock);CloseHandle(commandLock);
    g_vrMenuDirty=1;
}

/* Shader constants are baked in. Swap all affected shaders together or retain the old set. */
static int vrMenuShaders(void)
{
    if(!g_dev)return 1;
    const char *sources[]={SRC,SRC_OV,SRC_TEX};
    ID3D11PixelShader *replacement[3]={0};
    ID3D11PixelShader **targets[]={&g_ps,&g_psOv,&g_psTex};
    int ok=1;
    for(int i=0;i<3 && ok;i++) {
        ID3DBlob *code=NULL,*error=NULL;
        ok=SUCCEEDED(compileG(sources[i],"ps","ps_4_0",&code,&error));
        if(ok)ok=SUCCEEDED(ID3D11Device_CreatePixelShader(g_dev,ID3D10Blob_GetBufferPointer(code),
            ID3D10Blob_GetBufferSize(code),NULL,&replacement[i]));
        if(error){if(!ok)note("Menu shader: %s\n",(const char *)ID3D10Blob_GetBufferPointer(error));ID3D10Blob_Release(error);}
        if(code)ID3D10Blob_Release(code);
    }
    for(int i=0;i<3;i++) {
        if(ok){if(*targets[i])ID3D11PixelShader_Release(*targets[i]);*targets[i]=replacement[i];}
        else if(replacement[i])ID3D11PixelShader_Release(replacement[i]);
    }
    return ok;
}

static int vrMenuSet(VrMenuItem *item,int next)
{
    if(g_previewOnly){g_vrMenuError="READ ONLY PREVIEW";return 0;}
    int previous=item->value;
    if(next==previous)return 1;
    if(!(item->flags&(MENU_RESTART_VIEWER|MENU_RESTART_GAME))) {
        switch(item->kind) {
        case MENU_SCALE:*(float *)item->target=(float)next/100.0f;break;
        case MENU_UNSIGNED:*(unsigned *)item->target=(unsigned)next;break;
        case MENU_CHEAT:g_cheatFlags=(g_cheatFlags&~item->mask)|(next?item->mask:0);break;
        default:*(int *)item->target=(int)next;break;
        }
        if(item->flags&MENU_SHADER) {
            float oldGamma=g_gammaOut;g_gammaOut=g_cfgGamma>0?g_cfgGamma/100.0f:1;
            if(!vrMenuShaders()) {
                *(int *)item->target=previous;g_gammaOut=oldGamma;
                g_vrMenuError="CHANGE FAILED   TRY AGAIN";return 0;
            }
        }
        if(item->target==&g_cfgContinueGame)g_cfgContinueEdited=1;
        pushCheats();
    }
    item->value=next;g_vrMenuError=NULL;g_vrMenuDirty=1;return 1;
}
static void vrMenuChange(VrMenuItem *item,int direction)
{
    int previous=item->value;long long next;
    if(item->max==1)next=!previous;
    else if(item->flags&MENU_POWER2)next=direction<0?(previous>4?4:previous>2?2:1):(previous<2?2:previous<4?4:8);
    else next=(long long)previous+direction*item->step;
    if(next<item->min)next=item->min;
    if(next>item->max)next=item->max;
    vrMenuSet(item,(int)next);
}
static void vrMenuResetDefaults(void)
{
    if(g_previewOnly){g_vrMenuError="READ ONLY PREVIEW";return;}
    for(unsigned g=0;g<MENU_GROUP_COUNT;g++)for(unsigned r=0;r<vrMenuCounts[g];r++) {
        VrMenuItem *item=&vrMenuItems[g][r];
        if(!vrMenuSet(item,item->defaults)) {
            g_vrMenuError="RESET INCOMPLETE   TRY AGAIN";return;
        }
    }
    g_vrMenuResetPending=1;g_vrMenuError=NULL;g_vrMenuDirty=1;
}

static int vrMenuSave(void)
{
    int dirty=g_vrMenuResetPending;
    for(unsigned g=0;g<MENU_GROUP_COUNT;g++)for(unsigned r=0;r<vrMenuCounts[g];r++)
        dirty|=vrMenuItems[g][r].value!=vrMenuItems[g][r].initial;
    if(!dirty){g_vrMenuError=NULL;return 1;}
    if(g_previewOnly)return 1;
    char directory[MAX_PATH],temporary[MAX_PATH]={0},backup[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA before,now;
    int exists=GetFileAttributesExA(g_vrIniPath,GetFileExInfoStandard,&before);
    DWORD error=GetLastError();
    if(!exists && error!=ERROR_FILE_NOT_FOUND)goto failed;
    lstrcpynA(directory,g_vrIniPath,sizeof(directory));
    char *slash=strrchr(directory,'\\');if(!slash){SetLastError(ERROR_BAD_PATHNAME);goto failed;}
    slash[1]=0;
    if(!GetTempFileNameA(directory,"h2m",0,temporary))goto failed;
    if(exists && !CopyFileA(g_vrIniPath,temporary,FALSE))goto failed;
    for(unsigned g=0;g<MENU_GROUP_COUNT;g++)for(unsigned r=0;r<vrMenuCounts[g];r++) {
        VrMenuItem *item=&vrMenuItems[g][r];char value[32];
        if(item->value==item->initial && !g_vrMenuResetPending)continue;
        if(item->kind==MENU_SCALE)snprintf(value,sizeof(value),"%.2f",item->value/100.0);
        else snprintf(value,sizeof(value),"%d",item->value);
        if(!WritePrivateProfileStringA(item->kind==MENU_BRIDGE?"bridge":"vr",item->key,
            item->value==item->defaults?NULL:value,temporary))goto failed;
    }
    WritePrivateProfileStringA(NULL,NULL,NULL,temporary); /* Cache flush returns zero even on success. */
    HANDLE staged=CreateFileA(temporary,GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(staged==INVALID_HANDLE_VALUE)goto failed;
    int flushed=FlushFileBuffers(staged);error=GetLastError();CloseHandle(staged);
    if(!flushed){SetLastError(error);goto failed;}
    if(exists) {
        if(!GetFileAttributesExA(g_vrIniPath,GetFileExInfoStandard,&now))goto failed;
        if(CompareFileTime(&before.ftLastWriteTime,&now.ftLastWriteTime) ||
           before.nFileSizeLow!=now.nFileSizeLow || before.nFileSizeHigh!=now.nFileSizeHigh) {
            SetLastError(ERROR_SHARING_VIOLATION);goto failed;
        }
        FILETIME time;GetSystemTimeAsFileTime(&time);
        if(snprintf(backup,sizeof(backup),"%s.menu-%08lx%08lx.bak",g_vrIniPath,
                    (unsigned long)time.dwHighDateTime,(unsigned long)time.dwLowDateTime)>=(int)sizeof(backup)) {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);goto failed;
        }
        if(!ReplaceFileA(g_vrIniPath,temporary,backup,0,NULL,NULL))goto failed;
        note("Menu saved: %s (backup %s)\n",g_vrIniPath,backup);
    } else {
        if(!MoveFileExA(temporary,g_vrIniPath,MOVEFILE_WRITE_THROUGH))goto failed;
        note("Menu saved: %s\n",g_vrIniPath);
    }
    WritePrivateProfileStringA(NULL,NULL,NULL,g_vrIniPath);
    for(unsigned g=0;g<MENU_GROUP_COUNT;g++)for(unsigned r=0;r<vrMenuCounts[g];r++)
        vrMenuItems[g][r].initial=vrMenuItems[g][r].value;
    g_vrMenuResetPending=0;g_vrMenuError=NULL;return 1;
failed:
    error=GetLastError();if(temporary[0])DeleteFileA(temporary);
    note("Menu save failed: %lu (%s)\n",(unsigned long)error,g_vrIniPath);
    g_vrMenuError="SAVE FAILED   CLOSE AGAIN TO RETRY";g_vrMenuDirty=1;return 0;
}
static int vrMenuClose(void)
{
    if(!vrMenuSave())return 0;
    g_vrMenuOpen=0;g_vrMenuBlock=1;return 1;
}

static void vrMenuQuitGame(void)
{
    if(g_previewOnly){g_vrMenuError="READ ONLY PREVIEW";return;}
    HWND window=g_frame?(HWND)(ULONG_PTR)g_frame->game_hwnd:NULL;
    DWORD owner=0;
    if(!window || !GetWindowThreadProcessId(window,&owner) || !owner || owner!=g_frame->game_pid) {
        g_vrMenuError="GAME WINDOW UNAVAILABLE";return;
    }
    if(!vrMenuSave())return;
    if(!PostMessageA(window,WM_CLOSE,0,0)) {
        note("Menu quit failed: %lu\n",(unsigned long)GetLastError());
        g_vrMenuError="QUIT FAILED   TRY AGAIN";return;
    }
    g_vrMenuBlock=1;g_vrMenuError="QUIT REQUESTED";
}

static int vrMenuLoadFont(void)
{
    char path[MAX_PATH];DWORD n=GetModuleFileNameA(NULL,path,sizeof(path));
    if(!n || n>=sizeof(path))return 0;
    while(n && path[n-1]!='\\')n--;
    lstrcpynA(path+n,"menu-font.rgba",sizeof(path)-n);
    FILE *file=fopen(path,"rb");if(!file)return 0;
    char magic[4];
    int ok=fread(magic,1,4,file)==4 && !memcmp(magic,"H2PF",4) &&
        fread(g_vrMenuFont,1,sizeof(g_vrMenuFont),file)==sizeof(g_vrMenuFont) && fgetc(file)==EOF;
    fclose(file);return ok;
}

/* Menu owns input only while open or waiting for the closing press to release. */
static unsigned vrMenuCount(void)
{
    return g_vrMenuPage<0?vrMenuRootCount:g_vrMenuPage==MENU_STAGES?6:
        g_vrMenuPage==MENU_SCENES?g_vrMenuSceneCount:
        g_vrMenuPage==MENU_LOAD_MODES || g_vrMenuPage==MENU_STAGE_MODES?2:vrMenuCounts[g_vrMenuPage];
}
static void vrMenuOpenScenes(unsigned stage)
{
    uint32_t scenes[8];
    if(!g_frame){g_vrMenuError="WAITING FOR GAME SCENES";return;}
    /* Catalog is immutable after DLL initialization; frame publication cannot tear it. */
    memcpy(scenes,g_frame->checkpoint_stage_scenes[stage-1],sizeof(scenes));
    g_vrMenuSceneCount=0;
    for(unsigned scene=0;scene<256;scene++)if(scenes[scene/32]&(1u<<(scene%32)))
        g_vrMenuScenes[g_vrMenuSceneCount++]=scene;
    if(!g_vrMenuSceneCount){g_vrMenuError="NO AVAILABLE SCENES";return;}
    g_vrMenuStage=stage;g_vrMenuPage=MENU_SCENES;g_vrMenuRow=0;g_vrMenuError=NULL;
    for(unsigned row=0;row<g_vrMenuSceneCount;row++)
        if(g_vrMenuScenes[row]==(unsigned)hotd_checkpoint_first_scene(stage))g_vrMenuRow=(int)row;
}
static int vrMenuControl(unsigned input)
{
    if(!g_vrMenuEnabled)return 0;
    vrMenuCheckpointPoll();
    unsigned pressed=input&~g_vrMenuPrevious;g_vrMenuPrevious=input;
    if(g_vrMenuBlock){if(!input)g_vrMenuBlock=0;return 1;}
    if(pressed&MENU_TOGGLE) {
        if(g_vrMenuOpen)vrMenuClose();
        else {g_vrMenuOpen=1;g_vrMenuPage=-1;g_vrMenuRow=0;g_vrMenuArmed=1;g_vrMenuDirty=1;}
        return 1;
    }
    if(!g_vrMenuOpen)return 0;
    if(g_vrMenuArmed){if(!input)g_vrMenuArmed=0;return 1;}
    if(!g_vrMenuRequest && (pressed&(VC2_BTN_UP|VC2_BTN_DOWN|VC2_BTN_LEFT|VC2_BTN_RIGHT|VC2_BTN_BACK|MENU_KEY_BACK)))
        g_vrMenuStatus[0]=0;
    if(pressed&(VC2_BTN_BACK|MENU_KEY_BACK)) {
        if(g_vrMenuPage==MENU_SCENES){g_vrMenuRow=(int)g_vrMenuStage-1;g_vrMenuPage=MENU_STAGES;g_vrMenuError=NULL;}
        else if(g_vrMenuPage==MENU_STAGES){g_vrMenuRow=(int)g_vrMenuMode;g_vrMenuPage=MENU_STAGE_MODES;g_vrMenuError=NULL;}
        else if(g_vrMenuPage==MENU_LOAD_MODES || g_vrMenuPage==MENU_STAGE_MODES) {
            g_vrMenuRow=g_vrMenuPage==MENU_LOAD_MODES?MENU_LOAD_GAME:MENU_STAGE_SELECT;g_vrMenuPage=-1;g_vrMenuError=NULL;
        }
        else if(g_vrMenuPage>=0){g_vrMenuRow=g_vrMenuPage+MENU_GROUP_FIRST;g_vrMenuPage=-1;}
        else if(pressed&MENU_KEY_BACK)vrMenuClose();
        g_vrMenuDirty=1;return 1;
    }
    unsigned count=vrMenuCount();if(!count)return 1;
    if(pressed&VC2_BTN_UP)g_vrMenuRow=(g_vrMenuRow+(int)count-1)%(int)count;
    if(pressed&VC2_BTN_DOWN)g_vrMenuRow=(g_vrMenuRow+1)%(int)count;
    if((pressed&VC2_BTN_START) && g_vrMenuPage<0) {
        if(g_vrMenuRow==MENU_SAVE_GAME) {
            if(!g_frame || g_frame->checkpoint_game_mode>1)g_vrMenuError="SAVE REQUIRES ARCADE OR ORIGINAL";
            else {g_vrMenuMode=g_frame->checkpoint_game_mode;vrMenuCheckpoint(HOTD_CHECKPOINT_SAVE,0,0);}
        }
        else if(g_vrMenuRow==MENU_LOAD_GAME || g_vrMenuRow==MENU_STAGE_SELECT) {
            g_vrMenuPage=g_vrMenuRow==MENU_LOAD_GAME?MENU_LOAD_MODES:MENU_STAGE_MODES;
            g_vrMenuRow=g_frame && g_frame->checkpoint_game_phase==6 && g_frame->checkpoint_game_mode<=1?(int)g_frame->checkpoint_game_mode:0;
            g_vrMenuError=NULL;
        }
        else if(g_vrMenuRow==MENU_RESET)vrMenuResetDefaults();
        else if(g_vrMenuRow==MENU_TITLE){if(vrMenuSave())vrMenuCheckpoint(HOTD_CHECKPOINT_TITLE,0,0);}
        else if(g_vrMenuRow==MENU_QUIT)vrMenuQuitGame();
        else {g_vrMenuPage=g_vrMenuRow-MENU_GROUP_FIRST;g_vrMenuRow=0;}
    } else if(g_vrMenuPage==MENU_LOAD_MODES || g_vrMenuPage==MENU_STAGE_MODES) {
        if(pressed&VC2_BTN_START) {
            g_vrMenuMode=(unsigned)g_vrMenuRow;
            if(g_vrMenuPage==MENU_LOAD_MODES)vrMenuCheckpoint(HOTD_CHECKPOINT_LOAD,0,0);
            else {
                g_vrMenuPage=MENU_STAGES;g_vrMenuRow=0;
                if(g_frame && g_frame->checkpoint_game_mode==g_vrMenuMode && g_frame->checkpoint_current_stage>=1 && g_frame->checkpoint_current_stage<=6)
                    g_vrMenuRow=(int)g_frame->checkpoint_current_stage-1;
            }
        }
    } else if(g_vrMenuPage==MENU_STAGES) {
        if(pressed&VC2_BTN_START)vrMenuOpenScenes((unsigned)g_vrMenuRow+1);
    } else if(g_vrMenuPage==MENU_SCENES) {
        if(pressed&VC2_BTN_START)vrMenuCheckpoint(HOTD_CHECKPOINT_SELECT,g_vrMenuStage,g_vrMenuScenes[g_vrMenuRow]);
    } else if(g_vrMenuPage>=0 && (pressed&(VC2_BTN_START|VC2_BTN_LEFT|VC2_BTN_RIGHT))) {
        VrMenuItem *item=&vrMenuItems[g_vrMenuPage][g_vrMenuRow];
        vrMenuChange(item,(pressed&VC2_BTN_LEFT)?-1:1);
    }
    if(pressed)g_vrMenuDirty=1;
    return 1;
}

static void vrMenuRect(int x,int y,int width,int height,DWORD colour)
{
    for(int py=y;py<y+height && py<480;py++)for(int px=x;px<x+width && px<640;px++)
        if(px>=0 && py>=0)g_vrMenuPixels[py][px]=colour;
}
static void vrMenuText(int x,int y,const char *text,float scale,DWORD colour)
{
    const int width=(int)(32*scale),height=(int)(32*scale),advance=(int)(16*scale);
    for(;*text;text++,x+=advance) {
        unsigned character=(unsigned char)*text;if(character>=128)continue;
        for(int py=0;py<height;py++)for(int px=0;px<width;px++) {
            int dx=x+px,dy=y+py+(*text>='a' && *text<='z'?(int)(7*scale):0);
            if(dx<0||dx>=640||dy<0||dy>=480)continue;
            const BYTE *source=g_vrMenuFont[character][(int)(py/scale)][(int)(px/scale)];
            DWORD previous=g_vrMenuPixels[dy][dx];
            unsigned backgroundAlpha=previous>>24;
            unsigned coverage=source[3]*255+backgroundAlpha*(255-source[3]);
            unsigned alpha=(coverage+127)/255;
            DWORD result=alpha<<24;
            for(unsigned c=0;c<3;c++) {
                unsigned value=source[c]*((colour>>(c*8))&255)/255;
                if(coverage)result|=((value*source[3]*255+((previous>>(c*8))&255)*backgroundAlpha*(255-source[3]))/coverage)<<(c*8);
            }
            g_vrMenuPixels[dy][dx]=result;
        }
    }
}
static void vrMenuDestination(void)
{
    char text[96]={0};
    if((g_vrMenuPage<0 && g_vrMenuRow==MENU_SAVE_GAME) || g_vrMenuPage==MENU_LOAD_MODES) {
        unsigned stage=0,scene=0,mode=0;int stable=0;
        if(g_frame)for(unsigned retry=0;retry<8;retry++) {
            unsigned seq=g_frame->seq;if(seq&1)continue;
            MemoryBarrier();
            mode=g_vrMenuPage==MENU_LOAD_MODES?(unsigned)g_vrMenuRow:g_frame->checkpoint_game_mode;
            stage=g_vrMenuPage==MENU_LOAD_MODES?g_frame->checkpoint_mode_saves[mode][0]:g_frame->checkpoint_current_stage;
            scene=g_vrMenuPage==MENU_LOAD_MODES?g_frame->checkpoint_mode_saves[mode][1]:g_frame->checkpoint_current_scene;
            MemoryBarrier();if(seq==g_frame->seq){stable=1;break;}
        }
        if(!stable)lstrcpyA(text,"WAITING FOR GAME LOCATION");
        else if(stage>=1 && stage<=6 && scene<=255)
            snprintf(text,sizeof(text),"%s %s STAGE %u %s SCENE %u",g_vrMenuPage==MENU_LOAD_MODES?"LOAD":"SAVE",hotd_checkpoint_mode_name(mode),stage,vrMenuChapters[stage-1],scene);
        else lstrcpyA(text,g_vrMenuPage==MENU_LOAD_MODES?"NO SAVED GAME":"SAVE UNAVAILABLE   NO ACTIVE ARCADE OR ORIGINAL SCENE");
    }
    if(strcmp(text,g_vrMenuDestination)){lstrcpyA(g_vrMenuDestination,text);g_vrMenuDirty=1;}
}
static void vrMenuCanvas(void)
{
    vrMenuDestination();
    const DWORD white=0xFFE8E8E8,red=0xFF4646ED,grey=0xFF989898;
    vrMenuRect(0,0,640,480,0x8017191D); /* 50% panel opacity; text keeps its own coverage. */
    vrMenuRect(18,16,4,448,0xFF2424A4);
    vrMenuText(42,24,"VR SETTINGS",1.4f,white);
    char heading[64];
    if(g_vrMenuPage==MENU_SCENES)snprintf(heading,sizeof(heading),"%s STAGE %u %s",hotd_checkpoint_mode_name(g_vrMenuMode),g_vrMenuStage,vrMenuChapters[g_vrMenuStage-1]);
    if(g_vrMenuPage==MENU_STAGES)snprintf(heading,sizeof(heading),"%s CHOOSE STAGE",hotd_checkpoint_mode_name(g_vrMenuMode));
    vrMenuText(44,67,g_vrMenuPage<0?"THE HOUSE OF THE DEAD 2":g_vrMenuPage==MENU_LOAD_MODES?"LOAD GAME MODE":g_vrMenuPage==MENU_STAGE_MODES?"SELECT GAME MODE":g_vrMenuPage==MENU_STAGES?heading:
        g_vrMenuPage==MENU_SCENES?heading:vrMenuGroups[g_vrMenuPage],.7f,grey);
    vrMenuRect(42,99,552,2,0xFF303077);
    unsigned count=vrMenuCount();
    unsigned perPage=g_vrMenuPage<0?8:6,first=(unsigned)g_vrMenuRow/perPage*perPage;
    for(unsigned row=first;row<count && row<first+perPage;row++) {
        int y=112+(int)(row-first)*(g_vrMenuPage<0?32:43),selected=(int)row==g_vrMenuRow;
        if(selected){vrMenuRect(34,y+(g_vrMenuPage<0?7:11),4,23,red);vrMenuRect(44,y+(g_vrMenuPage<0?3:6),552,g_vrMenuPage<0?30:35,0x8020202B);}
        char label[64];
        if(g_vrMenuPage==MENU_STAGES)snprintf(label,sizeof(label),"STAGE %u %s",row+1,vrMenuChapters[row]);
        if(g_vrMenuPage==MENU_SCENES)snprintf(label,sizeof(label),"SCENE %u",g_vrMenuScenes[row]);
        if(g_vrMenuPage==MENU_LOAD_MODES || g_vrMenuPage==MENU_STAGE_MODES)lstrcpyA(label,hotd_checkpoint_mode_name(row));
        vrMenuText(52,y,g_vrMenuPage<0?vrMenuRootLabels[row]:g_vrMenuPage>=MENU_STAGES?label:vrMenuItems[g_vrMenuPage][row].label,
            g_vrMenuPage<0?.9f:.85f,selected?red:white);
        if(g_vrMenuPage>=0 && g_vrMenuPage<MENU_STAGES) {
            VrMenuItem *item=&vrMenuItems[g_vrMenuPage][row];char value[24];
            if(item->max==1)lstrcpyA(value,item->value?"ON":"OFF");
            else snprintf(value,sizeof(value),"%d",item->value);
            vrMenuText(500,y,value,.85f,selected?red:white);
        }
    }
    const char *hint=g_vrMenuPage>=0 && g_vrMenuPage<MENU_GROUP_COUNT?vrMenuItems[g_vrMenuPage][g_vrMenuRow].help:
        g_vrMenuRow>=MENU_GROUP_FIRST && g_vrMenuRow<MENU_RESET?vrMenuGroupHelp[g_vrMenuRow-MENU_GROUP_FIRST]:"";
    if((g_vrMenuPage<0 && g_vrMenuRow==MENU_SAVE_GAME) || g_vrMenuPage==MENU_LOAD_MODES)hint=g_vrMenuDestination;
    if(g_vrMenuPage<0 && g_vrMenuRow==MENU_LOAD_GAME)hint="CHOOSE ARCADE OR ORIGINAL SAVE";
    if(g_vrMenuPage==MENU_STAGE_MODES || (g_vrMenuPage<0 && g_vrMenuRow==MENU_STAGE_SELECT))hint="CHOOSE MODE THEN STAGE AND SCENE";
    if(g_vrMenuPage<0 && g_vrMenuRow==MENU_RESET)
        hint=g_vrMenuResetPending?"DEFAULTS RESTORED   SAVE ON CLOSE":"RESET ALL OPTIONS   CHEATS OFF";
    if(g_vrMenuPage<0 && g_vrMenuRow==MENU_QUIT)hint="SAVE OPTIONS AND QUIT GAME";
    if(g_vrMenuPage<0 && g_vrMenuRow==MENU_TITLE)hint="END CURRENT GAME AND RETURN TO TITLE";
    char target[96];
    if(g_vrMenuPage==MENU_STAGES)hint="SELECT STAGE TO CHOOSE ITS SCENE";
    if(g_vrMenuPage==MENU_SCENES && count) {
        snprintf(target,sizeof(target),"START STAGE %u %s SCENE %u",g_vrMenuStage,vrMenuChapters[g_vrMenuStage-1],g_vrMenuScenes[g_vrMenuRow]);
        hint=target;
    }
    const char *notice=NULL;
    if(g_vrMenuPage>=0 && g_vrMenuPage<MENU_GROUP_COUNT) {
        int flags=vrMenuItems[g_vrMenuPage][g_vrMenuRow].flags;
        if(flags&MENU_RESTART_VIEWER)notice="RESTART VIEWER TO APPLY";
        if(flags&MENU_RESTART_GAME)notice="RESTART GAME TO APPLY";
        if((flags&MENU_CAPTURE) && g_nativePaused)notice="APPLIES WHEN GAME RESUMES";
    }
    vrMenuText(48,378,hint,.65f,grey);
    notice=g_vrMenuError?g_vrMenuError:g_previewOnly?"READ ONLY PREVIEW":g_vrMenuStatus[0]?g_vrMenuStatus:notice;
    if(notice)vrMenuText(48,400,notice,.5f,grey);
    if(count>perPage){char page[32];snprintf(page,sizeof(page),"PAGE %u OF %u",first/perPage+1,(count+perPage-1)/perPage);
        vrMenuText(440,72,page,.6f,grey);vrMenuText(440,356,page,.6f,grey);}
    vrMenuRect(42,418,552,1,0xFF303077);
    vrMenuText(44,427,"ARROWS MOVE   ENTER SELECT   CTRL BACK",.65f,white);
    vrMenuText(44,451,"F8 LEFT CLICK TOGGLE   STICK TRIGGER B IN VR",.6f,grey);
    g_vrMenuRevision++;g_vrMenuDirty=0;g_vrMenuTextureDirty=1;
}
static void vrMenuNativePublish(void)
{
    if(!g_frame || g_previewOnly)return;
    int open=g_vrMenuEnabled && g_vrMenuOpen;
    if(open){vrMenuDestination();if(g_vrMenuDirty)vrMenuCanvas();}
    if(open!=g_vrMenuPublishedOpen || (open && g_vrMenuRevision!=g_vrMenuPublishedRevision)) {
        g_frame->menu_seq++;MemoryBarrier();
        g_frame->menu_open=open;
        if(open)memcpy(g_frame->menu_pixels,g_vrMenuPixels,sizeof(g_vrMenuPixels));
        MemoryBarrier();g_frame->menu_seq++;
        g_vrMenuPublishedOpen=open;g_vrMenuPublishedRevision=g_vrMenuRevision;
    }
    MemoryBarrier();g_frame->menu_tick=GetTickCount();
}
static int vrMenuInput(unsigned input)
{
    g_vrMenuOwnsInput=vrMenuControl(input);
    vrMenuNativePublish();return g_vrMenuOwnsInput;
}
static void vrMenuKeyboard(void)
{
    int previous=g_vrMenuOwnsInput;
    int owned=vrMenuInput(vrMenuKeyboardInput());
    if(owned || previous){pushAim(320,240,0);pushAim2(320,240,0);}
}
static void vrMenuDraw(void)
{
    if(!g_vrMenuEnabled || !g_vrMenuOpen || g_inScope)return;
    vrMenuDestination();
    if(!g_vrMenuTexture) {
        D3D11_TEXTURE2D_DESC desc={0};desc.Width=640;desc.Height=480;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DYNAMIC;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        if(FAILED(ID3D11Device_CreateTexture2D(g_dev,&desc,NULL,&g_vrMenuTexture)) ||
           FAILED(ID3D11Device_CreateShaderResourceView(g_dev,(ID3D11Resource *)g_vrMenuTexture,NULL,&g_vrMenuSRV)))
            die("menu texture");
        g_vrMenuTextureDirty=1;
    }
    if(!g_vrMenuBlend) {
        D3D11_BLEND_DESC blend={0};
        blend.RenderTarget[0].BlendEnable=TRUE;
        blend.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        if(FAILED(ID3D11Device_CreateBlendState(g_dev,&blend,&g_vrMenuBlend)))die("menu blend state");
    }
    D3D11_MAPPED_SUBRESOURCE mapped;
    if(g_vrMenuDirty)vrMenuCanvas();
    if(g_vrMenuTextureDirty) {
        if(FAILED(ID3D11DeviceContext_Map(g_ctx,(ID3D11Resource *)g_vrMenuTexture,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return;
        for(unsigned y=0;y<480;y++)memcpy((BYTE *)mapped.pData+y*mapped.RowPitch,g_vrMenuPixels[y],640*4);
        ID3D11DeviceContext_Unmap(g_ctx,(ID3D11Resource *)g_vrMenuTexture,0);g_vrMenuTextureDirty=0;
    }
    if(SUCCEEDED(ID3D11DeviceContext_Map(g_ctx,(ID3D11Resource *)g_cbRes,0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) {
        const float res[4]={640,480,0,0};memcpy(mapped.pData,res,sizeof(res));
        ID3D11DeviceContext_Unmap(g_ctx,(ID3D11Resource *)g_cbRes,0);
    }
    UINT stride=sizeof(TexVtx),offset=0;
    ID3D11DeviceContext_OMSetDepthStencilState(g_ctx,g_dssUI,0);
    ID3D11DeviceContext_OMSetBlendState(g_ctx,g_vrMenuBlend,NULL,0xFFFFFFFF);
    ID3D11DeviceContext_IASetInputLayout(g_ctx,g_ilTex);
    ID3D11DeviceContext_IASetVertexBuffers(g_ctx,0,1,&g_scrVB,&stride,&offset);
    ID3D11DeviceContext_VSSetShader(g_ctx,g_vsTex,NULL,0);
    ID3D11DeviceContext_PSSetShader(g_ctx,g_psTex,NULL,0);
    ID3D11DeviceContext_PSSetConstantBuffers(g_ctx,1,1,&g_cbRes);
    ID3D11DeviceContext_PSSetShaderResources(g_ctx,0,1,&g_vrMenuSRV);
    ID3D11DeviceContext_PSSetSamplers(g_ctx,0,1,&g_samp);
    ID3D11DeviceContext_Draw(g_ctx,6,0);
    ID3D11DeviceContext_OMSetBlendState(g_ctx,NULL,NULL,0xFFFFFFFF);
}
