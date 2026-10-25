/* Native audio hooks with fake DirectSound buffers. No game/audio device starts. */
#include <assert.h>
#include "../src/bridge.c"
typedef struct { IDirectSoundBuffer iface;LONG volume,heard;unsigned sets,plays,restores;int lost;HRESULT failure,restore_failure,play_failure;DWORD reserved,priority,flags; } FakeBuffer;
static BYTE channels[HOTD_AUDIO_CHANNELS*0x44];
static unsigned native_calls;static int native_channel,native_loop;
static HRESULT WINAPI set_volume(IDirectSoundBuffer *self,LONG value)
{FakeBuffer *b=(FakeBuffer *)self;b->sets++;if(b->lost)return DSERR_BUFFERLOST;if(FAILED(b->failure))return b->failure;b->volume=value;return DS_OK;}
static HRESULT WINAPI play_buffer(IDirectSoundBuffer *self,DWORD reserved,DWORD priority,DWORD flags)
{FakeBuffer *b=(FakeBuffer *)self;b->plays++;b->heard=b->volume;b->reserved=reserved;b->priority=priority;b->flags=flags;return b->play_failure;}
static HRESULT WINAPI restore_buffer(IDirectSoundBuffer *self)
{FakeBuffer *b=(FakeBuffer *)self;b->restores++;if(FAILED(b->restore_failure))return b->restore_failure;b->lost=0;b->volume=0;return DS_OK;}
static int __cdecl play_channel(int channel,int loop)
{
    native_calls++;native_channel=channel;native_loop=loop;
    if(channel<0||channel>=HOTD_AUDIO_CHANNELS)return 0;
    IDirectSoundBuffer *buffer=*(IDirectSoundBuffer **)(channels+channel*0x44);if(!buffer)return 0;
    HRESULT hr=IDirectSoundBuffer_Play(buffer,0,0,loop?DSBPLAY_LOOPING:0);
    if(hr==DSERR_BUFFERLOST&&SUCCEEDED(IDirectSoundBuffer_Restore(buffer)))hr=IDirectSoundBuffer_Play(buffer,0,0,loop?DSBPLAY_LOOPING:0);
    return SUCCEEDED(hr);
}
static void attach(unsigned channel,FakeBuffer *buffer)
{*(IDirectSoundBuffer **)(channels+channel*0x44)=buffer?&buffer->iface:NULL;*(DWORD *)(channels+channel*0x44+0x40)=buffer?0x80000000u:0;}
static void pure_checks(void)
{
    const char *bad[]={"","-1","+1","101","50x","50.0"," 50","50 ","NaN","4294967296"};unsigned value=777;
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){assert(!hotd_parse_volume(bad[i],&value));assert(value==777);}
    assert(!hotd_parse_volume(NULL,&value)&&!hotd_parse_volume("10",NULL));
    for(unsigned i=0;i<=100;i++){char text[4];sprintf(text,"%u",i);assert(hotd_parse_volume(text,&value)&&value==i);}
    assert(hotd_parse_volume("00050",&value)&&value==50);
    assert(hotd_pack_audio(100,100,100)==HOTD_AUDIO_DEFAULT&&hotd_pack_audio(101,100,100)==HOTD_AUDIO_DEFAULT);
    assert(hotd_audio_attenuation(HOTD_AUDIO_DEFAULT,15)==0&&hotd_audio_attenuation(0,0)==DSBVOLUME_MIN);
    assert(hotd_audio_attenuation(hotd_pack_audio(50,50,100),15)==-1204);
    assert(hotd_audio_attenuation(hotd_pack_audio(50,50,100),16)==-602);
    assert(hotd_audio_attenuation(hotd_pack_audio(100,0,100),15)==DSBVOLUME_MIN);
    for(unsigned ch=0;ch<HOTD_AUDIO_CHANNELS;ch++)assert(hotd_audio_attenuation(hotd_pack_audio(100,100,0),ch)==(ch==15?0:DSBVOLUME_MIN));
    assert(hotd_audio_sanitize(0xFFFFFFFFu)==HOTD_AUDIO_DEFAULT);
    {
        char temp[MAX_PATH],file[MAX_PATH];GetTempPathA(sizeof(temp),temp);assert(GetTempFileNameA(temp,"hva",0,file));strcpy(ini_path,file);
        assert(ini_percent("master_volume")==100);
        assert(WritePrivateProfileStringA("vr","master_volume","25",file)&&ini_percent("master_volume")==25);
        assert(WritePrivateProfileStringA("vr","master_volume","101",file)&&ini_percent("master_volume")==100);
        assert(WritePrivateProfileStringA("vr","master_volume","00000000000000000000000000000000000000001x",file)&&ini_percent("master_volume")==100);
        assert(DeleteFileA(file));
    }
}
static void profile_checks(void)
{
    static const unsigned offsets[]={0xA34B0,0xA34CE,0xA34DA,0xA34E1,0xABF5E,0xA4192,0xA42CD};
    static const unsigned sizes[]={5,6,5,8,8,12,12};
    static const BYTE bytes[7][12]={{0xA1,0xB8,0xE5,0x7D,0},{0x3B,0x3D,0xBC,0xE5,0x7D,0},{0xA1,0xA8,0xE5,0x7D,0},
      {0xC1,0xE6,4,3,0xF7,0xC1,0xE6,2},{0x6A,0x15,0x50,0xE8,0xEA,0x6F,0xFF,0xFF},
      {0x25,0xB0,0xFF,1,0,5,0xE0,0x81,1,0,0x0C,8},{0x25,0xD0,0xFF,1,0,5,0xE0,0x80,0,0,0x0C,2}};
    assert(!hotd_audio_profile(game_base));for(unsigned i=0;i<7;i++)memcpy(game_base+offsets[i],bytes[i],sizes[i]);
    assert(hotd_audio_profile(game_base));for(unsigned i=0;i<7;i++)for(unsigned b=0;b<sizes[i];b++){
        game_base[offsets[i]+b]^=1;assert(!hotd_audio_profile(game_base));game_base[offsets[i]+b]^=1;}
}
int main(void)
{
    IDirectSoundBufferVtbl vt={.Play=play_buffer,.SetVolume=set_volume,.Restore=restore_buffer},vt2=vt;
    FakeBuffer music={.iface={&vt}},effect={.iface={&vt}},voice={.iface={&vt2}},foreign={.iface={&vt}};
    game_base=VirtualAlloc(NULL,0x600000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    frame=VirtualAlloc(NULL,sizeof(*frame),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(game_base&&frame);
    pure_checks();profile_checks();audio_ready=1;native_audio_play_original=play_channel;
    *(DWORD *)(game_base+0x3DE5B8)=0x80000000u;*(DWORD *)(game_base+0x3DE5BC)=HOTD_AUDIO_CHANNELS;
    *(BYTE **)(game_base+0x3DE5A8)=channels;attach(15,&music);attach(0,&effect);attach(16,&voice);
    frame->audio_levels=hotd_pack_audio(100,50,25);
    assert(native_audio_play(15,1)&&music.heard==-602&&native_channel==15&&native_loop==1&&music.flags==DSBPLAY_LOOPING);
    assert(native_audio_play(0,0)&&effect.heard==-1204&&native_audio_play(16,0)&&voice.heard==-1204);
    unsigned ms=music.sets,es=effect.sets,vs=voice.sets;update_audio_levels();update_audio_levels();
    assert(music.sets==ms&&effect.sets==es&&voice.sets==vs);
    frame->audio_levels=hotd_pack_audio(50,100,50);update_audio_levels();
    assert(music.volume==-602&&effect.volume==-1204&&voice.volume==-1204);
    update_audio_levels();assert(music.sets==ms+1&&effect.sets==es+1&&voice.sets==vs+1);
    /* Already-playing music retries transient failures, without replay or log spam. */
    frame->audio_levels=hotd_pack_audio(100,50,100);music.failure=DSERR_GENERIC;
    ms=music.sets;unsigned mp=music.plays;logfile=tmpfile();assert(logfile);
    update_audio_levels();update_audio_levels();update_audio_levels();
    assert(music.sets==ms+3&&music.plays==mp&&!audio_applied[15].attempted);
    rewind(logfile);unsigned logs=0;char line[256];while(fgets(line,sizeof(line),logfile))logs++;
    assert(logs==1);music.failure=DS_OK;update_audio_levels();update_audio_levels();
    assert(music.sets==ms+4&&music.plays==mp&&music.volume==-602&&!(audio_failures&(1u<<15)));
    fclose(logfile);logfile=NULL;
    frame->audio_levels=hotd_pack_audio(50,100,50);update_audio_levels();
    /* Duplicate/recycled addresses must force absolute volume, never multiply inherited attenuation. */
    effect.volume=-9000;assert(native_audio_play(0,0)&&effect.heard==-1204);
    attach(1,&effect);assert(native_audio_play(1,0)&&effect.heard==-1204);attach(1,NULL);
    frame->audio_levels=HOTD_AUDIO_DEFAULT;assert(native_audio_play(0,0)&&effect.heard==0);
    frame->audio_levels=0;music.lost=1;ms=music.plays;
    assert(native_audio_play(15,1)&&music.restores==1&&music.plays==ms+1&&music.heard==DSBVOLUME_MIN);
    music.lost=1;music.restore_failure=DSERR_GENERIC;ms=music.plays;
    assert(!native_audio_play(15,1)&&music.plays==ms);music.restore_failure=DS_OK;music.lost=0;
    /* Requested attenuation never starts loud on failure; neutral settings retain native playback. */
    music.failure=DSERR_CONTROLUNAVAIL;ms=music.plays;
    assert(!native_audio_play(15,1)&&music.plays==ms);
    frame->audio_levels=HOTD_AUDIO_DEFAULT;music.volume=0;
    assert(native_audio_play(15,1)&&music.plays==ms+1&&music.heard==0);music.failure=DS_OK;
    music.play_failure=DSERR_GENERIC;assert(!native_audio_play(15,1));music.play_failure=DS_OK;
    /* Shared-vtable primary/foreign buffers keep their original parameters and volume. */
    assert(SUCCEEDED(IDirectSoundBuffer_Play(&foreign.iface,17,23,31))&&foreign.sets==0&&foreign.reserved==17&&foreign.priority==23&&foreign.flags==31);
    frame->audio_levels=hotd_pack_audio(100,0,100);*(DWORD *)(channels+15*0x44+0x40)=0;ms=music.sets;
    assert(SUCCEEDED(IDirectSoundBuffer_Play(&music.iface,0,0,0))&&music.sets==ms);
    *(DWORD *)(channels+15*0x44+0x40)=0x80000000u;
    unsigned before=native_calls;assert(!native_audio_play(-1,0)&&!native_audio_play(21,1)&&native_calls==before+2);
    audio_ready=0;update_audio_levels();ms=music.sets;assert(native_audio_play(15,1)&&music.sets==ms);
    audio_ready=1;*(DWORD *)(game_base+0x3DE5BC)=20;update_audio_levels();assert(native_audio_play(15,1)&&music.sets==ms);
    VirtualFree(frame,0,MEM_RELEASE);VirtualFree(game_base,0,MEM_RELEASE);
    puts("Audio controls: parsing, categories, absolute gains, playback, restoration and failures passed.");return 0;
}
