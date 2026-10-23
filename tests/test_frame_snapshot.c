/* Test the real snapshot reader; never enter the viewer's application main. */
#include <assert.h>
#define COBJMACROS
#include <windows.h>
static SHORT menu_keyboard_keys[256];
static SHORT WINAPI menu_test_key(int key){return menu_keyboard_keys[key];}
static HWND WINAPI menu_test_foreground(void){return (HWND)(ULONG_PTR)1;}
static DWORD WINAPI menu_test_owner(HWND window,LPDWORD owner){(void)window;*owner=GetCurrentProcessId();return 1;}
#define GetAsyncKeyState menu_test_key
#define GetForegroundWindow menu_test_foreground
#define GetWindowThreadProcessId menu_test_owner
static void snapshot_copy(void *dst,const void *src,size_t bytes);
#undef CopyMemory
#define CopyMemory snapshot_copy
#define main viewer_application_main
#include "../src/hotd2vr.c"
#undef main

static int interrupt_copy;
static unsigned controller_pauses,controller_backs,controller_reloads,controller_fires,controller_lost,controller_recentres,controller_settings;
static float controller_sticks[2][2];
static XrQuaternionf controller_rotations[2]={{0,0,0,1},{0,0,0,1}};
static int controller_sticks_active,controller_sync_lost;
static XrResult XRAPI_CALL controller_sync(XrSession session,const XrActionsSyncInfo *info)
{(void)session;(void)info;return controller_sync_lost?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;}
static XrResult XRAPI_CALL controller_boolean(XrSession session,const XrActionStateGetInfo *info,XrActionStateBoolean *out)
{
    unsigned hand=info->subactionPath==g_handPath[0]?0u:1u;
    (void)session;out->isActive=XR_TRUE;out->changedSinceLastSync=XR_TRUE;
    unsigned held=info->action==g_actSettings?controller_settings:info->action==g_actRecentre?controller_recentres:info->action==g_actPause?controller_pauses:info->action==g_actBack?controller_backs:info->action==g_actReload?controller_reloads:info->action==g_actFire?controller_fires:0;
    out->currentState=info->subactionPath==XR_NULL_PATH ? held!=0:(held&(1u<<hand))!=0;
    return XR_SUCCESS;
}
static XrResult XRAPI_CALL controller_float(XrSession session,const XrActionStateGetInfo *info,XrActionStateFloat *out)
{
    unsigned hand=info->subactionPath==g_handPath[0]?0u:1u;
    (void)session;out->isActive=XR_FALSE;out->currentState=0;
    if(info->action==g_actMenu || info->action==g_actMenuX) {
        out->isActive=controller_sticks_active;
        out->currentState=controller_sticks[hand][info->action==g_actMenuX];
    }
    return XR_SUCCESS;
}
static XrResult XRAPI_CALL controller_pose(XrSpace space,XrSpace base,XrTime time,XrSpaceLocation *out)
{
    unsigned hand=space==g_aimSpace[0]?0u:1u;
    (void)base;(void)time;
    out->locationFlags=controller_lost&(1u<<hand)?0:XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    out->pose.orientation=controller_rotations[hand];out->pose.position.x=hand?.2f:-.2f;
    return XR_SUCCESS;
}
static void controller_tick(void)
{const XrQuaternionf head={0,0,0,1};assert(!handleControllers(XR_NULL_HANDLE,XR_NULL_HANDLE,0,0,0,0,0,&head));}
static XrQuaternionf test_rotation(float pitch,float yaw,float roll)
{
    const float d=.00872664626f;
    float sx=sinf(pitch*d),cx=cosf(pitch*d),sy=sinf(yaw*d),cy=cosf(yaw*d),sz=sinf(roll*d),cz=cosf(roll*d);
    XrQuaternionf q={cy*sx*cz+sy*cx*sz,sy*cx*cz-cy*sx*sz,cy*cx*sz-sy*sx*cz,cy*cx*cz+sy*sx*sz};
    return q;
}
static void check_head_relative_reload(void)
{
    const float angles[]={49.9f,50.1f,-44.9f,-45.1f,0,0,0,0};
    const float sideways[]={0,0,0,0,59.9f,60.1f,-59.9f,-60.1f};
    g_cfgAkimbo=1;controller_lost=0;controller_pauses=controller_backs=controller_reloads=0;controller_sticks_active=0;
    for(unsigned h=0;h<2;h++)for(unsigned turn=0;turn<3;turn++)for(unsigned a=0;a<8;a++) {
        XrQuaternionf head=test_rotation(turn==0?0:turn==1?35:-35,turn*70.0f,turn==2?25:0);
        XrQuaternionf relative=test_rotation(angles[a],sideways[a],0),gun;
        /* Compose head * relative. */
        gun.x=head.w*relative.x+head.x*relative.w+head.y*relative.z-head.z*relative.y;
        gun.y=head.w*relative.y-head.x*relative.z+head.y*relative.w+head.z*relative.x;
        gun.z=head.w*relative.z+head.x*relative.y-head.y*relative.x+head.z*relative.w;
        gun.w=head.w*relative.w-head.x*relative.x-head.y*relative.y-head.z*relative.z;
        controller_rotations[h]=gun;controller_fires=1u<<h;g_resetControls=1;
        assert(!handleControllers(XR_NULL_HANDLE,XR_NULL_HANDLE,0,1.7f,3,4,5,&head));
        unsigned action=h==1?g_frame->aim_buttons:g_frame->aim2_buttons;
        assert((action&(VC2_BTN_FIRE|VC2_BTN_RELOAD))==((a==1||a==3||a==5||a==7)?VC2_BTN_RELOAD:VC2_BTN_FIRE));
    }
    controller_rotations[0]=controller_rotations[1]=(XrQuaternionf){0,0,0,1};
    XrQuaternionf invalid={0},head={0,0,0,1};controller_fires=1;g_resetControls=1;
    assert(!handleControllers(XR_NULL_HANDLE,XR_NULL_HANDLE,0,0,0,0,0,&invalid));
    assert(!(g_frame->aim2_buttons&(VC2_BTN_FIRE|VC2_BTN_RELOAD)));
    assert(!handleControllers(XR_NULL_HANDLE,XR_NULL_HANDLE,0,0,0,0,0,&head));
    assert(!(g_frame->aim2_buttons&(VC2_BTN_FIRE|VC2_BTN_RELOAD))); /* Invalid head disarms until release. */
    controller_fires=0;controller_tick();controller_fires=1;controller_tick();
    assert(g_frame->aim2_buttons&VC2_BTN_FIRE);
    controller_fires=0;g_resetControls=1;controller_tick();
}
static unsigned haptic_calls[2];
static XrResult XRAPI_CALL controller_haptic(XrSession session,const XrHapticActionInfo *info,const XrHapticBaseHeader *event)
{
    (void)session;
    unsigned hand=info->subactionPath==g_handPath[0]?0u:1u;
    const XrHapticVibration *pulse=(const XrHapticVibration *)event;
    assert(event->type==XR_TYPE_HAPTIC_VIBRATION && pulse->amplitude==1.0f && llabs(pulse->duration-30000000)<=1);
    haptic_calls[hand]++;return XR_SUCCESS;
}
static void check_shot_haptics(void)
{
    pxrApplyHapticFeedback=controller_haptic;g_cfgHaptic=1;g_cfgAkimbo=1;
    g_shotEvents[0]=g_shotEvents[1]=0;g_nativePaused=0;
    g_resetControls=1;controller_fires=0;controller_lost=0;controller_tick();
    controller_fires=3;controller_tick();
    assert(!haptic_calls[0] && !haptic_calls[1]); /* Input alone is not a confirmed shot. */
    unsigned rate=g_cfgRapidRate;
    g_shotEvents[0]++;controller_tick();assert(!haptic_calls[0] && haptic_calls[1]==1);
    g_shotEvents[1]++;controller_tick();assert(haptic_calls[0]==1 && haptic_calls[1]==1);
    controller_tick();assert(haptic_calls[0]==1 && haptic_calls[1]==1);
    g_cfgHaptic=0;g_shotEvents[0]++;controller_tick();
    g_cfgHaptic=1;controller_tick();assert(haptic_calls[1]==1);
    controller_lost=2;g_shotEvents[0]++;controller_tick();controller_lost=0;controller_tick();
    assert(haptic_calls[1]==1);
    controller_sync_lost=1;g_shotEvents[0]++;controller_tick();controller_sync_lost=0;controller_tick();
    assert(haptic_calls[1]==1 && g_cfgRapidRate==rate);
    g_nativePaused=1;g_shotEvents[1]++;controller_tick();g_nativePaused=0;controller_tick();
    assert(haptic_calls[0]==1);
    g_cfgHaptic=0;controller_fires=0;g_resetControls=1;controller_tick();
}
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

    /* 35-percent flashes retain their off-center anchors and shared surface depth. */
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
    assert(fabsf(g_local[11].x/g_local[11].z-(-.3f+.35f*(4.3f/-.8f+.3f)))<.001f);
    assert(fabsf(g_local[3].x/g_local[3].z-3.2f)<.001f);
    assert(fabsf(g_local[6].x/g_local[6].z-3.8f)<.001f);
    assert(fabsf((g_local[3].x+g_local[4].x)*.5f/g_local[3].z+.3f)<.001f);
    assert(fabsf((g_local[6].x+g_local[7].x)*.5f/g_local[6].z-.3f)<.001f);
    assert(fabsf(fabsf((g_local[4].x-g_local[3].x)/g_local[3].z)-7)<.001f);
    assert(fabsf(fabsf((g_local[5].y-g_local[3].y)/g_local[3].z)-7)<.001f);
    assert(g_local[0].x==-10 && g_local[0].z==-6); /* World is untouched. */
    test_plane(0,0,-.2f,0);test_plane(3,0,-1,HOTD_SHOT);g_localCount=6;
    placeShotEffects();assert(fabsf(g_local[3].z+59.98f)<.001f); /* Miss uses aimreach, not HUD depth. */
    assert(fabsf(g_local[3].x/g_local[3].z-3.5f)<.001f); /* Misses retain 35 percent size too. */
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
    g_actPause=(XrAction)(uintptr_t)3;g_actFire=(XrAction)(uintptr_t)4;
    g_actBack=(XrAction)(uintptr_t)5;
    g_actReload=(XrAction)(uintptr_t)9;
    g_actMenu=(XrAction)(uintptr_t)6;g_actMenuX=(XrAction)(uintptr_t)7;
    g_cfgHaptic=0;g_localCount=0;g_unitsPerMetre=10;g_cfgAkimbo=0;g_resetControls=1;
    g_activePlayers=0;
    controller_tick();assert(!g_gunValid && !g_gun2Valid);
    controller_fires=1;controller_tick();assert(!g_gunValid && !(g_frame->aim_buttons&VC2_BTN_FIRE));
    assert(g_frame->aim2_buttons&VC2_BTN_START); /* Trigger can join before its gun is visible. */
    controller_tick();
    assert(!g_gunValid && !g_gun2Valid); /* Start alone does not confirm a join. */
    assert((g_frame->aim2_buttons&VC2_BTN_START) && !(g_frame->aim_buttons&VC2_BTN_START));
    g_activePlayers=2;controller_tick(); /* Native P2 accepted. */
    assert(!g_gunValid && g_gun2Valid && g_gun2Pose.position.x<0 && !(g_frame->aim2_buttons&VC2_BTN_FIRE));
    for(unsigned player=0;player<2;player++)for(unsigned palette=0;palette<2;palette++)for(unsigned firing=0;firing<2;firing++)for(unsigned laser=0;laser<2;laser++) {
        g_activePlayers=1u<<player;
        g_cfgArcadeGunColors=palette;
        g_cfgLaser=laser;g_cfgCrosshair=!laser;controller_fires=firing<<(1-player);controller_tick();
        assert(g_ovCount);
        if(laser && !firing)assert(g_ovVerts[0].colour==(player?0x808F4B1Du:0x802B24DEu));
        for(unsigned i=0;i<g_ovCount;i++) {
            DWORD color=g_ovVerts[i].colour;
            unsigned r=color&255,g=(color>>8)&255,b=(color>>16)&255;
            assert((color>>24)==0x80);
            if(player)assert(b>g && g>=r);
            else assert(r>b && b>=g && b*2<r);
        }
    }
    g_activePlayers=2;g_cfgArcadeGunColors=0;g_cfgLaser=g_cfgCrosshair=1;controller_fires=0;controller_tick();
    controller_fires=0;controller_tick();
    controller_fires=1;controller_tick();assert(g_frame->aim2_buttons&VC2_BTN_FIRE);
    controller_fires=2;controller_tick();assert(!g_gunValid && g_gun2Valid && !(g_frame->aim_buttons&VC2_BTN_FIRE));
    controller_tick();assert(!g_gunValid && g_gun2Valid);
    assert((g_frame->aim_buttons&VC2_BTN_START) && !(g_frame->aim2_buttons&VC2_BTN_START));
    g_activePlayers=3;controller_tick();
    assert(g_gunValid && g_gun2Valid && g_gunPose.position.x>0 && !(g_frame->aim_buttons&VC2_BTN_FIRE));
    controller_lost=2;controller_tick();assert(!g_gunValid && g_gun2Valid);
    controller_lost=0;controller_tick();assert(g_gunValid && !(g_frame->aim_buttons&VC2_BTN_FIRE));
    g_activePlayers=2;controller_tick();assert(!g_gunValid && g_gun2Valid); /* P1 out. */
    g_activePlayers=0;controller_tick();assert(!g_gunValid && !g_gun2Valid); /* Menu/game over. */
    g_activePlayers=1;controller_tick();assert(g_gunValid && !g_gun2Valid); /* Keyboard/native join works too. */
    controller_fires=0;g_cfgAkimbo=1;g_resetControls=1;g_activePlayers=0;controller_tick();
    assert(g_gunValid && g_gun2Valid && g_frame->akimbo && g_gunPose.position.x>0 && g_gun2Pose.position.x<0);
    assert(g_frame->aim_buttons&VC2_BTN_PAIR_START);
    controller_fires=1;controller_tick();assert(g_frame->aim2_buttons&VC2_BTN_START);
    controller_fires=2;controller_tick();assert(g_frame->aim_buttons&VC2_BTN_START);
    g_cfgAkimbo=0;g_resetControls=1;controller_fires=0;controller_tick();
    controller_fires=2;controller_tick();assert(!g_gunValid && !g_gun2Valid);
    g_activePlayers=1;controller_tick();assert(g_gunValid && !g_gun2Valid);
    controller_fires=0;controller_tick();controller_fires=1;controller_tick();
    assert(g_gunValid && !g_gun2Valid);
    g_activePlayers=3;controller_tick();
    assert(g_gunValid && g_gun2Valid && g_gunPose.position.x>0 && g_gun2Pose.position.x<0);
    assert(!(g_frame->aim_buttons&VC2_BTN_PAIR_START));
    /* Face actions remain separate, even before joining. */
    controller_fires=0;g_activePlayers=0;
    controller_backs=2;controller_pauses=1;controller_tick();
    assert((g_frame->aim_buttons&(VC2_BTN_START|VC2_BTN_FIRE|VC2_BTN_BACK|VC2_BTN_PAUSE))==VC2_BTN_BACK);
    assert((g_frame->aim2_buttons&(VC2_BTN_START|VC2_BTN_FIRE|VC2_BTN_BACK|VC2_BTN_PAUSE))==(VC2_BTN_START|VC2_BTN_PAUSE));
    controller_backs=1;controller_pauses=2;controller_tick();
    assert((g_frame->aim_buttons&(VC2_BTN_START|VC2_BTN_FIRE|VC2_BTN_BACK|VC2_BTN_PAUSE))==(VC2_BTN_START|VC2_BTN_PAUSE));
    assert((g_frame->aim2_buttons&(VC2_BTN_START|VC2_BTN_FIRE|VC2_BTN_BACK|VC2_BTN_PAUSE))==VC2_BTN_BACK);
    controller_backs=controller_pauses=0;
    controller_reloads=3;controller_tick();
    assert(g_frame->aim_buttons==VC2_BTN_RELOAD && g_frame->aim2_buttons==VC2_BTN_RELOAD);
    controller_reloads=0;controller_tick();
    for(unsigned paired=0;paired<2;paired++)for(unsigned hand=0;hand<2;hand++) {
        g_cfgAkimbo=paired;g_activePlayers=3;controller_backs=1u<<hand;controller_tick();
        unsigned mask=VC2_BTN_BACK|VC2_BTN_RELOAD|VC2_BTN_FIRE;
        assert((g_frame->aim_buttons&mask)==(hand==1?VC2_BTN_BACK:0));
        assert((g_frame->aim2_buttons&mask)==(hand==0?VC2_BTN_BACK:0));
        controller_backs=0;controller_tick();
    }
    g_cfgAkimbo=0;
    /* Both sticks navigate independently of gun visibility or player joining. */
    controller_fires=0;g_activePlayers=0;controller_sticks_active=1;
    const unsigned directions[]={VC2_BTN_UP,VC2_BTN_DOWN,VC2_BTN_RIGHT,VC2_BTN_LEFT};
    const unsigned nav_mask=VC2_BTN_UP|VC2_BTN_DOWN|VC2_BTN_LEFT|VC2_BTN_RIGHT;
    for(unsigned hand=0;hand<2;hand++)for(unsigned dir=0;dir<4;dir++) {
        memset(controller_sticks,0,sizeof(controller_sticks));controller_tick();
        controller_sticks[hand][dir/2]=dir&1?-.8f:.8f;controller_tick();
        assert((g_frame->aim_buttons&nav_mask)==directions[dir]);
        assert(!(g_frame->aim_buttons&VC2_BTN_FIRE) && !(g_frame->aim2_buttons&VC2_BTN_FIRE));
        controller_sticks[hand][dir/2]=dir&1?-.4f:.4f;controller_tick();
        assert((g_frame->aim_buttons&nav_mask)==directions[dir]);
    }
    controller_sticks_active=0;controller_tick();assert(!(g_frame->aim_buttons&nav_mask));
    memset(controller_sticks,0,sizeof(controller_sticks));
    /* Stick click recenters once; tilting down remains a native menu arrow. */
    g_actRecentre=(XrAction)(uintptr_t)8;
    controller_recentres=1;
    controller_tick(); /* Left click reserved, including runtime remaps. */
    controller_recentres=0;controller_tick();
    const XrQuaternionf head={0,0,0,1};
    controller_recentres=2;g_nativePaused=1;
    assert(handleControllers(XR_NULL_HANDLE,XR_NULL_HANDLE,0,0,0,0,0,&head));
    controller_tick();
    controller_recentres=0;controller_tick();g_nativePaused=0;
    controller_sticks_active=1;controller_sticks[0][0]=-.8f;
    controller_tick();assert(g_frame->aim_buttons&VC2_BTN_DOWN);
    assert(!(g_frame->aim_buttons&(VC2_BTN_START|VC2_BTN_BACK|VC2_BTN_PAUSE|VC2_BTN_FIRE)));
    controller_sticks_active=0;memset(controller_sticks,0,sizeof(controller_sticks));controller_tick();
}
static void snapshot_copy(void *dst,const void *src,size_t bytes)
{
    memcpy(dst,src,bytes);
    if(interrupt_copy && src==(const void *)g_frame->atlas)g_frame->seq+=2;
}

