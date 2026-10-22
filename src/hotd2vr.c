/* HOTD2 PC adaptation of NeuralF HOTD2VR; see LICENSE and THIRD-PARTY.md. */
/*
 * HOTD2VR.exe - renders The House of the Dead 2 in a headset.
 *
 * The game is a 32-bit program from 1997 and SteamVR is 64-bit, so they run
 * apart. HOTD2Bridge.dll, loaded inside the game, turns each frame's screen-space
 * quads back into 3D and publishes them to shared memory. This program reads
 * that and draws it, once per eye, through OpenXR.
 *
 * Controllers (headset mode):
 *      trigger            shoot and native Start / confirm
 *      Head-relative up 50deg/down 45deg/sides 60deg + trigger   Reload that gun
 *      A / X              native Start / Pause
 *      B / Y              native Skip / Cancel
 *      Grip               Reload
 *      thumbstick four-way native menu arrows
 *      right stick click  recentre; left stick click VR Settings
 *
 * The controller is the light gun: its ray is cast against the actual level
 * geometry, the hit point is projected through the game's own camera, and the
 * resulting coordinate, including outside the original viewport, goes through
 * HOTD2Bridge.dll into native gun input. Laser and hit marker stay in the world.
 *
 * The floating screen shows a live capture of the game window. It appears by
 * itself whenever the 3D scene is nearly empty - menus, scores, name entry -
 * because those are 2D sprites that never reach the quad slot and would
 * otherwise leave the headset in a void. Point at the screen and the ray maps
 * straight onto it for aiming. Menus use thumbstick arrow keys.
 *
 * Independent outputs ([vr] window=0, vrwindow=1 by default):
 *      HOTD2VR.exe --window --no-vrwindow    ordinary desktop preview
 *      HOTD2VR.exe --window --vrwindow       desktop preview plus headset
 *
 * --window exists so that a failure can be pinned down. If the window shows the
 * scene but the headset does not, the problem is OpenXR; if neither shows it,
 * the problem is the handoff or the game side.
 *
 * Needs openxr_loader.dll beside the executable. It ships with the OpenXR SDK
 * release from Khronos and is not something SteamVR provides.
 */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include "openxr/openxr.h"
#include "openxr/openxr_platform.h"

#include "hotd_share.h"
#include "game_input.h"
#include "player_hud.h"
#include "audio_control.h"

#define NEAR_M 0.05f
#define FAR_M  4000.0f

static void die(const char *what)
{
    printf("\n[fail] %s\n", what);
    ExitProcess(1);
}

static void note(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

/* ------------------------------------------------------------------ matrices */
/* stored row-major as m[row * 4 + col]; the shader does mul(m, vector) */

static void mIdent(float *m)
{
    int i;
    for (i = 0; i < 16; i++) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mMul(const float *a, const float *b, float *o)
{
    float t[16];
    int r, c, k;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++) {
            float s = 0.0f;
            for (k = 0; k < 4; k++) s += a[r * 4 + k] * b[k * 4 + c];
            t[r * 4 + c] = s;
        }
    for (r = 0; r < 16; r++) o[r] = t[r];
}

/* right handed, looking down -Z, depth 0..1 - what both OpenXR and D3D want */
static void mProjFov(float aL, float aR, float aU, float aD, float zn, float zf, float *m)
{
    float tl = tanf(aL), tr = tanf(aR), tu = tanf(aU), td = tanf(aD);
    float w = tr - tl, h = tu - td;
    mIdent(m);
    m[0] = 2.0f / w;  m[2]  = (tr + tl) / w;
    m[5] = 2.0f / h;  m[6]  = (tu + td) / h;
    /* reversed depth: near maps to 1, far to 0. With a float depth buffer this
     * spends the precision where it matters and stops distant walls flickering. */
    m[10] = zn / (zf - zn);
    m[11] = zf * zn / (zf - zn);
    m[14] = -1.0f;
    m[15] = 0.0f;
}

static void mFromQuat(const XrQuaternionf *q, float *m)
{
    float x = q->x, y = q->y, z = q->z, w = q->w;
    mIdent(m);
    m[0]  = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y - z * w);     m[2]  = 2 * (x * z + y * w);
    m[4]  = 2 * (x * y + z * w);     m[5] = 1 - 2 * (x * x + z * z); m[6]  = 2 * (y * z - x * w);
    m[8]  = 2 * (x * z - y * w);     m[9] = 2 * (y * z + x * w);     m[10] = 1 - 2 * (x * x + y * y);
}

/* view matrix from a head pose: undo the rotation, then undo the translation */
static void mView(const XrPosef *p, float *out)
{
    float r[16], rt[16], t[16];
    int i, j;
    mFromQuat(&p->orientation, r);
    mIdent(rt);
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) rt[i * 4 + j] = r[j * 4 + i];
    mIdent(t);
    t[3] = -p->position.x; t[7] = -p->position.y; t[11] = -p->position.z;
    mMul(rt, t, out);
}

/* ---------------------------------------------------------------- shared mem */

static HANDLE    g_map;
static Vc2Frame *g_frame;
static Vc2Vertex g_vertexBuffers[2][VC2_MAX_VERTS];
static Vc2Vertex *g_local=g_vertexBuffers[0],*g_readVerts=g_vertexBuffers[1];
static unsigned  g_localCount;
static unsigned  g_lastFrame;
static unsigned  g_activePlayers;
static uint32_t g_shotEvents[2];
static float     g_shotOrigins[64][3];
static float     g_unitsPerMetre = 130.0f;
static float     g_gfx = 554.3f, g_gfy = 623.6f;   /* the game's projection */
static float     g_gcx = 320.0f, g_gcy = 240.0f;   /* and its centre        */
static int       g_projLive;        /* projection read out of the game itself */
static float     g_aimSX = 320.0f, g_aimSY = 240.0f;   /* last aim we pushed */
static void placeShotEffects(void);

static unsigned  g_addStart;            /* first translucent vertex        */
static unsigned  g_nativePaused;
/* v8: where the flat overlay starts. The DLL sorts the drum, the lives, the
 * credits line and its own HUD to the tail of the buffer and says so here, so
 * they can be drawn through a second matrix that the zoom never touches. An
 * older DLL says nothing (reserved[2] == 0) and everything stays as it was. */
static unsigned  g_ovStart;
static unsigned  g_scrPix[VC2_SCREEN_MAX_W * VC2_SCREEN_MAX_H];
static unsigned  g_atlasBuffers[2][VC2_ATLAS_W * VC2_ATLAS_H];
static unsigned *g_atlPix=g_atlasBuffers[0],*g_readAtlas=g_atlasBuffers[1];
static unsigned  g_atlSeqSeen;
static int       g_atlFresh;
static unsigned  g_scrW = 640, g_scrH = 480;    /* size of the last capture */

/* HOTD2VR.ini next to the exe */
static int g_cfgDepth;              /* [vr] depth: 0 painter, 1 engine keys, 2 true z-buffer */
static int g_cfgDepthBias = 2;      /* [vr] depthbias: per-quad paint-order bias, 1e-6 units */
static int g_cfgHaptic = 1;             /* [vr] haptic: buzz on every shot */
static int g_cfgMsaa = 4;         /* 1 / 2 / 4 / 8 */
static int g_cfgSuper = 140;      /* eye buffer scale in percent */
static float g_cfgWorldScale = 1.25f; /* world size multiplier; 1 preserves the native unit conversion */
static int g_cfgNativeLighting = 1;
static int g_cfgExpandMeshes = 1;
static int g_cfgExpandWorld;
static int g_cfgLaser = 1;
static int g_cfgCrosshair = 1;
static int g_cfgWindow, g_cfgVrWindow, g_previewOnly,g_vrMenuEnabled;
static int g_cfgSwapPlayerHud=1;
static int g_cfgContinueGame,g_cfgContinueEdited;
static int g_cfgBrightness = 90;
static int g_cfgGamma = 220;      /* [vr] gamma: linearise for the sRGB eye buffer, percent; 100 = off */
/* The eye swapchain is sRGB, so the GPU encodes whatever the shader writes.
 * The game's texels and shades are already gamma-encoded (a 1997 renderer
 * knew nothing else), so writing them raw encoded them TWICE - that was the
 * washed-out, too-bright headset picture the flat window never showed.
 * pow(x, gamma) before the write undoes it; the preview windows keep 1.0. */
static float g_gammaOut = 1.0f;
static int g_cfgZoomX = 400;      /* zoom magnification in percent (400 = 4x) */
static int g_cfgZoomSpd = 4;      /* zoom glide, percent of the gap per frame */
static int g_cfgReach = 6;        /* metres to the aim point when the ray misses */
static int g_cfgAutoZoom = 1;     /* follow the game's own camera zoom */
static int g_cfgFocus = 1;        /* pull the game window to the front on start */
static int g_cfgAkimbo = 0;       /* Show/join both together, or each player independently. */
static char g_vrIniPath[MAX_PATH];
static int g_cfgScope;            /* 1 = optic on the gun, 0 = magnify the whole view */
static int g_cfgScopeDeg = 12;    /* angular radius of the disc, degrees */
static int g_cfgScopeDist = 25;   /* how far out the head-locked disc hangs, m */
static int g_cfgScopeMount = 1;   /* 1 = glass on the gun, 0 = disc on the head */
static int g_cfgScopeGlass = 26;  /* gun sight glass radius, millimetres        */
static int g_cfgHudZoom;          /* 1 = let the HUD magnify with the world     */
static int g_cfgHudSharp = 1;     /* sharp-bilinear the overlay instead of soft */
static int g_cfgAutoAt = 30;      /* autozoom engages past this % of extra zoom */
static int g_cfgZoomSmooth = 55;  /* %% of the zoomed view's wobble to damp out    */
static char g_cfgGun[MAX_PATH];   /* OBJ to draw in the gun hand; empty = none */
static int  g_cfgGunScale = 100;  /* percent */
static int  g_cfgReloadMs = 180;  /* reload flick, ms */
static unsigned g_cheatFlags;
static unsigned g_cfgRapidRate=4;
static unsigned g_cfgMasterVolume=100,g_cfgMusicVolume=100,g_cfgEffectsVolume=100;
static unsigned readAudioVolume(const char *path,const char *key)
{
    char text[32];unsigned volume;
    DWORD n=GetPrivateProfileStringA("vr",key,"100",text,sizeof(text),path);
    if(n<sizeof(text)-1 && hotd_parse_volume(text,&volume))return volume;
    note("Invalid %s; using 100%% (allowed 0..100).\n",key);return 100;
}
static void readGraphicsFlags(const char *path)
{
    g_cfgSwapPlayerHud=GetPrivateProfileIntA("vr","swap_player_hud",1,path)!=0;
    g_cfgLaser=GetPrivateProfileIntA("vr","laser",1,path)!=0;
    g_cfgCrosshair=GetPrivateProfileIntA("vr","crosshair",1,path)!=0;
    g_cfgWindow=GetPrivateProfileIntA("vr","window",0,path)!=0;
    g_cfgVrWindow=GetPrivateProfileIntA("vr","vrwindow",1,path)!=0;
    g_cfgNativeLighting=GetPrivateProfileIntA("vr","native_lighting",1,path)!=0;
    g_cfgExpandMeshes=GetPrivateProfileIntA("vr","expand_meshes",1,path)!=0;
    g_cfgExpandWorld=GetPrivateProfileIntA("vr","expand_world",0,path)!=0;
}
static int parseWorldScale(const char *text,float *scale)
{
    char *end;float value;
    if(!text || ((*text<'0' || *text>'9') && *text!='.'))return 0;
    value=strtof(text,&end);
    if(end==text || *end || !isfinite(value) || value<.1f || value>10.0f)return 0;
    *scale=value;return 1;
}
static int readWorldScale(const char *path,float *scale)
{
    char text[32];
    GetPrivateProfileStringA("vr","worldscale","1.25",text,sizeof(text),path);
    return parseWorldScale(text,scale);
}
static int readRapidRate(const char *path,unsigned *rate)
{
    char text[32];
    GetPrivateProfileStringA("vr","rapid_fire_rate","4",text,sizeof(text),path);
    return hotd_parse_rapid_rate(text,rate);
}
static int parseBrightness(const char *text,int *brightness)
{
    char *end;long value;
    if(!text || *text<'0' || *text>'9')return 0;
    value=strtol(text,&end,10);
    if(*end || value<25 || value>400)return 0;
    *brightness=(int)value;return 1;
}
static int readBrightness(const char *path,int *brightness)
{
    char text[32];
    GetPrivateProfileStringA("vr","brightness","90",text,sizeof(text),path);
    return parseBrightness(text,brightness);
}
static unsigned readCheats(const char *path)
{
    return (GetPrivateProfileIntA("vr","infinite_ammo",0,path)?HOTD_CHEAT_AMMO:0) |
        (GetPrivateProfileIntA("vr","infinite_lives",0,path)?HOTD_CHEAT_LIVES:0) |
        (GetPrivateProfileIntA("vr","invincibility",0,path)?HOTD_CHEAT_INVINCIBILITY:0) |
        (GetPrivateProfileIntA("vr","infinite_continues",0,path)?HOTD_CHEAT_CONTINUES:0) |
        (GetPrivateProfileIntA("vr","rapid_fire",0,path)?HOTD_CHEAT_RAPID:0);
}
static unsigned cheatArg(const char *arg)
{
    if(!strcmp(arg,"--infinite-ammo"))return HOTD_CHEAT_AMMO;
    if(!strcmp(arg,"--infinite-lives"))return HOTD_CHEAT_LIVES;
    if(!strcmp(arg,"--invincibility"))return HOTD_CHEAT_INVINCIBILITY;
    if(!strcmp(arg,"--infinite-continues"))return HOTD_CHEAT_CONTINUES;
    if(!strcmp(arg,"--rapid-fire"))return HOTD_CHEAT_RAPID;
    return 0;
}

static void loadCfg(void)
{
    char *path = g_vrIniPath;
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    while (n > 0 && path[n - 1] != '\\') n--;
    lstrcpyA(path + n, "HOTD2VR.ini");
    readGraphicsFlags(path);
    g_vrMenuEnabled=GetPrivateProfileIntA("vr","menu",1,path)!=0;
    g_cfgContinueGame=GetPrivateProfileIntA("vr","continue_game",0,path)!=0;
    g_cheatFlags=readCheats(path);
    g_cfgMasterVolume=readAudioVolume(path,"master_volume");
    g_cfgMusicVolume=readAudioVolume(path,"music_volume");
    g_cfgEffectsVolume=readAudioVolume(path,"effects_volume");
    if(!readBrightness(path,&g_cfgBrightness)) {
        note("Invalid brightness; using 90%% (allowed 25..400).\n");g_cfgBrightness=90;
    }
    if(!readRapidRate(path,&g_cfgRapidRate)) {
        note("Invalid rapid_fire_rate; using 4 shots/sec (allowed 1..30).\n");g_cfgRapidRate=4;
    }
    if(!readWorldScale(path,&g_cfgWorldScale)) {
        note("Invalid worldscale; using 1.25x (allowed 0.1..10).\n");g_cfgWorldScale=1.25f;
    }
    g_cfgMsaa  = (int)GetPrivateProfileIntA("vr", "msaa", 4, path);
    g_cfgDepth = (int)GetPrivateProfileIntA("vr", "depth", 0, path);
    g_cfgDepthBias = (int)GetPrivateProfileIntA("vr", "depthbias", 2, path);
    if (g_cfgDepthBias < 0)   g_cfgDepthBias = 0;
    if (g_cfgDepthBias > 200) g_cfgDepthBias = 200;
    g_cfgHaptic = (int)GetPrivateProfileIntA("vr", "haptic", 1, path);
    g_cfgSuper = (int)GetPrivateProfileIntA("vr", "supersample", 140, path);
    g_cfgGamma = (int)GetPrivateProfileIntA("vr", "gamma", 220, path);
    g_cfgZoomX   = (int)GetPrivateProfileIntA("vr", "zoom", 400, path);
    g_cfgZoomSpd = (int)GetPrivateProfileIntA("vr", "zoomspeed", 4, path);
    g_cfgReach   = (int)GetPrivateProfileIntA("vr", "aimreach", 6, path);
    g_cfgAutoZoom  = (int)GetPrivateProfileIntA("vr", "autozoom", 1, path);
    g_cfgAkimbo    = (int)GetPrivateProfileIntA("vr", "akimbo", 0, path);
    g_cfgFocus     = (int)GetPrivateProfileIntA("vr", "focusgame", 1, path);
    g_cfgScope     = (int)GetPrivateProfileIntA("vr", "scope", 0, path);
    g_cfgScopeDeg  = (int)GetPrivateProfileIntA("vr", "scopesize", 12, path);
    g_cfgScopeDist = (int)GetPrivateProfileIntA("vr", "scopedist", 25, path);
    g_cfgScopeMount= (int)GetPrivateProfileIntA("vr", "scopemount", 1, path);
    g_cfgScopeGlass= (int)GetPrivateProfileIntA("vr", "scopeglass", 26, path);
    g_cfgHudZoom   = (int)GetPrivateProfileIntA("vr", "hudzoom", 0, path);
    g_cfgHudSharp  = (int)GetPrivateProfileIntA("vr", "hudsharp", 1, path);
    g_cfgAutoAt    = (int)GetPrivateProfileIntA("vr", "autozoomat", 30, path);
    g_cfgZoomSmooth= (int)GetPrivateProfileIntA("vr", "zoomsmooth", 55, path);
    GetPrivateProfileStringA("vr", "gunmodel", "", g_cfgGun, MAX_PATH, path);
    g_cfgGunScale  = (int)GetPrivateProfileIntA("vr", "gunscale", 100, path);
    g_cfgReloadMs  = (int)GetPrivateProfileIntA("vr", "reloadms", 180, path);
    if (g_cfgReloadMs < 60)   g_cfgReloadMs = 60;
    if (g_cfgReloadMs > 1000) g_cfgReloadMs = 1000;
    if (g_cfgGunScale < 10)   g_cfgGunScale = 10;
    if (g_cfgGunScale > 1000) g_cfgGunScale = 1000;
    if (g_cfgGun[0] && g_cfgGun[1] != ':' && g_cfgGun[0] != '\\') {
        /* relative to the exe, like the ini itself */
        char full[MAX_PATH];
        lstrcpynA(full, path, (int)(n + 1));
        lstrcpynA(full + n, g_cfgGun, (int)(MAX_PATH - n));
        lstrcpyA(g_cfgGun, full);
    }
    if (g_cfgScopeDeg < 3)  g_cfgScopeDeg = 3;
    if (g_cfgScopeDeg > 40) g_cfgScopeDeg = 40;
    if (g_cfgScopeGlass < 8)   g_cfgScopeGlass = 8;
    if (g_cfgScopeGlass > 120) g_cfgScopeGlass = 120;
    if (g_cfgScopeDist < 2)   g_cfgScopeDist = 2;
    if (g_cfgScopeDist > 200) g_cfgScopeDist = 200;
    if (g_cfgZoomSmooth < 0)  g_cfgZoomSmooth = 0;
    if (g_cfgZoomSmooth > 90) g_cfgZoomSmooth = 90;
    if (g_cfgAutoAt < 5)   g_cfgAutoAt = 5;
    if (g_cfgAutoAt > 200) g_cfgAutoAt = 200;
    if (g_cfgReach < 1)   g_cfgReach = 1;
    if (g_cfgReach > 100) g_cfgReach = 100;
    if (g_cfgZoomX < 110) g_cfgZoomX = 110;
    if (g_cfgZoomX > 1200) g_cfgZoomX = 1200;
    if (g_cfgZoomSpd < 1)  g_cfgZoomSpd = 1;
    if (g_cfgZoomSpd > 50) g_cfgZoomSpd = 50;
    if (g_cfgMsaa < 1) g_cfgMsaa = 1;
    if (g_cfgMsaa > 8) g_cfgMsaa = 8;
    if (g_cfgSuper < 50)  g_cfgSuper = 50;
    if (g_cfgSuper > 250) g_cfgSuper = 250;
}
static unsigned  g_scrSeqSeen;
static int       g_scrFresh;        /* new pixels waiting to be uploaded */
static int       g_scrHave;         /* capture is alive at all */

static int openShare(void)
{
    DWORD access=g_previewOnly?FILE_MAP_READ:FILE_MAP_ALL_ACCESS;
    g_map = OpenFileMappingA(access, FALSE, VC2_SHARE_NAME);
    if (!g_map) return 0;
    g_frame = (Vc2Frame *)MapViewOfFile(g_map, access, 0, 0, sizeof(Vc2Frame));
    if (!g_frame || g_frame->magic != VC2_SHARE_MAGIC || g_frame->version != VC2_SHARE_VERSION) {
        note("bridge protocol mismatch\n");
        if (g_frame) UnmapViewOfFile(g_frame);
        CloseHandle(g_map); g_map = NULL; g_frame = NULL;
        return 0;
    }
    return 1;
}

static void pushCheats(void)
{
    if(!g_frame || g_previewOnly)return;
    g_frame->render_flags=(g_cfgNativeLighting?0:HOTD_RENDER_UNLIT) |
        (g_cfgExpandMeshes?0:HOTD_RENDER_NATIVE_MESH_CULLING) |
        (g_cfgExpandWorld?HOTD_RENDER_EXPAND_WORLD:0);
    g_frame->rapid_fire_rate=g_cfgRapidRate;
    g_frame->audio_levels=hotd_pack_audio(g_cfgMasterVolume,g_cfgMusicVolume,g_cfgEffectsVolume);
    g_frame->cheat_flags=g_cheatFlags;
    if(g_cfgContinueEdited)g_frame->checkpoint_autosave=g_cfgContinueGame?2:1;
    MemoryBarrier();
    g_frame->cheat_tick=GetTickCount();
}
/* seqlock read: copy, then check nothing moved underneath us */
static int pullFrame(void)
{
    unsigned a, b, n, i, atlasSeq;
    if (!g_frame) return 0;
    pushCheats();
    for (i = 0; i < 8; i++) {
        HotdLens lens;
        unsigned lensValid,addStart,paused,ovStart,frameNo,activePlayers;
        uint32_t shotEvents[2];
        float units,fov;
        float shotOrigins[64][3];
        a = g_frame->seq;
        if (a & 1u) continue;
        MemoryBarrier();
        if (g_frame->magic != VC2_SHARE_MAGIC || g_frame->version != VC2_SHARE_VERSION) return 0;
        lens=g_frame->lens;lensValid=g_frame->lens_valid;
        frameNo=g_frame->frame;units=g_frame->units_per_metre;fov=g_frame->fov_degrees;
        activePlayers=g_frame->active_players;
        memcpy(shotEvents,g_frame->shot_events,sizeof(shotEvents));
        CopyMemory(shotOrigins,g_frame->shot_origins,sizeof(shotOrigins));
        n = g_frame->vertex_count;
        if (n > VC2_MAX_VERTS) n = VC2_MAX_VERTS;
        addStart = g_frame->reserved[0];
        paused = g_frame->reserved[1];
        {   unsigned r2 = g_frame->reserved[2];
            ovStart = (r2 && r2 - 1u <= n) ? r2 - 1u : n;
        }
        CopyMemory(g_readVerts, g_frame->verts, n * sizeof(Vc2Vertex));
        atlasSeq=g_frame->atlas_seq;
        if(atlasSeq&1u)continue;
        if(atlasSeq!=g_atlSeqSeen)
            CopyMemory(g_readAtlas,(const void *)g_frame->atlas,sizeof(g_atlasBuffers[0]));
        MemoryBarrier();
        b = g_frame->seq;
        if (a == b && atlasSeq == g_frame->atlas_seq) {
            Vc2Vertex *previous=g_local;
            g_local=g_readVerts;g_readVerts=previous;
            if(atlasSeq!=g_atlSeqSeen) {
                unsigned *oldAtlas=g_atlPix;
                g_atlPix=g_readAtlas;g_readAtlas=oldAtlas;
                g_atlSeqSeen=atlasSeq;g_atlFresh=1;
            }
            if(lensValid) {
                g_gfx=lens.fx;g_gfy=lens.fy;g_gcx=lens.cx;g_gcy=lens.cy;g_projLive=1;
            }
            if(isfinite(units) && units>1.0f) {
                g_unitsPerMetre=units/g_cfgWorldScale;
                for(unsigned v=0;v<n;v++) {
                    float scale;
                    /* Native book animation is authored around a one-unit camera plane. */
                    if(g_local[v].address&HOTD_GFILE)scale=g_unitsPerMetre*HOTD_GFILE_DISTANCE_M;
                    else if(v>=ovStart && g_cfgWorldScale!=1.0f)scale=g_unitsPerMetre/units;
                    else continue;
                    g_local[v].x*=scale;g_local[v].y*=scale;g_local[v].z*=scale;
                }
            }
            if(!g_projLive && fov>5.0f && fov<170.0f) {
                double half=fov*3.14159265358979/360.0;
                g_gfx=(float)(320.0/tan(half));g_gfy=1.125f*g_gfx;
            }
            if(g_cfgSwapPlayerHud)hotd_swap_player_hud(g_local,ovStart,n);
            g_addStart=addStart;g_nativePaused=paused;g_ovStart=ovStart;
            g_localCount = n;
            g_lastFrame = frameNo;
            g_activePlayers=activePlayers;
            memcpy(g_shotEvents,shotEvents,sizeof(shotEvents));
            memcpy(g_shotOrigins,shotOrigins,sizeof(shotOrigins));
            placeShotEffects();
            return 1;
        }
    }
    return 0;
}

