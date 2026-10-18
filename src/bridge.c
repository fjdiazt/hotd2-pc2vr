/* HOTD2's Direct3D7 geometry and DirectInput bridge. No game files are patched. */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0700
#include <windows.h>
#include <d3d.h>
#include <dinput.h>
#include <mmsystem.h>
#include <dsound.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "hotd_share.h"
#include "game_profile.h"
#include "game_input.h"
#include "texture_atlas.h"
#include "lighting.h"
#include "capture_visibility.h"
#include "crosshair_capture.h"
#include "player_hud.h"
#include "audio_control.h"
#include "desktop_pause.h"
#include "native_menu.h"
#include "checkpoint.h"

static FILE *logfile;
static Vc2Frame *frame;
static HANDLE mapping;
static HWND game_window;
static int checkpoint_continue,checkpoint_stage,checkpoint_scene=-1,checkpoint_armed,checkpoint_ready,checkpoint_bootstrap;
static HotdCheckpoint checkpoint_target,checkpoint_saved;
static HotdCheckpoint checkpoint_preview_saved;
static DWORD checkpoint_preview_tick;
static DWORD checkpoint_retry;
static unsigned checkpoint_pending;
static DWORD checkpoint_deadline;
static int checkpoint_entered,checkpoint_unpause;
static char checkpoint_path[MAX_PATH];
static char checkpoint_game_directory[MAX_PATH];
static void (__cdecl *checkpoint_load_original)(void);
static void (__cdecl *checkpoint_start_original)(void **,int);
static void (__cdecl *checkpoint_event_original)(void);
static unsigned count, calls, dropped, frame_no, atlas_dirty;
static Vc2Vertex vertices[VC2_MAX_VERTS];
#define HOTD_MAX_HUD_VERTS (4096*6)
static Vc2Vertex hud_vertices[HOTD_MAX_HUD_VERTS];
static unsigned hud_count;
static int pause_scene_ready;
static D3DMATRIX pause_projection;
static D3DVIEWPORT7 pause_viewport;
static HotdPauseRaster desktop_raster[VC2_MAX_VERTS/3],hud_raster[HOTD_MAX_HUD_VERTS/3],pause_raster[VC2_MAX_VERTS/3];
static HotdLens lens;
static unsigned lens_valid;
static unsigned capture_kind,shot_group;
static unsigned capture_skip;
static unsigned capture_model;
static DWORD capture_visibility_planes;
static HotdStageRecords stage_records;
static HotdStageRecords gfile_records;
static const BYTE *gfile_immediate;
static int gfile_ready,capture_gfile;
static HotdSpriteRecords crosshair_records,player_hud_records[4];
static unsigned player_hud_owner,capture_player_hud;
static int player_hud_ready;
static const BYTE *player_hud_immediate;
static unsigned player_hud_immediate_group;
static void (__cdecl *native_hud_ammo_original)(const BYTE *);
static DWORD (__cdecl *native_hud_lives_original)(const BYTE *);
static int crosshair_ready;
static const BYTE *crosshair_definition,*crosshair_immediate;
static unsigned crosshair_draw_depth,crosshair_draw_base_skip;
static int visibility_hooks_ready,visibility_ready;
static const BYTE *visibility_submit_record;
static int visibility_submit_stage;
static DWORD visibility_frame_flags;
static int visibility_flags_latched;
static unsigned visibility_queue_drops;
static unsigned visibility_pressure;
static unsigned visibility_last_pressure_log;
static float shot_origins[64][3];
static D3DMATRIX world, view, projection;
static D3DVIEWPORT7 viewport;
static DWORD fvf_logged[32], fvf_count;
static BYTE *game_base;
static int input_ready;
static DWORD aim_seq, aim_tick;
static unsigned buttons[2];
static unsigned physical_start_keys;
static unsigned physical_back_keys;
static DWORD reload_input;
static int cheats_supported;
static unsigned cheat_owned,cheat_applied;
static BYTE saved_cheats[2][2];
static int continues_supported,invincibility_ready;
static DWORD saved_freeplay;
static uint32_t shot_events[2];
static void (__cdecl *native_shot_ray_original)(int,float,float);
static HotdRapid rapid_fire[2];
static char ini_path[MAX_PATH];
static IDirect3DDevice7 *active_device;
static void publish_frame(void);
static int (WINAPI *original_message_box)(HWND,LPCSTR,LPCSTR,UINT);
static int audio_ready;

static void logline(const char *fmt, ...)
{
    va_list ap;
    if (!logfile) return;
    va_start(ap, fmt); vfprintf(logfile, fmt, ap); va_end(ap);
    fputc('\n', logfile); fflush(logfile);
}

static int WINAPI device_message_box(HWND hwnd,LPCSTR text,LPCSTR caption,UINT flags)
{
    if(hotd_quit_prompt(text,caption,flags)) {
        logline("native quit confirmation auto-accepted");
        return IDYES;
    }
    if(hotd_device_prompt(text,caption,flags)) {
        logline("native renderer auto-selected: %s",text);
        return IDYES;
    }
    return original_message_box(hwnd,text,caption,flags);
}

static int replace_pointer(void **slot, void *hook, void **original)
{
    DWORD old, unused;
    if (*slot == hook) return 1;
    if (*original && *original != *slot) { logline("different COM implementation; hook refused"); return 0; }
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) return 0;
    *original = *slot;
    InterlockedExchangePointer(slot, hook);
    VirtualProtect(slot, sizeof(*slot), old, &unused);
    return 1;
}

typedef HRESULT (WINAPI *AudioPlay)(IDirectSoundBuffer *,DWORD,DWORD,DWORD);
typedef struct { const IDirectSoundBufferVtbl *vtable;AudioPlay original; } AudioPlayHook;
static AudioPlayHook audio_play_hooks[HOTD_AUDIO_CHANNELS];
static struct { IDirectSoundBuffer *buffer;uint32_t levels;int attempted; } audio_applied[HOTD_AUDIO_CHANNELS];
static DWORD audio_failures;
static int (__cdecl *native_audio_play_original)(int,int);

static BYTE *native_audio_channels(void)
{
    if(!audio_ready || !(*(DWORD *)(game_base+0x3DE5B8)&0x80000000u) ||
       *(DWORD *)(game_base+0x3DE5BC)!=HOTD_AUDIO_CHANNELS)return NULL;
    return *(BYTE **)(game_base+0x3DE5A8);
}
static IDirectSoundBuffer *native_audio_buffer(BYTE *channels,unsigned channel)
{
    BYTE *record=channels+channel*0x44;
    return (*(DWORD *)(record+0x40)&0x80000000u)?*(IDirectSoundBuffer **)record:NULL;
}
static HRESULT apply_audio_volume(IDirectSoundBuffer *buffer,unsigned channel,uint32_t levels)
{
    if(audio_applied[channel].buffer!=buffer || audio_applied[channel].levels!=levels)
        audio_failures&=~(1u<<channel);
    HRESULT hr=IDirectSoundBuffer_SetVolume(buffer,hotd_audio_attenuation(levels,channel));
    if(SUCCEEDED(hr))audio_failures&=~(1u<<channel);
    if(FAILED(hr) && !(audio_failures&(1u<<channel))) {
        audio_failures|=1u<<channel;logline("native audio volume failed: channel=%u hr=%08lx",channel,(unsigned long)hr);
    }
    audio_applied[channel].buffer=buffer;audio_applied[channel].levels=levels;audio_applied[channel].attempted=SUCCEEDED(hr);
    return hr;
}
static HRESULT WINAPI audio_buffer_play(IDirectSoundBuffer *self,DWORD reserved,DWORD priority,DWORD flags)
{
    AudioPlay original=NULL;
    for(unsigned i=0;i<HOTD_AUDIO_CHANNELS;i++)if(audio_play_hooks[i].vtable==self->lpVtbl) {
        original=audio_play_hooks[i].original;break;
    }
    if(!original)return DSERR_GENERIC;
    BYTE *channels=native_audio_channels();
    if(channels && frame)for(unsigned i=0;i<HOTD_AUDIO_CHANNELS;i++)
        if(native_audio_buffer(channels,i)==self) {
            /* A lost-buffer failure reaches the game's existing Restore/retry before any Play. */
            uint32_t levels=hotd_audio_sanitize(frame->audio_levels);
            HRESULT hr=apply_audio_volume(self,i,levels);
            if(FAILED(hr) && (hr==DSERR_BUFFERLOST || hotd_audio_attenuation(levels,i)<0))return hr;
            break;
        }
    return original(self,reserved,priority,flags);
}
static int hook_audio_buffer(IDirectSoundBuffer *buffer)
{
    AudioPlayHook *free_slot=NULL;
    for(unsigned i=0;i<HOTD_AUDIO_CHANNELS;i++) {
        AudioPlayHook *hook=audio_play_hooks+i;
        if(hook->vtable==buffer->lpVtbl)return hook->original!=NULL;
        if(!hook->vtable && !free_slot)free_slot=hook;
    }
    if(!free_slot)return 0;
    free_slot->vtable=buffer->lpVtbl;
    if(!replace_pointer((void **)&buffer->lpVtbl->Play,audio_buffer_play,(void **)&free_slot->original)) {
        memset(free_slot,0,sizeof(*free_slot));return 0;
    }
    return 1;
}
static int __cdecl native_audio_play(int channel,int loop)
{
    BYTE *channels=native_audio_channels();
    if(channels && channel>=0 && channel<HOTD_AUDIO_CHANNELS) {
        IDirectSoundBuffer *buffer=native_audio_buffer(channels,(unsigned)channel);
        if(buffer && !hook_audio_buffer(buffer) && frame &&
           hotd_audio_attenuation(frame->audio_levels,(unsigned)channel)!=0)return 0;
    }
    return native_audio_play_original(channel,loop);
}
static void update_audio_levels(void)
{
    BYTE *channels=native_audio_channels();
    if(!channels || !frame)return;
    uint32_t levels=hotd_audio_sanitize(frame->audio_levels);
    for(unsigned i=0;i<HOTD_AUDIO_CHANNELS;i++) {
        IDirectSoundBuffer *buffer=native_audio_buffer(channels,i);
        if(buffer && (!audio_applied[i].attempted || audio_applied[i].buffer!=buffer || audio_applied[i].levels!=levels)) {
            apply_audio_volume(buffer,i,levels);
        }
        if(!buffer){audio_applied[i].buffer=NULL;audio_applied[i].attempted=0;}
    }
}
static unsigned audio_ini_volume(const char *key)
{
    char text[32];unsigned value;
    DWORD n=GetPrivateProfileStringA("vr",key,"100",text,sizeof(text),ini_path);
    return n<sizeof(text)-1 && hotd_parse_volume(text,&value)?value:100;
}

static int write_code(BYTE *at, const BYTE *bytes, unsigned n)
{
    DWORD old, unused;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(at, bytes, n);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    VirtualProtect(at, n, old, &unused);
    return 1;
}

