/* Exercise the real frame conversion and aiming without starting OpenXR or the game. */
#include <assert.h>
#define main viewer_application_main
#include "../src/hotd2vr.c"
#undef main
#include <d3d.h>

int main(void)
{
    char temp[MAX_PATH],ini[MAX_PATH];float scale=0;
    assert(g_cfgWorldScale==1.25f);
    assert(GetTempPathA(MAX_PATH,temp) && GetTempFileNameA(temp,"hws",0,ini));
    assert(readWorldScale(ini,&scale) && scale==1.25f);
    assert(WritePrivateProfileStringA("vr","worldscale","2",ini));
    assert(readWorldScale(ini,&scale) && scale==2);
    assert(WritePrivateProfileStringA("vr","worldscale","invalid",ini));
    assert(!readWorldScale(ini,&scale) && scale==2);
    assert(DeleteFileA(ini));
    assert(parseWorldScale("0.1",&scale) && scale==.1f);
    assert(parseWorldScale("10",&scale) && scale==10);
    assert(parseWorldScale(".8",&scale) && scale==.8f);
    assert(parseWorldScale("1.0",&scale) && scale==1);
    const char *invalid[]={"",".","0","0.0999","10.0001","140","100","-1.4","+1.4",
        "1.4junk","1.4.5","1,4","nan","NaN","inf","1e99","1e-99"," 1.4"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        assert(!parseWorldScale(invalid[i],&scale) && scale==1);
    }
    char *missing[]={"viewer","--world-scale"};
    char *bad[]={"viewer","--world-scale","invalid"};
    char *override[]={"viewer","--world-scale","1.2","--invalid-after-scale"};
    assert(viewer_application_main(2,missing)==2);
    assert(viewer_application_main(3,bad)==2);
    /* Deliberately stop parsing before any bridge connection or graphics startup. */
    assert(viewer_application_main(4,override)==2 && g_cfgWorldScale==1.2f);
    g_cfgWorldScale=1.25f;
    g_frame=calloc(1,sizeof(*g_frame));assert(g_frame);
    g_frame->magic=VC2_SHARE_MAGIC;g_frame->version=VC2_SHARE_VERSION;
    g_frame->seq=g_frame->atlas_seq=2;g_frame->units_per_metre=10;
    g_frame->vertex_count=9;g_frame->reserved[2]=7;
    g_frame->lens_valid=1;g_frame->lens=(HotdLens){500,600,320,240};
    g_frame->verts[0]=(Vc2Vertex){.x=-10,.y=-10,.z=-20,.style=HOTD_WORLD};
    g_frame->verts[1]=(Vc2Vertex){.x=10,.y=-10,.z=-20,.style=HOTD_WORLD};
    g_frame->verts[2]=(Vc2Vertex){.x=0,.y=10,.z=-20,.style=HOTD_WORLD};
    for(unsigned i=3;i<6;i++)g_frame->verts[i]=(Vc2Vertex){.x=.01f*(i-4.0f),.z=-1,.style=HOTD_SHOT};
    g_frame->shot_origins[0][2]=-1;
    for(unsigned i=6;i<9;i++)g_frame->verts[i]=(Vc2Vertex){.x=-10,.y=7.5f,.z=-20,.depth=1};
    assert(pullFrame());
    assert(fabsf(g_local[0].z/g_unitsPerMetre+2.5f)<.0001f);
    assert(fabsf(g_local[6].z/g_unitsPerMetre+2.0f)<.0001f);
    const float scales[]={1.0f,1.25f,2.0f};
    const float origin[3]={.14f,0,-.14f},forward[3]={0,0,-1};
    for(unsigned i=0;i<sizeof(scales)/sizeof(scales[0]);i++) {
        float sx,sy,hit[3],metres=2.0f*scales[i];
        g_cfgWorldScale=scales[i];
        for(int repeat=0;repeat<2;repeat++) {
            assert(pullFrame());
            assert(g_local[0].z==-20 && g_frame->verts[6].z==-20);
            assert(fabsf(g_local[0].z/g_unitsPerMetre+metres)<.0001f);
            assert(fabsf(g_local[6].x/g_unitsPerMetre+1)<.0001f);
            assert(fabsf(g_local[6].y/g_unitsPerMetre-.75f)<.0001f);
            assert(fabsf(g_local[6].z/g_unitsPerMetre+2)<.0001f);
            assert(fabsf(g_local[3].z/g_unitsPerMetre+metres-.002f)<.0001f);
            assert(aimFromPose(origin,forward,0,0,0,0,&sx,&sy,hit)==1);
            assert(fabsf(hit[0]-.14f)<.0001f && fabsf(hit[2]+metres)<.0001f);
            assert(fabsf(sx-(320+.14f*g_unitsPerMetre*500/20))<.001f && sy==240);
        }
        g_scrVisible=g_scrHave=1;setScreenPose(0,0,0,0);
        const float centre[3]={0};
        assert(aimFromPose(centre,forward,0,0,0,0,&sx,&sy,hit)==2);
        assert(sx==320 && sy==240 && hit[2]==-2);
        g_scrVisible=g_scrHave=0;
    }
    /* Book cover/pages retain their 3D animation and projected size at a fixed physical distance. */
    const float book[3][3]={{-.2f,-.1f,-1},{.3f,-.2f,-.75f},{.1f,.25f,-1.1f}};
    for(unsigned v=0;v<3;v++)g_frame->verts[3+v]=(Vc2Vertex){
        .x=book[v][0],.y=book[v][1],.z=book[v][2],.colour=0x89ABCDEF,.u=.3f,.v=.7f,
        .address=HOTD_GFILE|hotd_address(2,3),.style=2u<<HOTD_CULL_SHIFT};
    for(unsigned s=0;s<sizeof(scales)/sizeof(scales[0]);s++)for(unsigned repeat=0;repeat<2;repeat++) {
        float distance;const float centre[3]={0};g_cfgWorldScale=scales[s];assert(pullFrame());
        for(unsigned v=0;v<3;v++) {
            const Vc2Vertex *b=g_local+3+v;
            assert(fabsf(b->z/g_unitsPerMetre-book[v][2]*HOTD_GFILE_DISTANCE_M)<.0001f);
            assert(fabsf(b->x/b->z-book[v][0]/book[v][2])<.0001f);
            assert(fabsf(b->y/b->z-book[v][1]/book[v][2])<.0001f);
            assert(b->colour==0x89ABCDEF && b->u==.3f && b->v==.7f);
            assert(b->address==(HOTD_GFILE|hotd_address(2,3)) && b->style==(2u<<HOTD_CULL_SHIFT));
            assert(g_frame->verts[3+v].z==book[v][2]);
        }
        assert(g_local[0].z==-20 && fabsf(g_local[6].z/g_unitsPerMetre+2)<.0001f);
        assert(rayVsScene(centre,forward,&distance) && fabsf(distance-20)<.0001f);
    }
    free(g_frame);g_frame=NULL;
    puts("World scale: INI/CLI, 1/1.25/2x geometry, aim, shot placement, fixed HUD/menu and animated G's File depth passed.");
    return 0;
}
