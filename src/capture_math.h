#ifndef HOTD_CAPTURE_MATH_H
#define HOTD_CAPTURE_MATH_H
#include <math.h>
#include <stdint.h>

typedef struct { float fx, fy, cx, cy; } HotdLens;

/* Game coordinates use 640x480; the viewer uses right/up/backward axes. */
static inline int hotd_unproject(const HotdLens *l, float sx, float sy, float rhw, float p[3])
{
    float z;
    if (!isfinite(rhw) || rhw <= 0 || !isfinite(sx) || !isfinite(sy) ||
        !isfinite(l->fx) || !isfinite(l->fy) || l->fx <= 0 || l->fy <= 0) return 0;
    z = 1.0f / rhw;
    p[0] = (sx-l->cx) * z / l->fx;
    p[1] = -(sy-l->cy) * z / l->fy;
    p[2] = -z;
    return isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]);
}

static inline int hotd_project(const HotdLens *l, const float p[3], float *sx, float *sy)
{
    if (!isfinite(p[2]) || p[2] >= 0 || l->fx <= 0 || l->fy <= 0) return 0;
    *sx = l->cx - p[0] * l->fx / p[2];
    *sy = l->cy + p[1] * l->fy / p[2];
    return isfinite(*sx) && isfinite(*sy);
}

static inline unsigned hotd_fvf_stride(uint32_t fvf)
{
    unsigned pos = fvf & 0x400Eu, n, t, i;
    if (pos == 2) n = 12;
    else if (pos == 4 || pos == 0x4002) n = 16;
    else return 0;
    if (fvf & 0x10) n += 12;
    if (fvf & 0x20) n += 4;
    if (fvf & 0x40) n += 4;
    if (fvf & 0x80) n += 4;
    t = (fvf >> 8) & 15;
    if (t > 8) return 0;
    for (i = 0; i < t; i++) {
        static const unsigned dims[4] = { 2, 3, 4, 1 };
        n += 4 * dims[(fvf >> (16 + i*2)) & 3];
    }
    return n;
}
#endif