static int checkpoint_word(DWORD address,DWORD *value)
{
    uintptr_t offset=(uintptr_t)address-(uintptr_t)game_base;
    if((address&3) || offset<0x577200 || offset>0x5A1A00-4)return 0;
    memcpy(value,(const void *)(uintptr_t)address,4);return 1;
}
static int checkpoint_script(HotdCheckpoint point,unsigned variant,DWORD *out)
{
    DWORD scenes,variants=0,script,opcode;
    if(!hotd_checkpoint_valid(point) || variant>1)return 0;
    memcpy(&scenes,game_base+0x577200+(point.stage-1)*4,4);
    for(int i=0;i<=point.scene;i++)
        if(!checkpoint_word(scenes+i*4,&variants) || !variants || variants==0xFFFFFFFFu)return 0;
    if(!checkpoint_word(variants+variant*4,&script) || !checkpoint_word(script,&opcode) ||
       !opcode || opcode>=0x60 || opcode==0x2A || opcode==0x34 || opcode==0x3C || opcode==0x4C)return 0;
    *out=script;return 1;
}
static int checkpoint_scene_available(HotdCheckpoint point)
{
    DWORD script;return checkpoint_script(point,1,&script);
}
static int checkpoint_auto_enabled(void)
{
    if(frame && frame->cheat_tick && (DWORD)(GetTickCount()-frame->cheat_tick)<500 &&
       (frame->checkpoint_autosave==1 || frame->checkpoint_autosave==2))return frame->checkpoint_autosave==2;
    return checkpoint_continue;
}
static void __cdecl checkpoint_event(void)
{
    DWORD script;
    checkpoint_event_original();
    if(checkpoint_bootstrap && *(DWORD *)(game_base+0x5C8E98)==6 && *(DWORD *)(game_base+0x5CA08C)==0 &&
       *(short *)(game_base+0x5A1A08)==checkpoint_target.stage-1 &&
       *(short *)(game_base+0x5A2BC0)==checkpoint_target.scene) {
        /* Native scene variant 0 supplies resources/section setup before variant 1 gameplay. */
        if(checkpoint_script(checkpoint_target,0,&script)) {
            *(short *)(game_base+0x5A2BB0)=0;*(DWORD *)(game_base+0x5C7108)=script;
            logline("checkpoint scene setup: stage=%d scene=%d variant=0",checkpoint_target.stage,checkpoint_target.scene);
        } else logline("checkpoint scene setup unavailable; native gameplay entry retained");
    }
    checkpoint_bootstrap=0;
    if(checkpoint_pending && *(DWORD *)(game_base+0x5C8E98)==6 &&
       *(short *)(game_base+0x5A1A08)==checkpoint_target.stage-1 &&
       *(short *)(game_base+0x5A2BC0)==checkpoint_target.scene)checkpoint_entered=1;
}
static void __cdecl checkpoint_load(void)
{
    checkpoint_armed=0;
    checkpoint_bootstrap=0;
    /* Only fresh Arcade starts; demos, Original, Training and Boss modes stay native. */
    if(*(DWORD *)(game_base+0x5CA08C)==0) {
        if(checkpoint_pending)checkpoint_armed=1;
        else if(checkpoint_stage) {
            checkpoint_target=(HotdCheckpoint){checkpoint_stage,checkpoint_scene>=0?
                checkpoint_scene:hotd_checkpoint_first_scene(checkpoint_stage)};
            checkpoint_armed=1;
        } else if(checkpoint_auto_enabled()) {
            checkpoint_armed=hotd_checkpoint_read(checkpoint_path,&checkpoint_target);
            if(!checkpoint_armed)logline("checkpoint resume: no valid saved location; normal Arcade start");
        }
        if(checkpoint_armed)*(short *)(game_base+0x5A1A08)=(short)(checkpoint_target.stage-1);
    }
    checkpoint_load_original();
}
static void __cdecl checkpoint_start(void **out,int mode)
{
    if(checkpoint_armed && *(DWORD *)(game_base+0x5CA08C)==0) {
        if(checkpoint_scene_available(checkpoint_target)) {
            *(short *)(game_base+0x5A2BC0)=(short)checkpoint_target.scene;
            checkpoint_bootstrap=checkpoint_target.scene!=hotd_checkpoint_first_scene(checkpoint_target.stage);
            logline("checkpoint start: stage=%d scene=%d (experimental scene entry)",checkpoint_target.stage,checkpoint_target.scene);
        } else {
            logline("checkpoint rejected: stage=%d scene=%d has no Arcade script; starting normally",checkpoint_target.stage,checkpoint_target.scene);
            *(short *)(game_base+0x5A1A08)=*(short *)(game_base+0x5A2BC0)=0;
            checkpoint_load_original();
        }
    }
    checkpoint_armed=0;
    checkpoint_start_original(out,mode);
}
static int checkpoint_current(unsigned active,HotdCheckpoint *point)
{
    if(!active ||
       *(DWORD *)(game_base+0x5C8E98)!=6 || *(DWORD *)(game_base+0x5CA08C)!=0 ||
       *(DWORD *)(game_base+0x5C90A4)!=2)return 0;
    *point=(HotdCheckpoint){*(short *)(game_base+0x5A1A08)+1,*(short *)(game_base+0x5A2BC0)};
    return hotd_checkpoint_valid(*point);
}
static void checkpoint_refresh_saved(void)
{
    DWORD now=GetTickCount();
    if(!checkpoint_preview_tick || (DWORD)(now-checkpoint_preview_tick)>=1000) {
        if(!hotd_checkpoint_read(checkpoint_path,&checkpoint_preview_saved))checkpoint_preview_saved=(HotdCheckpoint){0};
        checkpoint_preview_tick=now;
    }
}
static void checkpoint_locations(unsigned active)
{
    HotdCheckpoint point={0};
    if(checkpoint_pending || !checkpoint_current(active,&point))point=(HotdCheckpoint){0};
    frame->checkpoint_current_stage=point.stage;frame->checkpoint_current_scene=point.scene;
    frame->checkpoint_saved_stage=checkpoint_preview_saved.stage;frame->checkpoint_saved_scene=checkpoint_preview_saved.scene;
}
static void checkpoint_reply(unsigned request,unsigned result,HotdCheckpoint point)
{
    frame->checkpoint_result=result;frame->checkpoint_stage=point.stage;frame->checkpoint_scene=point.scene;
    MemoryBarrier();InterlockedExchange((volatile LONG *)&frame->checkpoint_response,request);
}
static void checkpoint_manual(unsigned active)
{
    HotdCheckpoint point={0};unsigned request=frame->checkpoint_request,result;
    if(checkpoint_pending) {
        if(checkpoint_entered && checkpoint_current(active,&point) &&
           point.stage==checkpoint_target.stage && point.scene==checkpoint_target.scene) {
            checkpoint_reply(checkpoint_pending,HOTD_SAVE_OK,point);
            logline("checkpoint loaded: stage=%d scene=%d",point.stage,point.scene);
            checkpoint_pending=0;
        } else if((LONG)(GetTickCount()-checkpoint_deadline)>=0) {
            checkpoint_reply(checkpoint_pending,HOTD_SAVE_LOAD_FAILED,(HotdCheckpoint){0});checkpoint_pending=0;
        }
        return;
    }
    if(request==frame->checkpoint_response)return;
    MemoryBarrier();
    if(frame->checkpoint_action==HOTD_CHECKPOINT_LOAD || frame->checkpoint_action==HOTD_CHECKPOINT_NEW ||
       frame->checkpoint_action==HOTD_CHECKPOINT_SELECT) {
        unsigned phase=*(DWORD *)(game_base+0x5C8E98);
        if(!checkpoint_ready || phase<3 || phase>12 ||
           (phase==6 && *(DWORD *)(game_base+0x5C90A4)!=2) ||
           *(int *)(game_base+0x3C17A0)>=0)result=HOTD_SAVE_UNAVAILABLE;
        else if(frame->checkpoint_action==HOTD_CHECKPOINT_LOAD && !hotd_checkpoint_read(checkpoint_path,&point))
            result=HOTD_SAVE_INVALID;
        else {
            if(frame->checkpoint_action==HOTD_CHECKPOINT_NEW)point=(HotdCheckpoint){1,0};
            else if(frame->checkpoint_action==HOTD_CHECKPOINT_SELECT) {
                if(frame->checkpoint_target_stage<1 || frame->checkpoint_target_stage>6 || frame->checkpoint_target_scene>255) {
                    checkpoint_reply(request,HOTD_SAVE_INVALID,(HotdCheckpoint){0});return;
                }
                point=(HotdCheckpoint){(int)frame->checkpoint_target_stage,(int)frame->checkpoint_target_scene};
            }
            if(!hotd_checkpoint_file_scene(checkpoint_game_directory,point))result=HOTD_SAVE_INVALID;
            else {
                checkpoint_target=point;checkpoint_pending=request;
                checkpoint_entered=0;checkpoint_deadline=GetTickCount()+30000;
                checkpoint_unpause=*(short *)(game_base+0x5A1A00)!=0;
                if(!active)active=frame->akimbo?3:1;
                for(unsigned p=0;p<2;p++)*(short *)(game_base+0x5A5C62+p*0x130)=(active&(1u<<p))?0:9;
                *(DWORD *)(game_base+0x5CA08C)=0;
                /* Let Game_ApplyState clear tasks/resources, then Game6_Start rebuilds the destination. */
                *(int *)(game_base+0x3C17A0)=6;
                logline("checkpoint load queued: stage=%d scene=%d",point.stage,point.scene);
                return;
            }
        }
        checkpoint_reply(request,result,(HotdCheckpoint){0});return;
    }
    if(frame->checkpoint_action!=HOTD_CHECKPOINT_SAVE) {
        checkpoint_reply(request,HOTD_SAVE_UNAVAILABLE,point);return;
    }
    if(!checkpoint_current(active,&point) || !checkpoint_scene_available(point))result=HOTD_SAVE_UNAVAILABLE;
    else if(!hotd_checkpoint_write(checkpoint_path,point)) {
        result=HOTD_SAVE_IO_ERROR;
        logline("manual checkpoint save failed: error=%lu",(unsigned long)GetLastError());
    } else {
        result=HOTD_SAVE_OK;checkpoint_saved=point;checkpoint_retry=0;checkpoint_preview_tick=0;
        logline("manual checkpoint saved: stage=%d scene=%d",point.stage,point.scene);
    }
    checkpoint_reply(request,result,result==HOTD_SAVE_OK?point:(HotdCheckpoint){0});
}
static void checkpoint_record(unsigned active)
{
    HotdCheckpoint point;DWORD now;
    if(!checkpoint_ready || checkpoint_pending || !checkpoint_auto_enabled() || !checkpoint_current(active,&point))return;
    if(point.stage==checkpoint_saved.stage && point.scene==checkpoint_saved.scene)return;
    now=GetTickCount();
    if(checkpoint_retry && (LONG)(now-checkpoint_retry)<0)return;
    if(!checkpoint_scene_available(point))return;
    if(hotd_checkpoint_write(checkpoint_path,point)) {
        checkpoint_saved=point;checkpoint_retry=0;checkpoint_preview_tick=0;
        logline("checkpoint saved: stage=%d scene=%d",point.stage,point.scene);
    } else {
        checkpoint_retry=now+5000;
        logline("checkpoint save failed: error=%lu; retry in 5 seconds",(unsigned long)GetLastError());
    }
}

static int checkpoint_environment(const wchar_t *key,int minimum,int maximum,int *value)
{
    wchar_t text[16];DWORD n=GetEnvironmentVariableW(key,text,16);
    if(!n)return GetLastError()==ERROR_ENVVAR_NOT_FOUND;
    return n<16 && hotd_checkpoint_number(text,minimum,maximum,value);
}
static int install_checkpoints(void)
{
    static const BYTE load[]={0xE8,0x51,0x32,0xFB,0xFF},start[]={0xE8,0x57,0,0,0},event[]={0xE8,0xAB,0xE4,0xFF,0xFF};
    BYTE patch[5]={0xE8};DWORD jump;char *slash;
    checkpoint_continue=GetPrivateProfileIntA("vr","continue_game",0,ini_path)!=0;
    if(!checkpoint_environment(L"HOTD2VR_START_STAGE",1,6,&checkpoint_stage) ||
       !checkpoint_environment(L"HOTD2VR_START_SCENE",0,HOTD_CHECKPOINT_MAX_SCENE,&checkpoint_scene) ||
       !checkpoint_environment(L"HOTD2VR_CONTINUE_GAME",0,1,&checkpoint_continue) ||
       (checkpoint_scene>=0 && !checkpoint_stage))return 0;
    strcpy(checkpoint_path,ini_path);slash=strrchr(checkpoint_path,'\\');
    if(!slash || slash-checkpoint_path+sizeof("HOTD2VR.checkpoint.ini")>=MAX_PATH)return 0;
    strcpy(slash+1,"HOTD2VR.checkpoint.ini");
    DWORD n=GetModuleFileNameA(NULL,checkpoint_game_directory,sizeof(checkpoint_game_directory));
    slash=n && n<sizeof(checkpoint_game_directory)?strrchr(checkpoint_game_directory,'\\'):NULL;
    if(!slash)return 0;
    *slash=0;
    /* Pass-through entry hooks remain available for live Autosave and Load game. */
    if(memcmp(game_base+0x5FF0A,load,sizeof(load)) || memcmp(game_base+0x5FFD4,start,sizeof(start)) ||
       memcmp(game_base+0x60710,event,sizeof(event)))return 0;
    checkpoint_load_original=(void *)((uintptr_t)game_base+0x13160);
    checkpoint_start_original=(void *)((uintptr_t)game_base+0x60030);
    checkpoint_event_original=(void *)((uintptr_t)game_base+0x5EBC0);
    jump=(DWORD)((BYTE *)checkpoint_load-(game_base+0x5FF0A+5));memcpy(patch+1,&jump,4);
    if(!write_code(game_base+0x5FF0A,patch,sizeof(patch)))return 0;
    jump=(DWORD)((BYTE *)checkpoint_start-(game_base+0x5FFD4+5));memcpy(patch+1,&jump,4);
    if(!write_code(game_base+0x5FFD4,patch,sizeof(patch)))return 0;
    jump=(DWORD)((BYTE *)checkpoint_event-(game_base+0x60710+5));memcpy(patch+1,&jump,4);
    if(!write_code(game_base+0x60710,patch,sizeof(patch)))return 0;
    checkpoint_ready=1;
    if(frame)for(unsigned stage=1;stage<=6;stage++)
        hotd_checkpoint_file_scenes(checkpoint_game_directory,stage,frame->checkpoint_stage_scenes[stage-1]);
    logline("checkpoint controls: ready; autosave=%d explicit_stage=%d scene=%d file=%s",checkpoint_continue,checkpoint_stage,checkpoint_scene,checkpoint_path);
    return 1;
}

static int active_input(void)
{
    DWORD seq;
    if (!frame || !input_ready) return 0;
    seq = frame->aim_seq;
    if (seq != aim_seq) { aim_seq = seq; aim_tick = GetTickCount(); }
    return aim_tick && GetTickCount()-aim_tick < 500;
}

static int native_cheat_profile(const BYTE *base)
{
    static const BYTE life[]={0x8A,0x0C,0x85,0xD8,0x9F,0x9C,0x00};
    static const BYTE ammo[]={0x8A,0x51,0x79},ammo_original[]={0x8A,0x42,0x79};
    return !memcmp(base+0x15342,life,sizeof(life)) && !memcmp(base+0x15464,life,sizeof(life)) &&
        !memcmp(base+0x149D9,ammo,sizeof(ammo)) && !memcmp(base+0x14C47,ammo_original,sizeof(ammo_original));
}