static void check_swapped_player_hud(void)
{
    float scale=g_cfgWorldScale;int akimbo=g_cfgAkimbo,swap=g_cfgSwapPlayerHud;
    const float x[16]={-8,-6,6,8,-8,-5,5,8,-9,-3,3,9,-2,-4,-6,-7};
    for(unsigned sc=0;sc<3;sc++)for(unsigned enabled=0;enabled<2;enabled++)for(unsigned two=0;two<2;two++) {
        g_cfgWorldScale=sc==0?1:sc==1?1.25f:2;g_cfgSwapPlayerHud=enabled;g_cfgAkimbo=two;
        g_frame->vertex_count=16;g_frame->reserved[0]=0;g_frame->reserved[2]=1;g_frame->units_per_metre=10;
        for(unsigned i=0;i<16;i++)g_frame->verts[i]=(Vc2Vertex){.x=x[i],.y=-5,.z=-20,.depth=1,
            .address=i<12?((i/2)+1)<<HOTD_PLAYER_HUD_SHIFT:0,.colour=0xFFAC1234,.u=.25f,.v=.75f};
        g_frame->verts[13].style=HOTD_WORLD;g_frame->verts[13].address=1<<HOTD_PLAYER_HUD_SHIFT;
        g_frame->verts[14].address=HOTD_GFILE;g_frame->verts[15].address=7<<HOTD_PLAYER_HUD_SHIFT;
        for(unsigned repeat=0;repeat<2;repeat++) {
            g_frame->seq+=2;assert(pullFrame());
            const float changed[12]={6,8,-8,-6,5,8,-8,-5,3,9,-9,-3};float units=g_unitsPerMetre/10;
            for(unsigned i=0;i<12;i++) {
                assert(fabsf(g_local[i].x-(enabled?changed[i]:x[i])*units)<.0001f);
                assert(g_local[i].u==.25f && g_local[i].v==.75f && g_local[i].colour==0xFFAC1234);
                assert(g_local[i].address==g_frame->verts[i].address && g_frame->verts[i].x==x[i]);
                assert(g_local[i].y==-5*units && g_local[i].z==-20*units && g_local[i].depth==1);
            }
            assert(fabsf(g_local[12].x-x[12]*units)<.0001f); /* Attract caption/menu untagged. */
            assert(fabsf(g_local[13].x-x[13]*units)<.0001f); /* World, even if tag is corrupt. */
            assert(fabsf(g_local[15].x-x[15]*units)<.0001f); /* Unknown group retained. */
        }
    }
    g_cfgWorldScale=scale;g_cfgAkimbo=akimbo;g_cfgSwapPlayerHud=swap;
}