/* same idea for the captured game screen; a torn frame is retried next time */
static void pullScreen(void)
{
    unsigned a, b;
    if (!g_frame) return;
    a = g_frame->screen_seq;
    if ((a & 1u) || a == g_scrSeqSeen) return;
    MemoryBarrier();
    if (!g_frame->screen_ok) { g_scrSeqSeen = a; return; }
    {
        unsigned w = g_frame->screen_w, h = g_frame->screen_h;
        if (w < 8 || h < 8 || w > VC2_SCREEN_MAX_W || h > VC2_SCREEN_MAX_H) {
            g_scrSeqSeen = a;
            return;
        }
        CopyMemory(g_scrPix, (const void *)g_frame->screen, (size_t)w * h * 4);
        MemoryBarrier();
        b = g_frame->screen_seq;
        if (a == b) {
            g_scrW = w;
            g_scrH = h;
            g_scrSeqSeen = a;
            g_scrFresh = 1;
            g_scrHave = 1;
        }
    }
}

/* Game lifecycle and projection come from the bridge, never VC2 addresses. */
static HANDLE g_gameProc;
static HWND g_gameWnd;
static DWORD g_stopAt;
static int g_focusDone;
static float g_gfxBase;
static void gameFocus(void)
{
    if (!g_cfgFocus || g_focusDone || !g_frame || !g_frame->game_hwnd) return;
    g_gameWnd = (HWND)(ULONG_PTR)g_frame->game_hwnd;
    g_focusDone = 1;
    ShowWindow(g_gameWnd, SW_RESTORE);
    SetForegroundWindow(g_gameWnd);
}
static int gameExited(void)
{
    if (g_stopAt && (LONG)(GetTickCount()-g_stopAt) >= 0) return 1;
    if (!g_frame) return 0;
    if (!g_gameProc && g_frame->game_pid)
        g_gameProc = OpenProcess(SYNCHRONIZE, FALSE, g_frame->game_pid);
    return g_gameProc && WaitForSingleObject(g_gameProc, 0) == WAIT_OBJECT_0;
}
static void gameProjRead(void) { /* Lens is copied under the frame seqlock. */ }

/* aim result, VR -> game, single writer so a bare bump after the write is fine */
static void pushAim(float sx, float sy, unsigned buttons)
{
    if (!g_frame || g_previewOnly) return;
    g_aimSX = sx; g_aimSY = sy;
    g_frame->aim_x = sx;
    g_frame->aim_y = sy;
    g_frame->aim_buttons = buttons;
    MemoryBarrier();
    g_frame->aim_seq++;
}

/* second gun (akimbo). Its own block in shared memory; the DLL feeds it to the
 * game's joystick id 1 only when g_frame->akimbo is set. */
static void pushAim2(float sx, float sy, unsigned buttons)
{
    if (!g_frame || g_previewOnly) return;
    g_frame->aim2_x = sx;
    g_frame->aim2_y = sy;
    g_frame->aim2_buttons = buttons;
    MemoryBarrier();
    g_frame->aim2_seq++;
}

/* --------------------------------------------------------------------- D3D11 */

static ID3D11Device        *g_dev;
static ID3D11DeviceContext *g_ctx;
static ID3D11Buffer        *g_vb, *g_cb;
static ID3D11VertexShader  *g_vs;
static ID3D11PixelShader   *g_ps;
static ID3D11PixelShader   *g_psOv;   /* same rules, sharp-bilinear fetch */
static ID3D11InputLayout   *g_il;
static ID3D11RasterizerState *g_rs;
static ID3D11RasterizerState *g_nativeCull[4];
static ID3D11DepthStencilState *g_dss;
static ID3D11DepthStencilState *g_dssRead;
static ID3D11BlendState *g_nativeBlend[12][12];
static ID3D11DepthStencilState *g_dssUI;    /* no depth: laser and screen */

/* overlay: laser beam and hit marker, plain coloured triangles, local space */
#define OVERLAY_MAX 2048
static ID3D11Buffer *g_ovVB;
static Vc2Vertex     g_ovVerts[OVERLAY_MAX];
static unsigned      g_ovCount;

/* the floating screen: textured quad, local space */
typedef struct { float x, y, z, u, v; } TexVtx;
static ID3D11Buffer            *g_scrVB;
static ID3D11VertexShader      *g_vsTex;
static ID3D11PixelShader       *g_psTex;
static ID3D11InputLayout       *g_ilTex;
static ID3D11Texture2D         *g_scrTex;
static ID3D11ShaderResourceView *g_scrSRV;
static ID3D11SamplerState      *g_samp;
static ID3D11Buffer            *g_cbRes;
static ID3D11BlendState        *g_blendAdd;
static ID3D11Texture2D          *g_atlTex;
static ID3D11ShaderResourceView *g_atlSRV;

/* where the screen hangs, recomputed on every recentre */
static float g_scrC[3]  = { 0, 0, -2.0f };  /* centre       */
static float g_scrR[3]  = { 1, 0, 0 };      /* right        */
static float g_scrU[3]  = { 0, 1, 0 };      /* up           */
static float g_scrN[3]  = { 0, 0, 1 };      /* towards user */
static float g_scrHalfW = HOTD_PANEL_HALF_WIDTH_M;
static float g_scrHalfH = HOTD_PANEL_HALF_HEIGHT_M;
static int   g_scrMode;                     /* 0 auto, 1 on, 2 off */
static int   g_scrVisible;
static int   g_inScope;                     /* drawing INTO the sight, not the world */
static int screenVisible(void)
{
    return !g_nativePaused && ((g_scrMode==1) || (g_scrMode==0 && g_ovStart<900));
}

#define HOTD_ALPHA_SHADER \
"float opacity(VOut i,float4 s,bool flat) { return saturate((flat || (i.style & 512u)) ? i.c.a : 1.0) * (flat ? 1.0 : s.a); }\n" \
"void alphaTest(float a,uint style) { if(!(style & 1024u)) return; uint v=(uint)(saturate(a)*255.0+0.5); uint r=(style>>14u)&255u; uint f=(style>>11u)&7u;\n" \
" bool accepted=f==0u || (f==2u && v<r) || (f==3u && v==r) || (f==4u && v<=r) || (f==5u && v>r) || (f==6u && v!=r) || (f==7u && v>=r); clip(accepted ? 1.0 : -1.0); }\n"

#define HOTD_ADDRESS_SHADER \
"float addressCoord(float t,uint mode) { return mode==1u ? 1.0-abs(1.0-2.0*frac(t*0.5)) : mode==2u ? saturate(t) : frac(t); }\n" \
"float2 addressUV(float2 t,uint modes) { return float2(addressCoord(t.x,modes&3u),addressCoord(t.y,(modes>>2u)&3u)); }\n"

#define HOTD_LIGHT_SHADER \
"float3 nativeShade(float3 rgb,VOut i) { rgb=saturate(rgb+i.spec.rgb); uint mode=i.fogcol>>24u;\n" \
" if(mode!=0u) { float f=mode==2u ? exp(-i.fog) : mode==3u ? exp(-i.fog*i.fog) : i.fog;\n" \
" float3 c=float3(i.fogcol&255u,(i.fogcol>>8u)&255u,(i.fogcol>>16u)&255u)/255.0; rgb=lerp(c,rgb,saturate(f)); } return rgb; }\n"

/* World/overlay share native material opacity and alpha-test rules.
 * A zero atlas width marks an untextured primitive; draw runs carry blending. */
static const char *SRC =
"cbuffer C : register(b0) { row_major float4x4 mvp; };\n"
"Texture2D atlas : register(t0); SamplerState smp : register(s0);\n"
"struct VIn  { float3 p : POSITION; float4 c : COLOR; float2 t : TEXCOORD0; float d : TEXCOORD1; float4 r : TEXCOORD2; uint style : TEXCOORD3; uint address : TEXCOORD4; float4 spec : COLOR1; uint fogcol : TEXCOORD5; float fog : TEXCOORD6; };\n"
"struct VOut { float4 p : SV_POSITION; float4 c : COLOR; float2 t : TEXCOORD0; float d : TEXCOORD1; float4 r : TEXCOORD2; nointerpolation uint style : TEXCOORD3; nointerpolation uint address : TEXCOORD4; float4 spec : COLOR1; nointerpolation uint fogcol : TEXCOORD5; float fog : TEXCOORD6; };\n"
HOTD_ALPHA_SHADER
HOTD_ADDRESS_SHADER
HOTD_LIGHT_SHADER
"struct POut { float4 c : SV_TARGET; float d : SV_Depth; };\n"
"#ifdef TRUEZ\n"
/* TRUE Z. The DLL's exact geometry carries real positions, so the hardware
 * depth of the projected vertex is the right depth from ANY head pose - the
 * painter's list was sorted for the game's camera, not for a head that has
 * turned and stepped aside. The engine's own order survives only as a tie
 * breaker: quad k (six vertices each, in paint order) is pulled toward the
 * eye by BIAS * k relative, so a decal painted after its wall still sits on
 * the wall instead of fighting it. */
"VOut vs(VIn i, uint vid : SV_VertexID) { VOut o; o.p = mul(mvp, float4(i.p,1));\n"
"    o.p.z *= 1.0 + BIAS * (float)(vid / 6u); o.c = i.c; o.t = i.t; o.d = i.d; o.r = i.r; o.style=i.style; o.address=i.address; o.spec=i.spec; o.fogcol=i.fogcol; o.fog=i.fog; return o; }\n"
"#define POut float4\n"
"#else\n"
"VOut vs(VIn i) { VOut o; o.p = mul(mvp, float4(i.p,1)); o.c = i.c; o.t = i.t; o.d = i.d; o.r = i.r; o.style=i.style; o.address=i.address; o.spec=i.spec; o.fogcol=i.fogcol; o.fog=i.fog; return o; }\n"
"#endif\n"
"#ifdef TRUEZ\n"
"float4 ps(VOut i) : SV_TARGET {\n"
"#else\n"
"POut ps(VOut i) {\n"
"#endif\n"
"    POut o;\n"
"    float4 s = atlas.Sample(smp, i.r.xy + addressUV(i.t,i.address) * i.r.zw);\n"
"    bool flat = i.r.z <= 0;\n"
"    float a=opacity(i,s,flat); alphaTest(a,i.style);\n"
"    float3 rgb = nativeShade(flat ? i.c.rgb : saturate(s.rgb * i.c.rgb),i); rgb=saturate(rgb*(i.d>=1.0 ? 1.0 : BRIGHTNESS));\n"
"#ifdef TRUEZ\n"
"    return float4(pow(rgb, GAMMA), a);\n"
"#else\n"
"    o.c = float4(pow(rgb, GAMMA), a);\n"
"    o.d = i.d;\n"
"    return o;\n"
"#endif\n"
"}\n";

/* Sharp bilinear: snap sample positions to texel centres, but blend across
 * exactly one destination pixel at each texel seam. Pixel art stays crisp on
 * the two-metre screen without the shimmer of plain nearest sampling. */
static const char *SRC_TEX =
"cbuffer C : register(b0) { row_major float4x4 mvp; };\n"
"cbuffer R : register(b1) { float2 res; float2 pad; };\n"
"Texture2D tex : register(t0); SamplerState smp : register(s0);\n"
"struct VIn  { float3 p : POSITION; float2 t : TEXCOORD; };\n"
"struct VOut { float4 p : SV_POSITION; float2 t : TEXCOORD; };\n"
"VOut vs(VIn i) { VOut o; o.p = mul(mvp, float4(i.p,1)); o.t = i.t; return o; }\n"
"float4 ps(VOut i) : SV_TARGET {\n"
"    float2 pix = i.t * res - 0.5;\n"
"    float2 c   = floor(pix);\n"
"    float2 f   = pix - c;\n"
"    float2 w   = clamp(fwidth(pix), 1e-5, 1.0);\n"
"    f = clamp((f - 0.5) / w, -0.5, 0.5) + 0.5;\n"
"    float4 s = tex.Sample(smp, (c + 0.5 + f) / res);\n"
"    return float4(pow(s.rgb, GAMMA), s.a);\n"
"}\n";

/* The overlay is 72x72 texels of 1997 art shown at twice that on the lens, and
 * plain bilinear at 2x is a smear. Sharp bilinear snaps every sample to a texel
 * centre and blends across exactly one destination pixel at the seam: the art
 * is untouched, the magnification is crisp, and there is none of the crawling
 * that nearest sampling gives on a head that never holds still. Same clip and
 * same colour rules as the world shader - only the fetch differs. */
#define VC2_STR2(x) #x
#define VC2_STR(x)  VC2_STR2(x)
static const char *SRC_OV =
"cbuffer C : register(b0) { row_major float4x4 mvp; };\n"
"Texture2D atlas : register(t0); SamplerState smp : register(s0);\n"
"struct VIn  { float3 p : POSITION; float4 c : COLOR; float2 t : TEXCOORD0; float d : TEXCOORD1; float4 r : TEXCOORD2; uint style : TEXCOORD3; uint address : TEXCOORD4; float4 spec : COLOR1; uint fogcol : TEXCOORD5; float fog : TEXCOORD6; };\n"
"struct VOut { float4 p : SV_POSITION; float4 c : COLOR; float2 t : TEXCOORD0; float d : TEXCOORD1; float4 r : TEXCOORD2; nointerpolation uint style : TEXCOORD3; nointerpolation uint address : TEXCOORD4; float4 spec : COLOR1; nointerpolation uint fogcol : TEXCOORD5; float fog : TEXCOORD6; };\n"
HOTD_ALPHA_SHADER
HOTD_ADDRESS_SHADER
HOTD_LIGHT_SHADER
"struct POut { float4 c : SV_TARGET; float d : SV_Depth; };\n"
"#ifdef TRUEZ\n"
/* TRUE Z. The DLL's exact geometry carries real positions, so the hardware
 * depth of the projected vertex is the right depth from ANY head pose - the
 * painter's list was sorted for the game's camera, not for a head that has
 * turned and stepped aside. The engine's own order survives only as a tie
 * breaker: quad k (six vertices each, in paint order) is pulled toward the
 * eye by BIAS * k relative, so a decal painted after its wall still sits on
 * the wall instead of fighting it. */
"VOut vs(VIn i, uint vid : SV_VertexID) { VOut o; o.p = mul(mvp, float4(i.p,1));\n"
"    o.p.z *= 1.0 + BIAS * (float)(vid / 6u); o.c = i.c; o.t = i.t; o.d = i.d; o.r = i.r; o.style=i.style; o.address=i.address; o.spec=i.spec; o.fogcol=i.fogcol; o.fog=i.fog; return o; }\n"
"#define POut float4\n"
"#else\n"
"VOut vs(VIn i) { VOut o; o.p = mul(mvp, float4(i.p,1)); o.c = i.c; o.t = i.t; o.d = i.d; o.r = i.r; o.style=i.style; o.address=i.address; o.spec=i.spec; o.fogcol=i.fogcol; o.fog=i.fog; return o; }\n"
"#endif\n"
"#ifdef TRUEZ\n"
"float4 ps(VOut i) : SV_TARGET {\n"
"#else\n"
"POut ps(VOut i) {\n"
"#endif\n"
"    POut o;\n"
"    const float2 res = float2(" VC2_STR(VC2_ATLAS_W) ", " VC2_STR(VC2_ATLAS_H) ");\n"
"    float2 pix = (i.r.xy + addressUV(i.t,i.address) * i.r.zw) * res - 0.5;\n"
"    float2 c0  = floor(pix);\n"
"    float2 f   = pix - c0;\n"
"    float2 w   = clamp(fwidth(pix), 1e-5, 1.0);\n"
"    f = clamp((f - 0.5) / w, -0.5, 0.5) + 0.5;\n"
"    float4 s = atlas.Sample(smp, (c0 + 0.5 + f) / res);\n"
"    bool flat = i.r.z <= 0;\n"
"    float a=opacity(i,s,flat); alphaTest(a,i.style);\n"
"    float3 rgb = nativeShade(flat ? i.c.rgb : saturate(s.rgb * i.c.rgb),i); rgb=saturate(rgb*(i.d>=1.0 ? 1.0 : BRIGHTNESS));\n"
"#ifdef TRUEZ\n"
"    return float4(pow(rgb, GAMMA), a);\n"
"#else\n"
"    o.c = float4(pow(rgb, GAMMA), a);\n"
"    o.d = i.d;\n"
"    return o;\n"
"#endif\n"
"}\n";

static HRESULT compileG(const char *src, const char *entry, const char *prof, ID3DBlob **out, ID3DBlob **err)
{
    D3D_SHADER_MACRO m[5];
    char g[32], b[32], gain[32];
    int n = 0;
    sprintf(g, "%.4f", g_gammaOut);
    sprintf(gain, "%.4f", g_cfgBrightness/100.0);
    sprintf(b, "%.8f", (double)g_cfgDepthBias * 1e-6);
    m[n].Name = "GAMMA"; m[n].Definition = g; n++;
    m[n].Name = "BRIGHTNESS"; m[n].Definition = gain; n++;
    m[n].Name = "BIAS";  m[n].Definition = b; n++;
    if (g_cfgDepth >= 2) { m[n].Name = "TRUEZ"; m[n].Definition = "1"; n++; }
    m[n].Name = NULL; m[n].Definition = NULL;
    return D3DCompile(src, lstrlenA(src), NULL, m, NULL, entry, prof, 0, 0, out, err);
}

static void makePipeline(void)
{
    ID3DBlob *vsb = NULL, *psb = NULL, *err = NULL;
    D3D11_INPUT_ELEMENT_DESC ie[10];
    D3D11_BUFFER_DESC bd;
    D3D11_RASTERIZER_DESC rd;
    D3D11_DEPTH_STENCIL_DESC dd;

    if (FAILED(compileG(SRC, "vs", "vs_4_0", &vsb, &err)))
        die("vertex shader would not compile");
    if (FAILED(compileG(SRC, "ps", "ps_4_0", &psb, &err)))
        die("pixel shader would not compile");
    ID3D11Device_CreateVertexShader(g_dev, ID3D10Blob_GetBufferPointer(vsb),
                                    ID3D10Blob_GetBufferSize(vsb), NULL, &g_vs);
    ID3D11Device_CreatePixelShader(g_dev, ID3D10Blob_GetBufferPointer(psb),
                                   ID3D10Blob_GetBufferSize(psb), NULL, &g_ps);
    {   ID3DBlob *ovb = NULL, *ove = NULL;
        if (SUCCEEDED(compileG(SRC_OV, "ps", "ps_4_0", &ovb, &ove)) && ovb) {
            ID3D11Device_CreatePixelShader(g_dev, ID3D10Blob_GetBufferPointer(ovb),
                                           ID3D10Blob_GetBufferSize(ovb), NULL, &g_psOv);
            ID3D10Blob_Release(ovb);
        }
        if (ove) ID3D10Blob_Release(ove);
    }

    ZeroMemory(ie, sizeof(ie));
    ie[0].SemanticName = "POSITION"; ie[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    ie[1].SemanticName = "COLOR";    ie[1].Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ie[1].AlignedByteOffset = 12;
    ie[2].SemanticName = "TEXCOORD"; ie[2].Format = DXGI_FORMAT_R32G32_FLOAT;
    ie[2].AlignedByteOffset = 16;
    ie[3].SemanticName = "TEXCOORD"; ie[3].SemanticIndex = 1;
    ie[3].Format = DXGI_FORMAT_R32_FLOAT;
    ie[3].AlignedByteOffset = 24;
    ie[4].SemanticName = "TEXCOORD"; ie[4].SemanticIndex = 2;
    ie[4].Format = DXGI_FORMAT_R32G32B32A32_FLOAT; ie[4].AlignedByteOffset = 28;
    ie[5].SemanticName = "TEXCOORD"; ie[5].SemanticIndex = 3;
    ie[5].Format = DXGI_FORMAT_R32_UINT; ie[5].AlignedByteOffset = 44;
    ie[6].SemanticName = "TEXCOORD"; ie[6].SemanticIndex = 4;
    ie[6].Format = DXGI_FORMAT_R32_UINT; ie[6].AlignedByteOffset = offsetof(Vc2Vertex,address);
    ie[7].SemanticName = "COLOR"; ie[7].SemanticIndex = 1;
    ie[7].Format = DXGI_FORMAT_R8G8B8A8_UNORM; ie[7].AlignedByteOffset = offsetof(Vc2Vertex,specular);
    ie[8].SemanticName = "TEXCOORD"; ie[8].SemanticIndex = 5;
    ie[8].Format = DXGI_FORMAT_R32_UINT; ie[8].AlignedByteOffset = offsetof(Vc2Vertex,fog_colour);
    ie[9].SemanticName = "TEXCOORD"; ie[9].SemanticIndex = 6;
    ie[9].Format = DXGI_FORMAT_R32_FLOAT; ie[9].AlignedByteOffset = offsetof(Vc2Vertex,fog);
    ID3D11Device_CreateInputLayout(g_dev, ie, 10, ID3D10Blob_GetBufferPointer(vsb),
                                   ID3D10Blob_GetBufferSize(vsb), &g_il);
    ID3D10Blob_Release(vsb); ID3D10Blob_Release(psb);

    ZeroMemory(&bd, sizeof(bd));
    bd.ByteWidth = VC2_MAX_VERTS * sizeof(Vc2Vertex);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(ID3D11Device_CreateBuffer(g_dev, &bd, NULL, &g_vb))) die("vertex buffer");

    ZeroMemory(&bd, sizeof(bd));
    bd.ByteWidth = 64;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(ID3D11Device_CreateBuffer(g_dev, &bd, NULL, &g_cb))) die("constant buffer");

    ZeroMemory(&rd, sizeof(rd));
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    ID3D11Device_CreateRasterizerState(g_dev, &rd, &g_rs);
    for(unsigned cull=2;cull<=3;cull++) {
        /* D3D7 CW/CCW describe the rejected winding; D3D11's default front is CW. */
        rd.CullMode=cull==2?D3D11_CULL_FRONT:D3D11_CULL_BACK;
        if(FAILED(ID3D11Device_CreateRasterizerState(g_dev,&rd,&g_nativeCull[cull])))die("native cull state");
    }

    ZeroMemory(&dd, sizeof(dd));
    /* the game has no z-buffer (HGL_D3D sets ZENABLE 0): the DLL hands the
     * quads over already sorted back to front, so painting in order is the
     * faithful thing to do. depth=1 in HOTD2VR.ini brings the buffer back. */
    dd.DepthEnable = g_cfgDepth ? TRUE : FALSE;
    dd.DepthWriteMask = g_cfgDepth ? D3D11_DEPTH_WRITE_MASK_ALL
                                   : D3D11_DEPTH_WRITE_MASK_ZERO;
    /* GREATER_EQUAL, not GREATER: the sky carries depth exactly 0.0 and the
     * buffer is cleared to 0.0, so 0.0 > 0.0 failed and only the sky vanished
     * - in the headset, never in the DLL's own preview. Equal keys now resolve
     * last-drawn-wins, which is the engine's own paint order for coplanar
     * quads. */
    dd.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    ID3D11Device_CreateDepthStencilState(g_dev, &dd, &g_dss);
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    ID3D11Device_CreateDepthStencilState(g_dev, &dd, &g_dssRead);

    dd.DepthEnable = FALSE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    ID3D11Device_CreateDepthStencilState(g_dev, &dd, &g_dssUI);

    /* --- textured pipeline for the floating screen --- */
    if (FAILED(compileG(SRC_TEX, "vs", "vs_4_0", &vsb, &err)))
        die("textured vertex shader would not compile");
    if (FAILED(compileG(SRC_TEX, "ps", "ps_4_0", &psb, &err)))
        die("textured pixel shader would not compile");
    ID3D11Device_CreateVertexShader(g_dev, ID3D10Blob_GetBufferPointer(vsb),
                                    ID3D10Blob_GetBufferSize(vsb), NULL, &g_vsTex);
    ID3D11Device_CreatePixelShader(g_dev, ID3D10Blob_GetBufferPointer(psb),
                                   ID3D10Blob_GetBufferSize(psb), NULL, &g_psTex);
    ZeroMemory(ie, sizeof(ie));
    ie[0].SemanticName = "POSITION"; ie[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    ie[1].SemanticName = "TEXCOORD"; ie[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    ie[1].AlignedByteOffset = 12;
    ID3D11Device_CreateInputLayout(g_dev, ie, 2, ID3D10Blob_GetBufferPointer(vsb),
                                   ID3D10Blob_GetBufferSize(vsb), &g_ilTex);
    ID3D10Blob_Release(vsb); ID3D10Blob_Release(psb);

    ZeroMemory(&bd, sizeof(bd));
    bd.ByteWidth = OVERLAY_MAX * sizeof(Vc2Vertex);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(ID3D11Device_CreateBuffer(g_dev, &bd, NULL, &g_ovVB))) die("overlay buffer");

    bd.ByteWidth = 6 * sizeof(TexVtx);
    if (FAILED(ID3D11Device_CreateBuffer(g_dev, &bd, NULL, &g_scrVB))) die("screen buffer");

    {
        D3D11_BLEND_DESC bld;
        ZeroMemory(&bld, sizeof(bld));
        bld.RenderTarget[0].BlendEnable = TRUE;
        bld.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        bld.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        bld.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bld.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bld.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        bld.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bld.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        ID3D11Device_CreateBlendState(g_dev, &bld, &g_blendAdd);
    }
    bd.ByteWidth = 16;                    /* cbuffer R: screen resolution */
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(ID3D11Device_CreateBuffer(g_dev, &bd, NULL, &g_cbRes))) die("res cbuffer");

    {
        D3D11_SAMPLER_DESC sdd;
        ZeroMemory(&sdd, sizeof(sdd));
        sdd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sdd.AddressU = sdd.AddressV = sdd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        ID3D11Device_CreateSamplerState(g_dev, &sdd, &g_samp);
    }
    {
        D3D11_TEXTURE2D_DESC td;
        ZeroMemory(&td, sizeof(td));
        td.Width = VC2_ATLAS_W; td.Height = VC2_ATLAS_H;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &g_atlTex))) die("atlas texture");
        ID3D11Device_CreateShaderResourceView(g_dev, (ID3D11Resource *)g_atlTex, NULL, &g_atlSRV);
    }
    /* the screen texture itself is (re)made on demand - its size follows the
     * capture, see ensureScreenTex */
}

