#ifndef DRAW_H
#define DRAW_H

/*
 * Batched shape renderer. Every primitive is turned into triangles and pushed
 * into one big vertex buffer that is submitted with SDL_RenderGeometry, so
 * thousands of bullets cost only a handful of draw calls.
 */

#include "common.h"

void draw_init(SDL_Renderer *r);
SDL_Renderer *draw_renderer(void);

/* World-space offset (screen shake). Text is never offset. */
void draw_set_offset(V2 off);

void draw_blend_add(void);
void draw_blend_alpha(void);
void draw_flush(void);

void draw_tri(V2 a, V2 b, V2 c, SDL_FColor col);
void draw_quad(V2 a, V2 b, V2 c, V2 d, SDL_FColor col);
void draw_quad_grad(V2 a, V2 b, V2 c, V2 d, SDL_FColor ca, SDL_FColor cb, SDL_FColor cc, SDL_FColor cd);
void draw_rect(float x, float y, float w, float h, SDL_FColor col);
void draw_rect_outline(float x, float y, float w, float h, float t, SDL_FColor col);
void draw_line(V2 a, V2 b, float thick, SDL_FColor col);
void draw_circle(V2 c, float r, SDL_FColor col);
void draw_glow(V2 c, float r, SDL_FColor inner); /* radial gradient to transparent */
void draw_ring(V2 c, float r, float thick, SDL_FColor col);
void draw_arc(V2 c, float r, float thick, float a0, float a1, SDL_FColor col);
void draw_ngon(V2 c, float r, int sides, float angle, SDL_FColor col);
void draw_ngon_outline(V2 c, float r, int sides, float angle, float thick, SDL_FColor col);

/* Text uses SDL's built-in 8x8 debug font, scaled. ASCII only. */
void draw_text(float x, float y, float scale, SDL_FColor col, const char *fmt, ...);
void draw_text_centered(float cx, float y, float scale, SDL_FColor col, const char *fmt, ...);
float draw_text_width(const char *s, float scale);

#endif
