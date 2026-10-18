/* Frame transport adapted from NeuralF's MIT-licensed VC2VR. */
#ifndef HOTD_SHARE_H
#define HOTD_SHARE_H
#include <stddef.h>
#include <stdint.h>
#include "capture_math.h"
#include "draw_style.h"
#define VC2_SHARE_NAME "Local\\HOTD2_PC2VR_Frame"
#define VC2_SHARE_MAGIC 0x32544F48u
#define VC2_SHARE_VERSION 16
#define VC2_MAX_VERTS (40000 * 6)
#define VC2_SCREEN_MAX_W 1280
#define VC2_SCREEN_MAX_H 960
#define VC2_ATLAS_W 4096
#define VC2_ATLAS_H 4096
#define HOTD_RENDER_UNLIT 1u
#define HOTD_RENDER_NATIVE_MESH_CULLING 2u
#define HOTD_RENDER_EXPAND_WORLD 4u
#define VC2_BTN_FIRE 1u
#define VC2_BTN_START 2u
#define VC2_BTN_BACK 4u
#define VC2_BTN_RELOAD 8u
#define VC2_BTN_UP 16u
#define VC2_BTN_DOWN 32u
#define VC2_BTN_PAIR_START 64u
#define VC2_BTN_LEFT 2048u
#define VC2_BTN_RIGHT 4096u
#define HOTD_CHEAT_AMMO 256u
#define HOTD_CHEAT_LIVES 512u
#define HOTD_CHEAT_RAPID 1024u
#define HOTD_CHEAT_INVINCIBILITY 8192u
#define HOTD_CHEAT_CONTINUES 16384u
#define HOTD_CHEAT_MASK (HOTD_CHEAT_AMMO|HOTD_CHEAT_LIVES|HOTD_CHEAT_RAPID|HOTD_CHEAT_INVINCIBILITY|HOTD_CHEAT_CONTINUES)

#pragma pack(push, 4)
typedef struct {
    float x, y, z;
    uint32_t colour;
    float u, v, depth;
    float atlas_x, atlas_y, atlas_w, atlas_h;
    uint32_t style, address; /* Native U/V addressing plus HOTD_GFILE presentation provenance. */
    uint32_t specular, fog_colour; /* RGBA; fog alpha stores mode 0/off, 1/linear, 2/exp, 3/exp2. */
    float fog; /* Native camera fog coordinate; evaluated after texturing/specular. */
} Vc2Vertex;

typedef struct {
    volatile uint32_t seq;
    uint32_t magic, version, frame, vertex_count;
    float fov_degrees, units_per_metre;
    uint32_t akimbo; /* Enable P2 input; PAIR_START controls automatic paired joining. */
    volatile uint32_t aim2_seq;
    float aim2_x, aim2_y;
    uint32_t aim2_buttons, reserved[3]; /* World count, native pause state, HUD boundary + 1. */
    volatile uint32_t aim_seq;
    float aim_x, aim_y;
    uint32_t aim_buttons;
    volatile uint32_t screen_seq;
    uint32_t screen_frame, screen_ok, screen_w, screen_h;
    volatile uint32_t render_flags; /* Viewer -> bridge; uses previously reserved screen padding. */
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
    volatile uint32_t audio_levels; /* Viewer -> game: master, music, effects percent in low three bytes. */
    uint32_t shot_events[2]; /* Game -> viewer: confirmed shots per player, published under seq. */
} Vc2Frame;
#pragma pack(pop)
_Static_assert(sizeof(Vc2Vertex) == 64, "vertex layout changed");
_Static_assert(offsetof(Vc2Frame, verts) == 156, "frame layout changed");
_Static_assert(offsetof(Vc2Frame,cheat_flags)==87384220 && offsetof(Vc2Frame,shot_origins)==87384236 && offsetof(Vc2Frame,audio_levels)==87385004 && offsetof(Vc2Frame,shot_events)==87385008 && sizeof(Vc2Frame)==87385016,"effect layout changed");
#endif
