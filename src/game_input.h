#ifndef HOTD_GAME_INPUT_H
#define HOTD_GAME_INPUT_H
#include <stddef.h>
#include <stdint.h>

/* Native 0x28-byte gun state, shared by mouse and keyboard control schemes. */
typedef struct {
    int16_t raw_x, raw_y, x, y;
    float smooth_x, smooth_y;
    int16_t valid, tracked, fire;
    unsigned char rest[18];
} HotdGunInput;
_Static_assert(sizeof(HotdGunInput)==0x28,"Native gun input layout");
_Static_assert(offsetof(HotdGunInput,valid)==0x10 && offsetof(HotdGunInput,fire)==0x14,
    "Native gun valid/fire offsets");

static inline int hotd_apply_fire(HotdGunInput *gun,unsigned held,unsigned *previous)
{
    int pressed=held && !*previous;
    *previous=held;
    gun->valid=gun->tracked=1;
    gun->fire|=(int16_t)pressed;
    return pressed;
}
#endif
