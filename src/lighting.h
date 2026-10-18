#ifndef HOTD_LIGHTING_H
#define HOTD_LIGHTING_H
#include <windows.h>
#include <d3d.h>
#include <math.h>

#define HOTD_MAX_LIGHTS 16

typedef struct {
    D3DMATERIAL7 material;
    D3DMATRIX world,view;
    D3DLIGHT7 lights[HOTD_MAX_LIGHTS];
    unsigned light_count;
    DWORD ambient,lighting,colorvertex;
    DWORD diffuse_source,ambient_source,specular_source,emissive_source;
    DWORD specular_enable,local_viewer,normalize_normals;
    DWORD fog_enable,fog_vertex_mode,fog_table_mode,range_fog;
    float fog_start,fog_end,fog_density;
    float camera_world[16],normal_matrix[9];
    float camera_light_position[HOTD_MAX_LIGHTS][3],camera_light_direction[HOTD_MAX_LIGHTS][3];
} HotdLighting;

typedef struct {
    float diffuse[4],specular[3],fog;
    /* Shader modes: off, linear/vertex factor, exp coordinate, exp2 coordinate. */
    unsigned fog_mode;
} HotdLitVertex;

static inline float hotd_light_clamp(float x)
{ return x>0 ? (x<1 ? x : 1) : 0; }

