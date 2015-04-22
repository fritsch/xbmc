#pragma once

#include "GLES2/gl2.h"
#include "EGL/egl.h"
#include "EGL/eglext.h"

#define NUM_PARTICLES 2000
#define NUMCONSTS 9
#define NUMEMITTERS 30

typedef struct Particle
{
    GLfloat pos[4];
    GLfloat shade[4];
} Particle;
 
typedef struct Emitter
{
    GLfloat  x, y, z;
} Emitter;

typedef struct
{
   GLuint verbose;
   GLuint mvshader;
   GLuint mshader;
   GLuint program_particle;
   GLuint tex_particle;
   GLuint particleBuffer;
// particle attribs
   GLuint aPos, aShade, uProjectionMatrix, uTexture;

   Emitter emitter[NUMEMITTERS];
   Particle particles[NUM_PARTICLES];
   void *tex_particle_data;

   float _c[NUMCONSTS];
   float _ct[NUMCONSTS];
   float _cv[NUMCONSTS];

  unsigned int numWinds;
  unsigned int numEmitters;
  unsigned int numParticles;
  unsigned int whichParticle;
  float size;
  float windSpeed;
  float emitterSpeed;
  float particleSpeed;
  float blur;
  float eVel;
} CUBE_STATE_T;


void solarwinds_init_shaders(CUBE_STATE_T *state);
void solarwinds_init(CUBE_STATE_T *state);
void solarwinds_update(CUBE_STATE_T *state);
void solarwinds_render(CUBE_STATE_T *state);
void solarwinds_deinit_shaders(CUBE_STATE_T *state);


