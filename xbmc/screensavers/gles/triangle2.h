#pragma once

#include "GLES2/gl2.h"
#include "EGL/egl.h"
#include "EGL/eglext.h"

#define NUMCONSTS 9

typedef struct
{
   GLuint verbose;
   GLuint effect_vshader;
   GLuint effect_fshader;
   GLuint render_vshader;
   GLuint render_fshader;
   GLuint effect_program;
   GLuint render_program;
   GLuint effect_fb;
   GLuint framebuffer_texture;
   GLuint vertex_buffer;
   GLuint effect_texture;
   void *effect_texture_data;
// effect attribs
   GLuint uTime, uScale, uResolution, uMouse;
   GLuint uChannel0;
// render attribs
   GLuint attr_vertex, uTexture;
   uint64_t last_time;
   float _c[NUMCONSTS];
   float _ct[NUMCONSTS];
   float _cv[NUMCONSTS];

// config settings
   int width, height;
   int texwidth, texheight;
   int fbwidth, fbheight;
   int mousex, mousey;
   float time;
   const GLchar *effect_vshader_source;
   const GLchar *effect_fshader_source;
   const GLchar *render_vshader_source;
   const GLchar *render_fshader_source;

} CUBE_STATE_T;


void screensaver_init_shaders(CUBE_STATE_T *state);
void screensaver_init(CUBE_STATE_T *state);
void screensaver_update(CUBE_STATE_T *state);
void screensaver_render(CUBE_STATE_T *state);
void screensaver_deinit_shaders(CUBE_STATE_T *state);

void setup_screensaver_default(CUBE_STATE_T *state);
void setup_screensaver_warp(CUBE_STATE_T *state);
void setup_screensaver_plasma(CUBE_STATE_T *state);
void setup_screensaver_plasma2(CUBE_STATE_T *state);
void setup_screensaver_noise(CUBE_STATE_T *state);
void setup_screensaver_border(CUBE_STATE_T *state);
void setup_screensaver_stellar(CUBE_STATE_T *state);
void setup_screensaver_spiral(CUBE_STATE_T *state);
void setup_screensaver_ball(CUBE_STATE_T *state);



