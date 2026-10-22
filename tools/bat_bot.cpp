/* Diagnostic build only. Reuses the existing read-only HOTD2 RE target selector. */
#define NOMINMAX
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <cstdint>
#include "bat_bot.h"
static unsigned char *bot_base;
static uintptr_t bot_address(uintptr_t address) {return reinterpret_cast<uintptr_t>(bot_base)+address-0x400000;}
#include "bat_autoplay.inc"

static FILE *bot_log;
static char bot_directory[MAX_PATH];
static bool bot_seen,bot_ready,bot_pause_pending,bot_failed;
static DWORD bot_started;
static uint32_t bot_frame=UINT32_MAX;
static autoplay::Decision bot_previous;
template<class T> static T& bot_at(uintptr_t address)
{return *reinterpret_cast<T *>(bot_base+address-0x400000);}

void Log(const char *fmt,...)
{
    if(!bot_log)return;
    va_list args;va_start(args,fmt);vfprintf(bot_log,fmt,args);va_end(args);
    fputc('\n',bot_log);fflush(bot_log);
}
void hotd_bat_bot_init(unsigned char *base,const char *ini)
{
    bot_base=base;
    if(strlen(ini)>=MAX_PATH) {bot_failed=true;return;}
    strcpy(bot_directory,ini);char *slash=strrchr(bot_directory,'\\');
    if(!slash) {bot_failed=true;return;}
    slash[1]=0;char path[MAX_PATH];snprintf(path,sizeof(path),"%sBatBot.log",bot_directory);
    bot_log=fopen(path,"w");if(!bot_log){bot_failed=true;return;}
    autoplay::Init(1);Log("batbot armed: chapter=4 scene=0; normal input only; timeout=120s; near=30");
}
int hotd_bat_bot_poll(void)
{
    if(bot_failed || bot_at<unsigned>(0x9C8E98)!=6 || bot_at<unsigned>(0x9CA08C)!=0 ||
       bot_at<short>(0x9A1A08)!=3 || bot_at<short>(0x9A2BC0)!=0)return 0;
    bot_at<short>(0x9C8FD4)=0; // Never carry a shot into the bat encounter or pause.
    if(!bot_started)bot_started=GetTickCount();
    if(GetTickCount()-bot_started>120000){Log("batbot timeout; input released");bot_failed=true;return 0;}
    if(bot_at<short>(0x9A1A00))return 1;
    float nearest=std::numeric_limits<float>::infinity();
    for(unsigned i=0;i<25;i++) {
        uintptr_t a=bot_at<uint32_t>(0x7DC918+4*i);
        if(a<0x10000)continue;
        if(bot_at<unsigned>(a)!=0x42E230 || !bot_at<unsigned>(a+12) || bot_at<short>(a+0x1F4)!=0x1E)continue;
        float z=bot_at<float>(a+0x78);
        if(std::isfinite(z) && z<-.8f)nearest=std::min(nearest,-z);
    }
    if(std::isfinite(nearest)) {
        if(!bot_seen)Log("batbot bats detected: boot_frame=%u distance=%g; firing stopped",bot_at<unsigned>(0x9A5C40),nearest);
        bot_seen=true;
        if(nearest<=30 && !bot_ready){bot_ready=true;Log("batbot capture ready: boot_frame=%u distance=%g",bot_at<unsigned>(0x9A5C40),nearest);}
    }
    if(bot_seen)return 1;
    uint32_t current=bot_at<unsigned>(0x9A5C40);
    if(current==bot_frame || bot_at<short>(0x9A5C62)!=5)return 1;
    bot_frame=current;
    autoplay::Decision d=autoplay::Decide(0,current,bot_previous);bot_previous=d;
    if(!(d.buttons&1))return 1;
    int x=static_cast<int>(d.x*640),y=static_cast<int>(d.y*480);
    if(x<0 || x>=640 || y<0 || y>=480)return 1;
    autoplay::CalTable cal;autoplay::BuildCal(0,cal);
    if(!cal.okx[x] || !cal.oky[y])return 1;
    bot_at<short>(0x9C8FC4)=static_cast<short>(cal.cx[x]);bot_at<short>(0x9C8FC6)=static_cast<short>(cal.cy[y]);
    bot_at<short>(0x9C8FD0)=bot_at<short>(0x9C8FD2)=bot_at<short>(0x9C8FD4)=1;
    Log("batbot shot: boot_frame=%u raw=%d,%d aim=%d,%d",current,x,y,cal.cx[x],cal.cy[y]);
    return 1;
}
int hotd_bat_bot_ready(void) {return bot_ready && !bot_failed;}
int hotd_bat_bot_pause(void) {bool pending=bot_pause_pending;bot_pause_pending=false;return pending;}
void hotd_bat_bot_captured(int pause)
{
    char path[MAX_PATH];snprintf(path,sizeof(path),"%sBatBotActors.bin",bot_directory);
    FILE *file=fopen(path,"wb");
    if(file) {
        uint32_t header[4]={0x54434142,1,bot_at<unsigned>(0x9A5C40),0};fwrite(header,sizeof(header),1,file);
        std::vector<uintptr_t> tasks{0x7DF878};unsigned visited=0;
        while(!tasks.empty() && visited++<512) {
            uintptr_t task=tasks.back();tasks.pop_back();
            for(uintptr_t child=bot_at<uint32_t>(task+0x28);child && visited++<512;child=bot_at<uint32_t>(child+0x1C)) {
                if(child<0x10000)break;
                tasks.push_back(child);unsigned fn=bot_at<unsigned>(child);
                if((fn==0x42E230 || fn==0x42F660) && header[3]<32) {
                    uint32_t record[4]={static_cast<uint32_t>(child),fn,bot_at<unsigned>(child+8),0x13D8};
                    fwrite(record,sizeof(record),1,file);fwrite(&bot_at<unsigned char>(child),0x13D8,1,file);header[3]++;
                }
            }
        }
        rewind(file);fwrite(header,sizeof(header),1,file);fclose(file);
        Log("batbot actors saved: count=%u",header[3]);
    }
    autoplay::LogStats();bot_pause_pending=pause!=0;
    Log("batbot captured; pause requested=%d",pause!=0);
}
