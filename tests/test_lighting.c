#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/lighting.h"

static void near_value(float a,float b) { assert(isfinite(a) && fabsf(a-b)<.0001f); }
static D3DCOLORVALUE rgba(float r,float g,float b,float a)
{ D3DCOLORVALUE c; c.r=r;c.g=g;c.b=b;c.a=a;return c; }
static HotdLighting state(void)
{
    HotdLighting s={0};
    s.world._11=s.world._22=s.world._33=s.world._44=1;
    s.view=s.world;s.lighting=1;s.local_viewer=1;s.ambient_strength=1;
    s.material.diffuse=rgba(1,1,1,.4f);
    s.material.ambient=rgba(1,1,1,1);
    s.material.specular=rgba(.5f,.5f,.5f,1);s.material.power=4;
    s.light_count=1;s.lights[0].dltType=D3DLIGHT_DIRECTIONAL;
    s.lights[0].dvDirection.z=1;
    s.lights[0].dcvDiffuse=rgba(.6f,.6f,.6f,1);
    s.lights[0].dcvSpecular=rgba(.8f,.8f,.8f,1);
    return s;
}

int main(void)
{
    const float p[4]={0,0,4,.25f},front[3]={0,0,-1},back[3]={0,0,1};
    const float side[4]={3,0,4,.25f};
    const DWORD fvf=D3DFVF_XYZ|D3DFVF_NORMAL;
    HotdLighting s=state();HotdLitVertex v;
    hotd_lighting_prepare(&s);
    hotd_light_vertex(&s,p,front,0,0,fvf,&v);
    near_value(v.diffuse[0],.6f);near_value(v.diffuse[3],.4f);
    near_value(v.specular[0],0);assert(v.fog_mode==0);near_value(v.fog,1);
    hotd_light_vertex(&s,p,back,0,0,fvf,&v);near_value(v.diffuse[0],0);

    s.ambient=0xFF333333;s.material.ambient=rgba(.5f,.5f,.5f,1);
    s.lights[0].dcvAmbient=rgba(.2f,.2f,.2f,1);
    s.material.emissive=rgba(.1f,.2f,.3f,1);
    hotd_lighting_prepare(&s);hotd_light_vertex(&s,p,back,0,0,fvf,&v);
    near_value(v.diffuse[0],.3f);near_value(v.diffuse[1],.4f);near_value(v.diffuse[2],.5f);
    hotd_light_vertex(&s,p,NULL,0,0,D3DFVF_XYZ,&v);near_value(v.diffuse[0],.3f);

    /* Scale global and per-light ambient, retaining direct light, emission, alpha, specular and fog. */
    s.specular_enable=1;s.lights[0].dcvDiffuse=rgba(.2f,.2f,.2f,1);
    s.fog_enable=1;s.fog_table_mode=D3DFOG_LINEAR;s.fog_end=8;
    HotdLitVertex original,dimmed;
    hotd_light_vertex(&s,p,front,0,0,fvf,&original);
    near_value(original.diffuse[0],.5f);near_value(original.specular[0],.4f);
    s.ambient_strength=.5f;hotd_light_vertex(&s,p,front,0,0,fvf,&dimmed);
    for(unsigned k=0;k<3;k++) {
        near_value(dimmed.diffuse[k],original.diffuse[k]-.1f);
        near_value(dimmed.specular[k],original.specular[k]);
    }
    near_value(dimmed.diffuse[3],original.diffuse[3]);near_value(dimmed.fog,original.fog);
    assert(dimmed.fog_mode==original.fog_mode);
    s.ambient_strength=0;hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.diffuse[0],.3f);
    hotd_light_vertex(&s,p,back,0,0,fvf,&v);near_value(v.diffuse[0],.1f);
    s.lighting=0;hotd_light_vertex(&s,p,front,0x80402010,0,fvf|D3DFVF_DIFFUSE,&original);
    s.ambient_strength=1;hotd_light_vertex(&s,p,front,0x80402010,0,fvf|D3DFVF_DIFFUSE,&v);
    assert(!memcmp(&v,&original,sizeof(v))); /* Unlit textures retain their native colors. */
    s.lighting=1;hotd_light_vertex(&s,p,NULL,0x80402010,0,D3DFVF_XYZRHW|D3DFVF_DIFFUSE,&original);
    s.ambient_strength=0;hotd_light_vertex(&s,p,NULL,0x80402010,0,D3DFVF_XYZRHW|D3DFVF_DIFFUSE,&v);
    assert(!memcmp(&v,&original,sizeof(v))); /* Prelit UI is unchanged. */

    s=state();s.specular_enable=1;hotd_lighting_prepare(&s);
    hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.specular[0],.4f);
    hotd_light_vertex(&s,side,front,0,0,fvf,&v);near_value(v.specular[0],.324f);
    hotd_light_vertex(&s,p,back,0,0,fvf,&v);near_value(v.specular[0],0);
    s.material.power=0;hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.specular[0],.4f);
    s.material.power=4;s.local_viewer=0;
    hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.specular[0],0);
    s.lights[0].dvDirection.z=-1;hotd_lighting_prepare(&s);
    hotd_light_vertex(&s,p,back,0,0,fvf,&v);near_value(v.specular[0],.4f);

    s=state();s.lights[1]=s.lights[0];s.light_count=2;
    s.lights[0].dcvDiffuse=rgba(.2f,.4f,.6f,1);s.lights[1].dcvDiffuse=rgba(.3f,.4f,.6f,1);
    hotd_lighting_prepare(&s);hotd_light_vertex(&s,p,front,0,0,fvf,&v);
    near_value(v.diffuse[0],.5f);near_value(v.diffuse[1],.8f);near_value(v.diffuse[2],1);

    /* The normal length is retained unless NORMALIZENORMALS requests normalization. */
    s=state();s.world._33=2;hotd_lighting_prepare(&s);
    hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.diffuse[0],.3f);
    /* D3D7 caps N dot H before the power, even with unnormalized scaled normals. */
    s=state();s.world._11=s.world._22=s.world._33=.7f;s.specular_enable=1;
    s.material.specular=rgba(.29f,.29f,.31f,1);s.material.power=8;
    s.lights[0].dcvSpecular=rgba(.84f,.84f,.84f,1);hotd_lighting_prepare(&s);
    hotd_light_vertex(&s,p,front,0,0,fvf,&v);
    near_value(v.specular[0],.29f*.84f);near_value(v.specular[2],.31f*.84f);
    near_value(v.diffuse[0],.6f/.7f);
    s=state();s.world._33=2;hotd_lighting_prepare(&s);
    s.normalize_normals=1;hotd_lighting_prepare(&s);
    hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.diffuse[0],.6f);
    s.world._33=0;hotd_lighting_prepare(&s);
    hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.diffuse[0],0);
    s=state();s.world._11=0;s.world._13=1;s.world._31=-1;s.world._33=0;
    s.lights[0].dvDirection.x=-1;s.lights[0].dvDirection.z=0;
    hotd_lighting_prepare(&s);hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.diffuse[0],.6f);

    /* Material source selectors fall back individually when a requested vertex color is absent. */
    s=state();s.colorvertex=1;s.diffuse_source=D3DMCS_COLOR1;
    hotd_lighting_prepare(&s);hotd_light_vertex(&s,p,front,0x80402010,0,fvf|D3DFVF_DIFFUSE,&v);
    near_value(v.diffuse[0],.6f*64/255);near_value(v.diffuse[1],.6f*32/255);near_value(v.diffuse[3],128/255.0f);
    hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.diffuse[0],.6f);
    s.diffuse_source=D3DMCS_COLOR2;
    hotd_light_vertex(&s,p,front,0,0xFF204080,fvf|D3DFVF_SPECULAR,&v);near_value(v.diffuse[0],.6f*32/255);
    s.colorvertex=0;
    hotd_light_vertex(&s,p,front,0,0xFF204080,fvf|D3DFVF_SPECULAR,&v);near_value(v.diffuse[0],.6f);
    s.colorvertex=1;s.ambient=0xFFFFFFFF;s.ambient_source=D3DMCS_COLOR1;
    s.emissive_source=D3DMCS_COLOR2;
    hotd_light_vertex(&s,p,back,0xFF102030,0xFF010203,fvf|D3DFVF_DIFFUSE|D3DFVF_SPECULAR,&v);
    near_value(v.diffuse[0],17/255.0f);near_value(v.diffuse[1],34/255.0f);
    s=state();s.specular_enable=1;s.colorvertex=1;s.specular_source=D3DMCS_COLOR2;
    hotd_lighting_prepare(&s);hotd_light_vertex(&s,p,front,0,0xFF204080,fvf|D3DFVF_SPECULAR,&v);
    near_value(v.specular[0],.8f*32/255);

    /* Point light range and polynomial attenuation apply to ambient, diffuse and specular. */
    s=state();s.lights[0].dltType=D3DLIGHT_POINT;s.lights[0].dvRange=4;
    s.lights[0].dvAttenuation0=1;s.lights[0].dvAttenuation1=.25f;
    s.lights[0].dvAttenuation2=.0625f;s.lights[0].dcvAmbient=rgba(.3f,.3f,.3f,1);
    s.specular_enable=1;hotd_lighting_prepare(&s);
    hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.diffuse[0],.3f);near_value(v.specular[0],.4f/3);
    s.lights[0].dvRange=3.9f;hotd_lighting_prepare(&s);
    hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.diffuse[0],0);
    s.lights[0].dvRange=8;s.lights[0].dltType=D3DLIGHT_SPOT;
    s.lights[0].dvTheta=.6f;s.lights[0].dvPhi=1.2f;s.lights[0].dvFalloff=2;
    hotd_lighting_prepare(&s);hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.diffuse[0],.3f);
    const float outer[4]={4,0,4,.25f};
    hotd_light_vertex(&s,outer,front,0,0,fvf,&v);near_value(v.diffuse[0],0);
    const float penumbra[4]={2,0,4,.25f};
    hotd_light_vertex(&s,penumbra,front,0,0,fvf,&v);
    float dist=sqrtf(20),rho=4/dist;
    float spot=powf((rho-cosf(.6f))/(cosf(.3f)-cosf(.6f)),2);
    near_value(v.diffuse[0],(.3f+.6f*rho)*spot/(1+.25f*dist+.0625f*20));
    s.lights[0].dvPhi=0;hotd_lighting_prepare(&s);
    hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.diffuse[0],.3f);
    hotd_light_vertex(&s,penumbra,front,0,0,fvf,&v);near_value(v.diffuse[0],0);

    /* Both light position and vertex use camera space; translating the view changes neither distance nor diffuse. */
    s=state();s.lights[0].dltType=D3DLIGHT_POINT;s.lights[0].dvRange=10;s.lights[0].dvAttenuation0=1;
    s.view._41=2;s.view._42=3;s.view._43=4;hotd_lighting_prepare(&s);
    hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.diffuse[0],.6f);
    s.light_count=0;hotd_lighting_prepare(&s);
    hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.diffuse[0],0);
    s=state();s.view._11=0;s.view._13=1;s.view._31=-1;s.view._33=0;
    hotd_lighting_prepare(&s);hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.diffuse[0],.6f);

    s=state();s.lighting=0;s.specular_enable=1;hotd_lighting_prepare(&s);
    hotd_light_vertex(&s,p,front,0x80402010,0xFF102030,fvf|D3DFVF_DIFFUSE|D3DFVF_SPECULAR,&v);
    near_value(v.diffuse[0],64/255.0f);near_value(v.diffuse[3],128/255.0f);near_value(v.specular[2],48/255.0f);
    hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.diffuse[0],1);
    s.lighting=1;hotd_light_vertex(&s,p,NULL,0xFF402010,0,D3DFVF_XYZRHW|D3DFVF_DIFFUSE,&v);
    near_value(v.diffuse[0],64/255.0f);

    near_value(hotd_fog_factor(D3DFOG_LINEAR,3,1,5,1),.5f);
    near_value(hotd_fog_factor(D3DFOG_LINEAR,0,1,5,1),1);
    near_value(hotd_fog_factor(D3DFOG_LINEAR,6,1,5,1),0);
    near_value(hotd_fog_factor(D3DFOG_EXP,2,0,0,.5f),expf(-1));
    near_value(hotd_fog_factor(D3DFOG_EXP2,4,0,0,.5f),expf(-4));
    s=state();s.fog_enable=1;s.fog_vertex_mode=D3DFOG_LINEAR;s.fog_start=2;s.fog_end=6;
    hotd_lighting_prepare(&s);hotd_light_vertex(&s,side,front,0,0,fvf,&v);
    assert(v.fog_mode==1);near_value(v.fog,.5f);
    s.range_fog=1;hotd_light_vertex(&s,side,front,0,0,fvf,&v);near_value(v.fog,.25f);
    s.fog_vertex_mode=D3DFOG_EXP;s.fog_density=.25f;
    hotd_light_vertex(&s,side,front,0,0,fvf,&v);assert(v.fog_mode==1);near_value(v.fog,expf(-1.25f));
    s.fog_vertex_mode=D3DFOG_NONE;
    hotd_light_vertex(&s,side,front,0,0x80112233,fvf|D3DFVF_SPECULAR,&v);near_value(v.fog,128/255.0f);
    s.fog_table_mode=D3DFOG_LINEAR;s.fog_end=3;
    hotd_light_vertex(&s,side,front,0,0,fvf,&v);assert(v.fog_mode==1);near_value(v.fog,-1);
    s.fog_table_mode=D3DFOG_EXP;s.fog_density=.25f;
    hotd_light_vertex(&s,side,front,0,0,fvf,&v);assert(v.fog_mode==2);near_value(v.fog,1);
    s.fog_table_mode=D3DFOG_EXP2;
    hotd_light_vertex(&s,side,front,0,0,fvf,&v);assert(v.fog_mode==3);near_value(v.fog,1);
    s.fog_table_mode=D3DFOG_NONE;
    hotd_light_vertex(&s,p,NULL,0,0x40102030,D3DFVF_XYZRHW|D3DFVF_SPECULAR,&v);
    near_value(v.fog,64/255.0f);assert(v.fog_mode==1);
    s.fog_table_mode=D3DFOG_LINEAR;s.fog_start=2;s.fog_end=6;
    hotd_light_vertex(&s,p,NULL,0,0,D3DFVF_XYZRHW,&v);near_value(v.fog,.5f);
    s.fog_enable=0;hotd_light_vertex(&s,p,front,0,0,fvf,&v);near_value(v.fog,1);assert(v.fog_mode==0);
    puts("Native lighting: material sources, normal transforms, ambient/diffuse/emissive/specular, point/spot and fog passed.");
    return 0;
}