static void uploadAtlasTex(void)
{
    D3D11_MAPPED_SUBRESOURCE m;
    unsigned y;
    if (!g_atlFresh || !g_atlTex) return;
    if (SUCCEEDED(ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)g_atlTex, 0,
                  D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        for (y = 0; y < VC2_ATLAS_H; y++)
            CopyMemory((BYTE *)m.pData + y * m.RowPitch,
                       g_atlPix + (size_t)y * VC2_ATLAS_W, VC2_ATLAS_W * 4);
        ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)g_atlTex, 0);
    }
    g_atlFresh = 0;
}

/* recreate the capture texture whenever the incoming size changes */
static void ensureScreenTex(void)
{
    D3D11_TEXTURE2D_DESC td;
    static unsigned haveW, haveH;
    if (g_scrTex && haveW == g_scrW && haveH == g_scrH) return;
    if (g_scrSRV) { ID3D11ShaderResourceView_Release(g_scrSRV); g_scrSRV = NULL; }
    if (g_scrTex) { ID3D11Texture2D_Release(g_scrTex); g_scrTex = NULL; }
    ZeroMemory(&td, sizeof(td));
    td.Width = g_scrW; td.Height = g_scrH;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;       /* GDI hands out BGRA */
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DYNAMIC;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &g_scrTex))) return;
    ID3D11Device_CreateShaderResourceView(g_dev, (ID3D11Resource *)g_scrTex, NULL, &g_scrSRV);
    haveW = g_scrW; haveH = g_scrH;
}

/* ------------------------------------------------------- little vector math */

static float vDot(const float *a, const float *b)
{ return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

static void vCross(const float *a, const float *b, float *o)
{
    float t0 = a[1]*b[2] - a[2]*b[1];
    float t1 = a[2]*b[0] - a[0]*b[2];
    float t2 = a[0]*b[1] - a[1]*b[0];
    o[0] = t0; o[1] = t1; o[2] = t2;
}

static void vNorm(float *a)
{
    float l = sqrtf(vDot(a, a));
    if (l > 1e-9f) { a[0] /= l; a[1] /= l; a[2] /= l; }
}

static void quatRotate(const XrQuaternionf *q, const float *v, float *o)
{
    /* o = v + 2*cross(q.xyz, cross(q.xyz, v) + q.w*v) */
    float u[3] = { q->x, q->y, q->z };
    float t[3], t2[3];
    vCross(u, v, t);
    t[0] += q->w * v[0]; t[1] += q->w * v[1]; t[2] += q->w * v[2];
    vCross(u, t, t2);
    o[0] = v[0] + 2.0f * t2[0];
    o[1] = v[1] + 2.0f * t2[1];
    o[2] = v[2] + 2.0f * t2[2];
}

/* Moeller-Trumbore, nearest hit over the frame's triangles, game units */
static int rayVsScene(const float *org, const float *dir, float *tOut)
{
    unsigned i;
    float best = 1e30f;
    int found = 0;
    for (i = 0; i + 2 < g_localCount; i += 3) {
        float e1[3], e2[3], p[3], tv[3], qv[3];
        float det, inv, u, v, t;
        const Vc2Vertex *a = &g_local[i], *b = &g_local[i+1], *c = &g_local[i+2];
        /* Capture provenance, not VC2 painter depth, identifies shootable geometry. */
        if(!(a->style&HOTD_WORLD) || !(b->style&HOTD_WORLD) || !(c->style&HOTD_WORLD))continue;
        e1[0] = b->x - a->x; e1[1] = b->y - a->y; e1[2] = b->z - a->z;
        e2[0] = c->x - a->x; e2[1] = c->y - a->y; e2[2] = c->z - a->z;
        vCross(dir, e2, p);
        det = vDot(e1, p);
        if (det > -1e-6f && det < 1e-6f) continue;
        unsigned cull=(a->style&HOTD_CULL_MASK)>>HOTD_CULL_SHIFT;
        if((cull==2 && det<0) || (cull==3 && det>0))continue;
        inv = 1.0f / det;
        tv[0] = org[0] - a->x; tv[1] = org[1] - a->y; tv[2] = org[2] - a->z;
        u = vDot(tv, p) * inv;
        if (u < 0.0f || u > 1.0f) continue;
        vCross(tv, e1, qv);
        v = vDot(dir, qv) * inv;
        if (v < 0.0f || u + v > 1.0f) continue;
        t = vDot(e2, qv) * inv;
        if (t > .001f*g_unitsPerMetre && t < best) { best = t; found = 1; }
    }
    if (found) *tOut = best;
    return found;
}

static void placeShotEffects(void)
{
    const float origin[3]={0};
    for(unsigned start=0;start<g_localCount;) {
        unsigned end=start+1,tag=g_local[start].style&(HOTD_SHOT|HOTD_SHOT_GROUP_MASK);
        if(!(tag&HOTD_SHOT)){start++;continue;}
        while(end<g_localCount && (g_local[end].style&(HOTD_SHOT|HOTD_SHOT_GROUP_MASK))==tag)end++;
        const float *center=g_shotOrigins[tag>>26];
        float distance=sqrtf(vDot(center,center)),direction[3]={center[0],center[1],center[2]},hit;
        if(isfinite(distance) && distance>.0001f && center[2]<0) {
            vNorm(direction);
            if(!rayVsScene(origin,direction,&hit))hit=g_cfgReach*g_unitsPerMetre;
            /* Keep the native screen shape, without scaling a camera-plane mesh into a thick volume. */
            float z=center[2]*fmaxf(.001f*g_unitsPerMetre,hit-.002f*g_unitsPerMetre)/distance;
            float x=center[0]*z/center[2],y=center[1]*z/center[2];
            for(unsigned i=start;i<end;i++)if(g_local[i].z<-.0001f) {
                float scale=z/g_local[i].z;
                g_local[i].x=x+.35f*(g_local[i].x*scale-x);
                g_local[i].y=y+.35f*(g_local[i].y*scale-y);g_local[i].z=z;
            }
        }
        start=end;
    }
}

/* ------------------------------------------------------------- the sight
 * What was here: an opaque red bar of constant 4 mm width running the whole
 * way to the target, as two crossed ribbons that double their own density
 * where they cross, plus a green plus sign built along the LOCAL X and Y axes
 * - so it turned into a line the moment you were not square to it - all of it
 * punched through the world with no blending. Up close the bar is a plastic
 * stick lying across the street, and the cross keeps changing size because it
 * is 2 cm wide wherever it happens to land.
 *
 * What it is now:
 *   - the beam glows instead of occluding (additive), tapers from 3 mm at the
 *     muzzle to under a millimetre, and fades along its length, so it reads as
 *     light in the air rather than as an object;
 *   - the dot is a ring with a hot core, built in the plane perpendicular to
 *     the beam - which is the plane the laser would actually paint on a wall -
 *     and sized by ANGLE, so it stays the same apparent size at two metres and
 *     at two hundred;
 *   - a short flare at the muzzle, so the beam comes out of the gun.
 */
static void ovPush(const float *p, DWORD col)
{
    if (g_ovCount >= OVERLAY_MAX) return;
    g_ovVerts[g_ovCount].x = p[0];
    g_ovVerts[g_ovCount].y = p[1];
    g_ovVerts[g_ovCount].z = p[2];
    g_ovVerts[g_ovCount].colour = col;
    g_ovVerts[g_ovCount].u = -1.0f;
    g_ovVerts[g_ovCount].v = -1.0f;
    g_ovVerts[g_ovCount].depth = 1.0f;
    g_ovVerts[g_ovCount].style = 0;
    g_ovVerts[g_ovCount].address = 0;
    g_ovVerts[g_ovCount].specular = g_ovVerts[g_ovCount].fog_colour = 0;
    g_ovVerts[g_ovCount].fog = 1;
    g_ovCount++;
}

static void ovQuad(const float *a, const float *b, const float *c, const float *d,
                   DWORD ab, DWORD cd)
{
    ovPush(a, ab); ovPush(b, ab); ovPush(c, cd);
    ovPush(a, ab); ovPush(c, cd); ovPush(d, cd);
}

/* scale a colour's rgb, keeping the alpha byte: with additive blending the
 * value IS the brightness, so this is the fade */
static DWORD ovDim(DWORD col, float k)
{
    DWORD r = (DWORD)((col        & 0xFF) * k);
    DWORD g = (DWORD)((col >>  8  & 0xFF) * k);
    DWORD b = (DWORD)((col >> 16  & 0xFF) * k);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (col & 0xFF000000u) | (b << 16) | (g << 8) | r;
}

/* two perpendicular unit axes for a direction */
static void ovFrame(const float *d, float *u, float *v)
{
    float up[3] = { 0, 1, 0 };
    if (d[1] > 0.9f || d[1] < -0.9f) { up[0] = 1; up[1] = 0; }
    vCross(d, up, u);
    if (vDot(u, u) < 1e-12f) { u[0] = 1; u[1] = 0; u[2] = 0; }
    vNorm(u);
    vCross(d, u, v);
    vNorm(v);
}

/* one tapered, fading length of beam, as two crossed ribbons */
static void ovRibbon(const float *a, const float *b, float wa, float wb,
                     DWORD ca, DWORD cb)
{
    float d[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
    float s1[3], s2[3], q[4][3];
    int r, k;
    if (vDot(d, d) < 1e-12f) return;
    vNorm(d);
    ovFrame(d, s1, s2);
    for (r = 0; r < 2; r++) {
        const float *sv = r ? s2 : s1;
        for (k = 0; k < 3; k++) {
            q[0][k] = a[k] - sv[k]*wa;
            q[1][k] = a[k] + sv[k]*wa;
            q[2][k] = b[k] + sv[k]*wb;
            q[3][k] = b[k] - sv[k]*wb;
        }
        ovQuad(q[0], q[1], q[2], q[3], ca, cb);
    }
}

/* a filled disc in the plane perpendicular to dir */
static void ovDisc(const float *c, const float *dir, float rad, DWORD col, int segs)
{
    float u[3], v[3], prev[3], cur[3];
    int i, k;
    ovFrame(dir, u, v);
    for (i = 0; i <= segs; i++) {
        float a = (float)i * 6.2831853f / (float)segs;
        float ca = cosf(a) * rad, sa = sinf(a) * rad;
        for (k = 0; k < 3; k++) cur[k] = c[k] + u[k]*ca + v[k]*sa;
        if (i) { ovPush(c, col); ovPush(prev, col); ovPush(cur, col); }
        for (k = 0; k < 3; k++) prev[k] = cur[k];
    }
}

/* an annulus in the same plane */
static void ovRing(const float *c, const float *dir, float r0, float r1,
                   DWORD col, int segs)
{
    float u[3], v[3], pi[3], po[3], ci[3], co[3];
    int i, k;
    ovFrame(dir, u, v);
    for (i = 0; i <= segs; i++) {
        float a = (float)i * 6.2831853f / (float)segs;
        float ca = cosf(a), sa = sinf(a);
        for (k = 0; k < 3; k++) {
            ci[k] = c[k] + u[k]*ca*r0 + v[k]*sa*r0;
            co[k] = c[k] + u[k]*ca*r1 + v[k]*sa*r1;
        }
        if (i) ovQuad(pi, po, co, ci, col, col);
        for (k = 0; k < 3; k++) { pi[k] = ci[k]; po[k] = co[k]; }
    }
}

/* four ticks standing off the ring, at the diagonals so they never lie along
 * the horizon and vanish into it */
static void ovTicks(const float *c, const float *dir, float r0, float r1,
                    float halfW, DWORD col)
{
    float u[3], v[3], a[3], b[3];
    int i, k;
    ovFrame(dir, u, v);
    for (i = 0; i < 4; i++) {
        float ang = 0.7853982f + (float)i * 1.5707963f;
        float ca = cosf(ang), sa = sinf(ang);
        for (k = 0; k < 3; k++) {
            a[k] = c[k] + (u[k]*ca + v[k]*sa) * r0;
            b[k] = c[k] + (u[k]*ca + v[k]*sa) * r1;
        }
        ovRibbon(a, b, halfW, halfW, col, col);
    }
}

/* origin -> hit: flare, fading beam, ring, hot core, ticks */
#define OV_SEGS 6
static void ovSight(const float *aim, const float *hit, DWORD beamCol,
                    DWORD ringCol, DWORD coreCol)
{
    float o[3], d[3], dist, u[3], rad, muzzle;
    int i, k;

    d[0] = hit[0]-aim[0]; d[1] = hit[1]-aim[1]; d[2] = hit[2]-aim[2];
    dist = (float)sqrt((double)vDot(d, d));
    if (dist < 1e-4f) return;
    u[0] = d[0]/dist; u[1] = d[1]/dist; u[2] = d[2]/dist;

    /* The aim pose sits INSIDE the slide, so a beam starting there was drawn
     * across the pistol - which is what "лазер просвечивает сквозь пистолет"
     * is. It is not a depth bug and it does not want a depth fix: a laser
     * comes out of the muzzle. Start it there, in front of the metal. */
    muzzle = 0.075f * 1.35f * (float)g_cfgGunScale / 100.0f;
    if (muzzle > dist * 0.8f) muzzle = dist * 0.8f;
    for (k = 0; k < 3; k++) o[k] = aim[k] + u[k] * muzzle;
    dist -= muzzle;
    d[0] = hit[0]-o[0]; d[1] = hit[1]-o[1]; d[2] = hit[2]-o[2];

    /* the beam: bright and fat where it leaves the muzzle, thin and faint by
     * the time it gets there, so a long shot is not a long solid stripe */
    if (g_cfgLaser) {
        for (i = 0; i < OV_SEGS; i++) {
            float t0 = (float)i / OV_SEGS, t1 = (float)(i + 1) / OV_SEGS;
            float a[3], b[3];
            float w0 = 0.0030f - 0.0022f * t0, w1 = 0.0030f - 0.0022f * t1;
            float k0 = 1.0f - 0.72f * t0,      k1 = 1.0f - 0.72f * t1;
            int k;
            for (k = 0; k < 3; k++) {
                a[k] = o[k] + d[k] * t0;
                b[k] = o[k] + d[k] * t1;
            }
            ovRibbon(a, b, w0, w1, ovDim(beamCol, k0), ovDim(beamCol, k1));
        }

        ovDisc(o, u, 0.011f, ovDim(beamCol, 0.55f), 10);   /* flare at the muzzle */
    }

    /* the dot, sized by angle: 0.8 degrees wherever it lands */
    if (g_cfgCrosshair) {
        rad = dist * 0.01396f;                        /* tan(0.8 deg) */
        if (rad < 0.006f) rad = 0.006f;
        if (rad > 0.400f) rad = 0.400f;
        ovRing (hit, u, rad * 0.70f, rad, ringCol, 28);
        ovDisc (hit, u, rad * 0.26f, coreCol, 14);
        ovTicks(hit, u, rad * 1.35f, rad * 1.95f, rad * 0.10f, ovDim(ringCol, 0.6f));
    }
}

static void uploadVerts(void)
{
    D3D11_MAPPED_SUBRESOURCE m;
    if (!g_localCount) return;
    if (SUCCEEDED(ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)g_vb, 0,
                  D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        CopyMemory(m.pData, g_local, g_localCount * sizeof(Vc2Vertex));
        ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)g_vb, 0);
    }
}

/* Side by side needs two viewports in one back buffer, and only the second eye
 * must not wipe the first. g_vpX is where this eye starts, g_noClear says the
 * buffer has already been cleared this frame. Both are zero everywhere else,
 * so the headset path is byte for byte what it was. */
static float g_vpX;
static int   g_noClear;

static void drawNativeRuns(unsigned start,unsigned count,int ui)
{
    unsigned end=start+count;
    while(start<end) {
        unsigned key=g_local[start].style&HOTD_DRAW_STATE_MASK,stop=start+3;
        unsigned src=key&15,dst=(key>>4)&15;
        unsigned cull=(key&HOTD_CULL_MASK)>>HOTD_CULL_SHIFT;
        ID3D11BlendState *blend=NULL;
        while(stop<end && (g_local[stop].style&HOTD_DRAW_STATE_MASK)==key)stop+=3;
        if(src && src<=11 && dst>=1 && dst<=11) {
            if(!g_nativeBlend[src][dst]) {
                D3D11_BLEND_DESC desc={0};
                desc.RenderTarget[0].BlendEnable=TRUE;
                desc.RenderTarget[0].SrcBlend=(D3D11_BLEND)src;
                desc.RenderTarget[0].DestBlend=(D3D11_BLEND)dst;
                desc.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
                desc.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;
                desc.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO;
                desc.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
                desc.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
                if(FAILED(ID3D11Device_CreateBlendState(g_dev,&desc,&g_nativeBlend[src][dst])))die("native blend state");
            }
            blend=g_nativeBlend[src][dst];
        }
        ID3D11DeviceContext_OMSetBlendState(g_ctx,blend,NULL,0xFFFFFFFF);
        ID3D11DeviceContext_OMSetDepthStencilState(g_ctx,ui?g_dssUI:(key&HOTD_NO_ZWRITE?g_dssRead:g_dss),0);
        ID3D11DeviceContext_RSSetState(g_ctx,cull>=2?g_nativeCull[cull]:g_rs);
        ID3D11DeviceContext_Draw(g_ctx,stop-start,start);
        start=stop;
    }
    ID3D11DeviceContext_OMSetBlendState(g_ctx,NULL,NULL,0xFFFFFFFF);
    ID3D11DeviceContext_OMSetDepthStencilState(g_ctx,ui?g_dssUI:g_dss,0);
    ID3D11DeviceContext_RSSetState(g_ctx,g_rs);
}

/* mvpFlat, when given, is the same view with the zoom taken back out. The tail
 * of the buffer - the flat overlay the DLL pinned to the unzoomed focal length
 * - is drawn through it, so a cinematic zoom magnifies the street and leaves
 * the drum, the lives and the credits line exactly where they were. Without it
 * (or with hudzoom = 1) everything goes through one matrix, as before. */
static void drawWith(ID3D11RenderTargetView *rtv, ID3D11DepthStencilView *dsv,
                     int w, int h, const float *mvp, const float *mvpFlat)
{
    float clear[4] = { 0.02f, 0.02f, 0.03f, 1.0f };
    D3D11_MAPPED_SUBRESOURCE m;
    D3D11_VIEWPORT vp;
    UINT stride = sizeof(Vc2Vertex), offset = 0;
    /* The captured screen already contains UI; present that UI only once. */
    unsigned visibleCount = g_scrVisible && g_scrHave && g_scrSRV && !g_inScope && g_ovStart < g_localCount
        ? g_ovStart : g_localCount;

    if (!g_noClear) {
        ID3D11DeviceContext_ClearRenderTargetView(g_ctx, rtv, clear);
        ID3D11DeviceContext_ClearDepthStencilView(g_ctx, dsv, D3D11_CLEAR_DEPTH, 0.0f, 0);
    }
    ID3D11DeviceContext_OMSetRenderTargets(g_ctx, 1, &rtv, dsv);
    ID3D11DeviceContext_OMSetDepthStencilState(g_ctx, g_dss, 0);

    ZeroMemory(&vp, sizeof(vp));
    vp.TopLeftX = g_vpX;
    vp.Width = (float)w; vp.Height = (float)h; vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(g_ctx, 1, &vp);
    ID3D11DeviceContext_RSSetState(g_ctx, g_rs);

    if (SUCCEEDED(ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)g_cb, 0,
                  D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        CopyMemory(m.pData, mvp, 64);
        ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)g_cb, 0);
    }

    ID3D11DeviceContext_IASetInputLayout(g_ctx, g_il);
    ID3D11DeviceContext_IASetPrimitiveTopology(g_ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(g_ctx, 0, 1, &g_vb, &stride, &offset);
    ID3D11DeviceContext_VSSetShader(g_ctx, g_vs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(g_ctx, 0, 1, &g_cb);
    ID3D11DeviceContext_PSSetShader(g_ctx, g_ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(g_ctx, 0, 1, &g_atlSRV);
    ID3D11DeviceContext_PSSetSamplers(g_ctx, 0, 1, &g_samp);
    if (!visibleCount) return;
    if (g_cfgDepth >= 2) {
        /* TRUE Z: the world resolves itself in the depth buffer; only the
         * flat overlay at the tail (drum, lives, credits, our HUD) is drawn
         * on top with the test off, through the unzoomed matrix when there
         * is one. Nothing here depends on the buffer's paint order. */
        unsigned ov = g_ovStart;
        if (ov > visibleCount) ov = visibleCount;
        if (ov) drawNativeRuns(0,ov,0);
        if (ov < visibleCount) {
            const float *mo = (!g_cfgHudZoom && mvpFlat) ? mvpFlat : mvp;
            if (mo != mvp && SUCCEEDED(ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)g_cb, 0,
                          D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                CopyMemory(m.pData, mo, 64);
                ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)g_cb, 0);
            }
            ID3D11DeviceContext_OMSetDepthStencilState(g_ctx, g_dssUI, 0);
            if (g_cfgHudSharp && g_psOv) ID3D11DeviceContext_PSSetShader(g_ctx, g_psOv, NULL, 0);
            drawNativeRuns(ov,visibleCount-ov,1);
            if (g_cfgHudSharp && g_psOv) ID3D11DeviceContext_PSSetShader(g_ctx, g_ps, NULL, 0);
            ID3D11DeviceContext_OMSetDepthStencilState(g_ctx, g_dss, 0);
        }
        return;
    }
    {
        unsigned ov = g_ovStart;
        int split = (!g_cfgHudZoom && mvpFlat && mvpFlat != mvp &&
                     ov < visibleCount);
        if (!split) {
            /* still worth the sharper fetch on the overlay when there is no
             * zoom to undo - it costs one extra draw call and nothing else */
            if (g_cfgHudSharp && g_psOv && ov < visibleCount) {
                if (ov) drawNativeRuns(0,ov,0);
                ID3D11DeviceContext_PSSetShader(g_ctx, g_psOv, NULL, 0);
                drawNativeRuns(ov,visibleCount-ov,0);
                ID3D11DeviceContext_PSSetShader(g_ctx, g_ps, NULL, 0);
            } else {
                drawNativeRuns(0,visibleCount,0);
            }
            return;
        }
        if (ov) drawNativeRuns(0,ov,0);
        if (SUCCEEDED(ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)g_cb, 0,
                      D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            CopyMemory(m.pData, mvpFlat, 64);
            ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)g_cb, 0);
        }
        if (g_cfgHudSharp && g_psOv)
            ID3D11DeviceContext_PSSetShader(g_ctx, g_psOv, NULL, 0);
        drawNativeRuns(ov,visibleCount-ov,0);
        if (g_cfgHudSharp && g_psOv)
            ID3D11DeviceContext_PSSetShader(g_ctx, g_ps, NULL, 0);
    }
}

/* hang the screen in front of a pose: 2 m out, 4:3, facing back at it */
static void setScreenPose(float x, float y, float z, float yaw)
{
    float fwd[3] = { -sinf(yaw), 0.0f, -cosf(yaw) };
    D3D11_MAPPED_SUBRESOURCE m;
    TexVtx v[6];
    int i;
    static const float corner[6][2] = {
        { -1,  1 }, { 1, 1 }, { 1, -1 },
        { -1,  1 }, { 1, -1 }, { -1, -1 } };

    g_scrR[0] = cosf(yaw); g_scrR[1] = 0; g_scrR[2] = -sinf(yaw);
    g_scrU[0] = 0; g_scrU[1] = 1; g_scrU[2] = 0;
    g_scrN[0] = -fwd[0]; g_scrN[1] = 0; g_scrN[2] = -fwd[2];
    g_scrC[0] = x + fwd[0] * HOTD_PANEL_DISTANCE_M;
    g_scrC[1] = y;
    g_scrC[2] = z + fwd[2] * HOTD_PANEL_DISTANCE_M;

    if (!g_scrVB) return;
    for (i = 0; i < 6; i++) {
        float cu = corner[i][0], cv = corner[i][1];
        v[i].x = g_scrC[0] + g_scrR[0]*cu*g_scrHalfW + g_scrU[0]*cv*g_scrHalfH;
        v[i].y = g_scrC[1] + g_scrR[1]*cu*g_scrHalfW + g_scrU[1]*cv*g_scrHalfH;
        v[i].z = g_scrC[2] + g_scrR[2]*cu*g_scrHalfW + g_scrU[2]*cv*g_scrHalfH;
        v[i].u = (cu + 1.0f) * 0.5f;
        v[i].v = (1.0f - cv) * 0.5f;
    }
    if (SUCCEEDED(ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)g_scrVB, 0,
                  D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        CopyMemory(m.pData, v, sizeof(v));
        ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)g_scrVB, 0);
    }
}

static void uploadScreenTex(void)
{
    D3D11_MAPPED_SUBRESOURCE m;
    unsigned y;
    if (!g_scrFresh) return;
    ensureScreenTex();
    if (!g_scrTex) return;
    if (SUCCEEDED(ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)g_scrTex, 0,
                  D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        for (y = 0; y < g_scrH; y++)
            CopyMemory((BYTE *)m.pData + y * m.RowPitch,
                       g_scrPix + y * g_scrW, g_scrW * 4);
        ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)g_scrTex, 0);
    }
    if (SUCCEEDED(ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)g_cbRes, 0,
                  D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        float r[4];
        r[0] = (float)g_scrW; r[1] = (float)g_scrH; r[2] = r[3] = 0.0f;
        CopyMemory(m.pData, r, sizeof(r));
        ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)g_cbRes, 0);
    }
    g_scrFresh = 0;
}

static void uploadOverlay(void)
{
    D3D11_MAPPED_SUBRESOURCE m;
    if (!g_ovCount) return;
    if (SUCCEEDED(ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)g_ovVB, 0,
                  D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        CopyMemory(m.pData, g_ovVerts, g_ovCount * sizeof(Vc2Vertex));
        ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)g_ovVB, 0);
    }
}

