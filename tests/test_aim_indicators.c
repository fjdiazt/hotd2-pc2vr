/* Exercise viewer options and generated aim geometry without the game or OpenXR. */
#include <assert.h>
#define main viewer_application_main
#include "../src/hotd2vr.c"
#undef main

int main(void)
{
    char temp[MAX_PATH],ini[MAX_PATH];
    assert(g_cfgLaser && g_cfgCrosshair);
    assert(GetTempPathA(MAX_PATH,temp) && GetTempFileNameA(temp,"hai",0,ini));
    assert(g_cfgMasterVolume==100 && g_cfgMusicVolume==100 && g_cfgEffectsVolume==100);
    assert(readAudioVolume(ini,"master_volume")==100);
    assert(readAudioVolume(ini,"music_volume")==100);
    assert(WritePrivateProfileStringA("vr","music_volume","0",ini));
    assert(readAudioVolume(ini,"music_volume")==0 && readAudioVolume(ini,"effects_volume")==100);
    assert(WritePrivateProfileStringA("vr","music_volume","75",ini));
    assert(readAudioVolume(ini,"music_volume")==75);
    const char *bad_volumes[]={"", "101", "-1", "50.5", "+25", "25x", "999999999999999999999", "00000000000000000000000000000000000000001x"};
    for(unsigned v=0;v<sizeof(bad_volumes)/sizeof(*bad_volumes);v++) {
        assert(WritePrivateProfileStringA("vr","music_volume",bad_volumes[v],ini));
        assert(readAudioVolume(ini,"music_volume")==100);
    }
    int brightness=0;
    assert(readBrightness(ini,&brightness) && brightness==90);
    assert(WritePrivateProfileStringA("vr","brightness","80",ini));
    assert(readBrightness(ini,&brightness) && brightness==80);
    assert(WritePrivateProfileStringA("vr","brightness","bad",ini));
    assert(!readBrightness(ini,&brightness));
    readGraphicsFlags(ini);assert(g_cfgLaser && g_cfgCrosshair && g_cfgNativeLighting && g_cfgExpandMeshes && !g_cfgExpandWorld && !g_cfgWindow && g_cfgVrWindow);
    assert(g_cfgSwapPlayerHud);
    assert(WritePrivateProfileStringA("vr","swap_player_hud","0",ini));
    readGraphicsFlags(ini);assert(!g_cfgSwapPlayerHud);
    assert(WritePrivateProfileStringA("vr","swap_player_hud","1",ini));
    readGraphicsFlags(ini);assert(g_cfgSwapPlayerHud);
    assert(WritePrivateProfileStringA("vr","vrwindow","1",ini));
    readGraphicsFlags(ini);assert(g_cfgVrWindow);
    assert(WritePrivateProfileStringA("vr","vrwindow","0",ini));
    readGraphicsFlags(ini);assert(!g_cfgVrWindow);
    assert(WritePrivateProfileStringA("vr","laser","0",ini));
    readGraphicsFlags(ini);assert(!g_cfgLaser && g_cfgCrosshair);
    assert(WritePrivateProfileStringA("vr","laser","1",ini));
    assert(WritePrivateProfileStringA("vr","crosshair","0",ini));
    readGraphicsFlags(ini);assert(g_cfgLaser && !g_cfgCrosshair);
    assert(WritePrivateProfileStringA("vr","laser","0",ini));
    readGraphicsFlags(ini);assert(!g_cfgLaser && !g_cfgCrosshair);
    assert(WritePrivateProfileStringA("vr","native_lighting","0",ini));
    readGraphicsFlags(ini);assert(!g_cfgNativeLighting);
    assert(WritePrivateProfileStringA("vr","native_lighting","1",ini));
    readGraphicsFlags(ini);assert(g_cfgNativeLighting);
    assert(WritePrivateProfileStringA("vr","expand_meshes","0",ini));
    assert(WritePrivateProfileStringA("vr","expand_world","1",ini));
    readGraphicsFlags(ini);assert(!g_cfgExpandMeshes && g_cfgExpandWorld);
    assert(DeleteFileA(ini));

    char *hud_off[]={"viewer","--swap-player-hud","--no-swap-player-hud","--stop-before-bridge"};
    char *hud_on[]={"viewer","--no-swap-player-hud","--swap-player-hud","--stop-before-bridge"};
    assert(viewer_application_main(4,hud_off)==2 && !g_cfgSwapPlayerHud);
    assert(viewer_application_main(4,hud_on)==2 && g_cfgSwapPlayerHud);
    char *light_off[]={"viewer","--native-lighting","--no-native-lighting","--stop-before-bridge"};
    char *volumes[]={"viewer","--master-volume","80","--music-volume","0","--effects-volume","35","--master-volume","60","--stop-before-bridge"};
    assert(viewer_application_main(10,volumes)==2 && g_cfgMasterVolume==60 && !g_cfgMusicVolume && g_cfgEffectsVolume==35);
    for(unsigned v=0;v<sizeof(bad_volumes)/sizeof(*bad_volumes);v++) {
        char *invalid[]={"viewer","--music-volume",(char *)bad_volumes[v],"--stop-before-bridge"};
        assert(viewer_application_main(4,invalid)==2 && g_cfgMusicVolume==readAudioVolume(g_vrIniPath,"music_volume"));
    }
    char *missing_volume[]={"viewer","--effects-volume"};
    assert(viewer_application_main(2,missing_volume)==2);
    g_frame=calloc(1,sizeof(*g_frame));assert(g_frame);
    g_cfgMasterVolume=80;g_cfgMusicVolume=25;g_cfgEffectsVolume=40;pushCheats();
    assert(g_frame->audio_levels==hotd_pack_audio(80,25,40));free(g_frame);g_frame=NULL;
    char *light_on[]={"viewer","--no-native-lighting","--native-lighting","--stop-before-bridge"};
    assert(viewer_application_main(4,light_off)==2 && !g_cfgNativeLighting);
    assert(viewer_application_main(4,light_on)==2 && g_cfgNativeLighting);
    char *dark[]={"viewer","--brightness","80","--stop-before-bridge"};
    char *neutral[]={"viewer","--brightness","80","--brightness","100","--stop-before-bridge"};
    assert(viewer_application_main(4,dark)==2 && g_cfgBrightness==80);
    assert(viewer_application_main(6,neutral)==2 && g_cfgBrightness==100);
    const char *bad[]={"", "24", "401", "80.5", "-1", "hello", "999999999999999999999"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!parseBrightness(bad[i],&brightness));
    assert(parseBrightness("25",&brightness) && brightness==25);
    assert(parseBrightness("400",&brightness) && brightness==400);
    char *missing[]={"viewer","--brightness"};
    assert(viewer_application_main(2,missing)==2);
    char *wide[]={"viewer","--no-expand-meshes","--expand-world","--stop-before-bridge"};
    char *native[]={"viewer","--expand-world","--no-expand-world","--expand-meshes","--no-expand-meshes","--stop-before-bridge"};
    char *default_mesh[]={"viewer","--no-expand-meshes","--expand-meshes","--no-expand-world","--stop-before-bridge"};
    assert(viewer_application_main(4,wide)==2 && !g_cfgExpandMeshes && g_cfgExpandWorld);
    assert(viewer_application_main(6,native)==2 && !g_cfgExpandMeshes && !g_cfgExpandWorld);
    assert(viewer_application_main(5,default_mesh)==2 && g_cfgExpandMeshes && !g_cfgExpandWorld);
    const float aim[3]={0},hit[3]={.5f,0,-2};
    char *off[]={"viewer","--no-laser","--no-crosshair","--stop-before-bridge"};
    assert(viewer_application_main(4,off)==2);
    assert(!g_cfgLaser && !g_cfgCrosshair);
    g_ovCount=0;ovSight(aim,hit,0xFF0000FF,0xFF00FF00,0xFFFFFFFF);
    assert(!g_ovCount);
    char *laser[]={"viewer","--no-laser","--laser","--no-crosshair","--stop-before-bridge"};
    char *crosshair[]={"viewer","--no-crosshair","--crosshair","--no-laser","--stop-before-bridge"};
    char *on[]={"viewer","--laser","--crosshair","--stop-before-bridge"};
    assert(viewer_application_main(5,laser)==2 && g_cfgLaser && !g_cfgCrosshair);
    assert(viewer_application_main(5,crosshair)==2 && !g_cfgLaser && g_cfgCrosshair);
    assert(viewer_application_main(4,on)==2 && g_cfgLaser && g_cfgCrosshair);

    g_ovCount=0;ovSight(aim,hit,0xFF0000FF,0xFF00FF00,0xFFFFFFFF);
    unsigned both=g_ovCount;assert(both);
    Vc2Vertex *all=malloc(both*sizeof(*all));assert(all);
    memcpy(all,g_ovVerts,both*sizeof(*all));
    g_cfgCrosshair=0;g_ovCount=0;
    ovSight(aim,hit,0xFF0000FF,0xFF00FF00,0xFFFFFFFF);
    unsigned beam=g_ovCount;assert(beam && beam<both);
    assert(!memcmp(all,g_ovVerts,beam*sizeof(*all)));
    g_cfgLaser=0;g_cfgCrosshair=1;g_ovCount=0;
    ovSight(aim,hit,0xFF0000FF,0xFF00FF00,0xFFFFFFFF);
    assert(g_ovCount && beam+g_ovCount==both);
    assert(!memcmp(all+beam,g_ovVerts,g_ovCount*sizeof(*all)));
    g_cfgLaser=g_cfgCrosshair=1;g_ovCount=0;
    ovSight(aim,hit,0xFF0000FF,0xFF00FF00,0xFFFFFFFF);
    ovSight(hit,aim,0xFF0000FF,0xFF00FF00,0xFFFFFFFF);
    assert(g_ovCount==2*both);
    g_cfgLaser=g_cfgCrosshair=0;g_ovCount=0;
    ovSight(aim,hit,0xFF0000FF,0xFF00FF00,0xFFFFFFFF);
    ovSight(hit,aim,0xFF0000FF,0xFF00FF00,0xFFFFFFFF);
    assert(!g_ovCount);
    free(all);

    g_unitsPerMetre=10;g_localCount=0;g_scrVisible=0;
    g_gfx=500;g_gfy=600;g_gcx=320;g_gcy=240;
    float sx,sy,point[3],expectedX,expectedY,expected[3];
    const float forward[3]={0,0,-1};
    assert(aimFromPose(aim,forward,0,0,0,0,&sx,&sy,point)==1);
    g_cfgLaser=g_cfgCrosshair=1;
    assert(aimFromPose(aim,forward,0,0,0,0,&expectedX,&expectedY,expected)==1);
    assert(sx==expectedX && sy==expectedY && !memcmp(point,expected,sizeof(point)));
    puts("Aim indicators: default-on INI/CLI, independent geometry, both hands and unchanged aim passed.");
    return 0;
}
