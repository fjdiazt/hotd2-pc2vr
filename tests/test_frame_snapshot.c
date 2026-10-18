/* Test the real snapshot reader; never enter the viewer's application main. */
#include <assert.h>
#define COBJMACROS
#include <windows.h>
static void snapshot_copy(void *dst,const void *src,size_t bytes);
#undef CopyMemory
#define CopyMemory snapshot_copy
#define main viewer_application_main
#include "../src/hotd2vr.c"
#undef main

static int interrupt_copy;
static unsigned controller_starts,controller_fires,controller_lost;
static XrResult XRAPI_CALL controller_sync(XrSession session,const XrActionsSyncInfo *info)
{(void)session;(void)info;return XR_SUCCESS;}
static XrResult XRAPI_CALL controller_boolean(XrSession session,const XrActionStateGetInfo *info,XrActionStateBoolean *out)
{
    unsigned hand=info->subactionPath==g_handPath[0]?0u:1u;
    (void)session;out->isActive=XR_TRUE;out->changedSinceLastSync=XR_TRUE;
    out->currentState=((info->action==g_actStart?controller_starts:info->action==g_actFire?controller_fires:0)&(1u<<hand))!=0;
    return XR_SUCCESS;
}
static XrResult XRAPI_CALL controller_float(XrSession session,const XrActionStateGetInfo *info,XrActionStateFloat *out)
{(void)session;(void)info;out->isActive=XR_FALSE;out->currentState=0;return XR_SUCCESS;}
static XrResult XRAPI_CALL controller_pose(XrSpace space,XrSpace base,XrTime time,XrSpaceLocation *out)
{
    unsigned hand=space==g_aimSpace[0]?0u:1u;
    (void)base;(void)time;
    out->locationFlags=controller_lost&(1u<<hand)?0:XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    out->pose.orientation.w=1;out->pose.position.x=hand?.2f:-.2f;
    return XR_SUCCESS;
}
static void controller_tick(void)
{assert(!handleControllers(XR_NULL_HANDLE,XR_NULL_HANDLE,0,0,0,0,0));}
static void test_plane(unsigned at,float x,float z,unsigned style)
{
    g_local[at]=(Vc2Vertex){.x=x-10,.y=-10,.z=z,.style=style};
    g_local[at+1]=(Vc2Vertex){.x=x+10,.y=-10,.z=z,.style=style};
    g_local[at+2]=(Vc2Vertex){.x=x,.y=10,.z=z,.style=style};
    if(style&HOTD_SHOT){g_shotOrigins[style>>26][0]=x;g_shotOrigins[style>>26][1]=0;g_shotOrigins[style>>26][2]=z;}
}
static void check_near_aim_and_splashes(void)
{
    const float origin[3]={.2f,0,-.4f},direction[3]={0,0,-1},game_origin[3]={2,0,-4};
    float sx,sy,hit[3],distance;
    g_unitsPerMetre=10;g_cfgReach=6;g_gfx=500;g_gfy=600;g_gcx=320;g_gcy=240;
    g_scrVisible=0;g_localCount=9;
    test_plane(0,0,-6,HOTD_WORLD); /* Actual bridge painter depth remains zero. */
    test_plane(3,0,-4.1f,0); /* HUD is not a target. */
    test_plane(6,0,-4.2f,HOTD_SHOT); /* Nor a flash. */
    assert(aimFromPose(origin,direction,0,0,0,0,&sx,&sy,hit)==1);
    assert(fabsf(sx-486.6667f)<.01f && sy==240 && fabsf(hit[2]+.6f)<.0001f);
    test_plane(0,0,-4.5f,HOTD_WORLD);
    assert(rayVsScene(game_origin,direction,&distance) && fabsf(distance-.5f)<.0001f);
    /* A target closer than ten cm to the native camera also projects correctly. */
    const float near_origin[3]={.02f,0,0};
    test_plane(0,0,-.5f,HOTD_WORLD);g_localCount=3;
    assert(aimFromPose(near_origin,direction,0,0,0,0,&sx,&sy,hit)==1);
    assert(fabsf(sx-520)<.01f && fabsf(hit[2]+.05f)<.0001f);

    /* Independent off-center flashes: preserve screen angle, put both on the surface. */
    test_plane(0,0,-6,HOTD_WORLD);
    test_plane(3,.3f,-1,HOTD_SHOT|(1u<<26));
    test_plane(6,-.3f,-1,HOTD_SHOT|(2u<<26));
    test_plane(9,4.3f,-1,HOTD_SHOT|(4u<<26));g_localCount=12;
    memcpy(g_shotOrigins[4],g_shotOrigins[1],sizeof(g_shotOrigins[1])); /* Asymmetric mesh, same shot origin. */
    g_local[10].z=-1.2f;g_local[11].z=-.8f;
    placeShotEffects();
    assert(fabsf(g_local[3].z+5.98f)<.001f && fabsf(g_local[6].z+5.98f)<.001f);
    assert(fabsf(g_local[9].z-g_local[3].z)<.0001f);
    assert(g_local[9].z==g_local[10].z && g_local[10].z==g_local[11].z);
    assert(fabsf(g_local[11].x/g_local[11].z-4.3f/-.8f)<.001f);
    assert(fabsf(g_local[3].x/g_local[3].z-(-9.7f/-1))<.001f);
    assert(fabsf(g_local[6].x/g_local[6].z-(-10.3f/-1))<.001f);
    assert(g_local[0].x==-10 && g_local[0].z==-6); /* World is untouched. */
    test_plane(0,0,-.2f,0);test_plane(3,0,-1,HOTD_SHOT);g_localCount=6;
    placeShotEffects();assert(fabsf(g_local[3].z+59.98f)<.001f); /* Miss uses aimreach, not HUD depth. */
    g_localCount=0;
}
static void check_world_aim(void)
{
    float origin[3]={0},hit[3],sx,sy;
    const float directions[4][3]={{1,0,-1},{-1,0,-1},{0,1,-1},{0,-1,-1}};
    const float expected[4][2]={{820,240},{-180,240},{320,-360},{320,840}};
    g_scrVisible=0;g_localCount=0;g_unitsPerMetre=10;g_cfgReach=6;
    g_gfx=500;g_gfy=600;g_gcx=320;g_gcy=240;
    for(unsigned i=0;i<4;i++) {
        assert(aimFromPose(origin,directions[i],0,0,0,0,&sx,&sy,hit)==1);
        assert(fabsf(sx-expected[i][0])<.01f && fabsf(sy-expected[i][1])<.01f);
    }
    g_scrVisible=g_scrHave=1;setScreenPose(0,0,0,0);
    const float forward[3]={0,0,-1};
    assert(aimFromPose(origin,forward,0,0,0,0,&sx,&sy,hit)==2);
    assert(sx==320 && sy==240); /* Menu panel retains bounded pixel coordinates. */
    g_scrVisible=g_scrHave=0;
}
static void check_controller_modes(void)
{
    pxrSyncActions=controller_sync;pxrGetActionStateBoolean=controller_boolean;
    pxrGetActionStateFloat=controller_float;pxrLocateSpace=controller_pose;
    g_handPath[0]=1;g_handPath[1]=2;
    g_aimSpace[0]=(XrSpace)(uintptr_t)1;g_aimSpace[1]=(XrSpace)(uintptr_t)2;
    g_actStart=(XrAction)(uintptr_t)3;g_actFire=(XrAction)(uintptr_t)4;
    g_actBack=(XrAction)(uintptr_t)5;
    g_cfgHaptic=0;g_localCount=0;g_unitsPerMetre=10;g_cfgAkimbo=0;g_resetControls=1;
    g_activePlayers=0;
    controller_tick();assert(!g_gunValid && !g_gun2Valid);
    controller_fires=1;controller_tick();assert(!g_gunValid && !(g_frame->aim_buttons&VC2_BTN_FIRE));
    controller_starts=1;controller_tick();
    assert(!g_gunValid && !g_gun2Valid); /* Start alone does not confirm a join. */
    assert((g_frame->aim2_buttons&VC2_BTN_START) && !(g_frame->aim_buttons&VC2_BTN_START));
    controller_starts=0;g_activePlayers=2;controller_tick(); /* Native P2 accepted. */
    assert(!g_gunValid && g_gun2Valid && g_gun2Pose.position.x<0 && !(g_frame->aim2_buttons&VC2_BTN_FIRE));
    controller_fires=0;controller_tick();
    controller_fires=1;controller_tick();assert(g_frame->aim2_buttons&VC2_BTN_FIRE);
    controller_fires=2;controller_tick();assert(!g_gunValid && g_gun2Valid && !(g_frame->aim_buttons&VC2_BTN_FIRE));
    controller_starts=2;controller_tick();assert(!g_gunValid && g_gun2Valid);
    assert((g_frame->aim_buttons&VC2_BTN_START) && !(g_frame->aim2_buttons&VC2_BTN_START));
    g_activePlayers=3;controller_tick();
    assert(g_gunValid && g_gun2Valid && g_gunPose.position.x>0 && !(g_frame->aim_buttons&VC2_BTN_FIRE));
    controller_lost=2;controller_tick();assert(!g_gunValid && g_gun2Valid);
    controller_lost=0;controller_tick();assert(g_gunValid && !(g_frame->aim_buttons&VC2_BTN_FIRE));
    g_activePlayers=2;controller_tick();assert(!g_gunValid && g_gun2Valid); /* P1 out. */
    g_activePlayers=0;controller_tick();assert(!g_gunValid && !g_gun2Valid); /* Menu/game over. */
    g_activePlayers=1;controller_tick();assert(g_gunValid && !g_gun2Valid); /* Keyboard/native join works too. */
    controller_starts=controller_fires=0;g_cfgAkimbo=1;g_resetControls=1;g_activePlayers=0;controller_tick();
    assert(g_gunValid && g_gun2Valid && g_frame->akimbo && g_gunPose.position.x>0 && g_gun2Pose.position.x<0);
    assert(g_frame->aim_buttons&VC2_BTN_PAIR_START);
    controller_starts=1;controller_tick();assert(g_frame->aim2_buttons&VC2_BTN_START);
    controller_starts=2;controller_tick();assert(g_frame->aim_buttons&VC2_BTN_START);
    g_cfgAkimbo=0;g_resetControls=1;controller_starts=controller_fires=0;controller_tick();
    controller_starts=2;controller_tick();assert(!g_gunValid && !g_gun2Valid);
    g_activePlayers=1;controller_tick();assert(g_gunValid && !g_gun2Valid);
    controller_starts=0;controller_tick();controller_starts=1;controller_tick();
    assert(g_gunValid && !g_gun2Valid);
    g_activePlayers=3;controller_tick();
    assert(g_gunValid && g_gun2Valid && g_gunPose.position.x>0 && g_gun2Pose.position.x<0);
    assert(!(g_frame->aim_buttons&VC2_BTN_PAIR_START));
}
static void snapshot_copy(void *dst,const void *src,size_t bytes)
{
    memcpy(dst,src,bytes);
    if(interrupt_copy && src==(const void *)g_frame->atlas)g_frame->seq+=2;
}

