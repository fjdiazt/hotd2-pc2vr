/* Frame transport adapted from NeuralF's MIT-licensed VC2VR. */
#ifndef HOTD_SHARE_H
#define HOTD_SHARE_H
#include <stddef.h>
#include <stdint.h>
#include "capture_math.h"
#include "draw_style.h"
#define VC2_SHARE_NAME "Local\\HOTD2_PC2VR_Frame"
#define VC2_SHARE_MAGIC 0x32544F48u
#define VC2_SHARE_VERSION 8
#define VC2_MAX_VERTS (40000 * 6)
#define VC2_SCREEN_MAX_W 1280
#define VC2_SCREEN_MAX_H 960
#define VC2_ATLAS_W 4096
#define VC2_ATLAS_H 4096
#define VC2_BTN_FIRE 1u
#define VC2_BTN_START 2u
#define VC2_BTN_BACK 4u
#define VC2_BTN_RELOAD 8u
#define VC2_BTN_UP 16u
#define VC2_BTN_DOWN 32u
#define VC2_BTN_PAIR_START 64u
#define HOTD_CHEAT_AMMO 256u
#define HOTD_CHEAT_LIVES 512u
#define HOTD_CHEAT_RAPID 1024u
#define HOTD_CHEAT_MASK (HOTD_CHEAT_AMMO|HOTD_CHEAT_LIVES|HOTD_CHEAT_RAPID)

#pragma pack(push, 4)
typedef struct {
    float x, y, z;
    uint32_t colour;
    float u, v, depth;
    float atlas_x, atlas_y, atlas_w, atlas_h;
    uint32_t style;
} Vc2Vertex;

typedef struct {
    volatile uint32_t seq;
    uint32_t magic, version, frame, vertex_count;
    float fov_degrees, units_per_metre;
    uint32_t akimbo; /* Enable P2 input; PAIR_START controls automatic paired joining. */
    volatile uint32_t aim2_seq;
    float aim2_x, aim2_y;
    uint32_t aim2_buttons, reserved[3];
    volatile uint32_t aim_seq;
    float aim_x, aim_y;
    uint32_t aim_buttons;
    volatile uint32_t screen_seq;
    uint32_t screen_frame, screen_ok, screen_w, screen_h, screen_pad;
    volatile uint32_t atlas_seq;
    uint32_t atlas_frame;
    HotdLens lens;
    uint32_t game_pid, game_hwnd, heartbeat, input_supported;
    uint32_t draw_calls, dropped_triangles, texture_count, lens_valid;
    Vc2Vertex verts[VC2_MAX_VERTS];
    uint32_t screen[VC2_SCREEN_MAX_W * VC2_SCREEN_MAX_H];
    uint32_t atlas[VC2_ATLAS_W * VC2_ATLAS_H];
    volatile uint32_t cheat_flags,cheat_tick; /* Viewer -> game; independent of gun ownership. */
    volatile uint32_t rapid_fire_rate;
    uint32_t active_players; /* Game -> viewer: bit 0 P1, bit 1 P2; published under seq. */
    float shot_origins[64][3]; /* Exact camera-space anchors for grouped native flashes. */
} Vc2Frame;
#pragma pack(pop)
_Static_assert(sizeof(Vc2Vertex) == 48, "vertex layout changed");
_Static_assert(offsetof(Vc2Frame, verts) == 156, "frame layout changed");
_Static_assert(offsetof(Vc2Frame,cheat_flags)==83544220 && offsetof(Vc2Frame,shot_origins)==83544236 && sizeof(Vc2Frame)==83545004,"effect layout changed");
#endif