static int native_shot_profile(const BYTE *base)
{
    static const BYTE ray[]={0x83,0xEC,0x1C,0x8B,0x44,0x24,0x20};
    static const BYTE normal[]={0xE8,0xDF,0x16,0xFF,0xFF},special[]={0xE8,0x9F,0x13,0xFF,0xFF};
    return !memcmp(base+0x6110,ray,sizeof(ray)) && !memcmp(base+0x14A2C,normal,sizeof(normal)) &&
        !memcmp(base+0x14D6C,special,sizeof(special));
}
static void __cdecl native_shot_ray(int player,float x,float y)
{
    native_shot_ray_original(player,x,y);
    if(player>=0 && player<2 && *(DWORD *)(game_base+0x5C8E98)==6 && !*(short *)(game_base+0x5A1A00))
        shot_events[player]++;
}
static int native_continues_profile(const BYTE *base)
{
    static const BYTE shared[]={0x8B,0x0D,0x70,0x8E,0x9C,0x00,0x83,0xF9,0x01};
    static const BYTE separate[]={0x8B,0x15,0x70,0x8E,0x9C,0x00,0x8B,0x44,0x24,0x08,0x83,0xFA,0x01};
    static const BYTE available[]={0x8B,0x0D,0x70,0x8E,0x9C,0x00,0xB8,0x01,0x00,0x00,0x00};
    return !memcmp(base+0x6E1F,shared,sizeof(shared)) &&
        !memcmp(base+0x6E74,separate,sizeof(separate)) && !memcmp(base+0x6ED0,available,sizeof(available));
}
static int cheat_request_active(void)
{
    return frame && frame->cheat_tick && (DWORD)(GetTickCount()-frame->cheat_tick)<500;
}
static unsigned requested_cheats(void)
{
    return cheat_request_active() ? frame->cheat_flags&HOTD_CHEAT_MASK:0;
}
static void (__cdecl *native_damage_original)(int,int,int);
static void (__cdecl *native_damage_forced_original)(int,int,int,int,int);
static void __cdecl native_damage(int player,int hit,int kind)
{
    if(!invincibility_ready || !(requested_cheats()&HOTD_CHEAT_INVINCIBILITY))
        native_damage_original(player,hit,kind);
}
static void __cdecl native_damage_forced(int player,int hit,int kind,int force,int timer)
{
    if(!invincibility_ready || !(requested_cheats()&HOTD_CHEAT_INVINCIBILITY))
        native_damage_forced_original(player,hit,kind,force,timer);
}

static void native_cheats(unsigned requested)
{
    const unsigned bits[]={HOTD_CHEAT_LIVES,HOTD_CHEAT_AMMO};
    unsigned owned=cheat_request_active() ?
        (cheats_supported ? HOTD_CHEAT_LIVES|HOTD_CHEAT_AMMO:0) |
        (continues_supported ? HOTD_CHEAT_CONTINUES:0):0;
    requested&=owned;
    for(unsigned kind=0;kind<2;kind++)for(unsigned p=0;p<2;p++) {
        BYTE *flag=game_base+0x5C9FD8+p*0x7C+kind;
        if(owned&bits[kind]) {
            if(!(cheat_owned&bits[kind]))saved_cheats[p][kind]=*flag;
            *flag=!!(requested&bits[kind]); /* Explicit off overrides native saved cheats too. */
        } else if(cheat_owned&bits[kind])*flag=saved_cheats[p][kind];
    }
    DWORD *freeplay=(DWORD *)(game_base+0x5C8E70);
    if(owned&HOTD_CHEAT_CONTINUES) {
        if(!(cheat_owned&HOTD_CHEAT_CONTINUES))saved_freeplay=*freeplay;
        *freeplay=!!(requested&HOTD_CHEAT_CONTINUES);
    } else if(cheat_owned&HOTD_CHEAT_CONTINUES)*freeplay=saved_freeplay;
    if(requested!=cheat_applied || owned!=cheat_owned)logline("native cheats: ammo=%u lives=%u continues=%u override=%u",
        !!(requested&HOTD_CHEAT_AMMO),!!(requested&HOTD_CHEAT_LIVES),!!(requested&HOTD_CHEAT_CONTINUES),!!owned);
    cheat_owned=owned;cheat_applied=requested;
}

static void update_input(void)
{
    int active = active_input();
    buttons[0] = active ? frame->aim_buttons : 0;
    buttons[1] = active && frame->akimbo ? frame->aim2_buttons : 0;
    if(active && frame->akimbo && (buttons[0]&VC2_BTN_PAIR_START) && ((buttons[0]|buttons[1])&VC2_BTN_START)) {
        buttons[0]|=VC2_BTN_START;buttons[1]|=VC2_BTN_START;
    }
    if (active) {
        unsigned p;
        for (p=0;p<(frame->akimbo ? 2u:1u);p++) {
            float sx = p ? frame->aim2_x : frame->aim_x;
            float sy = p ? frame->aim2_y : frame->aim_y;
            volatile short *axes = (volatile short *)(game_base+0x5C8FC4+p*0x28);
            if (!isfinite(sx) || !isfinite(sy)) continue;
            /* VR can aim beyond the flat viewport; only the native WORD range limits it. */
            axes[0] = (short)lroundf(fmaxf(-32768,fminf(32767,sx-320)));
            axes[1] = (short)lroundf(fmaxf(-32768,fminf(32767,240-sy)));
        }
    }
}

static DWORD (__cdecl *native_input_original)(void);
static int (__cdecl *native_pause_original)(void);
static int __cdecl native_pause(void)
{
    if(checkpoint_unpause) {
        checkpoint_unpause=0;
        if(*(short *)(game_base+0x5A1A00))return 1;
    }
    static unsigned previous;
    DWORD *input=(DWORD *)(game_base+0x5C9028),saved=*input,pause_input=saved;
    unsigned held=0;
    update_input();
    for(unsigned p=0;p<2;p++) {
        DWORD start=8u<<(p*16);
        /* Only the pause reader loses synthetic Start; physical keyboard Start stays native. */
        if((buttons[p]&VC2_BTN_START) && !(physical_start_keys&(1u<<p)))pause_input&=~start;
        if(buttons[p]&VC2_BTN_PAUSE)held|=1u<<p;
        if((held&~previous)&(1u<<p))pause_input|=start;
    }
    previous=held;
    *input=pause_input;
    int result=native_pause_original();
    *input=saved;
    return result;
}
static int native_reload(unsigned player,HotdGunInput *gun)
{
    BYTE *profile=game_base+0x5A5D84+player*0x130;
    if(*(DWORD *)(game_base+0x5C8E98)!=6 ||
       *(short *)(game_base+0x5A5C62+player*0x130)!=5)return 0;
    if(profile[1]==1) {
        gun->valid=0;gun->fire=1; /* Native gun path: off-screen trigger means reload. */
        return 1;
    }
    if(profile[1]==0) {
        unsigned scheme=profile[0]==1 ? 2u:profile[0]==2 ? 3u:1u;
        DWORD mask=*(DWORD *)(game_base+0x5C9F6C+player*0x7C+scheme*20);
        reload_input|=mask;
        return mask!=0;
    }
    return 0;
}

static DWORD __cdecl native_input(void)
{
    static unsigned previous[2];
    unsigned p;
    DWORD result=native_input_original();
    unsigned requested=requested_cheats();
    update_input();
    reload_input=*(DWORD *)(game_base+0x5C9028);
    for(p=0;p<2;p++)
        if((buttons[p]&VC2_BTN_BACK) && !(physical_back_keys&(1u<<p)))
            reload_input&=~(2u<<(p*16)); /* Synthetic Cancel reaches skip/menus, never weapon reload. */
    native_cheats(requested);
    for(p=0;p<2;p++) {
        if(active_input() && (!p || frame->akimbo)) {
            HotdGunInput *gun=(HotdGunInput *)(game_base+0x5C8FC0+p*0x28);
            unsigned action=hotd_apply_gun_buttons(gun,buttons[p],previous+p);
            if(hotd_rapid_fire(rapid_fire+p,
                (buttons[p]&VC2_BTN_FIRE) && !(buttons[p]&VC2_BTN_RELOAD),
                (requested&HOTD_CHEAT_RAPID) && *(DWORD *)(game_base+0x5C8E98)==6 &&
                *(short *)(game_base+0x5A5C62+p*0x130)==5 && *(DWORD *)(game_base+0x5C8E00) &&
                !*(short *)(game_base+0x5A1A00),frame->rapid_fire_rate,GetTickCount())) {
                gun->fire=1;action=VC2_BTN_FIRE;
            }
            if(action==VC2_BTN_RELOAD) {
                int accepted=native_reload(p,gun);
                logline("native reload P%u: queued=%d ammo_before=%d phase=%lu status=%d control=%u",p+1,accepted,
                    *(short *)(game_base+0x5A5C7C+p*0x130),*(DWORD *)(game_base+0x5C8E98),
                    *(short *)(game_base+0x5A5C62+p*0x130),game_base[0x5A5D85+p*0x130]);
            }
            if(action==VC2_BTN_FIRE)
                logline("native trigger P%u: valid=%d fire=%d ammo=%d",p+1,gun->valid,gun->fire,
                    *(short *)(game_base+0x5A5C7C+p*0x130));
        } else {previous[p]=0;rapid_fire[p].active=0;}
    }
    return result;
}

typedef HRESULT (WINAPI *InputCreate)(HINSTANCE,DWORD,REFIID,void **,IUnknown *);
typedef HRESULT (WINAPI *CreateInputDevice)(void *,REFGUID,void **,IUnknown *);
typedef HRESULT (WINAPI *CreateInputDeviceEx)(void *,REFGUID,REFIID,void **,IUnknown *);
typedef HRESULT (WINAPI *GetDeviceState)(void *,DWORD,void *);
static InputCreate original_input_create;
static CreateInputDevice original_device_create;
static CreateInputDeviceEx original_device_create_ex;
static GetDeviceState original_keyboard_state;

static HRESULT WINAPI keyboard_state(void *self,DWORD size,void *out)
{
    HRESULT hr = original_keyboard_state(self,size,out);
    update_input();
    if(SUCCEEDED(hr) && size==256 && (buttons[0]&VC2_BTN_MENU)) {
        BYTE *keys=out;
        const unsigned menu_keys[]={DIK_F8,DIK_UP,DIK_DOWN,DIK_LEFT,DIK_RIGHT,DIK_RETURN,DIK_TAB,
            DIK_RCONTROL,DIK_LCONTROL,DIK_BACK,DIK_ESCAPE};
        for(unsigned k=0;k<sizeof(menu_keys)/sizeof(menu_keys[0]);k++)keys[menu_keys[k]]=0;
    }
    if(size==256) {
        BYTE *keys=out;
        physical_start_keys=SUCCEEDED(hr) ? ((keys[DIK_RETURN]&0x80?1u:0u)|(keys[DIK_TAB]&0x80?2u:0u)):0;
        physical_back_keys=SUCCEEDED(hr) ? ((keys[DIK_RCONTROL]&0x80?1u:0u)|(keys[DIK_LCONTROL]&0x80?2u:0u)):0;
    }
    if(FAILED(hr) && size==256 && active_input()) {
        memset(out,0,size);hr=DI_OK;
    }
    if (SUCCEEDED(hr) && size==256 && active_input()) {
        BYTE *keys = out;
        if(buttons[0]&VC2_BTN_START) keys[DIK_RETURN]=0x80;
        if(buttons[1]&VC2_BTN_START) keys[DIK_TAB]=0x80;
        if(buttons[0]&VC2_BTN_BACK) keys[DIK_RCONTROL]=0x80;
        if(buttons[1]&VC2_BTN_BACK) keys[DIK_LCONTROL]=0x80;
        unsigned nav=(buttons[0]|buttons[1])&(VC2_BTN_UP|VC2_BTN_DOWN);
        if(nav==VC2_BTN_UP)keys[DIK_UP]=0x80;
        if(nav==VC2_BTN_DOWN)keys[DIK_DOWN]=0x80;
        nav=(buttons[0]|buttons[1])&(VC2_BTN_LEFT|VC2_BTN_RIGHT);
        if(nav==VC2_BTN_LEFT)keys[DIK_LEFT]=0x80;
        if(nav==VC2_BTN_RIGHT)keys[DIK_RIGHT]=0x80;
    }
    return hr;
}

static void hook_keyboard(REFGUID guid,void *device)
{
    if (IsEqualGUID(guid,&GUID_SysKeyboard)) {
        void **vt = *(void ***)device;
        logline("keyboard GetDeviceState hook: %d",replace_pointer(vt+9,keyboard_state,(void **)&original_keyboard_state));
    }
}
static HRESULT WINAPI create_input_device(void *self,REFGUID guid,void **out,IUnknown *outer)
{
    HRESULT hr=original_device_create(self,guid,out,outer);
    if(SUCCEEDED(hr) && *out) hook_keyboard(guid,*out);
    return hr;
}
static HRESULT WINAPI create_input_device_ex(void *self,REFGUID guid,REFIID iid,void **out,IUnknown *outer)
{
    HRESULT hr=original_device_create_ex(self,guid,iid,out,outer);
    if(SUCCEEDED(hr) && *out) hook_keyboard(guid,*out);
    return hr;
}
static HRESULT WINAPI input_create(HINSTANCE instance,DWORD version,REFIID iid,void **out,IUnknown *outer)
{
    HRESULT hr=original_input_create(instance,version,iid,out,outer);
    if(SUCCEEDED(hr) && *out && IsEqualGUID(iid,&IID_IDirectInput7A)) {
        void **vt=*(void ***)*out;
        replace_pointer(vt+3,create_input_device,(void **)&original_device_create);
        replace_pointer(vt+9,create_input_device_ex,(void **)&original_device_create_ex);
        logline("DirectInput7 hooked");
    } else logline("DirectInputCreateEx hr=%08lx iid=%08lx",(unsigned long)hr,(unsigned long)iid->Data1);
    return hr;
}