int main(void)
{
    char temp[MAX_PATH],ini[MAX_PATH];
    assert(GetTempPathA(MAX_PATH,temp) && GetTempFileNameA(temp,"htc",0,ini));
    assert(!readCheats(ini));
    unsigned rate;
    assert(readRapidRate(ini,&rate) && rate==3);
    assert(WritePrivateProfileStringA("vr","rapid_fire_rate","5",ini));
    assert(readRapidRate(ini,&rate) && rate==5);
    assert(WritePrivateProfileStringA("vr","rapid_fire_rate","invalid",ini));
    assert(!readRapidRate(ini,&rate));
    assert(cheatArg("--infinite-ammo")==HOTD_CHEAT_AMMO);
    assert(cheatArg("--infinite-lives")==HOTD_CHEAT_LIVES);
    assert(cheatArg("--rapid-fire")==HOTD_CHEAT_RAPID);
    assert(!cheatArg("--infinite-amm"));
    assert(WritePrivateProfileStringA("vr","infinite_ammo","1",ini));
    assert(readCheats(ini)==HOTD_CHEAT_AMMO);
    assert(WritePrivateProfileStringA("vr","infinite_lives","1",ini));
    assert(WritePrivateProfileStringA("vr","rapid_fire","1",ini));
    assert(readCheats(ini)==HOTD_CHEAT_MASK);
    assert(WritePrivateProfileStringA("vr","infinite_ammo","0",ini));
    assert(readCheats(ini)==(HOTD_CHEAT_LIVES|HOTD_CHEAT_RAPID));
    assert(DeleteFileA(ini));
    const XrViewStateFlags tracked=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT|
        XR_VIEW_STATE_POSITION_TRACKED_BIT|XR_VIEW_STATE_ORIENTATION_TRACKED_BIT;
    assert(!headPoseReady(0,XR_SESSION_STATE_READY,XR_SUCCESS,2,tracked));
    assert(!headPoseReady(0,XR_SESSION_STATE_VISIBLE,XR_SUCCESS,2,tracked));
    for(unsigned bit=1;bit<=8;bit<<=1)
        assert(!headPoseReady(0,XR_SESSION_STATE_FOCUSED,XR_SUCCESS,2,tracked&~(XrViewStateFlags)bit));
    assert(!headPoseReady(0,XR_SESSION_STATE_FOCUSED,XR_ERROR_RUNTIME_FAILURE,2,tracked));
    assert(!headPoseReady(0,XR_SESSION_STATE_FOCUSED,XR_SUCCESS,1,tracked));
    assert(headPoseReady(0,XR_SESSION_STATE_FOCUSED,XR_SUCCESS,2,tracked));
    assert(headPoseReady(1,XR_SESSION_STATE_VISIBLE,XR_SUCCESS,2,
        XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT));
    assert(!headPoseReady(1,XR_SESSION_STATE_FOCUSED,XR_SUCCESS,2,0));
    g_frame=calloc(1,sizeof(*g_frame));assert(g_frame);
    g_cheatFlags=HOTD_CHEAT_MASK;pushCheats();
    assert(g_frame->cheat_flags==HOTD_CHEAT_MASK && !g_frame->aim_seq && !g_frame->aim_buttons);
    assert(g_frame->rapid_fire_rate==3);
    g_cfgRapidRate=5;pushCheats();assert(g_frame->rapid_fire_rate==5);
    g_cfgRapidRate=3;
    g_cheatFlags=0;pushCheats();assert(!g_frame->cheat_flags);
    g_frame->magic=VC2_SHARE_MAGIC;g_frame->version=VC2_SHARE_VERSION;
    g_frame->seq=g_frame->atlas_seq=2;g_frame->frame=1;g_frame->vertex_count=3;
    g_frame->verts[0].x=11;g_frame->atlas[0]=111;g_frame->active_players=1;
    g_frame->shot_origins[0][0]=11;
    assert(pullFrame() && g_local[0].x==11 && g_atlPix[0]==111 && g_lastFrame==1 && g_activePlayers==1);
    assert(g_shotOrigins[0][0]==11);
    Vc2Vertex *oldVertices=g_local;
    unsigned *oldAtlas=g_atlPix;
    g_frame->seq=g_frame->atlas_seq=3;g_frame->frame=2;
    g_frame->verts[0].x=22;g_frame->atlas[0]=222;g_frame->active_players=2;
    g_frame->shot_origins[0][0]=22;
    assert(!pullFrame() && g_local==oldVertices && g_atlPix==oldAtlas && g_activePlayers==1);
    assert(g_shotOrigins[0][0]==11);
    g_frame->seq=g_frame->atlas_seq=4;
    interrupt_copy=1;
    assert(!pullFrame()); /* Writer changed generation during every atlas copy. */
    assert(g_local==oldVertices && g_atlPix==oldAtlas && g_local[0].x==11 && g_atlPix[0]==111 && g_activePlayers==1);
    interrupt_copy=0;
    assert(pullFrame() && g_local[0].x==22 && g_atlPix[0]==222 && g_lastFrame==2 && g_activePlayers==2);
    assert(g_shotOrigins[0][0]==22);
    g_frame->seq+=2;g_frame->frame=3;g_frame->verts[0].x=33;
    oldAtlas=g_atlPix;
    assert(pullFrame() && g_local[0].x==33 && g_atlPix==oldAtlas && g_atlPix[0]==222);
    check_world_aim();
    check_near_aim_and_splashes();
    check_controller_modes();
    free(g_frame);
    puts("Viewer: independent/paired players, gun visibility, INI/CLI cheats, heartbeat and coherent snapshots passed.");
    return 0;
}
