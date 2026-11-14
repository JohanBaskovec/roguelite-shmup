#include "draw.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MAX_VERTS 32768
#define MAX_INDICES (MAX_VERTS * 3)
#define CIRCLE_SEG 32

static SDL_Renderer *R;
static SDL_Vertex verts[MAX_VERTS];
static int indices[MAX_INDICES];
static int nverts, nindices;
static V2 offset;
static SDL_BlendMode blend = SDL_BLENDMODE_BLEND;
static float ctab[CIRCLE_SEG + 1], stab[CIRCLE_SEG + 1];

void draw_init(SDL_Renderer *r) {
    R = r;
    for (int i = 0; i <= CIRCLE_SEG; i++) {
        float a = TAU_F * (float)i / (float)CIRCLE_SEG;
        ctab[i] = cosf(a);
        stab[i] = sinf(a);
    }
}

SDL_Renderer *draw_renderer(void) { return R; }

void draw_set_offset(V2 off) { offset = off; }

void draw_flush(void) {
    if (nindices > 0) {
        /* With no texture, SDL_RenderGeometry uses the renderer blend mode. */
        SDL_SetRenderDrawBlendMode(R, blend);
        SDL_RenderGeometry(R, NULL, verts, nverts, indices, nindices);
    }
    nverts = 0;
    nindices = 0;
}

static void set_blend(SDL_BlendMode m) {
    if (m != blend) {
        draw_flush();
        blend = m;
    }
}

void draw_blend_add(void) { set_blend(SDL_BLENDMODE_ADD); }
void draw_blend_alpha(void) { set_blend(SDL_BLENDMODE_BLEND); }

static inline void ensure(int nv, int ni) {
    if (nverts + nv > MAX_VERTS || nindices + ni > MAX_INDICES) draw_flush();
}

static inline int vtx(float x, float y, SDL_FColor c) {
    SDL_Vertex *v = &verts[nverts];
    v->position.x = x + offset.x;
    v->position.y = y + offset.y;
    v->color = c;
    v->tex_coord.x = 0.0f;
    v->tex_coord.y = 0.0f;
    return nverts++;
}

static inline void tri_idx(int a, int b, int c) {
    indices[nindices++] = a;
    indices[nindices++] = b;
    indices[nindices++] = c;
}

/* Pick a segment step into the 32-entry table based on radius. */
static inline int seg_step(float r) {
    if (r < 5.0f) return 4;  /* 8 segments */
    if (r < 18.0f) return 2; /* 16 segments */
    return 1;                /* 32 segments */
}

void draw_tri(V2 a, V2 b, V2 c, SDL_FColor col) {
    ensure(3, 3);
    int i = vtx(a.x, a.y, col);
    vtx(b.x, b.y, col);
    vtx(c.x, c.y, col);
    tri_idx(i, i + 1, i + 2);
}

void draw_quad_grad(V2 a, V2 b, V2 c, V2 d, SDL_FColor ca, SDL_FColor cb, SDL_FColor cc, SDL_FColor cd) {
    ensure(4, 6);
    int i = vtx(a.x, a.y, ca);
    vtx(b.x, b.y, cb);
    vtx(c.x, c.y, cc);
    vtx(d.x, d.y, cd);
    tri_idx(i, i + 1, i + 2);
    tri_idx(i, i + 2, i + 3);
}

void draw_quad(V2 a, V2 b, V2 c, V2 d, SDL_FColor col) { draw_quad_grad(a, b, c, d, col, col, col, col); }

void draw_rect(float x, float y, float w, float h, SDL_FColor col) {
    draw_quad(v2(x, y), v2(x + w, y), v2(x + w, y + h), v2(x, y + h), col);
}

void draw_rect_outline(float x, float y, float w, float h, float t, SDL_FColor col) {
    draw_rect(x, y, w, t, col);
    draw_rect(x, y + h - t, w, t, col);
    draw_rect(x, y + t, t, h - 2 * t, col);
    draw_rect(x + w - t, y + t, t, h - 2 * t, col);
}

void draw_line(V2 a, V2 b, float thick, SDL_FColor col) {
    V2 d = v2_sub(b, a);
    float len = v2_len(d);
    if (len < 1e-4f) return;
    V2 n = v2_scale(v2_perp(d), thick * 0.5f / len);
    draw_quad(v2_add(a, n), v2_add(b, n), v2_sub(b, n), v2_sub(a, n), col);
}

static void circle_impl(V2 c, float r, SDL_FColor center, SDL_FColor rim) {
    int step = seg_step(r);
    int seg = CIRCLE_SEG / step;
    ensure(seg + 1, seg * 3);
    int base = vtx(c.x, c.y, center);
    for (int i = 0; i < seg; i++) vtx(c.x + ctab[i * step] * r, c.y + stab[i * step] * r, rim);
    for (int i = 0; i < seg; i++) tri_idx(base, base + 1 + i, base + 1 + (i + 1) % seg);
}

