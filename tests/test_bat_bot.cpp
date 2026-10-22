#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../tools/bat_bot.h"
static unsigned char *test_base;
template<class T> static T& at(unsigned address) {return *reinterpret_cast<T *>(test_base+address-0x400000);}
int main(int argc,char **argv)
{
    assert(argc==2);
    auto *base=static_cast<unsigned char *>(VirtualAlloc(nullptr,0x600000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(base);test_base=base;
    hotd_bat_bot_init(base,argv[1]);
    at<short>(0x9C8FD4)=7;
    assert(!hotd_bat_bot_poll() && at<short>(0x9C8FD4)==7);
    at<unsigned>(0x9C8E98)=6;at<short>(0x9A1A08)=3;at<short>(0x9A2BC0)=0;
    at<short>(0x9A5C62)=5;at<short>(0x9A5C66)=5;at<short>(0x9A5C7C)=6;
    at<unsigned>(0x9C8E00)=1;at<unsigned>(0x588E24)=6;at<float>(0x9A2D70)=640;
    at<unsigned>(0x5A4C80)=1;at<unsigned>(0x59D8E8)=0x800000;
    at<unsigned>(0x800000)=0x4533F0;at<unsigned>(0x800008)=9;
    at<unsigned>(0x800034)=1;at<float>(0x800078)=-10;at<float>(0x800124)=2;
    at<unsigned>(0x9A5C40)=1002;
    assert(hotd_bat_bot_poll());
    assert(at<short>(0x9C8FD4)==1 && at<short>(0x9C8FD0)==1);
    at<short>(0x9A1A00)=1;at<unsigned>(0x9A5C40)++;
    assert(hotd_bat_bot_poll() && at<short>(0x9C8FD4)==0);
    at<short>(0x9A1A00)=0;
    at<unsigned>(0x7DC918)=0x810000;at<unsigned>(0x810000)=0x42E230;
    at<unsigned>(0x81000C)=1;at<short>(0x8101F4)=0x1E;at<float>(0x810078)=-100;
    at<unsigned>(0x9A5C40)++;
    assert(hotd_bat_bot_poll() && !hotd_bat_bot_ready() && !at<short>(0x9C8FD4));
    at<float>(0x810078)=-25;at<unsigned>(0x9A5C40)++;
    assert(hotd_bat_bot_poll() && hotd_bat_bot_ready() && !at<short>(0x9C8FD4));
    at<unsigned>(0x7DC918)=0;at<unsigned>(0x9A5C40)++;
    assert(hotd_bat_bot_poll() && !at<short>(0x9C8FD4)); // Firing stays off after detection.
    at<unsigned>(0x7DF8A0)=0x810000;at<unsigned>(0x81001C)=0x820000;
    at<unsigned>(0x820000)=0x42F660;
    hotd_bat_bot_captured(0);assert(!hotd_bat_bot_pause());
    hotd_bat_bot_captured(1);assert(hotd_bat_bot_pause());assert(!hotd_bat_bot_pause());
    char path[MAX_PATH];strcpy(path,argv[1]);strcpy(strrchr(path,'\\')+1,"BatBotActors.bin");
    FILE *dump=fopen(path,"rb");assert(dump);unsigned header[4];
    assert(fread(header,sizeof(header),1,dump)==1 && header[3]==2);
    fseek(dump,0,SEEK_END);assert(ftell(dump)==16+2*(16+0x13D8));fclose(dump);
    at<short>(0x9A2BC0)=1;at<short>(0x9C8FD4)=7;
    assert(!hotd_bat_bot_poll() && at<short>(0x9C8FD4)==7);
    puts("Bat bot: scoped normal shot, pause suppression, bat stop, near trigger and pause acknowledgment passed.");
    VirtualFree(base,0,MEM_RELEASE);return 0;
}
