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
static void snapshot_copy(void *dst,const void *src,size_t bytes)
{
    memcpy(dst,src,bytes);
    if(interrupt_copy && src==(const void *)g_frame->atlas)g_frame->seq+=2;
}

int main(void)
{
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
    g_frame->magic=VC2_SHARE_MAGIC;g_frame->version=VC2_SHARE_VERSION;
    g_frame->seq=g_frame->atlas_seq=2;g_frame->frame=1;g_frame->vertex_count=3;
    g_frame->verts[0].x=11;g_frame->atlas[0]=111;
    assert(pullFrame() && g_local[0].x==11 && g_atlPix[0]==111 && g_lastFrame==1);
    Vc2Vertex *oldVertices=g_local;
    unsigned *oldAtlas=g_atlPix;
    g_frame->seq=g_frame->atlas_seq=3;g_frame->frame=2;
    g_frame->verts[0].x=22;g_frame->atlas[0]=222;
    assert(!pullFrame() && g_local==oldVertices && g_atlPix==oldAtlas);
    g_frame->seq=g_frame->atlas_seq=4;
    interrupt_copy=1;
    assert(!pullFrame()); /* Writer changed generation during every atlas copy. */
    assert(g_local==oldVertices && g_atlPix==oldAtlas && g_local[0].x==11 && g_atlPix[0]==111);
    interrupt_copy=0;
    assert(pullFrame() && g_local[0].x==22 && g_atlPix[0]==222 && g_lastFrame==2);
    g_frame->seq+=2;g_frame->frame=3;g_frame->verts[0].x=33;
    oldAtlas=g_atlPix;
    assert(pullFrame() && g_local[0].x==33 && g_atlPix==oldAtlas && g_atlPix[0]==222);
    free(g_frame);
    puts("Frame snapshot: matched texture/geometry generations and rejected torn copies passed.");
    return 0;
}