static void drawGun(const float *vp);
#include "vr_menu.h"

/* laser, hit marker and the floating screen; call right after drawWith, with
 * view * proj only - all of it lives in local (metre) space, not game space */
static void drawUI(const float *vp)
{
    D3D11_MAPPED_SUBRESOURCE m;
    UINT stride, offset = 0;

    if (SUCCEEDED(ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)g_cb, 0,
                  D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        CopyMemory(m.pData, vp, 64);
        ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)g_cb, 0);
    }
    ID3D11DeviceContext_OMSetDepthStencilState(g_ctx, g_dssUI, 0);

    if (g_scrVisible && g_scrHave && g_scrSRV && !g_inScope) {
        stride = sizeof(TexVtx);
        ID3D11DeviceContext_IASetInputLayout(g_ctx, g_ilTex);
        ID3D11DeviceContext_IASetVertexBuffers(g_ctx, 0, 1, &g_scrVB, &stride, &offset);
        ID3D11DeviceContext_VSSetShader(g_ctx, g_vsTex, NULL, 0);
        ID3D11DeviceContext_PSSetShader(g_ctx, g_psTex, NULL, 0);
        ID3D11DeviceContext_PSSetConstantBuffers(g_ctx, 1, 1, &g_cbRes);
        ID3D11DeviceContext_PSSetShaderResources(g_ctx, 0, 1, &g_scrSRV);
        ID3D11DeviceContext_PSSetSamplers(g_ctx, 0, 1, &g_samp);
        ID3D11DeviceContext_Draw(g_ctx, 6, 0);
    }
    /* The sight goes down FIRST and the pistols on top of it. The beam is
     * drawn with no depth test at all - that is what lets it lie over the
     * world - so whatever is drawn after it wins those pixels, and whatever
     * is drawn before it loses them. Drawn first, the beam was showing
     * through the slide and the barrel of the gun it comes out of. The gun
     * has a real depth test, so putting it after costs nothing: it still
     * loses to nearer world geometry and still covers the beam it holds. */
    if (g_ovCount) {
        stride = sizeof(Vc2Vertex);
        ID3D11DeviceContext_IASetInputLayout(g_ctx, g_il);
        ID3D11DeviceContext_IASetVertexBuffers(g_ctx, 0, 1, &g_ovVB, &stride, &offset);
        ID3D11DeviceContext_VSSetShader(g_ctx, g_vs, NULL, 0);
        ID3D11DeviceContext_PSSetShader(g_ctx, g_ps, NULL, 0);
        /* light adds to what is behind it; it does not replace it */
        ID3D11DeviceContext_OMSetBlendState(g_ctx, g_blendAdd, NULL, 0xFFFFFFFF);
        ID3D11DeviceContext_Draw(g_ctx, g_ovCount, 0);
        ID3D11DeviceContext_OMSetBlendState(g_ctx, NULL, NULL, 0xFFFFFFFF);
    }
    vrMenuDraw();
    drawGun(vp);
}

/* --------------------------------------------------------------------- gun
 * A pistol in the gun hand. The laser already comes out of the controller's
 * aim pose every frame; the model is drawn at that same pose, so it points
 * where the laser goes by construction. The mesh is a plain Wavefront OBJ
 * (gunmodel in HOTD2VR.ini): metres, muzzle down -Z, origin at the aim point
 * (roughly the front of the controller). Colours come from the OBJ's .mtl
 * (Kd per material); no textures. Shading is baked per face against a fixed
 * light, which turns with the gun, but on a low-poly model that reads as
 * "edges are visible" and nothing more.
 *
 * Drawn with its own shader and a real depth test: the world shader writes
 * the engine's depth keys through SV_Depth and paints in order, neither of
 * which sorts a gun's own faces. The gun sits closer than anything the game
 * draws, so it wins the depth test against whatever the world left in the
 * buffer, and its own triangles resolve by hardware z. */
static ID3D11Buffer            *g_gunVB;
static unsigned                 g_gunCount;
static ID3D11ShaderResourceView *g_gunSRV;   /* map_Kd of the OBJ, if any */
static ID3D11VertexShader      *g_vsGun;
static ID3D11PixelShader       *g_psGun;
static ID3D11DepthStencilState *g_dssGun;
static XrPosef                  g_gunPose;
static int                      g_gunValid;
static XrPosef                  g_gun2Pose;         /* akimbo second gun         */
static int                      g_gun2Valid;
static float                    g_gun2Hit[3];
static float                    g_gun2Origin[3];
static unsigned                 g_gun2Fire;
static DWORD                    g_gunFireAt;    /* tick of the last trigger pull */
static DWORD                    g_gunReloadAt;  /* tick of the last reload pull  */
static DWORD                    g_gun2FireAt, g_gun2ReloadAt;   /* the same, gun 2 */

static const char *SRC_GUN =
"cbuffer C : register(b0) { row_major float4x4 mvp; };\n"
"Texture2D    gtex : register(t0);\n"
"SamplerState gsmp : register(s0);\n"
"struct VIn  { float3 p : POSITION; float4 c : COLOR; float2 t : TEXCOORD0; float d : TEXCOORD1; };\n"
"struct VOut { float4 p : SV_POSITION; float4 c : COLOR; float2 t : TEXCOORD0; };\n"
"VOut vs(VIn i) { VOut o; o.p = mul(mvp, float4(i.p,1)); o.c = i.c; o.t = i.t; return o; }\n"
/* u < 0 marks a vertex without a texture (the built-in pistol and Kd-only
 * OBJs); those keep the baked flat colour. Textured vertices carry the baked
 * face light in c and the texel is attenuated by it. */
"float4 ps(VOut i) : SV_TARGET {\n"
"  if (i.t.x < 0) return float4(i.c.rgb, 1);\n"
"  return float4(gtex.Sample(gsmp, i.t).rgb * i.c.rgb, 1);\n"
"}\n";

#define GUN_MAX_V   8192
#define GUN_MAX_OUT 65536
#define GUN_MAX_MTL 64


/* ----------------------------------------------------------- built-in pistol
 * gunmodel was an ini key with no default and no file shipped next to it, so
 * out of the box g_cfgGun is empty, gunLoad returns on its first line and
 * there is simply no gun - which is exactly what "оружия в руке так и нет"
 * describes. A hand holding an invisible pistol is worse than a wrong pistol,
 * so there is one built in now: eleven boxes, 132 triangles, no file, no path,
 * no ini. gunmodel still wins when it is set and the file opens.
 *
 * Metres, muzzle toward -Z, origin at the aim pose - the front of the
 * controller. The grip therefore sits 4..11 cm behind and 2..12 cm below the
 * origin, which is about where the hand actually is on a Touch or an Index. */
#define GUN_STEEL  0xFF665C59u          /* 0xAABBGGRR, Kd 0.35 0.36 0.40 */
#define GUN_DARK   0xFF35322Eu
#define GUN_GRIP   0xFF1F334Cu          /* Kd 0.30 0.20 0.12             */

static void gunFace(Vc2Vertex *out, unsigned *n, const float (*c)[3],
                    int a, int b, int d, int e, DWORD col, const float *L)
{
    static const int tri[6] = { 0, 1, 2, 0, 2, 3 };
    int q[4], k;
    float e1[3], e2[3], nrm[3], lit, sh;
    DWORD sc;
    q[0] = a; q[1] = b; q[2] = d; q[3] = e;
    for (k = 0; k < 3; k++) {
        e1[k] = c[q[1]][k] - c[q[0]][k];
        e2[k] = c[q[2]][k] - c[q[0]][k];
    }
    vCross(e1, e2, nrm);
    vNorm(nrm);
    lit = vDot(nrm, L);
    if (lit < 0) lit = -lit;
    sh = 0.42f + 0.58f * lit;
    sc = 0xFF000000u |
         ((DWORD)((col >> 16 & 0xFF) * sh) << 16) |
         ((DWORD)((col >>  8 & 0xFF) * sh) <<  8) |
          (DWORD)((col       & 0xFF) * sh);
    for (k = 0; k < 6; k++) {
        const float *v = c[q[tri[k]]];
        out[*n].x = v[0]; out[*n].y = v[1]; out[*n].z = v[2];
        out[*n].colour = sc;
        out[*n].u = -1.0f; out[*n].v = -1.0f; out[*n].depth = 0.0f;
        out[*n].address = 0;
        out[*n].specular = out[*n].fog_colour = 0; out[*n].fog = 1;
        (*n)++;
    }
}

/* an axis-aligned box, optionally raked back: zLean shifts the +Z face of the
 * bottom edge, which is all a pistol grip is */
static void gunBox(Vc2Vertex *out, unsigned *n, float hx,
                   float y0, float y1, float z0, float z1,
                   float zLean, DWORD col, const float *L)
{
    float c[8][3];
    int i;
    for (i = 0; i < 8; i++) {
        int right = i & 1, top = (i >> 1) & 1, back = (i >> 2) & 1;
        float lean = top ? 0.0f : zLean;
        c[i][0] = right ?  hx : -hx;
        c[i][1] = top   ?  y1 :  y0;
        c[i][2] = (back ?  z1 :  z0) + lean;
    }
    /* i: bit0 right, bit1 top, bit2 back */
    gunFace(out, n, c, 0, 1, 3, 2, col, L);   /* front  (-Z) */
    gunFace(out, n, c, 5, 4, 6, 7, col, L);   /* back   (+Z) */
    gunFace(out, n, c, 4, 0, 2, 6, col, L);   /* left   (-X) */
    gunFace(out, n, c, 1, 5, 7, 3, col, L);   /* right  (+X) */
    gunFace(out, n, c, 2, 3, 7, 6, col, L);   /* top    (+Y) */
    gunFace(out, n, c, 4, 5, 1, 0, col, L);   /* bottom (-Y) */
}

static unsigned gunBuiltin(Vc2Vertex *out)
{
    float L[3] = { 0.35f, 0.80f, 0.45f };
    unsigned n = 0;
    /* A real pistol at arm's length subtends less than you expect, and a
     * life-sized one in a headset reads as a toy - every VR shooter draws the
     * gun a third over size for exactly this reason. 1.35 is that third;
     * gunscale multiplies on top of it. */
    float k = 1.35f * (float)g_cfgGunScale / 100.0f;
    vNorm(L);
    /*            hx      y0      y1      z0      z1     lean  colour     */
    gunBox(out,&n,0.016f*k, 0.000f*k, 0.028f*k,-0.052f*k, 0.082f*k, 0.0f,    GUN_STEEL, L); /* slide       */
    gunBox(out,&n,0.009f*k, 0.005f*k, 0.023f*k,-0.072f*k,-0.050f*k, 0.0f,    GUN_DARK,  L); /* barrel      */
    gunBox(out,&n,0.014f*k,-0.016f*k, 0.000f*k,-0.048f*k, 0.078f*k, 0.0f,    GUN_STEEL, L); /* frame       */
    gunBox(out,&n,0.012f*k,-0.112f*k,-0.016f*k, 0.040f*k, 0.082f*k, 0.024f*k,GUN_GRIP,  L); /* grip, raked */
    gunBox(out,&n,0.013f*k,-0.122f*k,-0.108f*k, 0.062f*k, 0.110f*k, 0.0f,    GUN_DARK,  L); /* mag floor   */
    gunBox(out,&n,0.010f*k,-0.044f*k,-0.016f*k, 0.010f*k, 0.019f*k, 0.0f,    GUN_STEEL, L); /* guard front */
    gunBox(out,&n,0.010f*k,-0.046f*k,-0.038f*k, 0.010f*k, 0.058f*k, 0.0f,    GUN_STEEL, L); /* guard belly */
    gunBox(out,&n,0.005f*k,-0.036f*k,-0.016f*k, 0.030f*k, 0.039f*k, 0.0f,    GUN_DARK,  L); /* trigger     */
    gunBox(out,&n,0.006f*k, 0.026f*k, 0.042f*k, 0.074f*k, 0.086f*k, 0.0f,    GUN_DARK,  L); /* hammer      */
    gunBox(out,&n,0.014f*k, 0.028f*k, 0.035f*k, 0.064f*k, 0.070f*k, 0.0f,    GUN_DARK,  L); /* rear sight  */
    gunBox(out,&n,0.003f*k, 0.028f*k, 0.037f*k,-0.051f*k,-0.045f*k, 0.0f,    GUN_DARK,  L); /* front sight */
    return n;
}

/* map_Kd for the gun. BMP only, 24 or 32 bpp uncompressed, by design: forty
 * lines of fread instead of a COM excursion into WIC, and every path through
 * it can be read on paper. The shipped emerald revolver's texture is baked to
 * BMP offline; anyone bringing their own model converts once. */
static ID3D11ShaderResourceView *gunLoadBmp(const char *path)
{
    FILE *bf = fopen(path, "rb");
    unsigned char hdr[54];
    unsigned char *row, *rgba;
    int w, h, bpp, ofs, stride, y, x, flip;
    ID3D11Texture2D *tex = NULL;
    ID3D11ShaderResourceView *srv = NULL;
    D3D11_TEXTURE2D_DESC td;
    D3D11_SUBRESOURCE_DATA sd;

    if (!bf) { note("gunmodel: cannot open texture %s\n", path); return NULL; }
    if (fread(hdr, 1, 54, bf) != 54 || hdr[0] != 'B' || hdr[1] != 'M') {
        note("gunmodel: %s is not a BMP\n", path); fclose(bf); return NULL;
    }
    ofs = *(int *)(hdr + 10);
    w   = *(int *)(hdr + 18);
    h   = *(int *)(hdr + 22);
    bpp = *(unsigned short *)(hdr + 28);
    if (*(int *)(hdr + 30) != 0 || (bpp != 24 && bpp != 32) ||
        w <= 0 || w > 8192 || h == 0 || h > 8192 || h < -8192) {
        note("gunmodel: %s: need uncompressed 24/32bpp BMP\n", path);
        fclose(bf); return NULL;
    }
    flip = h > 0;                       /* positive height = bottom-up rows */
    if (h < 0) h = -h;
    stride = ((w * (bpp / 8)) + 3) & ~3;
    row  = (unsigned char *)malloc(stride);
    rgba = (unsigned char *)malloc((size_t)w * h * 4);
    if (!row || !rgba) { free(row); free(rgba); fclose(bf); return NULL; }
    fseek(bf, ofs, SEEK_SET);
    for (y = 0; y < h; y++) {
        unsigned char *dst = rgba + (size_t)(flip ? h - 1 - y : y) * w * 4;
        if (fread(row, 1, stride, bf) != (size_t)stride) break;
        for (x = 0; x < w; x++) {
            const unsigned char *s = row + x * (bpp / 8);
            dst[x * 4 + 0] = s[2];      /* BMP is BGR */
            dst[x * 4 + 1] = s[1];
            dst[x * 4 + 2] = s[0];
            dst[x * 4 + 3] = 255;
        }
    }
    fclose(bf); free(row);

    ZeroMemory(&td, sizeof(td));
    td.Width = w; td.Height = h;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ZeroMemory(&sd, sizeof(sd));
    sd.pSysMem = rgba;
    sd.SysMemPitch = w * 4;
    if (SUCCEEDED(ID3D11Device_CreateTexture2D(g_dev, &td, &sd, &tex))) {
        ID3D11Device_CreateShaderResourceView(g_dev, (ID3D11Resource *)tex,
                                              NULL, &srv);
        ID3D11Texture2D_Release(tex);
    }
    free(rgba);
    if (srv) note("gunmodel: texture %s, %dx%d\n", path, w, h);
    return srv;
}

static void gunLoad(void)
{
    FILE *f;
    char line[512], dir[MAX_PATH], mtlName[64] = "";
    float (*pos)[3];
    float (*uv)[2];
    Vc2Vertex *out;
    unsigned npos = 0, nuv = 0, nout = 0, nmtl = 0, i;
    struct { char name[64]; DWORD col; } mtl[GUN_MAX_MTL];
    DWORD curCol = 0xFF909090u;
    float L[3] = { 0.35f, 0.80f, 0.45f };
    D3D11_BUFFER_DESC bd;
    D3D11_SUBRESOURCE_DATA sd;
    D3D11_DEPTH_STENCIL_DESC dd;
    ID3DBlob *vsb = NULL, *psb = NULL, *err = NULL;

    vNorm(L);
    pos = (float (*)[3])malloc(sizeof(float[3]) * GUN_MAX_V);
    uv  = (float (*)[2])malloc(sizeof(float[2]) * GUN_MAX_V);
    out = (Vc2Vertex *)malloc(sizeof(Vc2Vertex) * GUN_MAX_OUT);
    if (!pos || !uv || !out) { free(pos); free(uv); free(out); return; }

    f = g_cfgGun[0] ? fopen(g_cfgGun, "r") : NULL;
    if (!f) {
        if (g_cfgGun[0]) note("gunmodel: cannot open %s\n", g_cfgGun);
        nout = gunBuiltin(out);
        note("gunmodel: built-in pistol, %u triangles\n", nout / 3);
        goto upload;
    }

    lstrcpyA(dir, g_cfgGun);
    for (i = lstrlenA(dir); i > 0 && dir[i - 1] != '\\' && dir[i - 1] != '/'; i--) ;
    dir[i] = 0;

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'v' && line[1] == ' ') {
            float x, y, z;
            if (npos < GUN_MAX_V && sscanf(line + 2, "%f %f %f", &x, &y, &z) == 3) {
                float k = (float)g_cfgGunScale / 100.0f;
                pos[npos][0] = x * k; pos[npos][1] = y * k; pos[npos][2] = z * k; npos++;
            }
        } else if (line[0] == 'v' && line[1] == 't' && line[2] == ' ') {
            float u, v;
            if (nuv < GUN_MAX_V && sscanf(line + 3, "%f %f", &u, &v) == 2) {
                uv[nuv][0] = u; uv[nuv][1] = 1.0f - v;   /* OBJ v is bottom-up */
                nuv++;
            }
        } else if (!strncmp(line, "mtllib ", 7)) {
            char path[MAX_PATH], mline[256], *e;
            FILE *mf;
            lstrcpyA(path, dir);
            lstrcatA(path, line + 7);
            for (e = path + lstrlenA(path); e > path && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '); e--) e[-1] = 0;
            mf = fopen(path, "r");
            if (!mf) { note("gunmodel: no mtl %s, grey it is\n", path); continue; }
            while (fgets(mline, sizeof(mline), mf)) {
                if (!strncmp(mline, "newmtl ", 7) && nmtl < GUN_MAX_MTL) {
                    sscanf(mline + 7, "%63s", mtl[nmtl].name);
                    mtl[nmtl].col = 0xFF909090u;
                    nmtl++;
                } else if (!strncmp(mline, "Kd ", 3) && nmtl) {
                    float r, g, b;
                    if (sscanf(mline + 3, "%f %f %f", &r, &g, &b) == 3)
                        mtl[nmtl - 1].col = 0xFF000000u |
                            ((DWORD)(b * 255.0f + 0.5f) << 16) |
                            ((DWORD)(g * 255.0f + 0.5f) << 8) |
                             (DWORD)(r * 255.0f + 0.5f);
                } else if (!strncmp(mline, "map_Kd ", 7) && !g_gunSRV) {
                    char tp[MAX_PATH], *e2;
                    lstrcpyA(tp, dir);
                    lstrcatA(tp, mline + 7);
                    for (e2 = tp + lstrlenA(tp); e2 > tp &&
                         (e2[-1] == '\n' || e2[-1] == '\r' || e2[-1] == ' '); e2--)
                        e2[-1] = 0;
                    g_gunSRV = gunLoadBmp(tp);
                }
            }
            fclose(mf);
        } else if (!strncmp(line, "usemtl ", 7)) {
            sscanf(line + 7, "%63s", mtlName);
            curCol = 0xFF909090u;
            for (i = 0; i < nmtl; i++)
                if (!lstrcmpA(mtl[i].name, mtlName)) { curCol = mtl[i].col; break; }
        } else if (line[0] == 'f' && line[1] == ' ') {
            int idx[64], tdx[64], n = 0;
            char *t = line + 2;
            while (*t && n < 64) {
                int v, tv = -1;
                while (*t == ' ' || *t == '\t') t++;
                if (*t < '0' && *t != '-') break;
                v = atoi(t);
                if (v < 0) v = (int)npos + v; else v = v - 1;
                {   /* v/vt/vn - the vt sits after the first slash, if any */
                    char *s = t;
                    while (*s && *s != '/' && *s != ' ' && *s != '\t' &&
                           *s != '\n' && *s != '\r') s++;
                    if (*s == '/' && s[1] != '/' && (s[1] >= '0' || s[1] == '-')) {
                        tv = atoi(s + 1);
                        if (tv < 0) tv = (int)nuv + tv; else tv = tv - 1;
                        if (tv < 0 || tv >= (int)nuv) tv = -1;
                    }
                }
                if (v >= 0 && v < (int)npos) { idx[n] = v; tdx[n] = tv; n++; }
                while (*t && *t != ' ' && *t != '\t' && *t != '\n' && *t != '\r') t++;
            }
            for (i = 1; i + 1 < (unsigned)n && nout + 3 <= GUN_MAX_OUT; i++) {
                const float *a = pos[idx[0]], *b = pos[idx[i]], *c = pos[idx[i + 1]];
                int ta = tdx[0], tb = tdx[i], tc = tdx[i + 1];
                int textured = g_gunSRV && ta >= 0 && tb >= 0 && tc >= 0;
                float e1[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
                float e2[3] = { c[0]-a[0], c[1]-a[1], c[2]-a[2] };
                float nrm[3], sh, lit;
                DWORD col, base;
                int k;
                vCross(e1, e2, nrm); vNorm(nrm);
                lit = vDot(nrm, L); if (lit < 0) lit = -lit;
                /* a texture carries its own contrast; light it gentler than
                 * the flat colours or the dark side turns to mud */
                sh = textured ? 0.62f + 0.38f * lit : 0.45f + 0.55f * lit;
                base = textured ? 0xFFFFFFFFu : curCol;
                col = 0xFF000000u |
                      ((DWORD)((base >> 16 & 0xFF) * sh) << 16) |
                      ((DWORD)((base >>  8 & 0xFF) * sh) <<  8) |
                       (DWORD)((base       & 0xFF) * sh);
                for (k = 0; k < 3; k++) {
                    const float *v = k == 0 ? a : k == 1 ? b : c;
                    int tk = k == 0 ? ta : k == 1 ? tb : tc;
                    out[nout].x = v[0]; out[nout].y = v[1]; out[nout].z = v[2];
                    out[nout].colour = col;
                    if (textured) { out[nout].u = uv[tk][0]; out[nout].v = uv[tk][1]; }
                    else          { out[nout].u = -1.0f;     out[nout].v = -1.0f;     }
                    out[nout].depth = 0.0f;
                    out[nout].address = 0;
                    out[nout].specular = out[nout].fog_colour = 0; out[nout].fog = 1;
                    nout++;
                }
            }
        }
    }
    fclose(f);
    if (!nout) {
        note("gunmodel: %s has no faces, using the built-in pistol\n", g_cfgGun);
        nout = gunBuiltin(out);
    }

upload:
    ZeroMemory(&bd, sizeof(bd));
    bd.ByteWidth = nout * sizeof(Vc2Vertex);
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    ZeroMemory(&sd, sizeof(sd));
    sd.pSysMem = out;
    if (FAILED(ID3D11Device_CreateBuffer(g_dev, &bd, &sd, &g_gunVB))) { free(pos); free(uv); free(out); return; }
    g_gunCount = nout;
    free(pos); free(uv); free(out);

    if (FAILED(D3DCompile(SRC_GUN, lstrlenA(SRC_GUN), NULL, NULL, NULL, "vs", "vs_4_0", 0, 0, &vsb, &err)))
        die("gun vertex shader would not compile");
    if (FAILED(D3DCompile(SRC_GUN, lstrlenA(SRC_GUN), NULL, NULL, NULL, "ps", "ps_4_0", 0, 0, &psb, &err)))
        die("gun pixel shader would not compile");
    ID3D11Device_CreateVertexShader(g_dev, ID3D10Blob_GetBufferPointer(vsb),
                                    ID3D10Blob_GetBufferSize(vsb), NULL, &g_vsGun);
    ID3D11Device_CreatePixelShader(g_dev, ID3D10Blob_GetBufferPointer(psb),
                                   ID3D10Blob_GetBufferSize(psb), NULL, &g_psGun);
    ID3D10Blob_Release(vsb); ID3D10Blob_Release(psb);

    ZeroMemory(&dd, sizeof(dd));
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_GREATER;    /* reversed depth, as mProjFov */
    ID3D11Device_CreateDepthStencilState(g_dev, &dd, &g_dssGun);
    note("gunmodel: %u triangles from %s\n", nout / 3, g_cfgGun);
}

