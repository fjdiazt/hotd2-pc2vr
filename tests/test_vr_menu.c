/* Real settings, input isolation, restart staging and atomic INI persistence. */
#include <assert.h>
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
    g_vrMenuEnabled=1;assert(vrMenuControl(0)==0 && !g_vrMenuOpen);
    g_nativePaused=1;assert(vrMenuControl(0)==0 && !g_vrMenuOpen);
    g_nativePaused=0;assert(vrMenuControl(0)==0);
    assert(vrMenuControl(MENU_TOGGLE)==1 && g_vrMenuOpen);
    assert(vrMenuControl(MENU_TOGGLE)==1 && g_vrMenuOpen);
    assert(vrMenuControl(0)==1);
    press(VC2_BTN_PAUSE);press(VC2_BTN_BACK);assert(g_vrMenuOpen && g_vrMenuPage==-1);
    int font=vrMenuLoadFont();if(font)image("build/menu-root.bmp");
    vrMenuCanvas();assert(g_vrMenuPixels[479][639]==0x8017191D);
    vrMenuRect(0,0,32,32,0x80FFFFFF);
    memset(g_vrMenuFont['?'][0][0],255,4);g_vrMenuFont['?'][0][0][3]=128;
    vrMenuText(0,0,"?",1,0xFFFFFFFF);assert(g_vrMenuPixels[0][0]==0xC0FFFFFF);
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
    g_vrMenuOpen=1;g_vrMenuPage=-1;g_vrMenuRow=5;
    g_vrMenuBlock=g_vrMenuArmed=0;g_vrMenuPrevious=0;g_nativePaused=1;
    press(VC2_BTN_DOWN);assert(g_vrMenuRow==6 && !strcmp(vrMenuGroups[6],"RESET DEFAULTS"));
    g_previewOnly=1;press(VC2_BTN_START);
    assert(g_cfgWorldScale==2 && g_cheatFlags==HOTD_CHEAT_AMMO && !g_vrMenuResetPending);
    g_previewOnly=0;press(VC2_BTN_START);
    assert(g_vrMenuPage==-1 && g_vrMenuRow==6 && g_vrMenuOpen && g_nativePaused);
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
    g_vrMenuOpen=1;g_vrMenuPage=-1;g_vrMenuRow=6;
    g_vrMenuBlock=g_vrMenuArmed=0;g_vrMenuPrevious=0;
    press(VC2_BTN_DOWN);assert(g_vrMenuRow==7 && !strcmp(vrMenuGroups[7],"QUIT GAME"));
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