int main(void)
{
    char temp[MAX_PATH],ini[MAX_PATH];
    assert(GetTempPathA(MAX_PATH,temp) && GetTempFileNameA(temp,"htc",0,ini));
    assert(!readCheats(ini));
    unsigned rate;
    assert(WritePrivateProfileStringA("vr","protect_rescue_npcs","1",ini));
    assert(readCheats(ini)==HOTD_CHEAT_RESCUE_NPCS);
    assert(WritePrivateProfileStringA("vr","protect_rescue_npcs","0",ini));
    assert(!readCheats(ini));
    assert(readRapidRate(ini,&rate) && rate==4);
    assert(WritePrivateProfileStringA("vr","rapid_fire_rate","5",ini));
    assert(readRapidRate(ini,&rate) && rate==5);
    assert(WritePrivateProfileStringA("vr","rapid_fire_rate","invalid",ini));
    assert(!readRapidRate(ini,&rate));
    assert(cheatArg("--infinite-ammo")==HOTD_CHEAT_AMMO);
    assert(cheatArg("--infinite-lives")==HOTD_CHEAT_LIVES);
    assert(cheatArg("--invincibility")==HOTD_CHEAT_INVINCIBILITY);
    assert(cheatArg("--infinite-continues")==HOTD_CHEAT_CONTINUES);
    assert(cheatArg("--rapid-fire")==HOTD_CHEAT_RAPID);
    assert(cheatArg("--protect-rescue-npcs")==HOTD_CHEAT_RESCUE_NPCS);
    assert(!cheatArg("--infinite-amm"));
    assert(WritePrivateProfileStringA("vr","infinite_ammo","1",ini));
    assert(readCheats(ini)==HOTD_CHEAT_AMMO);
    assert(WritePrivateProfileStringA("vr","infinite_lives","1",ini));
    assert(WritePrivateProfileStringA("vr","rapid_fire","1",ini));
    assert(readCheats(ini)==(HOTD_CHEAT_AMMO|HOTD_CHEAT_LIVES|HOTD_CHEAT_RAPID));
    assert(WritePrivateProfileStringA("vr","invincibility","1",ini));
    assert(readCheats(ini)==(HOTD_CHEAT_AMMO|HOTD_CHEAT_LIVES|HOTD_CHEAT_RAPID|HOTD_CHEAT_INVINCIBILITY));
    assert(WritePrivateProfileStringA("vr","infinite_continues","1",ini));
    assert(WritePrivateProfileStringA("vr","protect_rescue_npcs","1",ini));
    assert(readCheats(ini)==HOTD_CHEAT_MASK);
    assert(WritePrivateProfileStringA("vr","infinite_ammo","0",ini));
    assert(readCheats(ini)==(HOTD_CHEAT_MASK&~HOTD_CHEAT_AMMO));
    assert(WritePrivateProfileStringA("vr","infinite_lives","0",ini));
    assert(WritePrivateProfileStringA("vr","rapid_fire","0",ini));
    assert(WritePrivateProfileStringA("vr","invincibility","0",ini));
    assert(WritePrivateProfileStringA("vr","protect_rescue_npcs","0",ini));
    assert(readCheats(ini)==HOTD_CHEAT_CONTINUES);
    assert(WritePrivateProfileStringA("vr","infinite_continues","0",ini));
    assert(!readCheats(ini));
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
    assert(g_frame->rapid_fire_rate==4 && g_frame->render_flags==HOTD_RENDER_EXPAND_WORLD);
    g_cfgNativeLighting=0;pushCheats();assert(g_frame->render_flags==(HOTD_RENDER_UNLIT|HOTD_RENDER_EXPAND_WORLD));
    g_cfgNativeLighting=1;pushCheats();assert(g_frame->render_flags==HOTD_RENDER_EXPAND_WORLD);
    g_cfgExpandMeshes=0;g_cfgExpandWorld=1;pushCheats();
    assert(g_frame->render_flags==(HOTD_RENDER_NATIVE_MESH_CULLING|HOTD_RENDER_EXPAND_WORLD));
    g_cfgNativeLighting=0;pushCheats();
    assert(g_frame->render_flags==(HOTD_RENDER_UNLIT|HOTD_RENDER_NATIVE_MESH_CULLING|HOTD_RENDER_EXPAND_WORLD));
    g_cfgNativeLighting=1;g_cfgExpandMeshes=1;g_cfgExpandWorld=0;pushCheats();assert(!g_frame->render_flags);
    g_cfgRapidRate=5;pushCheats();assert(g_frame->rapid_fire_rate==5);
    g_cfgRapidRate=4;
    g_cheatFlags=0;pushCheats();assert(!g_frame->cheat_flags);
    g_frame->magic=VC2_SHARE_MAGIC;g_frame->version=VC2_SHARE_VERSION;
    g_frame->seq=g_frame->atlas_seq=2;g_frame->frame=1;g_frame->vertex_count=3;
    g_frame->verts[0].x=11;g_frame->atlas[0]=111;g_frame->active_players=1;
    g_frame->verts[0].address=hotd_address(2,3);
    g_frame->verts[0].specular=0x00332211;g_frame->verts[0].fog_colour=0x01112233;g_frame->verts[0].fog=.25f;
    g_frame->shot_origins[0][0]=11;
    assert(pullFrame() && g_local[0].x==11 && g_atlPix[0]==111 && g_lastFrame==1 && g_activePlayers==1);
    assert(g_local[0].address==hotd_address(2,3));
    assert(g_local[0].specular==0x00332211 && g_local[0].fog_colour==0x01112233 && g_local[0].fog==.25f);
    assert(g_shotOrigins[0][0]==11);
    assert(!g_shotEvents[0] && !g_shotEvents[1]);
    g_frame->reserved[1]=1;g_frame->seq+=2;
    assert(pullFrame() && g_nativePaused && g_local[0].x==11);
    g_scrMode=1;assert(!screenVisible());
    g_scrMode=0;assert(!screenVisible());
    g_frame->reserved[1]=0;g_frame->seq+=2;
    assert(pullFrame() && !g_nativePaused && screenVisible());
    Vc2Vertex *oldVertices=g_local;
    unsigned *oldAtlas=g_atlPix;
    g_frame->seq=g_frame->atlas_seq=3;g_frame->frame=2;
    g_frame->verts[0].x=22;g_frame->atlas[0]=222;g_frame->active_players=2;
    g_frame->shot_origins[0][0]=22;g_frame->shot_events[0]=17;g_frame->shot_events[1]=23;
    assert(!pullFrame() && g_local==oldVertices && g_atlPix==oldAtlas && g_activePlayers==1);
    assert(g_shotOrigins[0][0]==11 && !g_shotEvents[0] && !g_shotEvents[1]);
    g_frame->seq=g_frame->atlas_seq=4;
    interrupt_copy=1;
    assert(!pullFrame()); /* Writer changed generation during every atlas copy. */
    assert(g_local==oldVertices && g_atlPix==oldAtlas && g_local[0].x==11 && g_atlPix[0]==111 && g_activePlayers==1);
    interrupt_copy=0;
    assert(pullFrame() && g_local[0].x==22 && g_atlPix[0]==222 && g_lastFrame==2 && g_activePlayers==2);
    assert(g_shotOrigins[0][0]==22 && g_shotEvents[0]==17 && g_shotEvents[1]==23);
    g_frame->seq+=2;g_frame->frame=3;g_frame->verts[0].x=33;
    oldAtlas=g_atlPix;
    assert(pullFrame() && g_local[0].x==33 && g_atlPix==oldAtlas && g_atlPix[0]==222);
    g_frame->vertex_count=6;g_frame->reserved[2]=4;g_frame->units_per_metre=10;g_frame->seq+=2;
    g_frame->verts[3]=(Vc2Vertex){.x=-10,.y=7.5f,.z=-20,.depth=1};
    assert(pullFrame() && g_localCount==6 && g_ovStart==3);
    assert(g_local[3].z==-HOTD_PANEL_DISTANCE_M*g_unitsPerMetre && !(g_local[3].style&HOTD_WORLD));
    /* Shared crosshair artwork in an Options/HUD quad near either aim must survive byte-for-byte. */
    {
        const float xy[6][2]={{-.2f,.2f},{.2f,.2f},{-.2f,-.2f},{-.2f,-.2f},{.2f,.2f},{.2f,-.2f}};
        Vc2Vertex preview[6];float scale=g_cfgWorldScale;g_cfgWorldScale=1;
        memset(preview,0,sizeof(preview));
        for(unsigned v=0;v<6;v++)preview[v]=(Vc2Vertex){.x=xy[v][0],.y=xy[v][1],.z=-20,.depth=1,.colour=0xFF40A0FF};
        g_frame->reserved[0]=0;g_frame->reserved[2]=1;g_frame->vertex_count=6;
        g_frame->lens=(HotdLens){500,500,320,240};g_frame->lens_valid=1;
        memcpy(g_frame->verts,preview,sizeof(preview));g_aimSX=320;g_aimSY=240;
        g_frame->seq+=2;assert(pullFrame() && !memcmp(g_local,preview,sizeof(preview)));
        g_cfgWorldScale=scale;
    }
    check_world_aim();
    check_near_aim_and_splashes();
    check_controller_modes();
    check_head_relative_reload();
    check_shot_haptics();
    check_swapped_player_hud();
    g_cheatFlags=0;vrMenuInit();
    g_vrMenuEnabled=g_vrMenuOpen=g_vrMenuArmed=1;g_vrMenuPage=-1;g_vrMenuRow=MENU_GROUP_FIRST;
    g_vrMenuPrevious=0;g_vrMenuBlock=0;g_nativePaused=0;
    controller_fires=controller_pauses=controller_backs=controller_reloads=0;controller_tick();
    controller_fires=3;controller_tick();
    assert(g_vrMenuPage==0 && g_frame->aim_buttons==VC2_BTN_MENU && !g_frame->aim2_buttons);
    controller_fires=0;controller_tick();g_vrMenuPage=MENU_CHEATS;g_vrMenuRow=0;
    controller_fires=2;controller_tick();
    assert(vrMenuItems[MENU_CHEATS][0].value==1 && g_cheatFlags==HOTD_CHEAT_AMMO);
    assert(g_frame->cheat_flags==HOTD_CHEAT_AMMO);
    assert(g_frame->aim_buttons==VC2_BTN_MENU && !g_frame->aim2_buttons); /* UI Trigger cannot join/fire behind the page. */
    controller_fires=0;g_cheatFlags=0;vrMenuInit();
    g_actSettings=(XrAction)(uintptr_t)10;
    controller_backs=2;controller_tick(); /* Back to root; cannot reload behind settings. */
    assert(g_vrMenuPage==-1 && g_frame->aim_buttons==VC2_BTN_MENU && !g_frame->aim2_buttons);
    controller_backs=0;controller_tick();
    controller_backs=2;controller_tick();controller_backs=0;controller_tick();assert(g_vrMenuOpen);
    controller_pauses=3;controller_tick();controller_pauses=0;controller_tick();assert(g_vrMenuOpen);
    for(int h=0;h<2;h++) {
        controller_pauses=1u<<h;controller_tick();
        assert(((h?g_frame->aim_buttons:g_frame->aim2_buttons)&(VC2_BTN_START|VC2_BTN_PAUSE))==(VC2_BTN_START|VC2_BTN_PAUSE));
        assert(!((h?g_frame->aim2_buttons:g_frame->aim_buttons)&(VC2_BTN_START|VC2_BTN_PAUSE)));
        assert(!((g_frame->aim_buttons|g_frame->aim2_buttons)&(VC2_BTN_FIRE|VC2_BTN_RELOAD|VC2_BTN_BACK)) && g_vrMenuOpen);
        controller_pauses=0;controller_tick();
    }
    controller_settings=1;controller_tick();controller_settings=0;controller_tick();assert(!g_vrMenuOpen);
    for(int paused=0;paused<2;paused++) {
        g_nativePaused=paused;controller_tick();assert(!g_vrMenuOpen);
        controller_settings=1;controller_tick();controller_tick();assert(g_vrMenuOpen);
        assert(g_frame->aim_buttons==VC2_BTN_MENU && !g_frame->aim2_buttons);
        controller_settings=0;controller_tick();
        controller_reloads=3;controller_tick();assert(g_frame->aim_buttons==VC2_BTN_MENU && !g_frame->aim2_buttons);
        controller_reloads=0;controller_settings=1;controller_tick();assert(!g_vrMenuOpen);
        assert(g_nativePaused==paused && g_frame->aim_buttons==VC2_BTN_MENU && !g_frame->aim2_buttons);
        controller_tick();assert(!g_vrMenuOpen); /* Held click cannot reopen. */
        controller_settings=0;controller_tick();
    }
    /* Keyboard joins the same input sample; a held key stays held across controller frames. */
    controller_tick();menu_keyboard_keys[VK_F8]=(SHORT)0x8000;controller_tick();assert(g_vrMenuOpen);
    menu_keyboard_keys[VK_F8]=0;controller_tick();
    menu_keyboard_keys[VK_DOWN]=(SHORT)0x8000;controller_tick();int row=g_vrMenuRow;
    controller_tick();assert(g_vrMenuRow==row && row==1);
    menu_keyboard_keys[VK_DOWN]=0;controller_tick();
    g_vrMenuRow=MENU_GROUP_FIRST;controller_fires=2;controller_tick();assert(g_vrMenuPage==0);
    controller_fires=0;controller_tick();
    menu_keyboard_keys[VK_RCONTROL]=(SHORT)0x8000;controller_tick();assert(g_vrMenuPage==-1 && g_vrMenuOpen);
    menu_keyboard_keys[VK_RCONTROL]=0;controller_tick();
    controller_sync_lost=1;menu_keyboard_keys[VK_RCONTROL]=(SHORT)0x8000;
    controller_tick();assert(!g_vrMenuOpen && g_frame->aim_buttons==VC2_BTN_MENU);
    menu_keyboard_keys[VK_RCONTROL]=0;controller_tick();controller_tick();
    assert(!g_frame->aim_buttons && !g_frame->aim2_buttons);controller_sync_lost=0;
    g_vrMenuEnabled=0;g_cheatFlags=0;
    free(g_frame);
    puts("Viewer: independent/paired players, gun visibility, INI/CLI cheats, heartbeat and coherent snapshots passed.");
    return 0;
}