static inline float hotd_light_dot(const float a[3],const float b[3])
{ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

static inline void hotd_light_normalize(float v[3])
{
    float d=hotd_light_dot(v,v);
    if(isfinite(d) && d>1e-30f) { d=1/sqrtf(d);v[0]*=d;v[1]*=d;v[2]*=d; }
    else v[0]=v[1]=v[2]=0;
}

static inline void hotd_light_transform(const float in[3],const float m[16],float w,float out[3])
{
    unsigned j;
    for(j=0;j<3;j++)out[j]=in[0]*m[j]+in[1]*m[4+j]+in[2]*m[8+j]+w*m[12+j];
}

/* Cache draw-constant transforms; lights remain in the world space returned by GetLight. */
static inline void hotd_lighting_prepare(HotdLighting *s)
{
    const float *w=(const float *)&s->world,*v=(const float *)&s->view;
    float *m=s->camera_world,*n=s->normal_matrix,det;
    unsigned i,j,k;
    for(i=0;i<4;i++)for(j=0;j<4;j++) {
        float value=0;
        for(k=0;k<4;k++)value+=w[i*4+k]*v[k*4+j];
        m[i*4+j]=value;
    }
    n[0]=m[5]*m[10]-m[6]*m[9];n[1]=m[6]*m[8]-m[4]*m[10];n[2]=m[4]*m[9]-m[5]*m[8];
    n[3]=m[9]*m[2]-m[10]*m[1];n[4]=m[10]*m[0]-m[8]*m[2];n[5]=m[8]*m[1]-m[9]*m[0];
    n[6]=m[1]*m[6]-m[2]*m[5];n[7]=m[2]*m[4]-m[0]*m[6];n[8]=m[0]*m[5]-m[1]*m[4];
    det=m[0]*n[0]+m[1]*n[1]+m[2]*n[2];
    if(!isfinite(det) || fabsf(det)<1e-20f)for(i=0;i<9;i++)n[i]=0;
    else for(i=0;i<9;i++)n[i]/=det;
    if(s->light_count>HOTD_MAX_LIGHTS)s->light_count=HOTD_MAX_LIGHTS;
    for(i=0;i<s->light_count;i++) {
        hotd_light_transform((const float *)&s->lights[i].dvPosition,v,1,s->camera_light_position[i]);
        hotd_light_transform((const float *)&s->lights[i].dvDirection,v,0,s->camera_light_direction[i]);
        hotd_light_normalize(s->camera_light_direction[i]);
    }
}

static inline D3DCOLORVALUE hotd_light_unpack(DWORD c)
{
    D3DCOLORVALUE out;
    out.r=((c>>16)&255)/255.0f;out.g=((c>>8)&255)/255.0f;
    out.b=(c&255)/255.0f;out.a=(c>>24)/255.0f;
    return out;
}

static inline D3DCOLORVALUE hotd_light_source(const HotdLighting *s,DWORD source,
    D3DCOLORVALUE material,DWORD diffuse,DWORD specular,DWORD fvf)
{
    if(s->colorvertex) {
        if(source==D3DMCS_COLOR1 && (fvf&D3DFVF_DIFFUSE))return hotd_light_unpack(diffuse);
        if(source==D3DMCS_COLOR2 && (fvf&D3DFVF_SPECULAR))return hotd_light_unpack(specular);
    }
    return material;
}

static inline float hotd_fog_factor(DWORD mode,float d,float start,float end,float density)
{
    if(mode==D3DFOG_LINEAR)return hotd_light_clamp(end!=start ? (end-d)/(end-start) : d<end ? 1 : 0);
    if(mode==D3DFOG_EXP)return hotd_light_clamp(expf(-density*d));
    if(mode==D3DFOG_EXP2) { float a=density*d;return hotd_light_clamp(expf(-a*a)); }
    return 1;
}

/* XYZ takes three position floats; prelit XYZRHW takes four, including reciprocal W for table fog. */
static inline void hotd_light_vertex(const HotdLighting *s,const float *position,const float normal[3],
    DWORD diffuse_argb,DWORD specular_argb,DWORD fvf,HotdLitVertex *out)
{
    int tl=(fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW;
    float p[3]={0,0,0};
    D3DCOLORVALUE diffuse=hotd_light_unpack((fvf&D3DFVF_DIFFUSE)?diffuse_argb:0xFFFFFFFFu);
    D3DCOLORVALUE specular=hotd_light_unpack((fvf&D3DFVF_SPECULAR)?specular_argb:0);
    unsigned i,k;
    out->specular[0]=out->specular[1]=out->specular[2]=0;
    if(!tl)hotd_light_transform(position,s->camera_world,1,p);
    if(!tl && s->lighting) {
        D3DCOLORVALUE d=hotd_light_source(s,s->diffuse_source,s->material.diffuse,diffuse_argb,specular_argb,fvf);
        D3DCOLORVALUE a=hotd_light_source(s,s->ambient_source,s->material.ambient,diffuse_argb,specular_argb,fvf);
        D3DCOLORVALUE e=hotd_light_source(s,s->emissive_source,s->material.emissive,diffuse_argb,specular_argb,fvf);
        D3DCOLORVALUE c=hotd_light_source(s,s->specular_source,s->material.specular,diffuse_argb,specular_argb,fvf);
        D3DCOLORVALUE ambient=hotd_light_unpack(s->ambient);
        const float *md=(const float *)&d,*ma=(const float *)&a,*me=(const float *)&e,*ms=(const float *)&c;
        float n[3]={0,0,0},eye[3]={0,0,1};
        if((fvf&D3DFVF_NORMAL) && normal) {
            for(k=0;k<3;k++)n[k]=normal[0]*s->normal_matrix[k]+normal[1]*s->normal_matrix[3+k]+normal[2]*s->normal_matrix[6+k];
            if(s->normalize_normals)hotd_light_normalize(n);
        }
        if(s->local_viewer) { eye[0]=-p[0];eye[1]=-p[1];eye[2]=-p[2];hotd_light_normalize(eye); }
        for(k=0;k<3;k++)out->diffuse[k]=me[k]+ma[k]*((const float *)&ambient)[k];
        out->diffuse[3]=hotd_light_clamp(d.a);
        for(i=0;i<s->light_count && i<HOTD_MAX_LIGHTS;i++) {
            const D3DLIGHT7 *l=s->lights+i;
            float direction[3],atten=1,spot=1,ndl,shine=0;
            if(l->dltType==D3DLIGHT_DIRECTIONAL) {
                for(k=0;k<3;k++)direction[k]=-s->camera_light_direction[i][k];
            } else if(l->dltType==D3DLIGHT_POINT || l->dltType==D3DLIGHT_SPOT) {
                float distance,denom;
                for(k=0;k<3;k++)direction[k]=s->camera_light_position[i][k]-p[k];
                distance=sqrtf(hotd_light_dot(direction,direction));
                if(!isfinite(distance) || distance>l->dvRange)continue;
                denom=l->dvAttenuation0+distance*(l->dvAttenuation1+distance*l->dvAttenuation2);
                if(!isfinite(denom) || denom<=0)continue;
                atten=1/denom;hotd_light_normalize(direction);
                if(l->dltType==D3DLIGHT_SPOT) {
                    float rho=-hotd_light_dot(direction,s->camera_light_direction[i]);
                    float inner=cosf(l->dvTheta*.5f),outer=cosf(l->dvPhi*.5f);
                    /* Native table lights can leave phi=0: retain the hard theta cone. */
                    if(rho<=inner)spot=rho<=outer ? 0 : powf(hotd_light_clamp((rho-outer)/(inner-outer)),l->dvFalloff);
                }
            } else continue;
            ndl=hotd_light_dot(n,direction);
            if(s->specular_enable && ndl>0) {
                float half[3]={direction[0]+eye[0],direction[1]+eye[1],direction[2]+eye[2]},nh;
                hotd_light_normalize(half);nh=hotd_light_dot(n,half);
                shine=powf(nh>0 ? nh : 0,s->material.power);
            }
            for(k=0;k<3;k++) {
                out->diffuse[k]+=atten*spot*(ma[k]*((const float *)&l->dcvAmbient)[k]+md[k]*((const float *)&l->dcvDiffuse)[k]*(ndl>0 ? ndl : 0));
                out->specular[k]+=atten*spot*ms[k]*((const float *)&l->dcvSpecular)[k]*shine;
            }
        }
        for(k=0;k<3;k++) { out->diffuse[k]=hotd_light_clamp(out->diffuse[k]);out->specular[k]=hotd_light_clamp(out->specular[k]); }
    } else {
        for(k=0;k<4;k++)out->diffuse[k]=((const float *)&diffuse)[k];
        if(s->specular_enable)for(k=0;k<3;k++)out->specular[k]=((const float *)&specular)[k];
    }
    out->fog=1;out->fog_mode=0;
    if(s->fog_enable) {
        if(s->fog_table_mode==D3DFOG_LINEAR || s->fog_table_mode==D3DFOG_EXP || s->fog_table_mode==D3DFOG_EXP2) {
            float depth=tl ? (isfinite(position[3]) && position[3]>0 ? 1/position[3] : 0) : p[2];
            if(s->fog_table_mode==D3DFOG_LINEAR) {
                out->fog=s->fog_end!=s->fog_start ? (s->fog_end-depth)/(s->fog_end-s->fog_start) : depth<s->fog_end ? 1 : 0;
                out->fog_mode=1;
            } else { out->fog=depth*s->fog_density;out->fog_mode=s->fog_table_mode==D3DFOG_EXP ? 2 : 3; }
        } else {
            out->fog_mode=1;
            if(!tl && s->fog_vertex_mode!=D3DFOG_NONE)
                out->fog=hotd_fog_factor(s->fog_vertex_mode,s->range_fog ? sqrtf(hotd_light_dot(p,p)) : p[2],s->fog_start,s->fog_end,s->fog_density);
            else out->fog=(fvf&D3DFVF_SPECULAR) ? specular.a : 1;
        }
    }
}
#endif
