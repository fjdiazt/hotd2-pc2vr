#ifndef HOTD_BAT_BOT_H
#define HOTD_BAT_BOT_H
#ifdef __cplusplus
extern "C" {
#endif
void hotd_bat_bot_init(unsigned char *base,const char *ini);
int hotd_bat_bot_poll(void);
int hotd_bat_bot_ready(void);
int hotd_bat_bot_pause(void);
void hotd_bat_bot_captured(int pause);
#ifdef __cplusplus
}
#endif
#endif