/* into the current target, local-space view*proj; call from drawUI */
static void drawGunAt(const float *vp, const XrPosef *pose, DWORD fireAt, DWORD reloadAt)
{
    float r[16], m[16], mvp[16];
    D3D11_MAPPED_SUBRESOURCE mm;
    UINT stride = sizeof(Vc2Vertex), offset = 0;
    float anim[16];
    DWORD now;
    mFromQuat(&pose->orientation, r);
    mIdent(m);
    m[3] = pose->position.x; m[7] = pose->position.y; m[11] = pose->position.z;
    mMul(m, r, m);
    /* recoil: a centimetre back along the barrel, gone in 90 ms;
     * reload: the muzzle flicks up 35 degrees and back over reloadms ms */
    now = GetTickCount();
    mIdent(anim);
    if (fireAt && now - fireAt < 90) {
        float k = 1.0f - (float)(now - fireAt) / 90.0f;
        anim[11] = 0.012f * k;
    }
    if (reloadAt && now - reloadAt < (DWORD)g_cfgReloadMs) {
        float u = (float)(now - reloadAt) / (float)g_cfgReloadMs;
        float a = 0.61f * sinf(u * 3.14159265f);   /* 35 degrees peak */
        float rx[16];
        mIdent(rx);
        rx[5] = cosf(a); rx[6] = -sinf(a);
        rx[9] = sinf(a); rx[10] = cosf(a);
        mMul(anim, rx, anim);
    }
    mMul(m, anim, m);
    mMul(vp, m, mvp);
    if (SUCCEEDED(ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)g_cb, 0,
                  D3D11_MAP_WRITE_DISCARD, 0, &mm))) {
        CopyMemory(mm.pData, mvp, 64);
        ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)g_cb, 0);
    }
    ID3D11DeviceContext_OMSetDepthStencilState(g_ctx, g_dssGun, 0);
    ID3D11DeviceContext_IASetInputLayout(g_ctx, g_il);
    ID3D11DeviceContext_IASetVertexBuffers(g_ctx, 0, 1, &g_gunVB, &stride, &offset);
    ID3D11DeviceContext_VSSetShader(g_ctx, g_vsGun, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(g_ctx, 0, 1, &g_cb);
    ID3D11DeviceContext_PSSetShader(g_ctx, g_psGun, NULL, 0);
    if (g_gunSRV) {
        ID3D11DeviceContext_PSSetShaderResources(g_ctx, 0, 1, &g_gunSRV);
        ID3D11DeviceContext_PSSetSamplers(g_ctx, 0, 1, &g_samp);
    }
    ID3D11DeviceContext_Draw(g_ctx, g_gunCount, 0);
}

/* Both hands. In akimbo the other hand is a gun in its own right - it aims,
 * it fires and it has its own crosshair in the game - so it gets the same
 * model at its own pose, with its own recoil and reload. Without akimbo each
 * model follows its own player's native active status. */
static void drawGun(const float *vp)
{
    D3D11_MAPPED_SUBRESOURCE mm;
    int one = g_gunValid, two = g_gun2Valid;
    if (!g_gunVB || g_inScope || (!one && !two)) return;
    /* depth=1 fills the buffer with the ENGINE's sort keys (a floor tile of
     * layer 3 sits at 0.81), while the gun is tested with real reversed-Z
     * depth (0.1 at half a metre). Two different numbers in one buffer: the
     * gun lost to every wall and floor behind it and simply was not there.
     * Nothing in the game is nearer than a pistol in the hand, so clear the
     * buffer first - the gun then sorts only against itself, on top of all. */
    if (g_cfgDepth == 1) {
        ID3D11RenderTargetView *rtv = NULL;
        ID3D11DepthStencilView *dsv = NULL;
        ID3D11DeviceContext_OMGetRenderTargets(g_ctx, 1, &rtv, &dsv);
        if (dsv) {
            ID3D11DeviceContext_ClearDepthStencilView(g_ctx, dsv, D3D11_CLEAR_DEPTH, 0.0f, 0);
            ID3D11DepthStencilView_Release(dsv);
        }
        if (rtv) ID3D11RenderTargetView_Release(rtv);
    }
    if (one) drawGunAt(vp, &g_gunPose,  g_gunFireAt,  g_gunReloadAt);
    if (two) drawGunAt(vp, &g_gun2Pose, g_gun2FireAt, g_gun2ReloadAt);
    ID3D11DeviceContext_OMSetDepthStencilState(g_ctx, g_dssUI, 0);
    /* restore the shared view*proj for whoever draws next */
    if (SUCCEEDED(ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)g_cb, 0,
                  D3D11_MAP_WRITE_DISCARD, 0, &mm))) {
        CopyMemory(mm.pData, vp, 64);
        ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)g_cb, 0);
    }
}

/* ------------------------------------------------------------------- scope
 * Magnifying the whole view was the wrong tool: every degree of head wobble
 * gets multiplied by the zoom and the world shakes. So the zoom becomes a
 * scope instead - the scene is rendered a second time, from the head centre,
 * with the frustum narrowed by the zoom factor, into an off-screen target;
 * the main view stays at 1x, and a disc hung one metre in front of the head
 * shows the magnified image. Inside the disc the shake is still there (that
 * is what magnification means), but it lives in a small window while the
 * rest of the world holds still, which is what the eye needs.
 *
 * One scope pass per frame from the head centre, not one per eye: the disc
 * carries the same picture to both eyes, so it reads as a flat window at one
 * metre - a monocular sight, like the real thing - instead of two magnified
 * eyes fighting over depth. Each disc pixel finds its scope texel by pushing
 * its local-space position through the scope's own view*proj, so the
 * mapping is exact whatever the per-eye frustum is. */
static ID3D11Texture2D          *g_scopeTex;
static ID3D11RenderTargetView   *g_scopeRtv;
static ID3D11DepthStencilView   *g_scopeDsv;
static ID3D11ShaderResourceView *g_scopeSRV;
static ID3D11Buffer             *g_scopeVB;
static ID3D11Buffer             *g_cbScope;
static ID3D11VertexShader       *g_vsScope;
static ID3D11PixelShader        *g_psScope;
static int   g_scopeW, g_scopeH;
static float g_scopeVP[16];         /* view*proj of the scope pass, local space */
static int   g_scopeDrawn;          /* this frame has a scope image to show   */

static void releaseGraphics(void)
{
    unsigned src,dst;
#define RELEASE(p) do { if (p) { IUnknown_Release((IUnknown *)(p)); (p) = NULL; } } while (0)
    if (!g_ctx) return;
    ID3D11DeviceContext_ClearState(g_ctx);
    for(src=0;src<12;src++)for(dst=0;dst<12;dst++)RELEASE(g_nativeBlend[src][dst]);
    for(src=2;src<=3;src++)RELEASE(g_nativeCull[src]);
    RELEASE(g_dssRead);
    RELEASE(g_scopeSRV); RELEASE(g_scopeRtv); RELEASE(g_scopeDsv); RELEASE(g_scopeTex);
    RELEASE(g_scopeVB); RELEASE(g_cbScope); RELEASE(g_vsScope); RELEASE(g_psScope);
    RELEASE(g_gunVB); RELEASE(g_gunSRV); RELEASE(g_vsGun); RELEASE(g_psGun); RELEASE(g_dssGun);
    RELEASE(g_atlSRV); RELEASE(g_atlTex); RELEASE(g_scrSRV); RELEASE(g_scrTex);
    RELEASE(g_vrMenuSRV); RELEASE(g_vrMenuTexture); RELEASE(g_vrMenuBlend);
    RELEASE(g_scrVB); RELEASE(g_vsTex); RELEASE(g_psTex); RELEASE(g_ilTex);
    RELEASE(g_samp); RELEASE(g_cbRes); RELEASE(g_blendAdd);
    RELEASE(g_ovVB); RELEASE(g_vb); RELEASE(g_cb); RELEASE(g_vs); RELEASE(g_ps);
    RELEASE(g_psOv); RELEASE(g_il); RELEASE(g_rs); RELEASE(g_dss); RELEASE(g_dssUI);
    RELEASE(g_ctx); RELEASE(g_dev);
#undef RELEASE
}

/* The disc used to find its texel by pushing its own world position through
 * the scope camera. That reads as principled and is a no-op: the scope sits at
 * the head and the disc hangs on the head's forward axis, so a disc point at
 * angle theta projects to the scope's NDC for angle theta and samples the world
 * at angle theta. Magnification exactly 1.00 whatever the zoom key says - which
 * is the whole of "the sight barely zooms". It never zoomed at all.
 *
 * The mapping that does magnify is the trivial one: the disc spans the texture,
 * and the scope pass is rendered with a cone of half angle
 * atan(tan(scopesize) / zoom). A disc point at theta then shows the world at
 * atan(tan(theta) / zoom) - magnification exactly zoom, by construction. */
/* The sight's picture must sit where the TARGET is, not where the glass is.
 *
 * Both eyes used to get ONE image, so each eye read the same texel at the same
 * point ON the glass and the eyes converged on the glass - 40 cm away, on the
 * end of your arm - while the thing you were aiming at was fifty metres out.
 * That is the refocusing. Moving the glass cannot fix it: convergence follows
 * whatever surface the image is pinned to.
 *
 * Trying to undo it in the shader, by having every fragment work out its own
 * eye ray, was clever and did not survive contact. This does the dull thing
 * instead: render the magnified view TWICE, once from each eye, and let each
 * eye read its own texture through the same piece of glass. A point on the
 * glass then shows the same world DIRECTION to both eyes - parallel lines of
 * sight, convergence at infinity, which is where the enemy is - and near
 * objects keep their proper parallax because each image really was drawn from
 * that eye. It costs one extra pass over a 1997 game's geometry. It is free.
 *
 * uv is just the disc's own coordinate, and the pass is rendered with half
 * angle atan(tan(glass half angle) / zoom), so magnification is exactly zoom. */
static const char *SRC_SCOPE =
"cbuffer C : register(b0) { row_major float4x4 mvp; };\n"
"Texture2D tex : register(t0); SamplerState smp : register(s0);\n"
"struct VIn  { float3 p : POSITION; float2 t : TEXCOORD; };\n"
"struct VOut { float4 p : SV_POSITION; float2 t : TEXCOORD0; };\n"
"VOut vs(VIn i) { VOut o; o.p = mul(mvp, float4(i.p,1)); o.t = i.t; return o; }\n"
"float4 ps(VOut i) : SV_TARGET {\n"
"    float r2 = dot(i.t, i.t);\n"
"    clip(1.0 - r2);\n"
"    float2 uv = i.t * 0.5 + 0.5;\n"
"    uv.y = 1.0 - uv.y;\n"
"    float3 c = tex.Sample(smp, uv).rgb;\n"
"    float rim  = smoothstep(0.86, 0.97, r2);\n"
"    float tick = smoothstep(0.97, 0.99, r2);\n"
"    c = lerp(c, float3(0.015, 0.015, 0.020), rim);\n"
"    c = lerp(c, float3(0.55, 0.62, 0.70), tick);\n"
"    return float4(c, 1);\n"
"}\n";

static void makeScopePipeline(void)
{
    ID3DBlob *vsb = NULL, *psb = NULL, *err = NULL;
    D3D11_BUFFER_DESC bd;
    if (FAILED(D3DCompile(SRC_SCOPE, lstrlenA(SRC_SCOPE), NULL, NULL, NULL, "vs", "vs_4_0", 0, 0, &vsb, &err)))
        die("scope vertex shader would not compile");
    if (FAILED(D3DCompile(SRC_SCOPE, lstrlenA(SRC_SCOPE), NULL, NULL, NULL, "ps", "ps_4_0", 0, 0, &psb, &err)))
        die("scope pixel shader would not compile");
    ID3D11Device_CreateVertexShader(g_dev, ID3D10Blob_GetBufferPointer(vsb),
                                    ID3D10Blob_GetBufferSize(vsb), NULL, &g_vsScope);
    ID3D11Device_CreatePixelShader(g_dev, ID3D10Blob_GetBufferPointer(psb),
                                   ID3D10Blob_GetBufferSize(psb), NULL, &g_psScope);
    ID3D10Blob_Release(vsb); ID3D10Blob_Release(psb);

    ZeroMemory(&bd, sizeof(bd));
    bd.ByteWidth = 6 * sizeof(TexVtx);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Device_CreateBuffer(g_dev, &bd, NULL, &g_scopeVB);
    bd.ByteWidth = 176;   /* mvp + eye + three axes + params */
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ID3D11Device_CreateBuffer(g_dev, &bd, NULL, &g_cbScope);
}

static void ensureScopeTarget(int w, int h)
{
    D3D11_TEXTURE2D_DESC td;
    ID3D11Texture2D *depth = NULL;
    if (g_scopeTex && g_scopeW == w && g_scopeH == h) return;
    if (g_scopeSRV) { ID3D11ShaderResourceView_Release(g_scopeSRV); g_scopeSRV = NULL; }
    if (g_scopeRtv) { ID3D11RenderTargetView_Release(g_scopeRtv); g_scopeRtv = NULL; }
    if (g_scopeDsv) { ID3D11DepthStencilView_Release(g_scopeDsv); g_scopeDsv = NULL; }
    if (g_scopeTex) { ID3D11Texture2D_Release(g_scopeTex); g_scopeTex = NULL; }
    ZeroMemory(&td, sizeof(td));
    td.Width = (UINT)w; td.Height = (UINT)h;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &g_scopeTex))) die("scope colour target");
    ID3D11Device_CreateRenderTargetView(g_dev, (ID3D11Resource *)g_scopeTex, NULL, &g_scopeRtv);
    ID3D11Device_CreateShaderResourceView(g_dev, (ID3D11Resource *)g_scopeTex, NULL, &g_scopeSRV);
    td.Format = DXGI_FORMAT_D32_FLOAT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &depth))) die("scope depth target");
    ID3D11Device_CreateDepthStencilView(g_dev, (ID3D11Resource *)depth, NULL, &g_scopeDsv);
    ID3D11Texture2D_Release(depth);
    g_scopeW = w; g_scopeH = h;
}

/* Render the magnified view. head = pose the scope looks from (head centre),
 * fov = the eye's frustum to narrow, model = game->local as for the main pass,
 * zoom = magnification. Also hangs the disc: one metre ahead along the head's
 * forward vector, radius from scopesize. Leaves g_vpX / g_noClear as found. */
static void drawScopePass(const XrPosef *head, const XrFovf *fov, const float *model,
                          int w, int h, float zoom)
{
    float view[16], proj[16], mvp[16];
    float rot[16], fwd[3], ax[3], ay[3], c[3], R, half;
    float saveX = g_vpX; int saveClear = g_noClear;
    XrPosef cam;
    int side, k;
    D3D11_MAPPED_SUBRESOURCE m;
    TexVtx v[6];
    int i;
    static const float corner[6][2] = {
        { -1,  1 }, { 1, 1 }, { 1, -1 },
        { -1,  1 }, { 1, -1 }, { -1, -1 } };

    g_scopeDrawn = 0;
    if (!g_cfgScope || zoom <= 1.001f || !g_scopeVB) return;
    (void)fov;

    mFromQuat(&head->orientation, rot);
    fwd[0] = -rot[2]; fwd[1] = -rot[6]; fwd[2] = -rot[10];

    cam = *head;
    if (g_cfgScopeMount && g_gunValid) {
        /* ------------------------------------------------ sight on the gun
         * A disc pinned to your face is a HUD, and a HUD is the one thing a
         * headset cannot make you believe. Put the glass where a sight goes -
         * on the rail - and the problem solves itself: raise the gun and you
         * are looking through it, drop the gun and it is gone. Nothing hangs
         * in front of you and nothing has to be dismissed.
         *
         * The pass is rendered FROM THE HEAD, ALONG THE GUN's axis. That is
         * what a scope with a generous exit pupil does: the picture is
         * anchored to where the barrel points, not to where you look, so the
         * laser dot is always dead centre in the glass - and moving your head
         * moves the glass across your view without dragging the image with
         * it, which is the parallax the eye is expecting. */
        float grot[16], gf[3], toGlass[3], dHG, gs;
        gs = 1.35f * (float)g_cfgGunScale / 100.0f;
        mFromQuat(&g_gunPose.orientation, grot);
        ax[0] = grot[0]; ax[1] = grot[4]; ax[2] = grot[8];    /* gun right */
        ay[0] = grot[1]; ay[1] = grot[5]; ay[2] = grot[9];    /* gun up    */
        gf[0] = -grot[2]; gf[1] = -grot[6]; gf[2] = -grot[10];
        /* glass on the rail: up off the slide, back toward the shooter */
        c[0] = g_gunPose.position.x + ay[0]*0.055f*gs - gf[0]*0.030f*gs;
        c[1] = g_gunPose.position.y + ay[1]*0.055f*gs - gf[1]*0.030f*gs;
        c[2] = g_gunPose.position.z + ay[2]*0.055f*gs - gf[2]*0.030f*gs;
        toGlass[0] = c[0] - head->position.x;
        toGlass[1] = c[1] - head->position.y;
        toGlass[2] = c[2] - head->position.z;
        dHG = (float)sqrt((double)vDot(toGlass, toGlass));
        if (dHG < 0.08f) return;                     /* glass in your eye */
        /* skip the whole second pass when the gun is not in front of you */
        if ((toGlass[0]*fwd[0] + toGlass[1]*fwd[1] + toGlass[2]*fwd[2]) / dHG < 0.55f)
            return;
        R    = (float)g_cfgScopeGlass * 0.001f * gs;  /* millimetres */
        /* the cone the glass can show from where the eye is, with margin for
         * looking through it off-axis */
        half = atanf((R / dHG) / zoom);
        cam.orientation = g_gunPose.orientation;      /* look where it points */
    } else {
        /* ------------------------------------------- the old head-locked disc
         * kept for anyone who would rather have the sight always to hand. It
         * hangs at scopedist metres, not at one: the image is the same in both
         * eyes, so wherever the quad sits is where the eyes converge, and a
         * sight at arm's length means refocusing between it and the target. */
        float D = (float)g_cfgScopeDist;
        ax[0] = rot[0]; ax[1] = rot[4]; ax[2] = rot[8];
        ay[0] = rot[1]; ay[1] = rot[5]; ay[2] = rot[9];
        c[0] = head->position.x + fwd[0] * D;
        c[1] = head->position.y + fwd[1] * D;
        c[2] = head->position.z + fwd[2] * D;
        R    = D * tanf((float)g_cfgScopeDeg * 3.14159265f / 180.0f);
        half = atanf((R / D) / zoom);
    }

    side = h < w ? h : w;               /* the glass is round: render it square */
    ensureScopeTarget(side, side);

    mView(&cam, view);
    mProjFov(-half, half, half, -half, NEAR_M, FAR_M, proj);
    mMul(proj, view, g_scopeVP);
    mMul(view, model, mvp);
    mMul(proj, mvp, mvp);

    g_vpX = 0.0f; g_noClear = 0;
    drawWith(g_scopeRtv, g_scopeDsv, side, side, mvp, NULL);
    /* the laser belongs in the sight; the pistol and the floating screen do
     * not - at four times life size a gun held up to aim fills the whole glass
     * and the sight shows you your own slide */
    g_inScope = 1;
    drawUI(g_scopeVP);
    g_inScope = 0;
    g_vpX = saveX; g_noClear = saveClear;

    for (i = 0; i < 6; i++) {
        float cu = corner[i][0], cv = corner[i][1];
        for (k = 0; k < 3; k++)
            (&v[i].x)[k] = c[k] + ax[k] * cu * R + ay[k] * cv * R;
        v[i].u = cu; v[i].v = cv;       /* unit disc coordinates, not texture */
    }
    if (SUCCEEDED(ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)g_scopeVB, 0,
                  D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        CopyMemory(m.pData, v, sizeof(v));
        ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)g_scopeVB, 0);
    }
    g_scopeDrawn = 1;
}

/* the disc itself, into the current eye; call after drawUI(vp) with the same
 * local-space view*proj */