void draw_circle(V2 c, float r, SDL_FColor col) { circle_impl(c, r, col, col); }

void draw_glow(V2 c, float r, SDL_FColor inner) { circle_impl(c, r, inner, col_a(inner, 0.0f)); }

void draw_ring(V2 c, float r, float thick, SDL_FColor col) {
    int step = seg_step(r);
    int seg = CIRCLE_SEG / step;
    float ri = fmaxf(0.0f, r - thick * 0.5f), ro = r + thick * 0.5f;
    ensure(seg * 2, seg * 6);
    int base = nverts;
    for (int i = 0; i < seg; i++) {
        vtx(c.x + ctab[i * step] * ri, c.y + stab[i * step] * ri, col);
        vtx(c.x + ctab[i * step] * ro, c.y + stab[i * step] * ro, col);
    }
    for (int i = 0; i < seg; i++) {
        int j = (i + 1) % seg;
        tri_idx(base + i * 2, base + i * 2 + 1, base + j * 2 + 1);
        tri_idx(base + i * 2, base + j * 2 + 1, base + j * 2);
    }
}

void draw_arc(V2 c, float r, float thick, float a0, float a1, SDL_FColor col) {
    int seg = (int)(fabsf(a1 - a0) / TAU_F * 40.0f) + 2;
    float ri = fmaxf(0.0f, r - thick * 0.5f), ro = r + thick * 0.5f;
    ensure((seg + 1) * 2, seg * 6);
    int base = nverts;
    for (int i = 0; i <= seg; i++) {
        float a = lerpf(a0, a1, (float)i / (float)seg);
        float ca = cosf(a), sa = sinf(a);
        vtx(c.x + ca * ri, c.y + sa * ri, col);
        vtx(c.x + ca * ro, c.y + sa * ro, col);
    }
    for (int i = 0; i < seg; i++) {
        tri_idx(base + i * 2, base + i * 2 + 1, base + i * 2 + 3);
        tri_idx(base + i * 2, base + i * 2 + 3, base + i * 2 + 2);
    }
}

void draw_ngon(V2 c, float r, int sides, float angle, SDL_FColor col) {
    if (sides < 3) return;
    ensure(sides + 1, sides * 3);
    int base = vtx(c.x, c.y, col);
    for (int i = 0; i < sides; i++) {
        float a = angle + TAU_F * (float)i / (float)sides;
        vtx(c.x + cosf(a) * r, c.y + sinf(a) * r, col);
    }
    for (int i = 0; i < sides; i++) tri_idx(base, base + 1 + i, base + 1 + (i + 1) % sides);
}

void draw_ngon_outline(V2 c, float r, int sides, float angle, float thick, SDL_FColor col) {
    if (sides < 3) return;
    /* Push corners out so edges keep roughly the requested thickness. */
    float k = 1.0f / cosf(PI_F / (float)sides);
    float ri = fmaxf(0.0f, r - thick * 0.5f * k), ro = r + thick * 0.5f * k;
    ensure(sides * 2, sides * 6);
    int base = nverts;
    for (int i = 0; i < sides; i++) {
        float a = angle + TAU_F * (float)i / (float)sides;
        float ca = cosf(a), sa = sinf(a);
        vtx(c.x + ca * ri, c.y + sa * ri, col);
        vtx(c.x + ca * ro, c.y + sa * ro, col);
    }
    for (int i = 0; i < sides; i++) {
        int j = (i + 1) % sides;
        tri_idx(base + i * 2, base + i * 2 + 1, base + j * 2 + 1);
        tri_idx(base + i * 2, base + j * 2 + 1, base + j * 2);
    }
}

/* ---- Text ---------------------------------------------------------------- */

static void text_raw(float x, float y, float scale, SDL_FColor col, const char *s) {
    draw_flush();
    SDL_SetRenderScale(R, scale, scale);
    SDL_SetRenderDrawColorFloat(R, 0.0f, 0.0f, 0.0f, col.a * 0.75f);
    SDL_RenderDebugText(R, x / scale + 1.0f, y / scale + 1.0f, s);
    SDL_SetRenderDrawColorFloat(R, col.r, col.g, col.b, col.a);
    SDL_RenderDebugText(R, x / scale, y / scale, s);
    SDL_SetRenderScale(R, 1.0f, 1.0f);
}

float draw_text_width(const char *s, float scale) {
    return (float)strlen(s) * (float)SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * scale;
}

void draw_text(float x, float y, float scale, SDL_FColor col, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    text_raw(x, y, scale, col, buf);
}

void draw_text_centered(float cx, float y, float scale, SDL_FColor col, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    text_raw(cx - draw_text_width(buf, scale) * 0.5f, y, scale, col, buf);
}
