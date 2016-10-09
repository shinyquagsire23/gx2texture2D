#ifndef DRAW_H
#define DRAW_H

#include <gx2/mem.h>
#include <gx2/draw.h>
#include <gx2/enum.h>
#include <gx2/swap.h>
#include <gx2/clear.h>
#include <gx2/state.h>
#include <gx2/texture.h>
#include <gx2/display.h>
#include <gx2/context.h>
#include <gx2/shaders.h>
#include <gx2/registers.h>

#include <string.h>
#include <stdlib.h>

#include "gl-matrix.h"

#define TARGET_WIDTH (1920)
#define TARGET_HEIGHT (1080)

GX2VertexShader *vertexShader;
GX2PixelShader *pixelShader;
GX2FetchShader *fetchShader;
GX2AttribStream *attributes;
void *fetchShaderProgramm;
mat4_t projectionMtx;

void clean_tex_allocs();
void render_texture_partial_color(GX2Texture *render_texture, float x_pos, float y_pos, float width, float height, int partial_x, int partial_y, int partial_width, int partial_height, float r, float g, float b, float a);
void render_texture_partial(GX2Texture *render_texture, float x_pos, float y_pos, float width, float height, int partial_x, int partial_y, int partial_width, int partial_height);
void render_texture_color(GX2Texture *render_texture, float x_pos, float y_pos, float width, float height, float r, float g, float b, float a);
void render_texture(GX2Texture *render_texture, float x_pos, float y_pos, float width, float height);
void render_texture_nostretch(GX2Texture *render_texture, float x_pos, float y_pos, float x_corner_size, float top_y_corner_size, float bottom_y_corner_size, float width, float height);

#endif