static void drawScopeDisc(const float *vp)
{
    D3D11_MAPPED_SUBRESOURCE m;
    UINT stride = sizeof(TexVtx), offset = 0;
    ID3D11ShaderResourceView *none = NULL;
    if (!g_scopeDrawn || !g_scopeSRV) return;
    if (SUCCEEDED(ID3D11DeviceContext_Map(g_ctx, (ID3D11Resource *)g_cbScope, 0,
                  D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        CopyMemory(m.pData, vp, 64);
        ID3D11DeviceContext_Unmap(g_ctx, (ID3D11Resource *)g_cbScope, 0);
    }
    ID3D11DeviceContext_OMSetDepthStencilState(g_ctx, g_dssUI, 0);
    ID3D11DeviceContext_IASetInputLayout(g_ctx, g_ilTex);
    ID3D11DeviceContext_IASetVertexBuffers(g_ctx, 0, 1, &g_scopeVB, &stride, &offset);
    ID3D11DeviceContext_VSSetShader(g_ctx, g_vsScope, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(g_ctx, 0, 1, &g_cbScope);
    ID3D11DeviceContext_PSSetShader(g_ctx, g_psScope, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(g_ctx, 0, 1, &g_scopeSRV);
    ID3D11DeviceContext_PSSetSamplers(g_ctx, 0, 1, &g_samp);
    ID3D11DeviceContext_Draw(g_ctx, 6, 0);
    /* unbind: next frame this texture is a render target again */
    ID3D11DeviceContext_PSSetShaderResources(g_ctx, 0, 1, &none);
    ID3D11DeviceContext_VSSetConstantBuffers(g_ctx, 0, 1, &g_cb);
}

/* ------------------------------------------------------------------- OpenXR */

static PFN_xrGetInstanceProcAddr xrGIPA;
#define XRFN(name) static PFN_##name p##name
XRFN(xrCreateInstance); XRFN(xrDestroyInstance); XRFN(xrGetSystem);
XRFN(xrGetSystemProperties); XRFN(xrCreateSession); XRFN(xrDestroySession);
XRFN(xrCreateReferenceSpace); XRFN(xrDestroySpace);
XRFN(xrEnumerateViewConfigurationViews);
XRFN(xrCreateSwapchain); XRFN(xrDestroySwapchain); XRFN(xrEnumerateSwapchainImages);
XRFN(xrEnumerateSwapchainFormats);
XRFN(xrAcquireSwapchainImage); XRFN(xrWaitSwapchainImage); XRFN(xrReleaseSwapchainImage);
XRFN(xrBeginSession); XRFN(xrEndSession); XRFN(xrWaitFrame); XRFN(xrBeginFrame);
XRFN(xrEndFrame); XRFN(xrLocateViews); XRFN(xrPollEvent); XRFN(xrResultToString);
XRFN(xrEnumerateInstanceExtensionProperties);
XRFN(xrStringToPath); XRFN(xrCreateActionSet); XRFN(xrDestroyActionSet); XRFN(xrCreateAction);
XRFN(xrCreateActionSpace); XRFN(xrSuggestInteractionProfileBindings);
XRFN(xrAttachSessionActionSets); XRFN(xrSyncActions); XRFN(xrApplyHapticFeedback);
XRFN(xrGetActionStateBoolean); XRFN(xrGetActionStateFloat); XRFN(xrLocateSpace);
#undef XRFN

/* the loader can only name results once an instance exists, and the failures
 * that matter most happen before that, so keep a small table of our own */
static const char *xrName(XrResult r)
{
    switch ((int)r) {
    case  0: return "XR_SUCCESS";
    case -1: return "XR_ERROR_VALIDATION_FAILURE";
    case -2: return "XR_ERROR_RUNTIME_FAILURE";
    case -3: return "XR_ERROR_OUT_OF_MEMORY";
    case -4: return "XR_ERROR_API_VERSION_UNSUPPORTED (runtime is older than we asked for)";
    case -6: return "XR_ERROR_INITIALIZATION_FAILED (runtime present but would not start)";
    case -7: return "XR_ERROR_FUNCTION_UNSUPPORTED";
    case -8: return "XR_ERROR_FEATURE_UNSUPPORTED";
    case -9: return "XR_ERROR_EXTENSION_NOT_PRESENT";
    case -13: return "XR_ERROR_HANDLE_INVALID";
    case -50: return "XR_ERROR_FORM_FACTOR_UNSUPPORTED";
    case -51: return "XR_ERROR_FORM_FACTOR_UNAVAILABLE (no headset - enable the null driver)";
    default: return "unknown XrResult";
    }
}

static void loadXr(XrInstance inst)
{
#define GET(n) do { if (XR_FAILED(xrGIPA(inst, #n, (PFN_xrVoidFunction *)&p##n))) \
                        die("missing OpenXR entry point " #n); } while (0)
    GET(xrGetSystem); GET(xrGetSystemProperties); GET(xrCreateSession);
    GET(xrDestroySession); GET(xrCreateReferenceSpace); GET(xrDestroySpace);
    GET(xrEnumerateViewConfigurationViews); GET(xrCreateSwapchain); GET(xrDestroySwapchain);
    GET(xrEnumerateSwapchainImages); GET(xrEnumerateSwapchainFormats);
    GET(xrAcquireSwapchainImage); GET(xrWaitSwapchainImage);
    GET(xrReleaseSwapchainImage); GET(xrBeginSession); GET(xrEndSession);
    GET(xrWaitFrame); GET(xrBeginFrame); GET(xrEndFrame); GET(xrLocateViews);
    GET(xrPollEvent); GET(xrDestroyInstance); GET(xrResultToString);
    GET(xrStringToPath); GET(xrCreateActionSet); GET(xrDestroyActionSet); GET(xrCreateAction);
    GET(xrCreateActionSpace); GET(xrSuggestInteractionProfileBindings);
    GET(xrAttachSessionActionSets); GET(xrSyncActions); GET(xrApplyHapticFeedback);
    GET(xrGetActionStateBoolean); GET(xrGetActionStateFloat); GET(xrLocateSpace);
#undef GET
}

/* ------------------------------------------------------ controller actions */

static int headPoseReady(int anchored,XrSessionState state,XrResult located,
                         uint32_t count,XrViewStateFlags flags)
{
    XrViewStateFlags required=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    if(!anchored)required|=XR_VIEW_STATE_POSITION_TRACKED_BIT|XR_VIEW_STATE_ORIENTATION_TRACKED_BIT;
    return XR_SUCCEEDED(located) && count==2 && (flags&required)==required &&
        (anchored || state==XR_SESSION_STATE_FOCUSED);
}

static XrActionSet g_actSet;
static XrAction    g_actAim, g_actFire, g_actPause, g_actBack, g_actReload, g_actRecentre, g_actSettings, g_actMenu, g_actMenuX, g_actHaptic;
static volatile float g_zoomLevel = 1.0f;   /* 1 = no zoom; optional cinematic zoom */
static int g_autoOn;                        /* the game is in a zoom moment  */
static int g_resetControls;
static HotdShotFeedback g_shotFeedback[2];
static XrPath      g_handPath[2];               /* 0 = left, 1 = right */
static XrSpace     g_aimSpace[2];

static XrPath path(XrInstance inst, const char *s)
{
    XrPath p = XR_NULL_PATH;
    pxrStringToPath(inst, s, &p);
    return p;
}

/* one profile's worth of suggested bindings; a refusal is fine, some runtimes
 * simply do not know a given controller */
static void suggest(XrInstance inst, const char *profile,
                    const XrActionSuggestedBinding *b, uint32_t n)
{
    XrInteractionProfileSuggestedBinding s;
    XrResult r;
    ZeroMemory(&s, sizeof(s));
    s.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
    s.interactionProfile = path(inst, profile);
    s.suggestedBindings = b;
    s.countSuggestedBindings = n;
    r = pxrSuggestInteractionProfileBindings(inst, &s);
    if (XR_FAILED(r)) note("  bindings for %s not accepted (%d), skipped\n", profile, (int)r);
}

#define BIND(act, p) do { bb[bn].action = (act); bb[bn].binding = path(inst, p); bn++; } while (0)

static void initActions(XrInstance inst, XrSession session)
{
    XrActionSetCreateInfo asci;
    XrActionCreateInfo aci;
    XrSessionActionSetsAttachInfo at;
    XrActionSuggestedBinding bb[24];
    uint32_t bn;
    int h;

    g_handPath[0] = path(inst, "/user/hand/left");
    g_handPath[1] = path(inst, "/user/hand/right");

    ZeroMemory(&asci, sizeof(asci));
    asci.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    lstrcpyA(asci.actionSetName, "gameplay");
    lstrcpyA(asci.localizedActionSetName, "Gameplay");
    if (XR_FAILED(pxrCreateActionSet(inst, &asci, &g_actSet)))
        die("could not create the action set");

#define MAKE(var, name, loc, kind) do {                                   \
        ZeroMemory(&aci, sizeof(aci));                                    \
        aci.type = XR_TYPE_ACTION_CREATE_INFO;                            \
        lstrcpyA(aci.actionName, name);                                   \
        lstrcpyA(aci.localizedActionName, loc);                           \
        aci.actionType = kind;                                            \
        aci.countSubactionPaths = 2;                                      \
        aci.subactionPaths = g_handPath;                                  \
        if (XR_FAILED(pxrCreateAction(g_actSet, &aci, &var)))             \
            die("could not create action " name);                         \
    } while (0)

    MAKE(g_actAim,    "aim",    "Aim",             XR_ACTION_TYPE_POSE_INPUT);
    MAKE(g_actFire,   "fire",   "Shoot / Start",   XR_ACTION_TYPE_BOOLEAN_INPUT);
    MAKE(g_actPause,  "start_pause", "Start / Pause", XR_ACTION_TYPE_BOOLEAN_INPUT);
    MAKE(g_actBack,   "back",   "Skip / Cancel",  XR_ACTION_TYPE_BOOLEAN_INPUT);
    MAKE(g_actReload, "reload", "Reload",         XR_ACTION_TYPE_BOOLEAN_INPUT);
    MAKE(g_actRecentre, "recentre", "Recentre",    XR_ACTION_TYPE_BOOLEAN_INPUT);
    MAKE(g_actSettings, "vr_settings", "VR Settings", XR_ACTION_TYPE_BOOLEAN_INPUT);
    MAKE(g_actMenu,   "menu_vertical", "Menu up / down", XR_ACTION_TYPE_FLOAT_INPUT);
    MAKE(g_actMenuX,  "menu_horizontal", "Menu left / right", XR_ACTION_TYPE_FLOAT_INPUT);
    MAKE(g_actHaptic, "haptic",   "Recoil",           XR_ACTION_TYPE_VIBRATION_OUTPUT);
#undef MAKE

    /* the runtime coerces float sources to booleans and back where needed */
    bn = 0;
    BIND(g_actAim,  "/user/hand/left/input/aim/pose");
    BIND(g_actAim,  "/user/hand/right/input/aim/pose");
    BIND(g_actFire, "/user/hand/left/input/select/click");
    BIND(g_actFire, "/user/hand/right/input/select/click");
    BIND(g_actHaptic, "/user/hand/left/output/haptic");
    BIND(g_actHaptic, "/user/hand/right/output/haptic");
    BIND(g_actPause, "/user/hand/left/input/menu/click");
    BIND(g_actPause, "/user/hand/right/input/menu/click");
    suggest(inst, "/interaction_profiles/khr/simple_controller", bb, bn);

    bn = 0;
    BIND(g_actAim,  "/user/hand/left/input/aim/pose");
    BIND(g_actAim,  "/user/hand/right/input/aim/pose");
    BIND(g_actFire, "/user/hand/left/input/trigger/value");
    BIND(g_actFire, "/user/hand/right/input/trigger/value");
    BIND(g_actHaptic, "/user/hand/left/output/haptic");
    BIND(g_actHaptic, "/user/hand/right/output/haptic");
    BIND(g_actPause, "/user/hand/left/input/x/click");
    BIND(g_actPause, "/user/hand/right/input/a/click");
    BIND(g_actBack,  "/user/hand/left/input/y/click");
    BIND(g_actBack,  "/user/hand/right/input/b/click");
    BIND(g_actReload, "/user/hand/left/input/squeeze/value");
    BIND(g_actReload, "/user/hand/right/input/squeeze/value");
    BIND(g_actRecentre, "/user/hand/right/input/thumbstick/click");
    BIND(g_actSettings, "/user/hand/left/input/thumbstick/click");
    BIND(g_actMenu, "/user/hand/left/input/thumbstick/y");
    BIND(g_actMenu, "/user/hand/right/input/thumbstick/y");
    BIND(g_actMenuX, "/user/hand/left/input/thumbstick/x");
    BIND(g_actMenuX, "/user/hand/right/input/thumbstick/x");
    suggest(inst, "/interaction_profiles/oculus/touch_controller", bb, bn);

    bn = 0;
    BIND(g_actAim,  "/user/hand/left/input/aim/pose");
    BIND(g_actAim,  "/user/hand/right/input/aim/pose");
    BIND(g_actFire, "/user/hand/left/input/trigger/value");
    BIND(g_actFire, "/user/hand/right/input/trigger/value");
    BIND(g_actHaptic, "/user/hand/left/output/haptic");
    BIND(g_actHaptic, "/user/hand/right/output/haptic");
    BIND(g_actPause, "/user/hand/left/input/a/click");
    BIND(g_actPause, "/user/hand/right/input/a/click");
    BIND(g_actBack,  "/user/hand/left/input/b/click");
    BIND(g_actBack,  "/user/hand/right/input/b/click");
    BIND(g_actReload, "/user/hand/left/input/squeeze/value");
    BIND(g_actReload, "/user/hand/right/input/squeeze/value");
    BIND(g_actRecentre, "/user/hand/right/input/thumbstick/click");
    BIND(g_actSettings, "/user/hand/left/input/thumbstick/click");
    BIND(g_actMenu, "/user/hand/left/input/thumbstick/y");
    BIND(g_actMenu, "/user/hand/right/input/thumbstick/y");
    BIND(g_actMenuX, "/user/hand/left/input/thumbstick/x");
    BIND(g_actMenuX, "/user/hand/right/input/thumbstick/x");
    suggest(inst, "/interaction_profiles/valve/index_controller", bb, bn);

    bn = 0;
    BIND(g_actAim,  "/user/hand/left/input/aim/pose");
    BIND(g_actAim,  "/user/hand/right/input/aim/pose");
    BIND(g_actFire, "/user/hand/left/input/trigger/value");
    BIND(g_actFire, "/user/hand/right/input/trigger/value");
    BIND(g_actHaptic, "/user/hand/left/output/haptic");
    BIND(g_actHaptic, "/user/hand/right/output/haptic");
    BIND(g_actRecentre, "/user/hand/right/input/trackpad/click");
    BIND(g_actSettings, "/user/hand/left/input/trackpad/click");
    BIND(g_actPause, "/user/hand/left/input/menu/click");
    BIND(g_actPause, "/user/hand/right/input/menu/click");
    BIND(g_actReload, "/user/hand/left/input/squeeze/click");
    BIND(g_actReload, "/user/hand/right/input/squeeze/click");
    BIND(g_actMenu, "/user/hand/left/input/trackpad/y");
    BIND(g_actMenu, "/user/hand/right/input/trackpad/y");
    BIND(g_actMenuX, "/user/hand/left/input/trackpad/x");
    BIND(g_actMenuX, "/user/hand/right/input/trackpad/x");
    suggest(inst, "/interaction_profiles/htc/vive_controller", bb, bn);

    for (h = 0; h < 2; h++) {
        XrActionSpaceCreateInfo si;
        ZeroMemory(&si, sizeof(si));
        si.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
        si.action = g_actAim;
        si.subactionPath = g_handPath[h];
        si.poseInActionSpace.orientation.w = 1.0f;
        if (XR_FAILED(pxrCreateActionSpace(session, &si, &g_aimSpace[h])))
            die("could not create the aim space");
    }

    ZeroMemory(&at, sizeof(at));
    at.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    at.countActionSets = 1;
    at.actionSets = &g_actSet;
    if (XR_FAILED(pxrAttachSessionActionSets(session, &at)))
        die("could not attach the action set");
    note("controllers wired: trigger shoot/start, head-relative up 50deg/down 45deg/sides 60deg + trigger reload, A/X start/pause, B/Y skip/cancel, grip reload,\n"
         "                   stick four-way menus, right click recentre; left click VR Settings\n");
}
#undef BIND

/*
 * Turn one controller pose (origin p, forward d, both in local metre space)
 * into native aim coordinates plus the hit point in local space for the laser.
 * This is exactly the math the single-gun path has always used, pulled out so
 * the akimbo second hand runs the identical, proven projection rather than a
 * parallel copy. Writes sxOut/syOut and hitLocal; returns the haveHit kind
 * (0 nothing, 1 world, 2 floating screen).
 */
static int aimFromPose(const float *p, const float *d,
                       float yaw0, float ox, float oy, float oz,
                       float *sxOut, float *syOut, float *hitLocal)
{
    float sx = 320.0f, sy = 240.0f;
    int haveHit = 0;

    if (g_scrVisible && g_scrHave) {
        float toC[3] = { g_scrC[0]-p[0], g_scrC[1]-p[1], g_scrC[2]-p[2] };
        float denom = vDot(d, g_scrN);
        if (denom < -1e-6f || denom > 1e-6f) {
            float t = vDot(toC, g_scrN) / denom;
            if (t > 0.05f) {
                float hp[3] = { p[0]+d[0]*t, p[1]+d[1]*t, p[2]+d[2]*t };
                float rel[3] = { hp[0]-g_scrC[0], hp[1]-g_scrC[1], hp[2]-g_scrC[2] };
                float u = vDot(rel, g_scrR) / g_scrHalfW;
                float v = vDot(rel, g_scrU) / g_scrHalfH;
                if (u > -1.05f && u < 1.05f && v > -1.05f && v < 1.05f) {
                    if (u < -1) u = -1; if (u > 1) u = 1;
                    if (v < -1) v = -1; if (v > 1) v = 1;
                    sx = 320.0f + u * 319.0f;
                    sy = 240.0f - v * 239.0f;
                    hitLocal[0] = hp[0]; hitLocal[1] = hp[1]; hitLocal[2] = hp[2];
                    haveHit = 2;
                }
            }
        }
    }

    if (!haveHit) {
        float cy = cosf(-yaw0), syw = sinf(-yaw0);
        float pl[3] = { p[0]-ox, p[1]-oy, p[2]-oz };
        float pg[3], dg[3], t;
        pg[0] = ( cy*pl[0] + syw*pl[2]) * g_unitsPerMetre;
        pg[1] =   pl[1] * g_unitsPerMetre;
        pg[2] = (-syw*pl[0] + cy*pl[2]) * g_unitsPerMetre;
        dg[0] =  cy*d[0] + syw*d[2];
        dg[1] =  d[1];
        dg[2] = -syw*d[0] + cy*d[2];

        if (!rayVsScene(pg, dg, &t))
            t = (float)g_cfgReach * g_unitsPerMetre;    /* nothing hit */
        {
            float hg[3] = { pg[0]+dg[0]*t, pg[1]+dg[1]*t, pg[2]+dg[2]*t };
            float zv = -hg[2];
            if (zv > .001f*g_unitsPerMetre) {
                sx = g_gcx + hg[0] * g_gfx / zv;
                sy = g_gcy - hg[1] * g_gfy / zv;
            }
            {
                float s = 1.0f / g_unitsPerMetre;
                float gx = hg[0]*s, gy = hg[1]*s, gz = hg[2]*s;
                float rc = cosf(yaw0), rs = sinf(yaw0);
                hitLocal[0] = ox + rc*gx + rs*gz;
                hitLocal[1] = oy + gy;
                hitLocal[2] = oz - rs*gx + rc*gz;
                haveHit = 1;
            }
        }
    }
    *sxOut = sx; *syOut = sy;
    return haveHit;
}

/*
 * One tick of controller handling. Returns 1 if the player asked to recentre.
 *
 * The aim ray starts in local (metre) space. If the floating screen is up and
 * the ray lands on it, that intersection maps straight to 640x480 - native
 * screen aiming follows it. Otherwise the ray is moved into game space
 * (the exact inverse of the model matrix runVR builds: undo the recentre
 * translation, undo the yaw, multiply the scale back out), cast against the
 * frame's triangles, and the hit is projected through the game's own camera -
 * which is precisely what a light gun does.
 */
/* one short buzz on a hand: recoil */
static void buzz(XrSession session, int h, float seconds, float amp)
{
    XrHapticActionInfo hi;
    XrHapticVibration v;
    if (!g_cfgHaptic || !pxrApplyHapticFeedback) return;
    ZeroMemory(&hi, sizeof(hi)); ZeroMemory(&v, sizeof(v));
    hi.type = XR_TYPE_HAPTIC_ACTION_INFO;
    hi.action = g_actHaptic;
    hi.subactionPath = g_handPath[h];
    v.type = XR_TYPE_HAPTIC_VIBRATION;
    v.duration = (XrDuration)(seconds * 1e9);
    v.frequency = XR_FREQUENCY_UNSPECIFIED;
    v.amplitude = amp;
    pxrApplyHapticFeedback(session, &hi, (const XrHapticBaseHeader *)&v);
}

static int headRelativeDirection(const XrQuaternionf *head,const float *direction,float *relative)
{
    float length=head->x*head->x+head->y*head->y+head->z*head->z+head->w*head->w;
    if(!isfinite(length) || length<.000001f)return 0;
    length=sqrtf(length);
    XrQuaternionf inverse={-head->x/length,-head->y/length,-head->z/length,head->w/length};
    quatRotate(&inverse,direction,relative);
    return isfinite(relative[0]) && isfinite(relative[1]) && isfinite(relative[2]);
}

static int handleControllers(XrSession session, XrSpace space, XrTime when,
                             float yaw0, float ox, float oy, float oz,const XrQuaternionf *head)
{
    XrActionsSyncInfo si;
    XrActiveActionSet act;
    XrActionStateGetInfo gi;
    XrActionStateBoolean fire[2], bpause[2], bback[2], breload[2], bcentre,bsettings;
    XrSpaceLocation loc;
    XrSpaceLocation poses[2];
    int poseValid[2];
    unsigned triggerActions[2];
    static HotdTrigger triggers[2];
    static int prevCentre;
    static unsigned menuHeld[2],menuHorizontal[2];
    static unsigned prevButtons, prev2;
    unsigned starts;
    int recentre = 0, h, hand;
    unsigned buttons = 0;
    float p[3], d[3], fwd[3] = { 0, 0, -1 };
    float hitLocal[3];
    int haveHit = 0;
    float sx = 320.0f, sy = 240.0f;

    if (g_resetControls) {
        prevCentre = 0;
        menuHeld[0] = menuHeld[1] = 0;
        menuHorizontal[0] = menuHorizontal[1] = 0;
        prevButtons = prev2 = 0;
        memset(triggers,0,sizeof(triggers));
        memset(g_shotFeedback,0,sizeof(g_shotFeedback));
        g_resetControls = 0;
    }

    ZeroMemory(&act, sizeof(act));
    act.actionSet = g_actSet;
    act.subactionPath = XR_NULL_PATH;
    ZeroMemory(&si, sizeof(si));
    si.type = XR_TYPE_ACTIONS_SYNC_INFO;
    si.countActiveActionSets = 1;
    si.activeActionSets = &act;
    if (pxrSyncActions(session, &si) != XR_SUCCESS) {
        for(h=0;h<2;h++)hotd_shot_feedback(g_shotFeedback+h,g_shotEvents[1-h],0,GetTickCount());
        triggers[0].action=triggers[1].action=0;
        menuHeld[0]=menuHeld[1]=0;
        menuHorizontal[0]=menuHorizontal[1]=0;
        return 0;
    }

    gameProjRead();     /* the projection the game is using THIS frame */

#define GETB(out, act_, hpath) do {                                       \
        ZeroMemory(&gi, sizeof(gi)); ZeroMemory(&(out), sizeof(out));     \
        gi.type = XR_TYPE_ACTION_STATE_GET_INFO;                          \
        (out).type = XR_TYPE_ACTION_STATE_BOOLEAN;                        \
        gi.action = (act_); gi.subactionPath = (hpath);                   \
        pxrGetActionStateBoolean(session, &gi, &(out));                   \
    } while (0)

    GETB(fire[0], g_actFire, g_handPath[0]);
    GETB(fire[1], g_actFire, g_handPath[1]);
    GETB(bpause[0], g_actPause, g_handPath[0]);
    GETB(bpause[1], g_actPause, g_handPath[1]);
    GETB(bback[0], g_actBack, g_handPath[0]);
    GETB(bback[1], g_actBack, g_handPath[1]);
    GETB(breload[0], g_actReload, g_handPath[0]);
    GETB(breload[1], g_actReload, g_handPath[1]);
    GETB(bcentre, g_actRecentre, g_handPath[1]);
    GETB(bsettings, g_actSettings, g_handPath[0]);
#undef GETB
    starts=(fire[0].isActive && fire[0].currentState ? 1u:0u) |
        (fire[1].isActive && fire[1].currentState ? 2u:0u);
    g_frame->akimbo=1; /* Both native player input blocks are available in either mode. */
    for(h=0;h<2;h++) {
        float direction[3]={0},relative[3]={0};
        XrSpaceLocationFlags required=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        memset(poses+h,0,sizeof(poses[h]));poses[h].type=XR_TYPE_SPACE_LOCATION;
        poseValid[h]=XR_SUCCEEDED(pxrLocateSpace(g_aimSpace[h],space,when,poses+h)) &&
            (poses[h].locationFlags&required)==required;
        if(poseValid[h])quatRotate(&poses[h].pose.orientation,fwd,direction);
        int reloadValid=poseValid[h] && headRelativeDirection(head,direction,relative);
        triggerActions[h]=hotd_trigger_action(triggers+h,fire[h].isActive && fire[h].currentState,
            reloadValid && hotd_hand_visible(g_cfgAkimbo,g_activePlayers,h),relative[1],atan2f(relative[0],-relative[2]));
    }
    for(h=0;h<2;h++) {
        XrActionStateFloat stick={XR_TYPE_ACTION_STATE_FLOAT};
        gi.type=XR_TYPE_ACTION_STATE_GET_INFO;gi.action=g_actMenu;gi.subactionPath=g_handPath[h];
        int active=pxrGetActionStateFloat(session,&gi,&stick)==XR_SUCCESS && stick.isActive;
        menuHeld[h]=hotd_menu_stick(menuHeld[h],active,stick.currentState);
        buttons|=menuHeld[h];
        gi.action=g_actMenuX;
        memset(&stick,0,sizeof(stick));stick.type=XR_TYPE_ACTION_STATE_FLOAT;
        active=pxrGetActionStateFloat(session,&gi,&stick)==XR_SUCCESS && stick.isActive;
        menuHorizontal[h]=hotd_menu_stick(menuHorizontal[h],active,stick.currentState);
        if(menuHorizontal[h]==VC2_BTN_UP)buttons|=VC2_BTN_RIGHT;
        if(menuHorizontal[h]==VC2_BTN_DOWN)buttons|=VC2_BTN_LEFT;
    }
    if(g_vrMenuEnabled) {
        unsigned input=buttons;
        if(bsettings.isActive && bsettings.currentState)input|=MENU_TOGGLE;
        for(h=0;h<2;h++) {
            if(fire[h].isActive && fire[h].currentState)input|=VC2_BTN_START;
            if(bback[h].isActive && bback[h].currentState)input|=VC2_BTN_BACK;
            if(bpause[h].isActive && bpause[h].currentState)input|=VC2_BTN_PAUSE;
        }
        int owned=vrMenuControl(input);
        if(owned) {
            g_gunPose=poses[1].pose;g_gun2Pose=poses[0].pose;
            g_gunValid=poseValid[1];g_gun2Valid=poseValid[0];g_ovCount=0;
            unsigned first=bpause[1].isActive && bpause[1].currentState?VC2_BTN_START|VC2_BTN_PAUSE:0;
            unsigned second=bpause[0].isActive && bpause[0].currentState?VC2_BTN_START|VC2_BTN_PAUSE:0;
            if(g_cfgAkimbo && (first || second))first|=VC2_BTN_PAIR_START;
            pushAim(320,240,first);pushAim2(320,240,second);
            int centre=bcentre.isActive && bcentre.currentState;
            recentre=centre && !prevCentre;prevCentre=centre;
            return recentre;
        }
    }
    {
        float targetZoom=1.0f;
        /* In the flat game the zoom moments were authored: the engine walks
         * its fov from 60 down to ~18 degrees exactly when a distant enemy is
         * meant to fill the screen, and the player just keeps shooting. Here
         * the reconstruction stays true-scale through that (correct for the
         * world, useless for the fight), so follow the game instead: magnify
         * by the same ratio the game is applying, capped by the zoom key.
         * The player aims, the scope comes to them. */
        if (g_cfgAutoZoom && g_projLive && g_gfxBase > 300.0f &&
            !(g_scrVisible && g_scrHave)) {
            float az  = g_gfx / g_gfxBase;
            float on  = 1.0f + (float)g_cfgAutoAt / 100.0f;
            float off = 1.0f + (float)g_cfgAutoAt / 250.0f;
            float cap = (float)g_cfgZoomX / 100.0f;
            /* a deadband, so a percent of drift in the baseline is not taken
             * for a cinematic zoom: the sight turns up only when the game has
             * really narrowed its camera, and leaves again when it opens up */
            if (az > on) g_autoOn = 1;
            else if (az < off) g_autoOn = 0;
            if (g_autoOn) {
                if (az > cap) az = cap;
                if (az > targetZoom) targetZoom = az;
            }
        } else {
            g_autoOn = 0;
        }
        g_zoomLevel += (targetZoom - g_zoomLevel) * ((float)g_cfgZoomSpd / 100.0f);
        if (g_zoomLevel < 1.001f) g_zoomLevel = 1.0f;
    }

    /* Right always drives P1; left always drives P2. */
    hand = 1;
    for (h = 0; h < 2; h++)
        if (hotd_shot_feedback(g_shotFeedback+h,g_shotEvents[1-h],
            g_cfgHaptic && !g_nativePaused && poseValid[h] && hotd_hand_visible(g_cfgAkimbo,g_activePlayers,h),GetTickCount()))
            buzz(session, h, 0.03f, 1.0f);

    if (bcentre.isActive && bcentre.currentState && !prevCentre) recentre = 1;
    prevCentre = bcentre.isActive && bcentre.currentState;

    /* The bridge mirrors either player's Start to both native keys in akimbo. */
    if(g_cfgAkimbo)buttons|=VC2_BTN_PAIR_START;
    if(starts&(1u<<hand))buttons|=VC2_BTN_START;
    if(bpause[hand].isActive && bpause[hand].currentState)buttons|=VC2_BTN_START|VC2_BTN_PAUSE;
    if(bback[hand].isActive && bback[hand].currentState)buttons|=VC2_BTN_BACK;
    if(breload[hand].isActive && breload[hand].currentState)buttons|=VC2_BTN_RELOAD;
    /* where is the gun */
    loc=poses[hand];
    g_gunValid=poseValid[hand] && hotd_hand_visible(g_cfgAkimbo,g_activePlayers,hand);
    if(g_gunValid) {
        g_gunPose=loc.pose;
        p[0]=loc.pose.position.x;p[1]=loc.pose.position.y;p[2]=loc.pose.position.z;
        quatRotate(&loc.pose.orientation,fwd,d);
        haveHit=aimFromPose(p,d,yaw0,ox,oy,oz,&sx,&sy,hitLocal);
        buttons|=triggerActions[hand];
    }
    if((buttons&VC2_BTN_FIRE) && !(prevButtons&VC2_BTN_FIRE))g_gunFireAt=GetTickCount();
    if((buttons&VC2_BTN_RELOAD) && !(prevButtons&VC2_BTN_RELOAD))g_gunReloadAt=GetTickCount();
    prevButtons=buttons;
    pushAim(sx, sy, buttons);

    /* Left keeps native P2 input independently of automatic paired joining. */
    {
        int oh = 1 - hand;              /* the hand that is not the primary gun */
        XrSpaceLocation loc2;
        unsigned buttons2 = 0;
        if(bback[oh].isActive && bback[oh].currentState)buttons2|=VC2_BTN_BACK;
        if(starts&(1u<<oh))buttons2|=VC2_BTN_START;
        if(bpause[oh].isActive && bpause[oh].currentState)buttons2|=VC2_BTN_START|VC2_BTN_PAUSE;
        if(breload[oh].isActive && breload[oh].currentState)buttons2|=VC2_BTN_RELOAD;
        loc2=poses[oh];
        if (poseValid[oh] && hotd_hand_visible(g_cfgAkimbo,g_activePlayers,oh)) {
            float p2[3], d2[3], hit2[3], sx2 = 320.0f, sy2 = 240.0f;
            p2[0] = loc2.pose.position.x;
            p2[1] = loc2.pose.position.y;
            p2[2] = loc2.pose.position.z;
            quatRotate(&loc2.pose.orientation, fwd, d2);
            buttons2|=triggerActions[oh];
            aimFromPose(p2, d2, yaw0, ox, oy, oz, &sx2, &sy2, hit2);
            pushAim2(sx2, sy2, buttons2);
            g_gun2Pose = loc2.pose; g_gun2Valid = 1;
            g_gun2Hit[0] = hit2[0]; g_gun2Hit[1] = hit2[1]; g_gun2Hit[2] = hit2[2];
            g_gun2Origin[0] = p2[0]; g_gun2Origin[1] = p2[1]; g_gun2Origin[2] = p2[2];
            g_gun2Fire = buttons2;
            {
                if ((buttons2 & VC2_BTN_FIRE)  && !(prev2 & VC2_BTN_FIRE))  g_gun2FireAt   = GetTickCount();
                if ((buttons2 & VC2_BTN_RELOAD) && !(prev2 & VC2_BTN_RELOAD)) g_gun2ReloadAt = GetTickCount();
                prev2 = buttons2;
            }
        } else {
            g_gun2Valid = 0;
            g_gun2Fire=0;prev2=buttons2;
            pushAim2(320,240,buttons2);
        }
    }

    /* the sight: beam, ring and hot core. alpha 0x80 only so the shader's
     * cutout clip lets it through - the blend is additive, the rgb is the
     * brightness. 0xAABBGGRR. */
    g_ovCount = 0;
    if(g_gunValid) {
        int fired = (buttons & VC2_BTN_FIRE) != 0;
        ovSight(p, hitLocal,
                fired ? 0x8070D0FFu : 0x802828FFu,    /* beam  */
                fired ? 0x8090E0FFu : 0x804040FFu,    /* ring  */
                fired ? 0x80E0F0FFu : 0x80A0A0FFu);   /* core  */
    }
    if (g_gun2Valid) {   /* second gun, amber, to tell them apart */
        int fired = (g_gun2Fire & VC2_BTN_FIRE) != 0;
        ovSight(g_gun2Origin, g_gun2Hit,
                fired ? 0x8040D0FFu : 0x8010A0FFu,
                fired ? 0x8060E0FFu : 0x8020C0FFu,
                fired ? 0x80D0F0FFu : 0x8090E0FFu);
    }
    return recentre;
}

typedef struct {
    XrSwapchain sc;
    int w, h;
    uint32_t imgCount;
    XrSwapchainImageD3D11KHR *imgs;
    ID3D11RenderTargetView **rtv;   /* views on the runtime's images (resolve targets) */
    ID3D11DepthStencilView *dsv;
    /* with msaa > 1 everything is drawn into msTex and resolved into the
     * runtime's image afterwards; with msaa == 1 msTex stays NULL and the
     * runtime's image is rendered into directly, as before */
    ID3D11Texture2D        *msTex;
    ID3D11RenderTargetView *msRtv;
} Eye;

/* ask the device what it can do; drop to the next level down if refused */
static int pickMsaa(DXGI_FORMAT fmt, int want)
{
    UINT q = 0;
    while (want > 1) {
        if (SUCCEEDED(ID3D11Device_CheckMultisampleQualityLevels(g_dev, fmt,
                      (UINT)want, &q)) && q > 0 &&
            SUCCEEDED(ID3D11Device_CheckMultisampleQualityLevels(g_dev,
                      DXGI_FORMAT_D32_FLOAT, (UINT)want, &q)) && q > 0)
            return want;
        want /= 2;
    }
    return 1;
}

static void makeEyeTargets(Eye *e, DXGI_FORMAT fmt, int msaa)
{
    D3D11_TEXTURE2D_DESC td;
    ID3D11Texture2D *depth = NULL;
    D3D11_RENDER_TARGET_VIEW_DESC rd;
    uint32_t i;

    e->rtv = (ID3D11RenderTargetView **)calloc(e->imgCount, sizeof(void *));
    ZeroMemory(&rd, sizeof(rd));
    rd.Format = fmt;
    rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    for (i = 0; i < e->imgCount; i++)
        if (FAILED(ID3D11Device_CreateRenderTargetView(g_dev,
                   (ID3D11Resource *)e->imgs[i].texture, &rd, &e->rtv[i])))
            die("render target view for a swapchain image");

    ZeroMemory(&td, sizeof(td));
    td.Width = e->w; td.Height = e->h;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_D32_FLOAT;
    td.SampleDesc.Count = (UINT)msaa;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &depth))) die("depth texture");
    ID3D11Device_CreateDepthStencilView(g_dev, (ID3D11Resource *)depth, NULL, &e->dsv);
    ID3D11Texture2D_Release(depth);

    if (msaa > 1) {
        ZeroMemory(&td, sizeof(td));
        td.Width = e->w; td.Height = e->h;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = fmt;
        td.SampleDesc.Count = (UINT)msaa;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &e->msTex)))
            die("msaa colour target");
        if (FAILED(ID3D11Device_CreateRenderTargetView(g_dev,
                   (ID3D11Resource *)e->msTex, NULL, &e->msRtv)))
            die("msaa render target view");
    }
}