typedef struct { IDirectDrawSurface7 *surface; unsigned x,y,w,h,last_used; } Texture;
static Texture textures[2048];
static HotdAtlas atlas_space;
static unsigned texture_count, texture_evictions, texture_failures;
static IDirectDrawSurface7 *pause_texture(const Vc2Vertex *v)
{
    for(unsigned i=0;i<texture_count;i++)if(
        v->atlas_x==(textures[i].x+.5f)/VC2_ATLAS_W &&
        v->atlas_y==(textures[i].y+.5f)/VC2_ATLAS_H)
        return textures[i].surface;
    return NULL;
}
_Static_assert(VC2_ATLAS_W==HOTD_ATLAS_SIDE*HOTD_ATLAS_CELL && VC2_ATLAS_H==VC2_ATLAS_W,
    "Atlas allocator dimensions");

static int evict_texture(void)
{
    unsigned i,victim=texture_count;
    for(i=0;i<texture_count;i++)if(textures[i].last_used!=frame_no &&
        (victim==texture_count || textures[i].last_used<textures[victim].last_used))victim=i;
    if(victim==texture_count)return 0; /* Never evict a tile referenced by this frame. */
    Texture *t=textures+victim;
    hotd_atlas_free(&atlas_space,t->x-1,t->y-1,t->w+2,t->h+2);
    IDirectDrawSurface7_Release(t->surface);
    textures[victim]=textures[--texture_count];
    texture_evictions++;
    return 1;
}

static BYTE channel(DWORD value,DWORD mask)
{
    unsigned shift=0;
    if(!mask) return 255;
    while(!(mask&1)) {mask>>=1;shift++;}
    return (BYTE)(((value>>shift)&mask)*255u/mask);
}
static DWORD read_pixel(const BYTE *row,unsigned x,unsigned bytes)
{
    DWORD v=0;
    memcpy(&v,row+x*bytes,bytes);
    return v;
}

static DWORD pixel_bgra(DWORD value,const DDPIXELFORMAT *pf,const PALETTEENTRY *palette)
{
    if(pf->dwFlags&DDPF_PALETTEINDEXED8) {
        const PALETTEENTRY *p=palette+(value&255);
        return 0xFF000000u | ((DWORD)p->peRed<<16) | ((DWORD)p->peGreen<<8) | p->peBlue;
    }
    return ((DWORD)((pf->dwFlags&DDPF_ALPHAPIXELS) ? channel(value,pf->dwRGBAlphaBitMask):255)<<24) |
        ((DWORD)channel(value,pf->dwRBitMask)<<16) | ((DWORD)channel(value,pf->dwGBitMask)<<8) |
        channel(value,pf->dwBBitMask);
}

static int opaque_bitmap_alpha(IDirectDrawSurface7 *surface)
{
    DDSURFACEDESC2 desc={0};unsigned x,y;
    desc.dwSize=sizeof(desc);
    if(FAILED(IDirectDrawSurface7_Lock(surface,NULL,&desc,DDLOCK_WAIT,NULL)))return 0;
    int repaired=desc.ddpfPixelFormat.dwRGBBitCount==32 &&
        (desc.ddpfPixelFormat.dwFlags&(DDPF_RGB|DDPF_ALPHAPIXELS))==(DDPF_RGB|DDPF_ALPHAPIXELS) &&
        desc.ddpfPixelFormat.dwRGBAlphaBitMask==0xFF000000;
    if(repaired)for(y=0;y<desc.dwHeight;y++) {
        DWORD *row=(DWORD *)((BYTE *)desc.lpSurface+(LONG)y*desc.lPitch);
        for(x=0;x<desc.dwWidth;x++)row[x]|=0xFF000000;
    }
    IDirectDrawSurface7_Unlock(surface,NULL);
    return repaired;
}

static Texture *capture_texture(IDirect3DDevice7 *device)
{
    IDirectDrawSurface7 *surface=NULL;
    IDirectDrawPalette *pal=NULL;
    PALETTEENTRY entries[256];
    DDSURFACEDESC2 desc;
    DDCOLORKEY key;
    Texture *t;
    unsigned i,x,y,bytes,tx,ty;
    int has_key;
    if(FAILED(IDirect3DDevice7_GetTexture(device,0,&surface)) || !surface) return NULL;
    for(i=0;i<texture_count;i++) if(textures[i].surface==surface) {
        textures[i].last_used=frame_no;
        IDirectDrawSurface7_Release(surface); return textures+i;
    }
    if(texture_count>=2048 && !evict_texture()) {texture_failures++;IDirectDrawSurface7_Release(surface);return NULL;}
    memset(&desc,0,sizeof(desc));desc.dwSize=sizeof(desc);
    if(FAILED(IDirectDrawSurface7_Lock(surface,NULL,&desc,DDLOCK_WAIT|DDLOCK_READONLY,NULL))) {
        texture_failures++;
        IDirectDrawSurface7_Release(surface);return NULL;
    }
    bytes=desc.ddpfPixelFormat.dwRGBBitCount/8;
    if(!(desc.ddpfPixelFormat.dwFlags&(DDPF_RGB|DDPF_PALETTEINDEXED8)) || bytes<1 || bytes>4 ||
       !desc.dwWidth || !desc.dwHeight || desc.dwWidth+2>VC2_ATLAS_W || desc.dwHeight+2>VC2_ATLAS_H) goto fail;
    memset(entries,0,sizeof(entries));
    if(desc.ddpfPixelFormat.dwFlags&DDPF_PALETTEINDEXED8) {
        if(FAILED(IDirectDrawSurface7_GetPalette(surface,&pal))) goto fail;
        IDirectDrawPalette_GetEntries(pal,0,0,256,entries);
        IDirectDrawPalette_Release(pal);
    }
    while(!hotd_atlas_alloc(&atlas_space,desc.dwWidth+2,desc.dwHeight+2,&tx,&ty)) {
        if(!evict_texture())goto fail;
    }
    has_key=SUCCEEDED(IDirectDrawSurface7_GetColorKey(surface,DDCKEY_SRCBLT,&key));
    t=textures+texture_count++;
    t->surface=surface;t->x=tx+1;t->y=ty+1;t->w=desc.dwWidth;t->h=desc.dwHeight;t->last_used=frame_no;
    /* Reused tiles and their geometry must become visible together. */
    if(!atlas_dirty) {frame->seq++;frame->atlas_seq++;MemoryBarrier();atlas_dirty=1;}
    for(y=0;y<t->h;y++) {
        const BYTE *row=(const BYTE *)desc.lpSurface+(LONG)y*desc.lPitch;
        for(x=0;x<t->w;x++) {
            DWORD raw=read_pixel(row,x,bytes),color=pixel_bgra(raw,&desc.ddpfPixelFormat,entries);
            if(has_key && raw>=key.dwColorSpaceLowValue && raw<=key.dwColorSpaceHighValue) color&=0xFFFFFF;
            frame->atlas[(t->y+y)*VC2_ATLAS_W+t->x+x]=color;
        }
    }
    /* Duplicate edge texels so linear filtering cannot bleed into another tile. */
    for(y=0;y<t->h;y++) {
        uint32_t *row=frame->atlas+(t->y+y)*VC2_ATLAS_W;
        row[t->x-1]=row[t->x];row[t->x+t->w]=row[t->x+t->w-1];
    }
    memcpy(frame->atlas+(t->y-1)*VC2_ATLAS_W+t->x-1,
           frame->atlas+t->y*VC2_ATLAS_W+t->x-1,(t->w+2)*4);
    memcpy(frame->atlas+(t->y+t->h)*VC2_ATLAS_W+t->x-1,
           frame->atlas+(t->y+t->h-1)*VC2_ATLAS_W+t->x-1,(t->w+2)*4);
    IDirectDrawSurface7_Unlock(surface,NULL);
    if(texture_count<=10)logline("texture %u: %ux%u bits=%lu flags=%08lx",texture_count,t->w,t->h,
        (unsigned long)desc.ddpfPixelFormat.dwRGBBitCount,(unsigned long)desc.ddpfPixelFormat.dwFlags);
    /* shortcut: cached textures are immutable; add dirty tracking if a game surface changes in place. */
    return t; /* Keep one reference so reused COM addresses cannot select stale textures. */
fail:
    texture_failures++;
    IDirectDrawSurface7_Unlock(surface,NULL);IDirectDrawSurface7_Release(surface);return NULL;
}

static void transform_point(const float in[3],const D3DMATRIX *matrix,float out[3])
{
    const float *m=(const float *)matrix;
    unsigned j;
    for(j=0;j<3;j++)out[j]=in[0]*m[j]+in[1]*m[4+j]+in[2]*m[8+j]+m[12+j];
}

static void read_camera(IDirect3DDevice7 *device)
{
    IDirect3DDevice7_GetTransform(device,D3DTRANSFORMSTATE_WORLD,&world);
    IDirect3DDevice7_GetTransform(device,D3DTRANSFORMSTATE_VIEW,&view);
    IDirect3DDevice7_GetTransform(device,D3DTRANSFORMSTATE_PROJECTION,&projection);
    IDirect3DDevice7_GetViewport(device,&viewport);
    if(projection._34!=0 && isfinite(projection._11) && projection._11>0 && projection._22>0) {
        lens.fx=projection._11*320;lens.fy=projection._22*240;
        lens.cx=320;lens.cy=240;lens_valid=1;
    }
}

static HotdLighting draw_lighting;
typedef struct { DWORD colour,style,address,fog_colour; const HotdLighting *lighting; } DrawStyle;
static DWORD read_state(IDirect3DDevice7 *device,D3DRENDERSTATETYPE state,DWORD fallback)
{
    DWORD value=fallback;
    if(FAILED(IDirect3DDevice7_GetRenderState(device,state,&value)))return fallback;
    return value;
}
static float read_float_state(IDirect3DDevice7 *device,D3DRENDERSTATETYPE state,float fallback)
{
    DWORD bits;memcpy(&bits,&fallback,4);bits=read_state(device,state,bits);
    memcpy(&fallback,&bits,4);return fallback;
}
static void read_lighting(IDirect3DDevice7 *device,DWORD fvf)
{
    HotdLighting *s=&draw_lighting;
    memset(s,0,sizeof(*s));s->world=world;s->view=view;
    s->material.diffuse.r=s->material.diffuse.g=s->material.diffuse.b=s->material.diffuse.a=1;
    if((fvf&D3DFVF_POSITION_MASK)!=D3DFVF_XYZRHW)IDirect3DDevice7_GetMaterial(device,&s->material);
#define READ(field,state,value) s->field=read_state(device,D3DRENDERSTATE_##state,value)
    READ(lighting,LIGHTING,1);READ(colorvertex,COLORVERTEX,1);
    READ(diffuse_source,DIFFUSEMATERIALSOURCE,D3DMCS_COLOR1);READ(ambient_source,AMBIENTMATERIALSOURCE,D3DMCS_MATERIAL);
    READ(specular_source,SPECULARMATERIALSOURCE,D3DMCS_COLOR2);READ(emissive_source,EMISSIVEMATERIALSOURCE,D3DMCS_MATERIAL);
    READ(ambient,AMBIENT,0);READ(specular_enable,SPECULARENABLE,0);READ(local_viewer,LOCALVIEWER,1);
    READ(normalize_normals,NORMALIZENORMALS,0);READ(fog_enable,FOGENABLE,0);
    READ(fog_vertex_mode,FOGVERTEXMODE,D3DFOG_NONE);READ(fog_table_mode,FOGTABLEMODE,D3DFOG_NONE);
    READ(range_fog,RANGEFOGENABLE,0);
#undef READ
    s->fog_start=read_float_state(device,D3DRENDERSTATE_FOGSTART,0);
    s->fog_end=read_float_state(device,D3DRENDERSTATE_FOGEND,1);
    s->fog_density=read_float_state(device,D3DRENDERSTATE_FOGDENSITY,1);
    if(s->lighting && (fvf&D3DFVF_POSITION_MASK)!=D3DFVF_XYZRHW && device->lpVtbl->GetLightEnable && device->lpVtbl->GetLight)
        for(DWORD i=0;i<16;i++) {
            BOOL enabled=FALSE;D3DLIGHT7 light;
            if(SUCCEEDED(IDirect3DDevice7_GetLightEnable(device,i,&enabled)) && enabled &&
               SUCCEEDED(IDirect3DDevice7_GetLight(device,i,&light)))s->lights[s->light_count++]=light;
        }
    hotd_lighting_prepare(s);
}
static DrawStyle read_draw_style(IDirect3DDevice7 *device,DWORD fvf)
{
    DWORD blend=0,src=2,dst=1,zwrite=1,test=0,func=8,ref=0,alphaop=2,alphaarg=2;
    DWORD cull=D3DCULL_NONE;
    DWORD address_u=D3DTADDRESS_WRAP,address_v=D3DTADDRESS_WRAP;
    DrawStyle style={.colour=0xFFFFFFFF};
#define RS(name,out) IDirect3DDevice7_GetRenderState(device,D3DRENDERSTATE_##name,&out)
    RS(ALPHABLENDENABLE,blend);RS(SRCBLEND,src);RS(DESTBLEND,dst);RS(ZWRITEENABLE,zwrite);
    RS(ALPHATESTENABLE,test);RS(ALPHAFUNC,func);RS(ALPHAREF,ref);

    RS(CULLMODE,cull);
#undef RS
    IDirect3DDevice7_GetTextureStageState(device,0,D3DTSS_ALPHAOP,&alphaop);
    IDirect3DDevice7_GetTextureStageState(device,0,D3DTSS_ALPHAARG1,&alphaarg);
    IDirect3DDevice7_GetTextureStageState(device,0,D3DTSS_ADDRESSU,&address_u);
    IDirect3DDevice7_GetTextureStageState(device,0,D3DTSS_ADDRESSV,&address_v);
    style.address=hotd_address(address_u,address_v);
    if(!frame || !(frame->render_flags&HOTD_RENDER_UNLIT)) {
        read_lighting(device,fvf);style.lighting=&draw_lighting;
        DWORD fog=read_state(device,D3DRENDERSTATE_FOGCOLOR,0);
        style.fog_colour=(fog&0xFF00u)|((fog&255u)<<16)|((fog>>16)&255u);
    } else if((fvf&D3DFVF_POSITION_MASK)!=D3DFVF_XYZRHW && read_state(device,D3DRENDERSTATE_LIGHTING,0)) {
        DWORD colorvertex=read_state(device,D3DRENDERSTATE_COLORVERTEX,0);
        DWORD source=read_state(device,D3DRENDERSTATE_DIFFUSEMATERIALSOURCE,0);
        D3DMATERIAL7 material;
        if((!colorvertex || source==D3DMCS_MATERIAL || !(fvf&D3DFVF_DIFFUSE)) &&
           SUCCEEDED(IDirect3DDevice7_GetMaterial(device,&material)))
            style.colour=hotd_rgba(material.diffuse.r,material.diffuse.g,material.diffuse.b,material.diffuse.a);
    }
    style.style=hotd_style(blend,src,dst,zwrite,alphaop==D3DTOP_MODULATE ||
        (alphaop==D3DTOP_SELECTARG1 && (alphaarg&D3DTA_SELECTMASK)==D3DTA_DIFFUSE),test,func,ref) |
        ((cull&3u)<<HOTD_CULL_SHIFT);
    return style;
}