static int runVR(void)
{
    HMODULE loader;
    XrInstance inst = XR_NULL_HANDLE;
    XrSystemId sys = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    XrInstanceCreateInfo ici;
    XrSystemGetInfo sgi;
    XrGraphicsRequirementsD3D11KHR req;
    XrGraphicsBindingD3D11KHR bind;
    XrSessionCreateInfo sci;
    XrReferenceSpaceCreateInfo rsci;
    XrViewConfigurationView vcv[2];
    PFN_xrGetD3D11GraphicsRequirementsKHR pGetReq = NULL;
    const char *exts[1] = { XR_KHR_D3D11_ENABLE_EXTENSION_NAME };
    uint32_t n = 0, i;
    Eye eye[2];
    IDXGIFactory1 *fac = NULL;
    IDXGIAdapter1 *ad = NULL, *pick = NULL;
    int64_t *fmts = NULL;
    DXGI_FORMAT chosen = DXGI_FORMAT_R8G8B8A8_UNORM;
    int running = 0, haveOrigin = 0, gameEnded = 0;
    XrQuaternionf smooth[2] = { 0 };
    int haveSmooth = 0;
    int eyeMsaa[2] = { 1, 1 };
    float originYaw = 0.0f, ox = 0, oy = 0, oz = 0;
    D3D_FEATURE_LEVEL fl;

    loader = LoadLibraryA("openxr_loader.dll");
    if (!loader)
        die("openxr_loader.dll not found. Put it next to HOTD2VR.exe - it comes from\n"
            "       the Khronos OpenXR SDK release, github.com/KhronosGroup/OpenXR-SDK/releases\n"
            "       (the 64-bit one, from x64/bin inside the zip)");
    xrGIPA = (PFN_xrGetInstanceProcAddr)GetProcAddress(loader, "xrGetInstanceProcAddr");
    if (!xrGIPA) die("openxr_loader.dll has no xrGetInstanceProcAddr - wrong file?");
    if (XR_FAILED(xrGIPA(XR_NULL_HANDLE, "xrCreateInstance",
                         (PFN_xrVoidFunction *)&pxrCreateInstance)))
        die("the loader would not hand over xrCreateInstance");
    xrGIPA(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties",
           (PFN_xrVoidFunction *)&pxrEnumerateInstanceExtensionProperties);

    /* say what the runtime offers before asking it for anything */
    if (pxrEnumerateInstanceExtensionProperties) {
        uint32_t cnt = 0, k;
        XrExtensionProperties *ep;
        int haveD3D11 = 0;
        pxrEnumerateInstanceExtensionProperties(NULL, 0, &cnt, NULL);
        note("runtime offers %u extensions\n", cnt);
        if (cnt) {
            ep = (XrExtensionProperties *)calloc(cnt, sizeof(XrExtensionProperties));
            for (k = 0; k < cnt; k++) ep[k].type = XR_TYPE_EXTENSION_PROPERTIES;
            pxrEnumerateInstanceExtensionProperties(NULL, cnt, &cnt, ep);
            for (k = 0; k < cnt; k++)
                if (!lstrcmpA(ep[k].extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME))
                    haveD3D11 = 1;
            free(ep);
        }
        if (!haveD3D11)
            note("  warning: this runtime does not list %s\n",
                 XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
    }

    /* Ask for 1.0 first. SteamVR implements OpenXR 1.0 and refuses a 1.1
     * request outright, which is what the earlier build ran into. */
    {
        XrVersion tries[2];
        XrResult res = XR_ERROR_RUNTIME_FAILURE;
        int k;
        tries[0] = XR_MAKE_VERSION(1, 0, 0);
        tries[1] = XR_CURRENT_API_VERSION;
        for (k = 0; k < 2; k++) {
            ZeroMemory(&ici, sizeof(ici));
            ici.type = XR_TYPE_INSTANCE_CREATE_INFO;
            ici.applicationInfo.apiVersion = tries[k];
            lstrcpyA(ici.applicationInfo.applicationName, "HOTD2 PC2VR");
            ici.applicationInfo.applicationVersion = 1;
            lstrcpyA(ici.applicationInfo.engineName, "hotd2-pc2vr");
            ici.enabledExtensionCount = 1;
            ici.enabledExtensionNames = exts;
            res = pxrCreateInstance(&ici, &inst);
            note("xrCreateInstance with api %u.%u -> %d %s\n",
                 (unsigned)XR_VERSION_MAJOR(tries[k]),
                 (unsigned)XR_VERSION_MINOR(tries[k]), (int)res, xrName(res));
            if (XR_SUCCEEDED(res)) break;
        }
        if (XR_FAILED(res))
            die("OpenXR runtime refused to start. Connect the headset and start its VR runtime.");
    }
    note("openxr instance ok\n");
    loadXr(inst);
    xrGIPA(inst, "xrGetD3D11GraphicsRequirementsKHR", (PFN_xrVoidFunction *)&pGetReq);
    if (!pGetReq) die("the runtime does not offer D3D11 support");

    ZeroMemory(&sgi, sizeof(sgi));
    sgi.type = XR_TYPE_SYSTEM_GET_INFO;
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    {
        XrResult res = pxrGetSystem(inst, &sgi, &sys);
        if (XR_FAILED(res)) {
            note("xrGetSystem -> %d %s\n", (int)res, xrName(res));
            die("No headset available from the selected OpenXR runtime.");
        }
    }
    note("headset found\n");

    ZeroMemory(&req, sizeof(req));
    req.type = XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR;
    pGetReq(inst, sys, &req);

    /* the runtime dictates which GPU to render on */
    if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&fac))) die("dxgi factory");
    for (i = 0; IDXGIFactory1_EnumAdapters1(fac, i, &ad) == S_OK; i++) {
        DXGI_ADAPTER_DESC1 d;
        IDXGIAdapter1_GetDesc1(ad, &d);
        if (d.AdapterLuid.LowPart == req.adapterLuid.LowPart &&
            d.AdapterLuid.HighPart == req.adapterLuid.HighPart) { pick = ad; break; }
        IDXGIAdapter1_Release(ad);
    }
    if (!pick) die("the GPU the runtime asked for is not present");
    if (FAILED(D3D11CreateDevice((IDXGIAdapter *)pick, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0,
              &req.minFeatureLevel, 1, D3D11_SDK_VERSION, &g_dev, &fl, &g_ctx)))
        die("could not create a D3D11 device on that GPU");
    g_gammaOut = g_cfgGamma > 0 ? (float)g_cfgGamma / 100.0f : 1.0f;
    makePipeline();
    makeScopePipeline();
    gunLoad();
    note("d3d11 ok\n");

    ZeroMemory(&bind, sizeof(bind));
    bind.type = XR_TYPE_GRAPHICS_BINDING_D3D11_KHR;
    bind.device = g_dev;
    ZeroMemory(&sci, sizeof(sci));
    sci.type = XR_TYPE_SESSION_CREATE_INFO;
    sci.next = &bind;
    sci.systemId = sys;
    if (XR_FAILED(pxrCreateSession(inst, &sci, &session))) die("could not create the session");

    ZeroMemory(&rsci, sizeof(rsci));
    rsci.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rsci.poseInReferenceSpace.orientation.w = 1.0f;
    if (XR_FAILED(pxrCreateReferenceSpace(session, &rsci, &space))) die("reference space");

    initActions(inst, session);

    for (i = 0; i < 2; i++) { ZeroMemory(&vcv[i], sizeof(vcv[i])); vcv[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW; }
    n = 2;
    pxrEnumerateViewConfigurationViews(inst, sys,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &n, vcv);
    note("eye buffers %ux%u\n", vcv[0].recommendedImageRectWidth,
         vcv[0].recommendedImageRectHeight);

    /* pick a colour format the runtime actually offers */
    n = 0;
    pxrEnumerateSwapchainFormats(session, 0, &n, NULL);
    fmts = (int64_t *)calloc(n ? n : 1, sizeof(int64_t));
    pxrEnumerateSwapchainFormats(session, n, &n, fmts);
    for (i = 0; i < n; i++)
        if (fmts[i] == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
            fmts[i] == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) { chosen = (DXGI_FORMAT)fmts[i]; break; }
    if (i == n)
        for (i = 0; i < n; i++)
            if (fmts[i] == DXGI_FORMAT_R8G8B8A8_UNORM) { chosen = (DXGI_FORMAT)fmts[i]; break; }
    note("eye format %s, gamma %d%%\n",
         chosen == DXGI_FORMAT_R8G8B8A8_UNORM ? "unorm" : "srgb", g_cfgGamma);

    {
        int msaa = pickMsaa(chosen, g_cfgMsaa);
        note("msaa x%d, eye buffer scale %d%%\n", msaa, g_cfgSuper);
        for (i = 0; i < 2; i++) eyeMsaa[i] = msaa;
    }
    for (i = 0; i < 2; i++) {
        XrSwapchainCreateInfo ci;
        uint32_t k, cnt = 0, sw, sh;
        ZeroMemory(&ci, sizeof(ci));
        ci.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
        ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        ci.format = chosen;
        ci.sampleCount = 1;
        sw = vcv[i].recommendedImageRectWidth  * (uint32_t)g_cfgSuper / 100u;
        sh = vcv[i].recommendedImageRectHeight * (uint32_t)g_cfgSuper / 100u;
        if (vcv[i].maxImageRectWidth  && sw > vcv[i].maxImageRectWidth)  sw = vcv[i].maxImageRectWidth;
        if (vcv[i].maxImageRectHeight && sh > vcv[i].maxImageRectHeight) sh = vcv[i].maxImageRectHeight;
        ci.width = sw;
        ci.height = sh;
        ci.faceCount = 1; ci.arraySize = 1; ci.mipCount = 1;
        ZeroMemory(&eye[i], sizeof(eye[i]));
        XrResult created=pxrCreateSwapchain(session,&ci,&eye[i].sc);
        if(XR_FAILED(created))die("swapchain");
        eye[i].w = ci.width; eye[i].h = ci.height;
        pxrEnumerateSwapchainImages(eye[i].sc, 0, &cnt, NULL);
        eye[i].imgCount = cnt;
        eye[i].imgs = (XrSwapchainImageD3D11KHR *)calloc(cnt, sizeof(XrSwapchainImageD3D11KHR));
        for (k = 0; k < cnt; k++) eye[i].imgs[k].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
        pxrEnumerateSwapchainImages(eye[i].sc, cnt, &cnt,
                                    (XrSwapchainImageBaseHeader *)eye[i].imgs);
        makeEyeTargets(&eye[i], chosen, eyeMsaa[i]);
    }
    note("session ready - put the headset on; initial centre waits for focused tracking\n");

    for (;;) {
        XrEventDataBuffer ev;
        XrFrameWaitInfo fwi = { XR_TYPE_FRAME_WAIT_INFO };
        XrFrameState fs = { XR_TYPE_FRAME_STATE };
        XrFrameBeginInfo fbi = { XR_TYPE_FRAME_BEGIN_INFO };
        XrFrameEndInfo fei = { XR_TYPE_FRAME_END_INFO };
        XrViewLocateInfo vli = { XR_TYPE_VIEW_LOCATE_INFO };
        XrViewState vs = { XR_TYPE_VIEW_STATE };
        XrView views[2];
        XrCompositionLayerProjectionView pv[2];
        XrCompositionLayerProjection layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        const XrCompositionLayerBaseHeader *layers[1];
        uint32_t got = 0;

        if (gameExited()) { gameEnded = 1; goto cleanup; }

        for (;;) {
            ZeroMemory(&ev, sizeof(ev));
            ev.type = XR_TYPE_EVENT_DATA_BUFFER;
            if (pxrPollEvent(inst, &ev) != XR_SUCCESS) break;
            if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                XrEventDataSessionStateChanged *s = (XrEventDataSessionStateChanged *)&ev;
                state = s->state;
                note("OpenXR session state=%d\n",(int)state);
                if (state == XR_SESSION_STATE_READY) {
                    XrSessionBeginInfo bi = { XR_TYPE_SESSION_BEGIN_INFO };
                    bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    pxrBeginSession(session, &bi);
                    running = 1;
                    note("session running\n");
                } else if (state == XR_SESSION_STATE_STOPPING) {
                    pxrEndSession(session);
                    running = 0;
                } else if (state == XR_SESSION_STATE_EXITING ||
                           state == XR_SESSION_STATE_LOSS_PENDING) {
                    note("session ended\n");
                    goto cleanup;
                }
            }
        }
        if (!running) { g_shotFeedback[0].ready=g_shotFeedback[1].ready=0;Sleep(20);continue; }

        pxrWaitFrame(session, &fwi, &fs);
        pxrBeginFrame(session, &fbi);
        pullFrame();
        pullScreen();

        /* the screen shows itself whenever the 3D scene is nearly empty -
         * that is the menus, the scores, the name entry, the attract mode */
        g_scrVisible = screenVisible();

        for (i = 0; i < 2; i++) { ZeroMemory(&views[i], sizeof(views[i])); views[i].type = XR_TYPE_VIEW; }
        vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        vli.displayTime = fs.predictedDisplayTime;
        vli.space = space;
        XrResult located=pxrLocateViews(session, &vli, &vs, 2, &got, views);

        if (fs.shouldRender && headPoseReady(haveOrigin,state,located,got,vs.viewStateFlags)) {
            float scale = 1.0f / g_unitsPerMetre;
            float model[16], t[16], rot[16], tmp[16];

            /* VDXR's bootstrap origin can change before the headset becomes focused. */
            if (!haveOrigin) {
                const XrQuaternionf *q = &views[0].pose.orientation;
                originYaw = atan2f(2.0f * (q->w * q->y + q->x * q->z),
                                   1.0f - 2.0f * (q->y * q->y + q->x * q->x));
                ox = views[0].pose.position.x;
                oy = views[0].pose.position.y;
                oz = views[0].pose.position.z;
                haveOrigin = 1;
                setScreenPose(ox, oy, oz, originYaw);
                note("recentred on tracked head pose: session=%d flags=%llx position=%g,%g,%g\n",
                    (int)state,(unsigned long long)vs.viewStateFlags,ox,oy,oz);
            }

            if (handleControllers(session, space, fs.predictedDisplayTime,
                                  originYaw, ox, oy, oz,&views[0].pose.orientation))
                haveOrigin = 0;                    /* right stick click: recentre next frame */

            mIdent(model);
            model[0] = model[5] = model[10] = scale;
            mIdent(rot);
            rot[0] = cosf(originYaw);  rot[2]  = sinf(originYaw);
            rot[8] = -sinf(originYaw); rot[10] = cosf(originYaw);
            mIdent(t);
            t[3] = ox; t[7] = oy; t[11] = oz;
            mMul(rot, model, tmp);
            mMul(t, tmp, model);

            uploadVerts();
            uploadOverlay();
            uploadScreenTex();
            uploadAtlasTex();
            for (i = 0; i < 2; i++) {
                uint32_t idx = 0;
                XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
                XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
                XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                float view[16], proj[16], mvp[16], vp[16];
                float projFlat[16], mvpFlat[16];

                pxrAcquireSwapchainImage(eye[i].sc, &ai, &idx);
                wi.timeout = XR_INFINITE_DURATION;
                pxrWaitSwapchainImage(eye[i].sc, &wi);

                {
                    /* While the whole view is magnified, every degree of head
                     * wobble is magnified with it - the reason this mode was
                     * dropped for a glass disc in the first place. Damp the
                     * ORIENTATION that feeds the view (position is left alone,
                     * so the room does not swim) and the wobble goes with it.
                     * At zoom 1 the filter is bypassed entirely. */
                    XrPosef pv = views[i].pose;
                    if (!g_cfgScope && g_zoomLevel > 1.001f && g_cfgZoomSmooth > 0) {
                        float k = (float)g_cfgZoomSmooth / 100.0f;
                        XrQuaternionf *sq = &smooth[i];
                        const XrQuaternionf *q = &views[i].pose.orientation;
                        float dot, len;
                        if (!haveSmooth) { smooth[0] = views[0].pose.orientation;
                                           smooth[1] = views[1].pose.orientation;
                                           haveSmooth = 1; }
                        dot = sq->x*q->x + sq->y*q->y + sq->z*q->z + sq->w*q->w;
                        if (dot < 0.0f) { sq->x = -sq->x; sq->y = -sq->y;
                                          sq->z = -sq->z; sq->w = -sq->w; }
                        sq->x += (q->x - sq->x) * (1.0f - k);
                        sq->y += (q->y - sq->y) * (1.0f - k);
                        sq->z += (q->z - sq->z) * (1.0f - k);
                        sq->w += (q->w - sq->w) * (1.0f - k);
                        len = sqrtf(sq->x*sq->x + sq->y*sq->y + sq->z*sq->z + sq->w*sq->w);
                        if (len > 1e-6f) { sq->x/=len; sq->y/=len; sq->z/=len; sq->w/=len; }
                        pv.orientation = *sq;
                    } else {
                        haveSmooth = 0;
                    }
                    mView(&pv, view);
                }
                {
                    /* Magnifying the whole view multiplies every degree of
                     * head wobble by the same factor, which is why this was
                     * abandoned for a glass disc. It does not have to be: damp
                     * the head pose that feeds the zoomed view and the wobble
                     * goes with it, while the world still stands still in the
                     * room. zoomsmooth is how much of it to remove. */
                    float z = g_cfgScope ? 1.0f : g_zoomLevel;  /* scope: main view stays 1x */
                    float aL = views[i].fov.angleLeft, aR = views[i].fov.angleRight;
                    float aU = views[i].fov.angleUp,   aD = views[i].fov.angleDown;
                    /* the unzoomed frustum is kept as well: the flat overlay
                     * goes through it so the HUD does not swell with the zoom */
                    mProjFov(aL, aR, aU, aD, NEAR_M, FAR_M, projFlat);
                    if (z > 1.001f) {                   /* narrow the frustum = magnify */
                        aL = atanf(tanf(aL) / z); aR = atanf(tanf(aR) / z);
                        aU = atanf(tanf(aU) / z); aD = atanf(tanf(aD) / z);
                    }
                    mProjFov(aL, aR, aU, aD, NEAR_M, FAR_M, proj);
                }
                mMul(proj, view, vp);              /* UI lives in local space */
                mMul(view, model, mvp);            /* model-view */
                mMul(projFlat, mvp, mvpFlat);      /* unzoomed: the overlay */
                mMul(proj, mvp, mvp);              /* zoomed: the world      */
                {
                    ID3D11RenderTargetView *target =
                        eye[i].msRtv ? eye[i].msRtv : eye[i].rtv[idx];
                    /* one magnified pass per eye, FROM that eye, so the glass
                     * carries real stereo and the eyes converge on the target
                     * rather than on the glass */
                    drawScopePass(&views[i].pose, &views[i].fov, model,
                                  eye[i].w, eye[i].h, g_zoomLevel);
                    drawWith(target, eye[i].dsv, eye[i].w, eye[i].h,
                             mvp, mvpFlat);
                    drawUI(vp);
                    drawScopeDisc(vp);
                    if (eye[i].msRtv)
                        ID3D11DeviceContext_ResolveSubresource(g_ctx,
                            (ID3D11Resource *)eye[i].imgs[idx].texture, 0,
                            (ID3D11Resource *)eye[i].msTex, 0, chosen);
                }
                pxrReleaseSwapchainImage(eye[i].sc, &ri);

                ZeroMemory(&pv[i], sizeof(pv[i]));
                pv[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                pv[i].pose = views[i].pose;
                pv[i].fov = views[i].fov;
                pv[i].subImage.swapchain = eye[i].sc;
                pv[i].subImage.imageRect.extent.width = eye[i].w;
                pv[i].subImage.imageRect.extent.height = eye[i].h;
            }
            layer.space = space;
            layer.viewCount = 2;
            layer.views = pv;
            layers[0] = (const XrCompositionLayerBaseHeader *)&layer;
            fei.layerCount = 1;
            fei.layers = layers;
        } else g_shotFeedback[0].ready=g_shotFeedback[1].ready=0;
        fei.type = XR_TYPE_FRAME_END_INFO;
        fei.displayTime = fs.predictedDisplayTime;
        fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        pxrEndFrame(session, &fei);
        gameFocus();                    /* we are live; the game can have it back */
    }
cleanup:
    if (running && state != XR_SESSION_STATE_EXITING &&
        state != XR_SESSION_STATE_LOSS_PENDING)
        pxrEndSession(session);
    ID3D11DeviceContext_ClearState(g_ctx);
    for (i = 0; i < 2; i++) {
        uint32_t k;
        for (k = 0; k < eye[i].imgCount; k++)
            if (eye[i].rtv[k]) ID3D11RenderTargetView_Release(eye[i].rtv[k]);
        free(eye[i].rtv);
        if (eye[i].dsv) ID3D11DepthStencilView_Release(eye[i].dsv);
        if (eye[i].msRtv) ID3D11RenderTargetView_Release(eye[i].msRtv);
        if (eye[i].msTex) ID3D11Texture2D_Release(eye[i].msTex);
        free(eye[i].imgs);
        if (eye[i].sc) pxrDestroySwapchain(eye[i].sc);
        if (g_aimSpace[i]) pxrDestroySpace(g_aimSpace[i]);
        g_aimSpace[i] = XR_NULL_HANDLE;
    }
    if (space) pxrDestroySpace(space);
    if (g_actSet) pxrDestroyActionSet(g_actSet);
    g_actSet = XR_NULL_HANDLE;
    if (session) pxrDestroySession(session);
    if (inst) pxrDestroyInstance(inst);
    free(fmts);
    if (pick) IDXGIAdapter1_Release(pick);
    if (fac) IDXGIFactory1_Release(fac);
    FreeLibrary(loader);
    releaseGraphics();
    return gameEnded;
}