static int convert_vertex(const BYTE *v,DWORD fvf,Texture *tex,DrawStyle style,Vc2Vertex *out)
{
    unsigned pos=fvf&D3DFVF_POSITION_MASK,offset;
    DWORD colour=0xFFFFFFFF,specular=0;
    float p[3],tmp[3];
    const float *f=(const float *)v;
    if(pos==D3DFVF_XYZRHW) {
        if(!lens_valid || !viewport.dwWidth || !viewport.dwHeight ||
           !hotd_unproject(&lens,(f[0]-viewport.dwX)*640/viewport.dwWidth,
               (f[1]-viewport.dwY)*480/viewport.dwHeight,f[3],p)) return 0;
        offset=16;
    } else if(pos==D3DFVF_XYZ) {
        transform_point(f,&world,tmp);transform_point(tmp,&view,p);p[2]=-p[2];offset=12;
    } else return 0;
    if(!isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2]))return 0;
    out->x=p[0];out->y=p[1];out->z=p[2];
    if(fvf&D3DFVF_NORMAL)offset+=12;
    if(fvf&0x20)offset+=4;
    if(fvf&D3DFVF_DIFFUSE){memcpy(&colour,v+offset,4);offset+=4;}
    if(fvf&D3DFVF_SPECULAR){memcpy(&specular,v+offset,4);offset+=4;}
    if(style.lighting) {
        HotdLitVertex lit;
        hotd_light_vertex(style.lighting,f,(fvf&D3DFVF_NORMAL)?f+(pos==D3DFVF_XYZRHW?4:3):NULL,colour,specular,fvf,&lit);
        out->colour=hotd_rgba(lit.diffuse[0],lit.diffuse[1],lit.diffuse[2],lit.diffuse[3]);
        out->specular=hotd_rgba(lit.specular[0],lit.specular[1],lit.specular[2],0);
        out->fog_colour=style.fog_colour|(lit.fog_mode<<24);out->fog=lit.fog;
    } else {
        out->colour=(fvf&D3DFVF_DIFFUSE) ? (colour&0xFF00FF00u)|((colour&255)<<16)|((colour>>16)&255):style.colour;
        out->specular=out->fog_colour=0;out->fog=1;
    }
    out->style=style.style|capture_kind;
    out->address=style.address|(capture_gfile?HOTD_GFILE:0)|capture_player_hud;
    out->u=out->v=-1;out->depth=0;
    out->atlas_x=out->atlas_y=out->atlas_w=out->atlas_h=0;
    if(tex && (fvf&D3DFVF_TEXCOUNT_MASK)) {
        float uv[2];memcpy(uv,v+offset,8);
        if(!isfinite(uv[0])||!isfinite(uv[1]))return 0;
        out->u=uv[0];out->v=uv[1];
        out->atlas_x=(tex->x+0.5f)/VC2_ATLAS_W;
        out->atlas_y=(tex->y+0.5f)/VC2_ATLAS_H;
        out->atlas_w=(tex->w-1.0f)/VC2_ATLAS_W;
        out->atlas_h=(tex->h-1.0f)/VC2_ATLAS_H;
    }
    return 1;
}

static void trace_draw_state(IDirect3DDevice7 *device,DWORD fvf,Texture *tex,const void *data)
{
    static const D3DRENDERSTATETYPE rs[]={D3DRENDERSTATE_ALPHABLENDENABLE,D3DRENDERSTATE_SRCBLEND,
        D3DRENDERSTATE_DESTBLEND,D3DRENDERSTATE_ZWRITEENABLE,D3DRENDERSTATE_ALPHATESTENABLE,
        D3DRENDERSTATE_TEXTUREFACTOR,D3DRENDERSTATE_LIGHTING,D3DRENDERSTATE_COLORVERTEX,
        D3DRENDERSTATE_DIFFUSEMATERIALSOURCE,D3DRENDERSTATE_AMBIENT};
    static const D3DTEXTURESTAGESTATETYPE ts[]={D3DTSS_COLOROP,D3DTSS_COLORARG1,D3DTSS_COLORARG2,
        D3DTSS_ALPHAOP,D3DTSS_ALPHAARG1,D3DTSS_ALPHAARG2};
    static DWORD seen[32][19];
    static unsigned used;
    DWORD key[19]={0};D3DMATERIAL7 material={0};unsigned i;
    if(used==32 || frame_no%30)return;
    key[0]=fvf;key[1]=tex!=NULL;
    for(i=0;i<10;i++)IDirect3DDevice7_GetRenderState(device,rs[i],key+2+i);
    if(tex && !key[2])return;
    for(i=0;i<6;i++)IDirect3DDevice7_GetTextureStageState(device,0,ts[i],key+12+i);
    IDirect3DDevice7_GetMaterial(device,&material);memcpy(key+18,&material.diffuse.a,4);
    for(i=0;i<used;i++)if(!memcmp(key,seen[i],sizeof(key)))return;
    memcpy(seen[used++],key,sizeof(key));
    logline("render-state fvf=%lx tex=%lu blend=%lu src=%lu dst=%lu zwrite=%lu atest=%lu factor=%08lx lighting=%lu colorvertex=%lu matsrc=%lu ambient=%08lx colorop=%lu args=%lu,%lu alphaop=%lu args=%lu,%lu material=%g,%g,%g,%g first=%g,%g,%g",
        (unsigned long)fvf,(unsigned long)key[1],(unsigned long)key[2],(unsigned long)key[3],
        (unsigned long)key[4],(unsigned long)key[5],(unsigned long)key[6],(unsigned long)key[7],
        (unsigned long)key[8],(unsigned long)key[9],(unsigned long)key[10],(unsigned long)key[11],
        (unsigned long)key[12],(unsigned long)key[13],(unsigned long)key[14],(unsigned long)key[15],
        (unsigned long)key[16],(unsigned long)key[17],material.diffuse.r,material.diffuse.g,
        material.diffuse.b,material.diffuse.a,((const float *)data)[0],((const float *)data)[1],((const float *)data)[2]);
}

static void capture_draw(IDirect3DDevice7 *device,D3DPRIMITIVETYPE type,DWORD fvf,
                         const void *data,DWORD n,const WORD *indices,DWORD ni)
{
    unsigned stride=hotd_fvf_stride(fvf),i,total=indices?ni:n;
    Texture *texture;
    DrawStyle style;
    DWORD z_enable=0;
    HotdPauseRaster raster={.zfunc=D3DCMP_LESSEQUAL,.z_enable=D3DZB_TRUE,
        .mag_filter=D3DTFG_LINEAR,.min_filter=D3DTFN_LINEAR,.mip_filter=D3DTFP_NONE};
    Vc2Vertex recent_vertices[3];
    unsigned recent_indices[3]={~0u,~0u,~0u},recent_age[3]={0},vertex_clock=0;
    int hud=0,reuse=indices || type!=D3DPT_TRIANGLELIST;
    if(capture_skip || (input_ready && *(short *)(game_base+0x5A1A00)))return;
    calls++;
    if(!frame || !data || !stride || total<3)return;
    read_camera(device);
    for(i=0;i<fvf_count && fvf_logged[i]!=fvf;i++);
    if(i==fvf_count && fvf_count<32) {
        const float *v=data;
        fvf_logged[fvf_count++]=fvf;
        logline("draw fvf=%08lx stride=%u type=%u vertices=%lu first=%g,%g,%g,%g",
            (unsigned long)fvf,stride,type,(unsigned long)n,v[0],v[1],v[2],stride>=16?v[3]:0);
        logline("projection %g %g %g %g; viewport %lu %lu %lu %lu",projection._11,projection._22,
            projection._33,projection._34,(unsigned long)viewport.dwX,(unsigned long)viewport.dwY,
            (unsigned long)viewport.dwWidth,(unsigned long)viewport.dwHeight);
    }
    /* Native sprites/bitmaps are screen-space UI at any sort depth, in every mode. */
    hud=(fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW && !capture_kind && !capture_model;
    IDirect3DDevice7_GetRenderState(device,D3DRENDERSTATE_ZENABLE,&z_enable);
    if(!z_enable && !hud)return;
    raster.z_enable=(BYTE)z_enable;
    raster.zfunc=(BYTE)read_state(device,D3DRENDERSTATE_ZFUNC,D3DCMP_LESSEQUAL);
    raster.color_key=(BYTE)read_state(device,D3DRENDERSTATE_COLORKEYENABLE,FALSE);
    DWORD filter;
#define FILTER(name,field) if(SUCCEEDED(IDirect3DDevice7_GetTextureStageState(device,0,D3DTSS_##name,&filter)))raster.field=(BYTE)filter
    FILTER(MAGFILTER,mag_filter);FILTER(MINFILTER,min_filter);FILTER(MIPFILTER,mip_filter);
#undef FILTER
    texture=capture_texture(device);
    trace_draw_state(device,fvf,texture,data);
    style=read_draw_style(device,fvf);
    if(!hud && (capture_kind&HOTD_SHOT)) {
        const float origin[3]={0};float tmp[3],*anchor=shot_origins[capture_kind>>26];
        transform_point(origin,&world,tmp);transform_point(tmp,&view,anchor);anchor[2]=-anchor[2];
    }
    for(i=0;i+2<total;i+=(type==D3DPT_TRIANGLELIST?3:1)) {
        unsigned q[3]={i,i+1,i+2},k;
        Vc2Vertex tri[3];
        if(type==D3DPT_TRIANGLEFAN){q[0]=0;q[1]=i+1;q[2]=i+2;}
        else if(type==D3DPT_TRIANGLESTRIP && (i&1)){q[0]=i+1;q[1]=i;}
        else if(type!=D3DPT_TRIANGLELIST && type!=D3DPT_TRIANGLESTRIP)break;
        if((hud?hud_count:count)+3>(hud?HOTD_MAX_HUD_VERTS:VC2_MAX_VERTS)){dropped++;break;}
        for(k=0;k<3;k++) {
            unsigned idx=indices?indices[q[k]]:q[k];
            if(idx>=n)break;
            if(!reuse) {
                if(!convert_vertex((const BYTE *)data+idx*stride,fvf,texture,style,tri+k))break;
            } else {
                unsigned cached=0;
                while(cached<3 && recent_indices[cached]!=idx)cached++;
                if(cached<3)tri[k]=recent_vertices[cached];
                else {
                    if(!convert_vertex((const BYTE *)data+idx*stride,fvf,texture,style,tri+k))break;
                    cached=0;
                    for(unsigned c=1;c<3;c++)if(recent_age[c]<recent_age[cached])cached=c;
                    recent_indices[cached]=idx;recent_vertices[cached]=tri[k];
                }
                recent_age[cached]=++vertex_clock;
            }
            if(hud) {
                const float *v=(const float *)((const BYTE *)data+idx*stride);
                float sx=(v[0]-viewport.dwX)*640/viewport.dwWidth,sy=(v[1]-viewport.dwY)*480/viewport.dwHeight;
                tri[k].x=(sx/320-1)*HOTD_PANEL_HALF_WIDTH_M*frame->units_per_metre;
                tri[k].y=(1-sy/240)*HOTD_PANEL_HALF_HEIGHT_M*frame->units_per_metre;
                tri[k].z=-HOTD_PANEL_DISTANCE_M*frame->units_per_metre;
                tri[k].depth=1;
                tri[k].style=(tri[k].style&~(HOTD_WORLD|HOTD_SHOT|HOTD_SHOT_GROUP_MASK|HOTD_CULL_MASK))|HOTD_NO_ZWRITE;
            }
        }
        if(k!=3){dropped++;continue;}
        if(hud){hud_raster[hud_count/3]=raster;memcpy(hud_vertices+hud_count,tri,sizeof(tri));hud_count+=3;}
        else {desktop_raster[count/3]=raster;memcpy(vertices+count,tri,sizeof(tri));count+=3;}
    }
}

static void capture_screen(IDirect3DDevice7 *device)
{
    IDirectDrawSurface7 *surface=NULL;
    DDSURFACEDESC2 desc;
    unsigned w,h,x,y,bytes;
    if(FAILED(IDirect3DDevice7_GetRenderTarget(device,&surface)) || !surface)return;
    memset(&desc,0,sizeof(desc));desc.dwSize=sizeof(desc);
    if(FAILED(IDirectDrawSurface7_Lock(surface,NULL,&desc,DDLOCK_WAIT|DDLOCK_READONLY,NULL)))goto done;
    bytes=desc.ddpfPixelFormat.dwRGBBitCount/8;
    if(!(desc.ddpfPixelFormat.dwFlags&DDPF_RGB) || bytes<2 || bytes>4)goto unlock;
    w=desc.dwWidth;h=desc.dwHeight;
    if(w>VC2_SCREEN_MAX_W)w=VC2_SCREEN_MAX_W;
    if(h>VC2_SCREEN_MAX_H)h=VC2_SCREEN_MAX_H;
    frame->screen_seq++;MemoryBarrier();
    for(y=0;y<h;y++) {
        const BYTE *row=(const BYTE *)desc.lpSurface+(LONG)(y*desc.dwHeight/h)*desc.lPitch;
        for(x=0;x<w;x++)frame->screen[y*w+x]=pixel_bgra(read_pixel(row,x*desc.dwWidth/w,bytes),&desc.ddpfPixelFormat,NULL);
    }
    frame->screen_w=w;frame->screen_h=h;frame->screen_ok=1;frame->screen_frame++;
    MemoryBarrier();frame->screen_seq++;
unlock: IDirectDrawSurface7_Unlock(surface,NULL);
done: IDirectDrawSurface7_Release(surface);
}

typedef DWORD (__cdecl *NativeDraw)(const void *,DWORD);
typedef int (__cdecl *NativeModel)(const BYTE *,int);
static NativeModel native_model_original,native_model_alt_original;
typedef void (__cdecl *NativeSubmit)(const BYTE *);
typedef void (__cdecl *NativeSubmitAlt)(const BYTE *,float);
static NativeSubmit native_submit_original;
static NativeSubmit native_sprite_submit_original;
static NativeSubmit native_bitmap_original,native_sprite_draw_original;
static NativeSubmitAlt native_submit_alt_original;
static int (__cdecl *native_visibility_original)(float,float,float,float);
static HRESULT (WINAPI *original_sphere_visibility)(IDirect3DDevice7 *,D3DVECTOR *,D3DVALUE *,DWORD,DWORD,DWORD *);

static DWORD capture_render_flags(void)
{return visibility_flags_latched?visibility_frame_flags:frame?frame->render_flags:0;}
static int capture_world_enabled(void)
{return visibility_ready && frame && (capture_render_flags()&HOTD_RENDER_EXPAND_WORLD);}
static HotdNativeQueue native_capture_queue(void)
{
    HotdNativeQueue q;
    q.list=*(DWORD *)(game_base+0x3E78C4);q.cursor=*(DWORD *)(game_base+0x3E78C0);
    q.count=*(DWORD *)(game_base+0x3E793C);q.capacity=*(DWORD *)(game_base+0x3E78C8);
    return q;
}
static int native_queue_room(void)
{return hotd_native_queue_room(native_capture_queue(),(uintptr_t)game_base+0x3E814C);}
static int native_stage_task(void)
{
    const DWORD *task=*(const DWORD **)(game_base+0x3DF8B0);
    return task && task[0]==(DWORD)(ULONG_PTR)(game_base+0x1260);
}
static int native_gfile_profile(void)
{
    static const BYTE story[]={0x8B,0x44,0x24,0x04,0x83,0xEC,0x4C,0x53,0x8B,0x98,0x0C,0x13,0,0,0x55,0x8B};
    static const BYTE boss[]={0x83,0xEC,0x44,0x53,0x55,0x8B,0x6C,0x24,0x50,0x33,0xDB,0x56,0x57,0x66,0x8B,0x8D};
    return !memcmp(game_base+0x37AC0,story,sizeof(story)) && !memcmp(game_base+0x24900,boss,sizeof(boss));
}
static int native_gfile_task(void)
{
    if(!gfile_ready)return 0;
    const DWORD *task=*(const DWORD **)(game_base+0x3DF8B0);
    return task && (task[0]==(DWORD)(ULONG_PTR)(game_base+0x37AC0) ||
        task[0]==(DWORD)(ULONG_PTR)(game_base+0x24900));
}
static int native_stage_submission(const BYTE *record)
{
    DWORD model,black;
    if(!capture_world_enabled() || !native_queue_room() || stage_records.overflow || stage_records.count==HOTD_STAGE_RECORDS ||
       !native_stage_task())return 0;
    memcpy(&model,record+0xC,sizeof(model));
    memcpy(&black,game_base+0x5A66A4+0x93E*16,sizeof(black));
    return model && model!=black;
}
static void native_submit_model(const BYTE *record,float alpha,int alternate)
{
    const BYTE *previous=visibility_submit_record;
    const BYTE *previous_gfile=gfile_immediate;
    int previous_stage=visibility_submit_stage,stage=0,expanded=capture_world_enabled();
    int gfile=native_gfile_task();
    HotdNativeQueue before={0};
    if(gfile)before=native_capture_queue();
    if(expanded) {
        before=native_capture_queue();
        if((*(const DWORD *)record&0x80000000u) &&
           !hotd_native_queue_room(before,(uintptr_t)game_base+0x3E814C)) {
            visibility_queue_drops++;return;
        }
        stage=native_stage_submission(record);
    }
    visibility_submit_record=record;visibility_submit_stage=stage;
    gfile_immediate=gfile?record:NULL;
    if(alternate)native_submit_alt_original(record,alpha);else native_submit_original(record);
    visibility_submit_record=previous;visibility_submit_stage=previous_stage;
    gfile_immediate=previous_gfile;
    if(stage || gfile) {
        HotdNativeQueue after=native_capture_queue();
        if(hotd_native_queue_appended(before,after,(uintptr_t)game_base+0x3E814C) &&
           *(const DWORD *)(after.list+before.count*4)==before.cursor) {
            if(stage)hotd_remember_stage(&stage_records,(const BYTE *)before.cursor);
            if(gfile)hotd_remember_stage(&gfile_records,(const BYTE *)before.cursor);
        }
    }
}
static void __cdecl native_submit(const BYTE *record)
{native_submit_model(record,0,0);}
static void __cdecl native_submit_alt(const BYTE *record,float alpha)
{native_submit_model(record,alpha,1);}
static void __cdecl native_sprite_submit(const BYTE *record)
{
    const BYTE *previous=crosshair_immediate,*previous_hud=player_hud_immediate;
    unsigned hud_group=player_hud_owner,previous_group=player_hud_immediate_group;
    int owned=crosshair_ready && crosshair_definition!=NULL;
    int deferred=(*(const DWORD *)record&0x80000000u)!=0,tracked=0;
    HotdNativeQueue before={0};
    if(capture_world_enabled() && (*(const DWORD *)record&0x80000000u) && !native_queue_room()) {
        visibility_queue_drops++;return;
    }
    if((owned || hud_group) && deferred) {
        before=native_capture_queue();
        tracked=hotd_native_queue_room(before,(uintptr_t)game_base+0x3E814C);
    }
    crosshair_immediate=owned && !deferred?record:NULL;
    player_hud_immediate=hud_group && !deferred?record:NULL;player_hud_immediate_group=hud_group;
    native_sprite_submit_original(record);
    crosshair_immediate=previous;player_hud_immediate=previous_hud;player_hud_immediate_group=previous_group;
    if(tracked) {
        HotdNativeQueue after=native_capture_queue();
        if(hotd_native_queue_valid(after,(uintptr_t)game_base+0x3E814C) &&
           after.list==before.list && after.capacity==before.capacity && after.count==before.count+1 &&
           after.cursor==before.cursor+HOTD_SPRITE_RECORD_SIZE &&
           *(const DWORD *)(after.list+before.count*4)==before.cursor)
        {
            if(owned)hotd_remember_sprite(&crosshair_records,(const BYTE *)before.cursor);
            if(hud_group)hotd_remember_sprite(player_hud_records+(hud_group>>HOTD_PLAYER_HUD_SHIFT)-1,(const BYTE *)before.cursor);
        }
    }
}

static void native_bitmap_draw(const BYTE *definition,uintptr_t caller)
{
    const BYTE *previous=crosshair_definition;
    crosshair_definition=crosshair_ready && hotd_crosshair_definition(game_base,definition,caller)?definition:NULL;
    native_bitmap_original(definition);
    crosshair_definition=previous;
}
static void __cdecl native_bitmap(const BYTE *definition)
{native_bitmap_draw(definition,(uintptr_t)__builtin_return_address(0)-(uintptr_t)game_base);}
static void __cdecl native_sprite_draw(const BYTE *record)
{
    unsigned previous_skip=capture_skip,previous_depth=crosshair_draw_depth,previous_base=crosshair_draw_base_skip;
    unsigned base=crosshair_draw_depth?crosshair_draw_base_skip:capture_skip,previous_hud=capture_player_hud;
    capture_player_hud=0;
    if(player_hud_ready) {
        if(player_hud_immediate==record)capture_player_hud=player_hud_immediate_group;
        for(unsigned g=0;g<4;g++)if(hotd_take_sprite(player_hud_records+g,record))
            capture_player_hud=(g+1)<<HOTD_PLAYER_HUD_SHIFT;
    }
    int owned=0;
    if(crosshair_ready) {
        owned=hotd_take_sprite(&crosshair_records,record);
        owned|=crosshair_immediate==record;
    }
    if(owned) {
        static int logged;
        if(!logged){logline("native gameplay crosshair omitted from VR capture; desktop draw retained");logged=1;}
    }
    crosshair_draw_depth++;crosshair_draw_base_skip=base;capture_skip=base|owned;
    native_sprite_draw_original(record);
    capture_skip=previous_skip;crosshair_draw_depth=previous_depth;crosshair_draw_base_skip=previous_base;
    capture_player_hud=previous_hud;
}

static void __cdecl native_hud_ammo(const BYTE *task)
{
    unsigned previous=player_hud_owner;
    player_hud_owner=player_hud_ready?hotd_player_hud_group(task,0):0;
    native_hud_ammo_original(task);player_hud_owner=previous;
}
static DWORD __cdecl native_hud_lives(const BYTE *task)
{
    unsigned previous=player_hud_owner;
    player_hud_owner=player_hud_ready?hotd_player_hud_group(task,1):0;
    DWORD result=native_hud_lives_original(task);player_hud_owner=previous;return result;
}
static int native_stage_visibility(int visible,float radius,uintptr_t caller)
{
    return visible || (capture_world_enabled() && native_queue_room() &&
        !stage_records.overflow && stage_records.count<HOTD_STAGE_RECORDS && isfinite(radius) && radius>0 &&
        hotd_stage_visibility_caller(caller));
}
static int __cdecl native_visibility(float x,float y,float z,float radius)
{
    int visible=native_visibility_original(x,y,z,radius);
    return native_stage_visibility(visible,radius,(uintptr_t)__builtin_return_address(0)-(uintptr_t)game_base);
}
static HRESULT WINAPI sphere_visibility(IDirect3DDevice7 *self,D3DVECTOR *centres,D3DVALUE *radii,
                                       DWORD n,DWORD flags,DWORD *visibility)
{
    HRESULT hr=original_sphere_visibility(self,centres,radii,n,flags,visibility);
    if(SUCCEEDED(hr) && capture_model && capture_visibility_planes)
        for(DWORD i=0;i<n;i++)visibility[i]&=~capture_visibility_planes;
    return hr;
}
static unsigned native_model_kind(const BYTE *record)
{
    DWORD model;
    float z;
    memcpy(&model,record+0xC,4);memcpy(&z,record+0x34+14*4,4);
    /* Camera-plane flashes relocate; traveling tracers keep their depth but never block aim. */
    if(model)
        for(unsigned bank=0;bank<4;bank++) {
            unsigned first=*(unsigned short *)(game_base+0x179F78+bank*2);
            for(unsigned i=0;i<9;i++)
                if(model==*(DWORD *)(game_base+0x5A66A4+(first+i)*16))
                    return fabsf(fabsf(z)-1.0f)<.01f ? HOTD_SHOT|((shot_group++&63u)<<26):0;
        }
    return HOTD_WORLD;
}
static int native_letterbox(const BYTE *record)
{
    DWORD model,bar;
    float matrix[16];
    memcpy(&model,record+0xC,4);memcpy(&bar,game_base+0x5A66A4+0x93E*16,4);
    if(!bar || model!=bar)return 0;
    memcpy(matrix,record+0x34,sizeof(matrix));
    /* The same black model also supplies pause dimming and full-screen fades. */
    if(!(fabsf(matrix[13])>=.349f && fabsf(matrix[13])<=.451f) ||
       !(fabsf(fabsf(matrix[14])-1)<.0001f))return 0;
    for(unsigned i=0;i<16;i++)if(i!=13 && i!=14 &&
        !(fabsf(matrix[i]-(i%5==0?1.0f:0.0f))<.0001f))return 0;
    return 1;
}
static int native_model_draw(const BYTE *record,int pass,NativeModel original)
{
    unsigned previous=capture_kind,previous_skip=capture_skip,previous_model=capture_model;
    int previous_gfile=capture_gfile;
    DWORD previous_planes=capture_visibility_planes;
    int result,queued_stage=0,expanded=capture_world_enabled();
    capture_model=1;
    capture_skip|=native_letterbox(record);
    if(capture_skip && !previous_skip) {
        static unsigned logged;
        if(!logged++){logline("native letterbox bars omitted from VR capture");}
    }
    capture_gfile=(gfile_immediate==record) || (pass==1 && hotd_take_stage(&gfile_records,record));
    capture_kind=capture_gfile?0:native_model_kind(record);
    if(expanded && pass==1)queued_stage=hotd_take_stage(&stage_records,record);
    capture_visibility_planes=visibility_ready?hotd_visibility_expand(capture_render_flags(),
        capture_kind==HOTD_WORLD && !capture_skip &&
            ((visibility_submit_record==record && visibility_submit_stage) || queued_stage),
        original==native_model_alt_original):0;
    result=original(record,pass);
    /* Immediate draws still run; only their unsafe deferred append is suppressed. */
    if(expanded && pass==0 && visibility_submit_record==record && (BYTE)result && !native_queue_room()) {
        visibility_queue_drops++;result=0;
    }
    capture_kind=previous;capture_skip=previous_skip;capture_model=previous_model;
    capture_gfile=previous_gfile;
    capture_visibility_planes=previous_planes;
    return result;
}
static int __cdecl native_model(const BYTE *record,int pass)
{return native_model_draw(record,pass,native_model_original);}
static int __cdecl native_model_alt(const BYTE *record,int pass)
{return native_model_draw(record,pass,native_model_alt_original);}
static NativeDraw native_strip_original,native_list_original,native_tl_original;
static DWORD native_draw(const void *v,DWORD n,D3DPRIMITIVETYPE type,DWORD fvf,NativeDraw original)
{
    IDirect3DDevice7 *device=*(IDirect3DDevice7 **)(game_base+0x3DEB74);
    if(device){active_device=device;capture_draw(device,type,fvf,v,n,NULL,0);}
    return original(v,n);
}
static DWORD __cdecl native_strip(const void *v,DWORD n)
{return native_draw(v,n,D3DPT_TRIANGLESTRIP,0x112,native_strip_original);}
static DWORD __cdecl native_list(const void *v,DWORD n)
{return native_draw(v,n,D3DPT_TRIANGLELIST,0x112,native_list_original);}
static DWORD __cdecl native_tl(const void *v,DWORD n)
{return native_draw(v,n,D3DPT_TRIANGLESTRIP,0x1C4,native_tl_original);}

/* Callers supply whole, non-relative instructions (5-byte damage or 9-byte draw/input prologues). */
static int detour(DWORD rva,const BYTE *signature,unsigned n,void *hook,void **original)
{
    BYTE *at=game_base+rva,*trampoline,patch[9];DWORD jump;
    if(n<5 || n>sizeof(patch) || memcmp(at,signature,n))return 0;
    trampoline=VirtualAlloc(NULL,n+5,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    if(!trampoline)return 0;
    memcpy(trampoline,at,n);trampoline[n]=0xE9;
    jump=(DWORD)((at+n)-(trampoline+n+5));memcpy(trampoline+n+1,&jump,4);
    FlushInstructionCache(GetCurrentProcess(),trampoline,n+5);
    memset(patch,0x90,n);patch[0]=0xE9;
    jump=(DWORD)((BYTE *)hook-(at+5));memcpy(patch+1,&jump,4);
    *original=trampoline;
    if(!write_code(at,patch,n)){VirtualFree(trampoline,0,MEM_RELEASE);*original=NULL;return 0;}
    return 1;
}
static int install_reload_input(void)
{
    const DWORD sites[]={0x14A9A,0x14DD0};
    const BYTE signature[]={0xA1,0x28,0x90,0x9C,0x00};
    DWORD address=(DWORD)(uintptr_t)&reload_input;
    /* Only the two weapon reload reads change source; skip and menu reads stay native. */
    for(unsigned i=0;i<2;i++)
        if(memcmp(game_base+sites[i],signature,sizeof(signature)))return 0;
    for(unsigned i=0;i<2;i++)
        if(!write_code(game_base+sites[i]+1,(const BYTE *)&address,sizeof(address)))return 0;
    return 1;
}

static int install_invincibility(void)
{
    static const BYTE signature[]={0x56,0x8B,0x74,0x24,0x08}; /* push esi; mov esi,[esp+8] */
    if(!native_cheat_profile(game_base) || memcmp(game_base+0x15300,signature,sizeof(signature)) ||
       memcmp(game_base+0x15430,signature,sizeof(signature)))return 0;
    return detour(0x15300,signature,sizeof(signature),native_damage,(void **)&native_damage_original) &&
        detour(0x15430,signature,sizeof(signature),native_damage_forced,(void **)&native_damage_forced_original);
}

static int install_capture_visibility(void)
{
    static const BYTE submit[]={0x56,0x8B,0x74,0x24,0x08};
    static const BYTE sphere[]={0x83,0xEC,0x38,0xD9,0x05,0xCC,0x49,0x4C,0x00};
    /* Verify all call sites before any hook can enable expanded submission. */
    if(!hotd_visibility_profile(game_base))return 0;
    visibility_hooks_ready=detour(0xA7E50,submit,sizeof(submit),native_submit,(void **)&native_submit_original) &&
        detour(0xA8390,submit,sizeof(submit),native_submit_alt,(void **)&native_submit_alt_original) &&
        detour(0xA7A40,submit,sizeof(submit),native_sprite_submit,(void **)&native_sprite_submit_original) &&
        detour(0x6310,sphere,sizeof(sphere),native_visibility,(void **)&native_visibility_original);
    return visibility_hooks_ready;
}
static int install_player_hud(void)
{
    static const BYTE ammo[]={0x83,0xEC,0x14,0x8B,0x44,0x24,0x18};
    static const BYTE lives[]={0x8B,0x44,0x24,0x04,0x56,0x57};
    return crosshair_ready && hotd_player_hud_profile(game_base) &&
        detour(0x177D0,ammo,sizeof(ammo),native_hud_ammo,(void **)&native_hud_ammo_original) &&
        detour(0x174A0,lives,sizeof(lives),native_hud_lives,(void **)&native_hud_lives_original);
}

static int install_crosshair_capture(int profile_valid)
{
    static const BYTE bitmap[]={0x8B,0x44,0x24,0x04,0x83,0xEC,0x38,0x8B,0x08};
    static const BYTE sprite[]={0x81,0xEC,0xB0,0,0,0,0x53,0x55,0x56};
    if(!profile_valid || !native_sprite_submit_original)return 0;
    crosshair_ready=detour(0x1C630,bitmap,sizeof(bitmap),native_bitmap,(void **)&native_bitmap_original) &&
        detour(0xA7AB0,sprite,sizeof(sprite),native_sprite_draw,(void **)&native_sprite_draw_original);
    return crosshair_ready;
}

static HRESULT (WINAPI *original_present)(void *,DWORD);
static HRESULT (WINAPI *original_begin)(IDirect3DDevice7 *);
static HRESULT (WINAPI *original_end)(IDirect3DDevice7 *);
static HotdNativeMenu native_menu;
static HRESULT WINAPI native_present(void *self,DWORD flags)
{
    publish_frame();
    if(active_device && hotd_native_menu_visible(frame)) {
        HRESULT hr=original_begin(active_device);
        if(SUCCEEDED(hr)) {
            hr=hotd_native_menu_draw(active_device,frame,&native_menu);
            HRESULT ended=original_end(active_device);if(SUCCEEDED(hr))hr=ended;
        }
        static int reported;
        if(FAILED(hr) && !reported){reported=1;logline("native menu render failed: %08lx",(unsigned long)hr);}
    }
    return original_present(self,flags);
}

static HRESULT WINAPI begin_scene(IDirect3DDevice7 *self)
{
    if(!visibility_flags_latched) {
        visibility_frame_flags=frame?frame->render_flags:0;visibility_flags_latched=1;
    }
    active_device=self;update_input();update_audio_levels();return original_begin(self);
}
static HRESULT WINAPI end_scene(IDirect3DDevice7 *self)
{
    if(pause_scene_ready && input_ready && *(short *)(game_base+0x5A1A00)) {
        HRESULT hr=hotd_pause_render(self,frame,&pause_projection,&pause_viewport,pause_raster,pause_texture);
        static int reported;
        if(FAILED(hr) && !reported){reported=1;logline("desktop pause replay failed: %08lx",(unsigned long)hr);}
    }
    return original_end(self);
}
static BOOL CALLBACK find_game_window(HWND hwnd,LPARAM unused)
{
    DWORD pid=0;char title[128];RECT client,outer;
    (void)unused;
    GetWindowThreadProcessId(hwnd,&pid);
    if(pid!=GetCurrentProcessId())return TRUE;
    GetWindowTextA(hwnd,title,sizeof(title));
    if(strcmp(title,"The House of the Dead 2"))return TRUE;
    game_window=hwnd;
    GetClientRect(hwnd,&client);GetWindowRect(hwnd,&outer);
    if(client.right!=640 || client.bottom!=480)
        SetWindowPos(hwnd,NULL,0,0,outer.right-outer.left+640-client.right,
            outer.bottom-outer.top+480-client.bottom,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    return FALSE;
}
static unsigned native_active_players(void)
{
    unsigned active=0;
    if(input_ready)for(unsigned p=0;p<2;p++)
        if(hotd_player_active(*(DWORD *)(game_base+0x5C8E98),
            *(short *)(game_base+0x5A5C62+p*0x130)))active|=1u<<p;
    return active;
}
static void publish_frame(void)
{
    if(!frame || !active_device)return;
    int paused=input_ready && *(short *)(game_base+0x5A1A00)!=0;
    int retain_scene=paused && pause_scene_ready;
    frame_no++;
    if(!IsWindow(game_window))EnumWindows(find_game_window,0);
    if(!paused)capture_screen(active_device);
    if(!atlas_dirty)frame->seq++;
    MemoryBarrier();
    if(!retain_scene) {
        memcpy(frame->verts,vertices,count*sizeof(vertices[0]));
        unsigned hud_n=hud_count<VC2_MAX_VERTS-count?hud_count:VC2_MAX_VERTS-count;
        dropped+=(hud_count-hud_n)/3;
        memcpy(frame->verts+count,hud_vertices,hud_n*sizeof(hud_vertices[0]));
        memcpy(pause_raster,desktop_raster,count/3*sizeof(*pause_raster));
        memcpy(pause_raster+count/3,hud_raster,hud_n/3*sizeof(*pause_raster));
        frame->vertex_count=count+hud_n;
        frame->reserved[0]=count;frame->reserved[2]=count+1;
        frame->lens=lens;frame->lens_valid=lens_valid;
        memcpy(frame->shot_origins,shot_origins,sizeof(shot_origins));
        pause_scene_ready=input_ready && !paused && *(DWORD *)(game_base+0x5C8E98)==6 && count>0;
        if(pause_scene_ready){pause_projection=projection;pause_viewport=viewport;}
    }
    frame->reserved[1]=paused;frame->frame=frame_no;
    frame->draw_calls=calls;frame->dropped_triangles=dropped;frame->texture_count=texture_count;
    frame->active_players=native_active_players();
    checkpoint_locations(frame->active_players);
    memcpy(frame->shot_events,shot_events,sizeof(shot_events));
    frame->heartbeat=GetTickCount();frame->game_hwnd=(DWORD)(ULONG_PTR)game_window;
    if(atlas_dirty){frame->atlas_frame++;MemoryBarrier();frame->atlas_seq++;atlas_dirty=0;}
    MemoryBarrier();frame->seq++;
    checkpoint_manual(frame->active_players);
    checkpoint_record(frame->active_players);
    checkpoint_refresh_saved();
    unsigned pressure=stage_records.overflow || visibility_queue_drops;
    int report=frame_no==1 || frame_no%300==0 || texture_failures ||
        (pressure!=visibility_pressure && frame_no-visibility_last_pressure_log>=60);
    if(report)logline("frame=%u draws=%u vertices=%u dropped=%u textures=%u evictions=%u texture_failures=%u lens=%u screen=%u stage_records=%u stage_overflow=%u queue_drops=%u",
        frame_no,calls,count,dropped,texture_count,texture_evictions,texture_failures,lens_valid,frame->screen_ok,
        stage_records.accepted,stage_records.overflow,visibility_queue_drops);
    if(report)visibility_last_pressure_log=frame_no;
    visibility_pressure=pressure;
    texture_failures=0;
    count=hud_count=calls=dropped=shot_group=0;
    memset(&stage_records,0,sizeof(stage_records));memset(&crosshair_records,0,sizeof(crosshair_records));
    memset(&gfile_records,0,sizeof(gfile_records));memset(player_hud_records,0,sizeof(player_hud_records));
    visibility_queue_drops=0;visibility_flags_latched=0;
}
static HRESULT (WINAPI *original_create_device)(IDirect3D7 *,REFCLSID,IDirectDrawSurface7 *,IDirect3DDevice7 **);
static HRESULT WINAPI create_device(IDirect3D7 *self,REFCLSID cls,IDirectDrawSurface7 *target,IDirect3DDevice7 **out)
{
    HRESULT hr=original_create_device(self,cls,target,out);
    logline("CreateDevice hr=%08lx",(unsigned long)hr);
    if(SUCCEEDED(hr) && *out) {
        IDirect3DDevice7Vtbl *vt=(IDirect3DDevice7Vtbl *)(*out)->lpVtbl;
#define HOOK(member,fun,original) replace_pointer((void **)&vt->member,fun,(void **)&original)
        HOOK(BeginScene,begin_scene,original_begin);HOOK(EndScene,end_scene,original_end);
        visibility_ready=visibility_hooks_ready && HOOK(ComputeSphereVisibility,sphere_visibility,original_sphere_visibility);
        logline("native capture visibility policy: %s",visibility_ready?"ready":"unavailable; native culling retained");
#undef HOOK
    }
    return hr;
}

static HRESULT (WINAPI *original_query)(IDirectDraw7 *,REFIID,void **);
static HRESULT (WINAPI *original_coop)(IDirectDraw7 *,HWND,DWORD);
static HRESULT WINAPI query_interface(IDirectDraw7 *self,REFIID iid,void **out)
{
    HRESULT hr=original_query(self,iid,out);
    if(SUCCEEDED(hr) && *out && IsEqualGUID(iid,&IID_IDirect3D7)) {
        IDirect3D7Vtbl *vt=(IDirect3D7Vtbl *)((IDirect3D7 *)*out)->lpVtbl;
        logline("Direct3D7 CreateDevice hook: %d",replace_pointer((void **)&vt->CreateDevice,create_device,(void **)&original_create_device));
    }
    return hr;
}
static HRESULT WINAPI cooperative_level(IDirectDraw7 *self,HWND hwnd,DWORD flags)
{
    if(hwnd)game_window=hwnd;
    logline("SetCooperativeLevel hwnd=%p flags=%08lx",hwnd,(unsigned long)flags);
    return original_coop(self,hwnd,flags);
}
static HRESULT (WINAPI *original_release_dc)(IDirectDrawSurface7 *,HDC);
static HRESULT WINAPI release_bitmap_dc(IDirectDrawSurface7 *self,HDC dc)
{
    DWORD caller=(DWORD)(uintptr_t)__builtin_return_address(0)-(DWORD)(uintptr_t)game_base;
    HRESULT hr=original_release_dc(self,dc);
    /* GDI BitBlt clears alpha; these two native loaders upload opaque BMP artwork. */
    if(SUCCEEDED(hr) && hotd_bitmap_dc_caller(caller))
        logline("native bitmap upload: caller=%lx opaque32=%d",(unsigned long)caller,opaque_bitmap_alpha(self));
    return hr;
}
static HRESULT (WINAPI *original_create_surface)(IDirectDraw7 *,DDSURFACEDESC2 *,IDirectDrawSurface7 **,IUnknown *);
static HRESULT WINAPI create_surface(IDirectDraw7 *self,DDSURFACEDESC2 *desc,IDirectDrawSurface7 **out,IUnknown *outer)
{
    HRESULT hr=original_create_surface(self,desc,out,outer);
    if(SUCCEEDED(hr) && *out) {
        IDirectDrawSurface7Vtbl *vt=(IDirectDrawSurface7Vtbl *)(*out)->lpVtbl;
        replace_pointer((void **)&vt->ReleaseDC,release_bitmap_dc,(void **)&original_release_dc);
    }
    return hr;
}
static HRESULT (WINAPI *original_ddraw_create)(GUID *,void **,REFIID,IUnknown *);
static HRESULT WINAPI ddraw_create(GUID *guid,void **out,REFIID iid,IUnknown *outer)
{
    HRESULT hr=original_ddraw_create(guid,out,iid,outer);
    logline("DirectDrawCreateEx hr=%08lx iid=%08lx",(unsigned long)hr,(unsigned long)iid->Data1);
    if(SUCCEEDED(hr) && *out && IsEqualGUID(iid,&IID_IDirectDraw7)) {
        IDirectDraw7Vtbl *vt=(IDirectDraw7Vtbl *)((IDirectDraw7 *)*out)->lpVtbl;
        replace_pointer((void **)&vt->QueryInterface,query_interface,(void **)&original_query);
        replace_pointer((void **)&vt->SetCooperativeLevel,cooperative_level,(void **)&original_coop);
        replace_pointer((void **)&vt->CreateSurface,create_surface,(void **)&original_create_surface);
    }
    return hr;
}

static int hook_import(const char *dll,const char *name,void *hook,void **original)
{
    IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER *)game_base;
    IMAGE_NT_HEADERS32 *nt=(IMAGE_NT_HEADERS32 *)(game_base+dos->e_lfanew);
    DWORD rva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *imp=(IMAGE_IMPORT_DESCRIPTOR *)(game_base+rva);
    if(!rva)return 0;
    for(;imp->Name;imp++)if(!_stricmp((char *)game_base+imp->Name,dll)) {
        IMAGE_THUNK_DATA32 *names=(IMAGE_THUNK_DATA32 *)(game_base+imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA32 *iat=(IMAGE_THUNK_DATA32 *)(game_base+imp->FirstThunk);
        if(!imp->OriginalFirstThunk)return 0;
        for(;names->u1.AddressOfData;names++,iat++)if(!IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) {
            IMAGE_IMPORT_BY_NAME *entry=(IMAGE_IMPORT_BY_NAME *)(game_base+names->u1.AddressOfData);
            if(!strcmp((char *)entry->Name,name))return replace_pointer((void **)&iat->u1.Function,hook,original);
        }
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void *reserved)
{
    char path[MAX_PATH],*slash;
    static const BYTE xsig[]={0x66,0x89,0x46,0x04};
    static const BYTE ysig[]={0x66,0x89,0x46,0x06};
    (void)reserved;
    if(reason!=DLL_PROCESS_ATTACH)return TRUE;
    DisableThreadLibraryCalls(module);
    GetModuleFileNameA(module,path,MAX_PATH);slash=strrchr(path,'\\');if(!slash)return FALSE;
    strcpy(slash+1,"HOTD2Bridge.log");logfile=fopen(path,"w");
    strcpy(slash+1,"HOTD2VR.ini");strcpy(ini_path,path);
    game_base=(BYTE *)GetModuleHandleA(NULL);
    mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(Vc2Frame),VC2_SHARE_NAME);
    if(!mapping || GetLastError()==ERROR_ALREADY_EXISTS){logline("bridge mapping unavailable or another session active");return FALSE;}
    frame=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Vc2Frame));
    if(!frame)return FALSE;
    memset(frame,0,sizeof(*frame));frame->magic=VC2_SHARE_MAGIC;frame->version=VC2_SHARE_VERSION;
    frame->game_pid=GetCurrentProcessId();
    frame->audio_levels=hotd_pack_audio(audio_ini_volume("master_volume"),
        audio_ini_volume("music_volume"),audio_ini_volume("effects_volume"));
    frame->render_flags=GetPrivateProfileIntA("vr","native_lighting",1,ini_path)?0:HOTD_RENDER_UNLIT;
    if(!GetPrivateProfileIntA("vr","expand_meshes",1,ini_path))frame->render_flags|=HOTD_RENDER_NATIVE_MESH_CULLING;
    if(GetPrivateProfileIntA("vr","expand_world",0,ini_path))frame->render_flags|=HOTD_RENDER_EXPAND_WORLD;
    logline("native lighting: %s",frame->render_flags&HOTD_RENDER_UNLIT?"off":"on");
    frame->units_per_metre=(float)GetPrivateProfileIntA("bridge","units_per_metre",10,ini_path);
    if(frame->units_per_metre<1 || frame->units_per_metre>100000)frame->units_per_metre=10;
    /* Refuse input writes unless all signature sites and the PE image size match. */
    {
        IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER *)game_base;
        IMAGE_NT_HEADERS32 *nt=(IMAGE_NT_HEADERS32 *)(game_base+dos->e_lfanew);
        input_ready=nt->OptionalHeader.SizeOfImage>=0x5C9000 &&
            !memcmp(game_base+0xD1AD,xsig,4) && !memcmp(game_base+0xD1B7,ysig,4);
    }
    frame->input_supported=input_ready;
    logline("HOTD2 bridge pid=%lu input_profile=%d",(unsigned long)GetCurrentProcessId(),input_ready);
    if(!input_ready){logline("unsupported executable layout; injection refused");return FALSE;}
    if(!install_checkpoints()){logline("checkpoint configuration/signature invalid; injection refused");return FALSE;}
    frame->checkpoint_autosave=checkpoint_continue?2:1;
    {
        static const BYTE signature[]={0x83,0xEC,0x1C,0x8B,0x44,0x24,0x20};
        int ready=native_shot_profile(game_base) &&
            detour(0x6110,signature,sizeof(signature),native_shot_ray,(void **)&native_shot_ray_original);
        logline("native shot feedback: %s",ready?"ready":"unavailable; shot rumble disabled");
    }
    {
        static const BYTE signature[]={0xA1,0xB8,0xE5,0x7D,0};
        audio_ready=hotd_audio_profile(game_base) &&
            detour(0xA34B0,signature,sizeof(signature),native_audio_play,(void **)&native_audio_play_original);
        logline("native audio volume controls: %s",audio_ready?"ready":"unavailable; native audio unchanged");
    }
    cheats_supported=native_cheat_profile(game_base);
    logline("native cheat profile: %s",cheats_supported?"verified":"unsupported; native flags unchanged");
    continues_supported=native_continues_profile(game_base);
    logline("native continues profile: %s",continues_supported?"verified":"unsupported; free play unchanged");
    invincibility_ready=install_invincibility();
    logline("native invincibility hooks: %s",invincibility_ready?"ready":"unavailable; damage unchanged");
    int crosshair_profile_valid=hotd_crosshair_profile(game_base);
    logline("native capture visibility hooks: %s",install_capture_visibility()?"ready":"unavailable; native culling retained");
    gfile_ready=visibility_hooks_ready && native_gfile_profile();
    logline("G's File presentation capture: %s",gfile_ready?"ready":"unavailable; native depth retained");
    logline("native crosshair capture hooks: %s",install_crosshair_capture(crosshair_profile_valid)?"ready":"unavailable; native crosshairs retained");
    player_hud_ready=install_player_hud();
    logline("player HUD provenance: %s",player_hud_ready?"ready":"unavailable; original HUD positions retained");
    if(input_ready) {
        static const BYTE draw_signature[]={0x8B,0x54,0x24,0x04,0xA1,0x74,0xEB,0x7D,0x00};
        static const BYTE input_signature[]={0x83,0xEC,0x20,0x53,0x55,0x56,0x57,0x33,0xFF};
        static const BYTE model_signature[]={0x83,0xEC,0x1C,0xA1,0x74,0xEB,0x7D,0x00,0x53};
        static const BYTE pause_signature[]={0x66,0x83,0x3D,0x00,0x1A,0x9A,0x00,0x00};
        if(!install_reload_input() ||
           !detour(0xE9C0,pause_signature,sizeof(pause_signature),native_pause,(void **)&native_pause_original) ||
           !detour(0xCBB0,input_signature,sizeof(input_signature),native_input,(void **)&native_input_original) ||
           !detour(0xA7EF0,model_signature,sizeof(model_signature),native_model,(void **)&native_model_original) ||
           !detour(0xA8440,model_signature,sizeof(model_signature),native_model_alt,(void **)&native_model_alt_original) ||
           !detour(0xA59C0,draw_signature,sizeof(draw_signature),native_strip,(void **)&native_strip_original) ||
           !detour(0xA5A00,draw_signature,sizeof(draw_signature),native_tl,(void **)&native_tl_original) ||
           !detour(0xA5A40,draw_signature,sizeof(draw_signature),native_list,(void **)&native_list_original)) {
            logline("native submission/input hook failed");return FALSE;
        }
        logline("native polygon submission hooks ready");
        logline("native pause input separated from VR Start");
        logline("native reload input separated from VR Cancel/Skip");
        void **slot=(void **)(game_base+0x172304);
        if(*slot==game_base+0xBA3FF)
            logline("native frame presentation hook: %d",replace_pointer(slot,native_present,(void **)&original_present));
        else {logline("unsupported frame presentation layout");return FALSE;}
    }
    if(input_ready)logline("native device-selection hook: %d",
        hook_import("USER32.dll","MessageBoxA",device_message_box,(void **)&original_message_box));
    if(GetPrivateProfileIntA("bridge","windowed",1,ini_path)) {
        static const BYTE mode_signature[]={0x6A,0x0B,0xA3,0x28,0xE6,0x7D,0x00};
        const BYTE windowed_mode=7; /* Native table: 640x480, desktop-compatible 32-bit, windowed. */
        if(input_ready && !memcmp(game_base+0x9E461,mode_signature,sizeof(mode_signature)))
            logline("native windowed mode: %s",write_code(game_base+0x9E462,&windowed_mode,1)?"selected":"write failed");
        else logline("native windowed mode: unsupported executable; unchanged");
    }
    if(input_ready && GetPrivateProfileIntA("bridge","background",1,ini_path)) {
        static const BYTE activate_signature[]={0x83,0xFB,0x01,0x74,0x1C,0x83,0xFB,0x02};
        const BYTE jump=0xEB;
        if(!memcmp(game_base+0xA66E4,activate_signature,sizeof(activate_signature)))
            logline("background rendering: %s",write_code(game_base+0xA66E7,&jump,1)?"enabled":"write failed");
        else logline("background rendering: unsupported executable; unchanged");
    }
    logline("DirectInput import hook: %d",hook_import("DINPUT.dll","DirectInputCreateEx",input_create,(void **)&original_input_create));
    if(!hook_import("DDRAW.dll","DirectDrawCreateEx",ddraw_create,(void **)&original_ddraw_create)) {
        logline("DirectDraw import hook failed");return FALSE;
    }
    logline("DirectDraw import hooked");
    return TRUE;
}