/* ------------------------------------------------------- plain window mode */

static float w_yaw, w_pitch, w_x, w_y, w_z, w_speed = 2.0f;
static int   w_zoomOn;                  /* [Z] in window mode: scope on/off */
static int   g_recentre;
static BOOL  w_drag;
static POINT w_from;

static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_LBUTTONDOWN: w_drag = TRUE; w_from.x = LOWORD(l); w_from.y = HIWORD(l); SetCapture(h); return 0;
    case WM_LBUTTONUP:   w_drag = FALSE; ReleaseCapture(); return 0;
    case WM_MOUSEMOVE:
        if (w_drag) {
            int x = (short)LOWORD(l), y = (short)HIWORD(l);
            w_yaw += (x - w_from.x) * 0.005f;
            w_pitch += (y - w_from.y) * 0.005f;
            w_from.x = x; w_from.y = y;
        }
        return 0;
    case WM_MOUSEWHEEL: w_speed *= ((short)HIWORD(w) > 0) ? 1.4f : 0.71f; return 0;
    case WM_KEYDOWN: {
        if(g_vrMenuEnabled && w==VK_F8) {
            if(l&(1L<<30))return 0;
            if(g_vrMenuOpen)vrMenuClose();else {g_vrMenuOpen=1;g_vrMenuPage=-1;g_vrMenuRow=0;}
            g_vrMenuDirty=1;g_vrMenuArmed=1;return 0;
        }
        if(g_vrMenuEnabled && g_vrMenuOpen)return 0;
        float cy = cosf(w_yaw), sy = sinf(w_yaw), s = w_speed;
        if (w == 'W') { w_x -= sy * s; w_z -= cy * s; }
        if (w == 'S') { w_x += sy * s; w_z += cy * s; }
        if (w == 'D') { w_x += cy * s; w_z -= sy * s; }
        if (w == 'A') { w_x -= cy * s; w_z += sy * s; }
        if (w == 'R') { w_yaw = w_pitch = w_x = w_y = w_z = 0.0f; g_recentre = 1; }
        if (w == 'Z') w_zoomOn = !w_zoomOn;
        if (w == 'M') g_scrMode = (g_scrMode + 1) % 3;
        if (w == VK_ESCAPE) PostQuitMessage(0);
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static void vrMenuKeyboard(HWND hwnd)
{
    if(!g_vrMenuEnabled)return;
    unsigned input=0;
    if(GetForegroundWindow()==hwnd) {
        const int keys[]={VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,VK_RETURN,VK_BACK,VK_ESCAPE};
        const unsigned actions[]={VC2_BTN_UP,VC2_BTN_DOWN,VC2_BTN_LEFT,VC2_BTN_RIGHT,VC2_BTN_START,VC2_BTN_BACK,VC2_BTN_PAUSE};
        for(unsigned k=0;k<7;k++)if(GetAsyncKeyState(keys[k])&0x8000)input|=actions[k];
    }
    int owned=vrMenuControl(input);
    if(owned){pushAim(320,240,0);pushAim2(320,240,0);}
}

static int runWindow(void)
{
    WNDCLASSA wc;
    HWND hwnd;
    DXGI_SWAP_CHAIN_DESC sd;
    IDXGISwapChain *swap = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    ID3D11DepthStencilView *dsv = NULL;
    ID3D11Texture2D *back = NULL, *depth = NULL;
    D3D11_TEXTURE2D_DESC td;
    D3D_FEATURE_LEVEL fl;
    MSG msg;
    int W = 1280, H = 720;
    int gameEnded = 0;

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = wp;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = "HOTD2VRWin";
    RegisterClassA(&wc);
    {
        RECT r = { 0, 0, W, H };        /* W x H of actual picture, not frame */
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        hwnd = CreateWindowExA(0, "HOTD2VRWin", "HOTD2VR - window mode", WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               r.right - r.left, r.bottom - r.top,
                               NULL, NULL, wc.hInstance, NULL);
    }
    /* SW_SHOW takes the foreground, which pauses the game behind us */
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = W; sd.BufferDesc.Height = H;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    if (FAILED(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
              NULL, 0, D3D11_SDK_VERSION, &sd, &swap, &g_dev, &fl, &g_ctx)))
        die("d3d11 device");
    makePipeline();
    makeScopePipeline();
    gunLoad();

    IDXGISwapChain_GetBuffer(swap, 0, &IID_ID3D11Texture2D, (void **)&back);
    ID3D11Device_CreateRenderTargetView(g_dev, (ID3D11Resource *)back, NULL, &rtv);
    ID3D11Texture2D_Release(back);
    ZeroMemory(&td, sizeof(td));
    td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_D32_FLOAT; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &depth);
    ID3D11Device_CreateDepthStencilView(g_dev, (ID3D11Resource *)depth, NULL, &dsv);
    ID3D11Texture2D_Release(depth);

    setScreenPose(0.0f, 0.0f, 0.0f, 0.0f);      /* screen 2 m ahead of origin */

    note("window mode - W A S D, drag to look, R to reset, Esc to quit\n");
    for (;;) {
        float view[16], proj[16], model[16], mvp[16], t[16], rx[16], ry[16], tmp[16];
        float vp2[16];
        float scale, aspect = (float)W / (float)H, fovY = 1.2f;
        char title[200];

        if (gameExited()) { gameEnded = 1; goto cleanup; }
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto cleanup;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        pullFrame();
        vrMenuKeyboard(hwnd);
        pullScreen();
        g_scrVisible = screenVisible();
        scale = 1.0f / g_unitsPerMetre;

        mIdent(model);
        model[0] = model[5] = model[10] = scale;

        mIdent(t); t[3] = -w_x; t[7] = -w_y; t[11] = -w_z;
        mIdent(ry);
        ry[0] = cosf(w_yaw); ry[2] = -sinf(w_yaw);
        ry[8] = sinf(w_yaw); ry[10] = cosf(w_yaw);
        mIdent(rx);
        rx[5] = cosf(w_pitch); rx[6] = sinf(w_pitch);
        rx[9] = -sinf(w_pitch); rx[10] = cosf(w_pitch);
        mMul(rx, ry, tmp);
        mMul(tmp, t, view);

        mProjFov(-atanf(aspect * tanf(fovY / 2)), atanf(aspect * tanf(fovY / 2)),
                 fovY / 2, -fovY / 2, NEAR_M, FAR_M, proj);

        mMul(proj, view, vp2);
        mMul(view, model, mvp);
        mMul(proj, mvp, mvp);

        uploadVerts();
        uploadScreenTex();
        uploadAtlasTex();
        {   /* [Z]: the same scope the headset gets, from the window's head pose */
            XrPosef head; XrFovf fov;
            float cy2 = cosf(w_yaw * 0.5f), sy2 = sinf(w_yaw * 0.5f);
            float cp2 = cosf(w_pitch * 0.5f), sp2 = sinf(w_pitch * 0.5f);
            float target = w_zoomOn ? (float)g_cfgZoomX / 100.0f : 1.0f;
            g_zoomLevel += (target - g_zoomLevel) * ((float)g_cfgZoomSpd / 100.0f);
            if (g_zoomLevel < 1.001f) g_zoomLevel = 1.0f;
            head.orientation.w = cy2 * cp2; head.orientation.x = cy2 * sp2;
            head.orientation.y = sy2 * cp2; head.orientation.z = -sy2 * sp2;
            head.position.x = w_x; head.position.y = w_y; head.position.z = w_z;
            fov.angleLeft = -atanf(aspect * tanf(fovY / 2)); fov.angleRight = -fov.angleLeft;
            fov.angleUp = fovY / 2; fov.angleDown = -fovY / 2;
            drawScopePass(&head, &fov, model, W, H, g_zoomLevel);
            /* no controller here: park the gun low-right in front of the head */
            {
                float r[16], off[3] = { 0.18f, -0.18f, -0.45f }, o[3];
                mFromQuat(&head.orientation, r);
                o[0] = r[0]*off[0] + r[1]*off[1] + r[2]*off[2];
                o[1] = r[4]*off[0] + r[5]*off[1] + r[6]*off[2];
                o[2] = r[8]*off[0] + r[9]*off[1] + r[10]*off[2];
                g_gunPose.orientation = head.orientation;
                g_gunPose.position.x = w_x + o[0];
                g_gunPose.position.y = w_y + o[1];
                g_gunPose.position.z = w_z + o[2];
                g_gunValid = 1;
            }
        }
        drawWith(rtv, dsv, W, H, mvp, NULL);
        drawUI(vp2);
        drawScopeDisc(vp2);
        IDXGISwapChain_Present(swap, 1, 0);

        wsprintfA(title, "HOTD2VR window - game frame %u - %u tris - %d units/m",
                  g_lastFrame, g_localCount / 3, (int)g_unitsPerMetre);
        SetWindowTextA(hwnd, title);
    }
cleanup:
    ID3D11DeviceContext_ClearState(g_ctx);
    ID3D11RenderTargetView_Release(rtv);
    ID3D11DepthStencilView_Release(dsv);
    IDXGISwapChain_Release(swap);
    DestroyWindow(hwnd);
    while (PeekMessageA(&msg, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) ;
    releaseGraphics();
    return gameEnded;
}

/* --------------------------------------------------------------- stereo test
 * The null driver is a poor stand-in: no tracked controllers, a fixed pose,
 * and SteamVR puts the session to sleep the moment focus leaves. What it is
 * really being asked to prove is that the TWO EYE path works - the poses, the
 * asymmetric projections, the recentring, the model matrix that carries the
 * game's units into metres. None of that needs a runtime.
 *
 * So do it here: synthesise a head pose from the mouse and WASD, split it into
 * two eyes an IPD apart, and run EXACTLY the arithmetic runVR runs - mView on
 * a pose, mProjFov on four angles, the same model matrix, the same drawWith
 * and drawUI. What is left untested afterwards is the OpenXR session itself
 * and controller input, and no amount of software can test those without the
 * hardware. Everything else is on screen, both eyes, side by side. */
static int runStereo(void)
{
    WNDCLASSA wc;
    HWND hwnd;
    DXGI_SWAP_CHAIN_DESC sd;
    IDXGISwapChain *swap = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    ID3D11DepthStencilView *dsv = NULL;
    ID3D11Texture2D *back = NULL, *depth = NULL;
    D3D11_TEXTURE2D_DESC td;
    D3D_FEATURE_LEVEL fl;
    MSG msg;
    int W = 1600, H = 600, half = 800;
    int gameEnded = 0;
    int haveOrigin = 0;
    float originYaw = 0.0f, ox = 0.0f, oy = 0.0f, oz = 0.0f;
    float ipd = 0.064f;

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = wp;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = "HOTD2VRStereo";
    RegisterClassA(&wc);
    {
        RECT r = { 0, 0, W, H };
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        hwnd = CreateWindowExA(0, "HOTD2VRStereo", "HOTD2VR - stereo test",
                               WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                               r.right - r.left, r.bottom - r.top,
                               NULL, NULL, wc.hInstance, NULL);
    }
    /* SW_SHOW takes the foreground, which pauses the game behind us */
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = W; sd.BufferDesc.Height = H;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    if (FAILED(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
              NULL, 0, D3D11_SDK_VERSION, &sd, &swap, &g_dev, &fl, &g_ctx)))
        die("d3d11 device");
    makePipeline();
    makeScopePipeline();
    gunLoad();

    IDXGISwapChain_GetBuffer(swap, 0, &IID_ID3D11Texture2D, (void **)&back);
    ID3D11Device_CreateRenderTargetView(g_dev, (ID3D11Resource *)back, NULL, &rtv);
    ID3D11Texture2D_Release(back);
    ZeroMemory(&td, sizeof(td));
    td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_D32_FLOAT; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &depth);
    ID3D11Device_CreateDepthStencilView(g_dev, (ID3D11Resource *)depth, NULL, &dsv);
    ID3D11Texture2D_Release(depth);

    note("stereo test - left eye left, right eye right\n");
    note("W A S D move, drag to look, R recentre, Esc quit\n");
    note("hold a card between the halves and cross your eyes to check depth\n");

    for (;;) {
        XrPosef head, eyePose[2];
        float model[16], t[16], rot[16], tmp[16];
        float scale, aspect = (float)half / (float)H, fovY = 1.6f;
        float cy2, sy2, cp2, sp2, rightX, rightZ;
        char title[220];
        int i;

        if (gameExited()) { gameEnded = 1; goto cleanup; }
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto cleanup;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        pullFrame();
        pullScreen();
        g_scrVisible = screenVisible();

        vrMenuKeyboard(hwnd);
        /* a head pose built the way a runtime would report one */
        cy2 = cosf(w_yaw * 0.5f); sy2 = sinf(w_yaw * 0.5f);
        cp2 = cosf(w_pitch * 0.5f); sp2 = sinf(w_pitch * 0.5f);
        head.orientation.w = cy2 * cp2;
        head.orientation.x = cy2 * sp2;
        head.orientation.y = sy2 * cp2;
        head.orientation.z = -sy2 * sp2;
        head.position.x = w_x; head.position.y = w_y; head.position.z = w_z;

        rightX =  cosf(w_yaw);          /* head right vector, yaw only */
        rightZ = -sinf(w_yaw);
        for (i = 0; i < 2; i++) {
            float s2 = (i == 0) ? -0.5f : 0.5f;
            eyePose[i] = head;
            eyePose[i].position.x = w_x + rightX * ipd * s2;
            eyePose[i].position.z = w_z + rightZ * ipd * s2;
        }

        /* recentring, exactly as the headset path does it on the first pose */
        if (!haveOrigin) {
            const XrQuaternionf *q = &eyePose[0].orientation;
            originYaw = atan2f(2.0f * (q->w * q->y + q->x * q->z),
                               1.0f - 2.0f * (q->y * q->y + q->x * q->x));
            ox = eyePose[0].position.x;
            oy = eyePose[0].position.y;
            oz = eyePose[0].position.z;
            haveOrigin = 1;
        }
        if (g_recentre) { haveOrigin = 0; g_recentre = 0; }

        scale = 1.0f / g_unitsPerMetre;
        mIdent(model);
        model[0] = model[5] = model[10] = scale;
        mIdent(rot);
        rot[0] = cosf(originYaw);  rot[2]  = sinf(originYaw);
        rot[8] = -sinf(originYaw); rot[10] = cosf(originYaw);
        mIdent(t);
        t[3] = ox; t[7] = oy; t[11] = oz;
        mMul(rot, model, tmp);
        mMul(t, tmp, model);

        uploadVerts();
        uploadOverlay();
        uploadScreenTex();
        uploadAtlasTex();
        for (i = 0; i < 2; i++) {
            float view[16], proj[16], mvp[16], vp[16];
            float aH = atanf(aspect * tanf(fovY * 0.5f));
            mView(&eyePose[i], view);
            mProjFov(-aH, aH, fovY * 0.5f, -fovY * 0.5f, NEAR_M, FAR_M, proj);
            mMul(proj, view, vp);
            mMul(view, model, mvp);
            mMul(proj, mvp, mvp);
            g_vpX = (i == 0) ? 0.0f : (float)half;
            g_noClear = (i != 0);
            drawWith(rtv, dsv, half, H, mvp, NULL);
            drawUI(vp);
        }
        g_vpX = 0.0f; g_noClear = 0;
        IDXGISwapChain_Present(swap, 1, 0);

        wsprintfA(title, "HOTD2VR stereo - frame %u - %u tris - %d units/m - ipd %d mm",
                  g_lastFrame, g_localCount / 3, (int)g_unitsPerMetre,
                  (int)(ipd * 1000.0f));
        SetWindowTextA(hwnd, title);
    }
cleanup:
    ID3D11DeviceContext_ClearState(g_ctx);
    ID3D11RenderTargetView_Release(rtv);
    ID3D11DepthStencilView_Release(dsv);
    IDXGISwapChain_Release(swap);
    DestroyWindow(hwnd);
    while (PeekMessageA(&msg, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) ;
    releaseGraphics();
    return gameEnded;
}

typedef struct { HANDLE job, process; } DesktopPreview;

static void closeDesktopPreview(DesktopPreview *preview)
{
    if(preview->job)CloseHandle(preview->job); /* Kill only our preview, never the game. */
    if(preview->process)CloseHandle(preview->process);
    memset(preview,0,sizeof(*preview));
}
static int openDesktopPreview(DesktopPreview *preview,const char *command)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};
    STARTUPINFOA startup={sizeof(startup)};PROCESS_INFORMATION process={0};
    char executable[MAX_PATH],*line;
    size_t length=strlen(command)+sizeof(" --preview-only");
    if(!GetModuleFileNameA(NULL,executable,sizeof(executable)))return 0;
    line=malloc(length);if(!line)return 0;
    snprintf(line,length,"%s --preview-only",command);
    preview->job=CreateJobObjectA(NULL,NULL);
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!preview->job || !SetInformationJobObject(preview->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)) ||
       !CreateProcessA(executable,line,NULL,NULL,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,NULL,NULL,&startup,&process)) {
        free(line);closeDesktopPreview(preview);return 0;
    }
    free(line);preview->process=process.hProcess;
    if(!AssignProcessToJobObject(preview->job,process.hProcess) || ResumeThread(process.hThread)==(DWORD)-1) {
        TerminateProcess(process.hProcess,1);CloseHandle(process.hThread);closeDesktopPreview(preview);return 0;
    }
    CloseHandle(process.hThread);return 1;
}

int main(int argc, char **argv)
{

    int stereo = 0, seconds = 0, i;
    DesktopPreview preview={0};
    g_previewOnly=0;
    loadCfg();
    for (i = 1; i < argc; i++) {
        unsigned cheat=cheatArg(argv[i]);
        if (!strcmp(argv[i], "--window")) g_cfgWindow = 1;
        else if (!strcmp(argv[i], "--no-window")) g_cfgWindow = 0;
        else if (!strcmp(argv[i], "--vrwindow")) g_cfgVrWindow = 1;
        else if (!strcmp(argv[i], "--no-vrwindow")) g_cfgVrWindow = 0;
        else if (!strcmp(argv[i], "--preview-only")) g_previewOnly = 1;
        else if (!strcmp(argv[i], "--menu")) g_vrMenuEnabled = 1;
        else if (!strcmp(argv[i], "--no-menu")) g_vrMenuEnabled = 0;
        else if (!strcmp(argv[i], "--stereo")) stereo = 1;
        else if (!strcmp(argv[i], "--swap-player-hud")) g_cfgSwapPlayerHud=1;
        else if (!strcmp(argv[i], "--no-swap-player-hud")) g_cfgSwapPlayerHud=0;
        else if (!strcmp(argv[i], "--expand-meshes")) g_cfgExpandMeshes = 1;
        else if (!strcmp(argv[i], "--no-expand-meshes")) g_cfgExpandMeshes = 0;
        else if (!strcmp(argv[i], "--expand-world")) g_cfgExpandWorld = 1;
        else if (!strcmp(argv[i], "--no-expand-world")) g_cfgExpandWorld = 0;
        else if (!strcmp(argv[i], "--native-lighting")) g_cfgNativeLighting = 1;
        else if (!strcmp(argv[i], "--no-native-lighting")) g_cfgNativeLighting = 0;
        else if (!strcmp(argv[i], "--laser")) g_cfgLaser = 1;
        else if (!strcmp(argv[i], "--no-laser")) g_cfgLaser = 0;
        else if (!strcmp(argv[i], "--crosshair")) g_cfgCrosshair = 1;
        else if (!strcmp(argv[i], "--no-crosshair")) g_cfgCrosshair = 0;
        else if (!strcmp(argv[i], "--seconds") && i+1 < argc) seconds = atoi(argv[++i]);
        else if(!strcmp(argv[i],"--brightness")) {
            if(i+1>=argc || !parseBrightness(argv[++i],&g_cfgBrightness)) {
                note("--brightness requires an integer from 25 to 400 (percent).\n");return 2;
            }
        }
        else if(!strcmp(argv[i],"--master-volume") || !strcmp(argv[i],"--music-volume") || !strcmp(argv[i],"--effects-volume")) {
            const char *option=argv[i];
            unsigned *volume=!strcmp(option,"--master-volume")?&g_cfgMasterVolume:
                !strcmp(option,"--music-volume")?&g_cfgMusicVolume:&g_cfgEffectsVolume;
            if(i+1>=argc || !hotd_parse_volume(argv[++i],volume)) {
                note("%s requires an integer from 0 to 100 (percent).\n",option);return 2;
            }
        }
        else if(!strcmp(argv[i],"--rapid-fire-rate")) {
            if(i+1>=argc || !hotd_parse_rapid_rate(argv[++i],&g_cfgRapidRate)) {
                note("--rapid-fire-rate requires an integer from 1 to 30 (shots/sec).\n");return 2;
            }
        }
        else if(!strcmp(argv[i],"--world-scale")) {
            if(i+1>=argc || !parseWorldScale(argv[++i],&g_cfgWorldScale)) {
                note("--world-scale requires a multiplier from 0.1 to 10 (for example 1.25).\n");return 2;
            }
        }
        else if(cheat)g_cheatFlags|=cheat;
        else { note("usage: HOTD2VR [--menu | --no-menu] [--window | --no-window] [--vrwindow | --no-vrwindow] [--stereo] [--swap-player-hud | --no-swap-player-hud] [--seconds N] [--world-scale N] [--brightness N] [--master-volume N] [--music-volume N] [--effects-volume N] [--native-lighting | --no-native-lighting] [--expand-meshes | --no-expand-meshes] [--expand-world | --no-expand-world] [--laser | --no-laser] [--crosshair | --no-crosshair] [--infinite-ammo] [--infinite-lives] [--invincibility] [--infinite-continues] [--rapid-fire] [--rapid-fire-rate N]\n"); return 2; }
    }
    if(g_previewOnly) { g_cfgWindow=1;g_cfgVrWindow=0;stereo=0;g_cfgFocus=0; }
    int rendering=stereo || g_cfgWindow || g_cfgVrWindow;
    if(!rendering)g_vrMenuEnabled=0;
    if(g_vrMenuEnabled && !vrMenuLoadFont()) {
        note("Settings menu disabled: menu-font.rgba missing or invalid. Run Start-HOTD2.ps1 to generate it.\n");
        g_vrMenuEnabled=0;
    }
    note("Outputs: desktop preview=%d headset=%d%s\n",g_cfgWindow,g_cfgVrWindow,g_previewOnly?" (read-only preview)":"");
    note("Player HUD: swap=%d (akimbo=%d)\n",g_cfgSwapPlayerHud,g_cfgAkimbo);
    note("Aim indicators: laser=%d crosshair=%d\n",g_cfgLaser,g_cfgCrosshair);
    note("Capture expansion: meshes=%s world=%s\n",g_cfgExpandMeshes?"on":"off",g_cfgExpandWorld?"on":"off");
    note("Scene brightness: %d%%\n",g_cfgBrightness);
    note("Audio volume: master=%u%% music=%u%% effects/voices=%u%%\n",g_cfgMasterVolume,g_cfgMusicVolume,g_cfgEffectsVolume);
    note("Native lighting: %s (lights, specular, fog)\n",g_cfgNativeLighting?"on":"off");
    note("World scale: %gx (1 = original size)\n",g_cfgWorldScale);
    note("HOTD2 PC2VR - waiting for injected game bridge\n");
    for (i = 0; i < 100 && !openShare(); i++) Sleep(100);
    if (!g_frame) { note("No HOTD2 bridge. Start through Start-HOTD2.ps1.\n"); return 1; }
    vrMenuInit();
    if (seconds > 0) g_stopAt = GetTickCount() + (DWORD)seconds*1000;
    note("Bridge connected; input profile %s\n", g_frame->input_supported ? "verified" : "unavailable");
    pushCheats();
    note("Cheats requested: ammo=%u lives=%u invincibility=%u continues=%u rapid_fire=%u (up to %u shots/sec; native weapon handling applies)\n",
        !!(g_cheatFlags&HOTD_CHEAT_AMMO),!!(g_cheatFlags&HOTD_CHEAT_LIVES),
        !!(g_cheatFlags&HOTD_CHEAT_INVINCIBILITY),!!(g_cheatFlags&HOTD_CHEAT_CONTINUES),!!(g_cheatFlags&HOTD_CHEAT_RAPID),g_cfgRapidRate);
    if (stereo) runStereo();
    else if(g_cfgVrWindow) {
        if(g_cfgWindow && !openDesktopPreview(&preview,GetCommandLineA()))
            note("Desktop preview unavailable; headset continues.\n");
        runVR();
    } else if(g_cfgWindow)runWindow();
    else while(!gameExited()) { pushCheats();Sleep(10); }
    closeDesktopPreview(&preview);
    if(g_vrMenuEnabled)vrMenuSave();
    g_cheatFlags=0;
    pushCheats();
    if(rendering){pushAim(320, 240, 0); pushAim2(320, 240, 0);}
    if(!g_previewOnly)g_frame->cheat_tick=GetTickCount()-500; /* Release overrides on normal exit. */
    if (g_gameProc) CloseHandle(g_gameProc);
    UnmapViewOfFile(g_frame); CloseHandle(g_map);
    return 0;
}
