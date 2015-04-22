/*
Copyright (c) 2012, Broadcom Europe Ltd
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// OpenGL|ES 2 demo using shader to compute plasma/render sets
// Thanks to Peter de Rivas for original Python code

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>

#include "bcm_host.h"

#include "triangle2.h"

#define min(a,b) ((a)<(b)?(a):(b)) 
#define max(a,b) ((a)<(b)?(b):(a)) 
#define TO_STRING(...) #__VA_ARGS__

#define check() assert(glGetError() == 0)

static void showlog(GLint shader)
{
   // Prints the compile log for a shader
   char log[1024];
   glGetShaderInfoLog(shader, sizeof log, NULL, log);
   printf("%d:shader:\n%s\n", shader, log);
}

static void showprogramlog(GLint shader)
{
   // Prints the information log for a program object
   char log[1024];
   glGetProgramInfoLog(shader, sizeof log, NULL, log);
   printf("%d:program:\n%s\n", shader, log);
}

static void *create_particle_tex(int width, int height)
{
  int i, j;
  unsigned char *q = malloc(width * height * 4);
  if (!q)
    return NULL;
  unsigned char *p = q;
  for (j=0; j<height; j++) {
    for (i=0; i<width; i++) {
      float x = ((float)i + 0.5f) / (float)width  - 0.5f;
      float y = ((float)j + 0.5f) / (float)height - 0.5f;
      float d = 1.0f-2.0f*sqrtf(x*x + y*y);
      unsigned v = 255.0f * max(min(d, 1.0f), 0.0f);
      *p++ = 255;
      *p++ = 255;
      *p++ = 255;
      *p++ = v;
    }
  }
  return q;
}

static void *create_checkerboard_tex(int width, int height)
{
  int i, j;
  unsigned char *q = malloc(width * height * 4);
  if (!q)
    return NULL;
  unsigned char *p = q;
  for (j=0; j<height; j++) {
    for (i=0; i<width; i++) {
      int b = (i+j) & 1;
      *p++ = b ? 255:0;
      *p++ = b ? 255:0;
      *p++ = b ? 255:0;
      *p++ = b ? 255:0;
    }
  }
  return q;
}

static void *create_border_tex(int width, int height)
{
  int i, j;
  unsigned char *q = malloc(width * height * 4);
  if (!q)
    return NULL;
  unsigned char *p = q;
  for (j=0; j<height; j++) {
    for (i=0; i<width; i++) {
      int b = i == 16 || i == 20 || i == width-1-16 || j == 16 || j == height-1-16;
      *p++ = b ? 255:0;
      *p++ = b ? 255:0;
      *p++ = b ? 255:0;
      *p++ = b ? 255:0;
    }
  }
  return q;
}

static void *create_noise_tex(int width, int height)
{
  int i, j;
  unsigned char *q = malloc(width * height * 4);
  if (!q)
    return NULL;
  unsigned char *p = q;
  for (j=0; j<height; j++) {
    for (i=0; i<width; i++) {
      *p++ = (rand() >> 16) & 0xff;
      *p++ = (rand() >> 16) & 0xff;
      *p++ = (rand() >> 16) & 0xff;
      *p++ = 255;
    }
  }
  return q;
}

static void *create_framebuffer_tex(int width, int height)
{
#ifdef STANDALONE
  return create_border_tex(width, height);
#else
  unsigned char *q = malloc(width * height * 4);
  if (!q)
    return NULL;

  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, q);
  check();
  return q;
#endif
}

void screensaver_init_shaders(CUBE_STATE_T *state)
{
   static const GLfloat vertex_data[] = {
        -1.0,1.0,1.0,1.0,
        1.0,1.0,1.0,1.0,
        1.0,-1.0,1.0,1.0,
        -1.0,-1.0,1.0,1.0,
   };
        // effect
        state->effect_vshader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(state->effect_vshader, 1, &state->effect_vshader_source, 0);
        glCompileShader(state->effect_vshader);
        check();

        if (state->verbose)
            showlog(state->effect_vshader);
            
        state->effect_fshader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(state->effect_fshader, 1, &state->effect_fshader_source, 0);
        glCompileShader(state->effect_fshader);
        check();

        if (state->verbose)
            showlog(state->effect_fshader);

        state->effect_program = glCreateProgram();
        glAttachShader(state->effect_program, state->effect_vshader);
        glAttachShader(state->effect_program, state->effect_fshader);
        glLinkProgram(state->effect_program);
        glDetachShader(state->effect_program, state->effect_vshader);
        glDetachShader(state->effect_program, state->effect_fshader);
        glDeleteShader(state->effect_vshader);
        glDeleteShader(state->effect_fshader);
        check();

        if (state->verbose)
            showprogramlog(state->effect_program);

        state->attr_vertex   = glGetAttribLocation(state->effect_program,  "vertex");
        state->uResolution   = glGetUniformLocation(state->effect_program, "iResolution");
        state->uMouse        = glGetUniformLocation(state->effect_program, "iMouse");
        state->uTime         = glGetUniformLocation(state->effect_program, "iGlobalTime");
        state->uChannel0     = glGetUniformLocation(state->effect_program, "iChannel0");
        state->uScale        = glGetUniformLocation(state->effect_program, "uScale");
        check();

        // render
        state->render_vshader = glCreateShader(GL_VERTEX_SHADER);
        check();
        glShaderSource(state->render_vshader, 1, &state->render_vshader_source, 0);
        check();
        glCompileShader(state->render_vshader);
        check();

        if (state->verbose)
            showlog(state->render_vshader);
            
        state->render_fshader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(state->render_fshader, 1, &state->render_fshader_source, 0);
        glCompileShader(state->render_fshader);
        check();

        if (state->verbose)
            showlog(state->render_fshader);

        state->render_program = glCreateProgram();
        glAttachShader(state->render_program, state->render_vshader);
        glAttachShader(state->render_program, state->render_fshader);
        glLinkProgram(state->render_program);
        glDetachShader(state->render_program, state->render_vshader);
        glDetachShader(state->render_program, state->render_fshader);
        glDeleteShader(state->render_vshader);
        glDeleteShader(state->render_fshader);
        check();

        if (state->verbose)
            showprogramlog(state->render_program);

	state->uTexture      = glGetUniformLocation(state->render_program, "uTexture");
        check();
           
        if (state->fbwidth && state->fbheight)
        {
          // Prepare a texture to render to
          glGenTextures(1, &state->framebuffer_texture);
          check();
          glActiveTexture(GL_TEXTURE0);
          glBindTexture(GL_TEXTURE_2D, state->framebuffer_texture);
          check();
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, state->fbwidth, state->fbheight, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);
          check();
          glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
          check();
          // Prepare a framebuffer for rendering
          glGenFramebuffers(1, &state->effect_fb);
          check();
          glBindFramebuffer(GL_FRAMEBUFFER, state->effect_fb);
          check();
          glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, state->framebuffer_texture, 0);
          check();
          glBindFramebuffer(GL_FRAMEBUFFER, 0);
          check();
        }
        // Prepare a texture image
        if (state->effect_texture_data)
        {
          glGenTextures(1, &state->effect_texture);
          check();
          glActiveTexture(GL_TEXTURE0);
          glBindTexture(GL_TEXTURE_2D, state->effect_texture);
          check();
          glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, state->texwidth, state->texheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, state->effect_texture_data);
          check();
        }
        // Upload vertex data to a buffer
        glGenBuffers(1, &state->vertex_buffer);
        glBindBuffer(GL_ARRAY_BUFFER, state->vertex_buffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_data), vertex_data, GL_STATIC_DRAW);
        check();
}

void screensaver_deinit_shaders(CUBE_STATE_T *state)
{
  glDeleteProgram(state->render_program);
  glDeleteProgram(state->effect_program);
  check();

  glDeleteBuffers(1, &state->vertex_buffer);
  if (state->framebuffer_texture)
  {
    glDeleteTextures(1, &state->framebuffer_texture);
    check();
  }
  if (state->effect_fb)
  {
    glDeleteFramebuffers(1, &state->effect_fb);
    check();
  }
  if (state->effect_texture)
  {
    glDeleteTextures(1, &state->effect_texture);
    check();
  }
  if (state->effect_texture_data)
    free(state->effect_texture_data);
}

static void draw_effect_to_texture(CUBE_STATE_T *state)
{
        // Draw the effect to a texture
        if (state->effect_fb)
          glBindFramebuffer(GL_FRAMEBUFFER, state->effect_fb);
        else
          glBindFramebuffer(GL_FRAMEBUFFER, 0);
        check();

	glBindBuffer(GL_ARRAY_BUFFER, state->vertex_buffer);
        check();
        glUseProgram( state->effect_program );
        check();
        if (state->effect_texture)
        {
          glActiveTexture(GL_TEXTURE0);
          glBindTexture(GL_TEXTURE_2D, state->effect_texture);
          check();
          //glEnable(GL_BLEND);
          //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        glUniform3f(state->uResolution, state->width, state->height, 1.0f);
        glUniform2f(state->uMouse, state->mousex, state->mousey);
        glUniform1f(state->uTime, state->time);
        glUniform1i(state->uChannel0, 0); // first currently bound texture "GL_TEXTURE0"
        if (state->effect_fb)
          glUniform2f(state->uScale, (GLfloat)state->width/state->fbwidth, (GLfloat)state->height/state->fbheight);
        else
          glUniform2f(state->uScale, 1.0, 1.0);
        check();

        glVertexAttribPointer(state->attr_vertex, 4, GL_FLOAT, 0, 16, 0);
        glEnableVertexAttribArray(state->attr_vertex);

        glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );
        check();

	glDisableVertexAttribArray(state->attr_vertex);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        check();
}

static void draw_triangles(CUBE_STATE_T *state)
{
        // already on framebuffer
        if (!state->framebuffer_texture)
          return;
        // Now render to the main frame buffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        check();

        glBindBuffer(GL_ARRAY_BUFFER, state->vertex_buffer);
        check();
        glUseProgram ( state->render_program );
        check();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, state->framebuffer_texture);
        check();
        glUniform1i(state->uTexture, 0); // first currently bound texture "GL_TEXTURE0"
        check();

        glVertexAttribPointer(state->attr_vertex, 4, GL_FLOAT, 0, 16, 0);
        glEnableVertexAttribArray(state->attr_vertex);
        check();

        glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );
        check();

	glDisableVertexAttribArray(state->attr_vertex);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        check();
}

//==============================================================================

static float randrange(float min, float max)
{
   return min + rand() * ((max-min) / RAND_MAX);
}

void screensaver_init(CUBE_STATE_T *state)
{
  int i;
  for (i = 0; i < NUMCONSTS; ++i) {
    state->_ct[i] = randrange(0.0f, M_PI * 2.0f);
    state->_cv[i] = randrange(0.0f, 0.00005f) + 
                    0.00001f;
  }
}

uint64_t GetTimeStamp() {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return tv.tv_sec*(uint64_t)1000000+tv.tv_usec;
}

void screensaver_update(CUBE_STATE_T *state)
{
  int i;
	// update constants
	for (i = 0; i < NUMCONSTS; ++i) {
		state->_ct[i] += state->_cv[i];
		if (state->_ct[i] > M_PI * 2.0f)
		  state->_ct[i] -= M_PI * 2.0f;
		state->_c[i] = cosf(state->_ct[i]);
	}
  uint64_t t = GetTimeStamp();
  if (state->last_time)
     state->time += (t-state->last_time) * 1e-6;
  state->last_time = t;
  //if (state->time > 12.0f + M_PI)
  //  state->time -= 12.0f * M_PI;
}

void screensaver_render(CUBE_STATE_T *state)
{ 
  draw_effect_to_texture(state);
  draw_triangles(state);
}

void setup_screensaver_default(CUBE_STATE_T *state)
{
  state->effect_vshader_source = TO_STRING(
         attribute vec4 vertex;
         varying vec2 vTextureCoord;
         uniform vec2 uScale;
         void main(void)
         {
            gl_Position = vertex;
            vTextureCoord = vertex.xy*0.5+0.5;
            vTextureCoord.x = vTextureCoord.x * uScale.x;
            vTextureCoord.y = vTextureCoord.y * uScale.y;
         }
  );
  state->effect_fshader_source = TO_STRING(
	varying vec2 vTextureCoord;
	uniform float iGlobalTime;
	uniform vec3 iResolution;
	uniform vec2 iMouse;
	uniform sampler2D iChannel0;
  );
  state->render_vshader_source = TO_STRING(
         attribute vec4 vertex;
         varying vec2 vTextureCoord;
         void main(void)
         {
            gl_Position = vertex;
            vTextureCoord = vertex.xy*0.5+0.5;
         }
  );
  state->render_fshader_source = TO_STRING(
         varying vec2 vTextureCoord;
         uniform sampler2D uTexture;
         void main(void)
         {
            gl_FragColor = texture2D(uTexture, vTextureCoord);
            //gl_FragColor = vec4(vTextureCoord.x, vTextureCoord.y, 0.0, 1.0);
         }
  );
}

void setup_screensaver_plasma(CUBE_STATE_T *state)
{
  state->fbwidth = 960; state->fbheight = 540;
  state->effect_fshader_source = TO_STRING(
	precision lowp float;
	varying vec2 vTextureCoord;
	uniform highp float iGlobalTime;
	uniform vec3 iResolution;
	uniform vec2 iMouse;
	uniform sampler2D iChannel0;

	float     u_time=iGlobalTime*0.2;
        vec2      u_k = vec2(32.0, 32.0);
        precision mediump float;
        const float PI=3.1415926535897932384626433832795;
        void main()
        {
           float v = 0.0;
           vec2 c = vTextureCoord * u_k - u_k/2.0;
           v += sin((c.x+u_time));
           v += sin((c.y+u_time)/2.0);
           v += sin((c.x+c.y+u_time)/2.0);
           c += u_k/2.0 * vec2(sin(u_time/3.0), cos(u_time/2.0));
           v += sin(sqrt(c.x*c.x+c.y*c.y+1.0)+u_time);
           v = v/2.0;
           vec3 col = vec3(1.0, sin(PI*v), cos(PI*v));
           gl_FragColor = vec4(col*0.5 + 0.5, 1.0);
       }
  );
}


void setup_screensaver_plasma2(CUBE_STATE_T *state)
{
  state->fbwidth = 640; state->fbheight = 360;
  state->effect_fshader_source = TO_STRING(
	precision lowp float;
	varying vec2 vTextureCoord;
	uniform highp float iGlobalTime;
	uniform vec3 iResolution;
	uniform vec2 iMouse;
	uniform sampler2D iChannel0;

	float     u_time=iGlobalTime*0.2;
        vec2      u_k = vec2(32.0, 32.0);
        precision mediump float;
        const float PI=3.1415926535897932384626433832795;

		void mainImage( out vec4 fragColor, in vec2 fragCoord )
		{
			vec2 p = -1.0 + 2.0 * fragCoord.xy / iResolution.xy;
	
		// main code, *original shader by: 'Plasma' by Viktor Korsun (2011)
		float x = p.x;
		float y = p.y;
		float mov0 = x+y+cos(sin(iGlobalTime)*2.0)*100.+sin(x/100.)*1000.;
		float mov1 = y / 0.9 +  iGlobalTime;
		float mov2 = x / 0.2;
		float c1 = abs(sin(mov1+iGlobalTime)/2.+mov2/2.-mov1-mov2+iGlobalTime);
		float c2 = abs(sin(c1+sin(mov0/1000.+iGlobalTime)+sin(y/40.+iGlobalTime)+sin((x+y)/100.)*3.));
		float c3 = abs(sin(c2+cos(mov1+mov2+c2)+cos(mov2)+sin(x/1000.)));
		fragColor = vec4(c1,c2,c3,1.0);
	
		}

	void main () {
	    vec4 fragColor;
	    vec2 fragCoord = vTextureCoord * vec2(iResolution.x, iResolution.y);
	    mainImage(fragColor, fragCoord);
	    gl_FragColor = fragColor;
	    gl_FragColor.a = 1.0;
	}
  );
}


void setup_screensaver_border(CUBE_STATE_T *state)
{
  state->texwidth = state->texheight = 256;
  state->fbwidth = state->fbheight = 256;
  state->effect_texture_data = create_border_tex(state->texwidth, state->texheight);
  state->effect_fshader_source = TO_STRING(
	varying vec2 vTextureCoord;
	uniform float iGlobalTime;
	uniform vec3 iResolution;
	uniform vec2 iMouse;
	uniform sampler2D iChannel0;

         void main(void)
         {
            vec2 v = vTextureCoord;
            vec4 texture = texture2D(iChannel0, v);
            gl_FragColor = texture;
         }
  );
}


void setup_screensaver_spiral(CUBE_STATE_T *state)
{
  state->fbwidth = 800; state->fbheight = 450;
  state->effect_fshader_source = TO_STRING(
	precision lowp float;
	varying vec2 vTextureCoord;
	uniform highp float iGlobalTime;
	uniform vec3 iResolution;
	uniform vec2 iMouse;
	uniform sampler2D iChannel0;

	float     u_time=iGlobalTime*0.2;
        vec2      u_k = vec2(32.0, 32.0);
        precision mediump float;
        const float PI=3.1415926535897932384626433832795;
	const float TAU=6.283185307179586;

	void mainImage( out vec4 fragColor, in vec2 fragCoord )
	{
		vec2 p = 2.0*(0.5 * iResolution.xy - fragCoord.xy) / iResolution.xx;
		float angle = atan(p.y, p.x);
		float turn = (angle + PI) / TAU;
		float radius = sqrt(p.x*p.x + p.y*p.y);
	
		float rotation = 0.04 * TAU * iGlobalTime;
		float turn_1 = turn + rotation;
	
		float n_sub = 2.0;
	
		float turn_sub = mod(float(n_sub) * turn_1, float(n_sub));
	
		float k_sine = 0.1 * sin(3.0 * iGlobalTime);
		float sine = k_sine * sin(50.0 * (pow(radius, 0.1) - 0.4 * iGlobalTime));
		float turn_sine = turn_sub + sine;

		int n_colors = 5;
		int i_turn = int(mod(float(n_colors) * turn_sine, float(n_colors)));
	
		int i_radius = int(1.5/pow(radius*0.5, 0.6) + 5.0 * iGlobalTime);
		
		int i_color = int(mod(float(i_turn + i_radius), float(n_colors)));
	
		vec3 color;
		if(i_color == 0) { 
			color = vec3(1.0, 1.0, 1.0);		  
		} else if(i_color == 1) {
			color = vec3(0.0, 0.0, 0.0);	
		} else if(i_color == 2) {
			color = vec3(1.0, 0.0, 0.0);	
		} else if(i_color == 3) {
			color = vec3(1.0, 0.5, 0.0);	
		} else if(i_color == 4) {
			color = vec3(1.0, 1.0, 0.0);	
		}
	
		color *= pow(radius, 0.5)*1.0;
	
		fragColor = vec4(color, 1.0);
	}

	void main () {
	    vec4 fragColor;
	    vec2 fragCoord = vTextureCoord * vec2(iResolution.x, iResolution.y);
	    mainImage(fragColor, fragCoord);
	    gl_FragColor = fragColor;
	    gl_FragColor.a = 1.0;
	}
  );
}

void setup_screensaver_ball(CUBE_STATE_T *state)
{
  state->texwidth = state->width;
  state->texheight = state->height;
  state->fbwidth = 960;
  state->fbheight = 540;
  state->effect_texture_data = create_framebuffer_tex(state->texwidth, state->texheight);

  state->effect_fshader_source = TO_STRING(
	precision lowp float;
	varying vec2 vTextureCoord;
	uniform highp float iGlobalTime;
	uniform vec3 iResolution;
	uniform vec2 iMouse;
	uniform sampler2D iChannel0;

	float     u_time=iGlobalTime*0.2;
        vec2      u_k = vec2(32.0, 32.0);
        precision mediump float;
        const float PI=3.1415926535897932384626433832795;

	// by maq/floppy
	const float R=0.2;      // to play
	void mainImage( out vec4 fragColor, in vec2 fragCoord )
	{
		vec3 col;
		vec2 uv = -0.5+fragCoord.xy / iResolution.xy;
		uv.y*=0.66; // hack to get ar nice on 16:10
		vec2 p = uv;
		float d=sqrt(dot(p,p));
		float fac,fac2;
		if(d<R)
		{
			uv.x=p.x/(R+sqrt(R-d));
			uv.y=p.y/(R+sqrt(R-d));
			fac = 0.005;
			fac2 = 5.0;
		}
		else
		{
			uv.x=p.x/(d*d);
			uv.y=p.y/(d*d);
			fac = 0.02;
			fac2 = 25.0;
		}
	
		uv.x=uv.x-iMouse.x*fac+fac*500.0*sin(0.2*iGlobalTime);
		uv.y=uv.y-iMouse.y*fac+fac*500.0*sin(0.4*iGlobalTime);
		col = texture2D(iChannel0, uv/fac2).xyz;
		col = col*exp(-3.0*(d-R)); // some lighting
		col = col*(1.1-exp(-8.0*(abs(d-R)))); // and shading
	
	
		fragColor = vec4(col,1.0);
	}
	void main () {
	    vec4 fragColor;
	    vec2 fragCoord = vTextureCoord * vec2(iResolution.x, iResolution.y);
	    mainImage(fragColor, fragCoord);
	    gl_FragColor = fragColor;
	    gl_FragColor.a = 1.0;
	}
  );
}


void setup_screensaver_stellar(CUBE_STATE_T *state)
{
  state->texwidth = 256; state->texheight = 256;
  state->effect_texture_data = create_noise_tex(state->texwidth, state->texheight);
  state->fbwidth = 640; state->fbheight = 360;

  state->effect_fshader_source = TO_STRING(
	precision lowp float;
	varying vec2 vTextureCoord;
	uniform highp float iGlobalTime;
	uniform vec3 iResolution;
	uniform vec2 iMouse;
	uniform sampler2D iChannel0;

	const float tau = 6.28318530717958647692;
	vec4 Noise( in ivec2 x )
	{
		return texture2D( iChannel0, (vec2(x)+0.5)/256.0, -100.0 );
	}
	void main()
	{
		vec3 ray = vec3(vTextureCoord - vec2(0.5), 1.0);
		float offset = iGlobalTime*.5;	
		float speed2 = (cos(offset)+1.0)*2.0;
		float speed = speed2+.1;
		float ispeed = 1.0/speed;
		offset += sin(offset)*.96;
		offset *= 2.0;	
		vec3 col = vec3(0);
		vec3 stp = ray/max(abs(ray.x),abs(ray.y));
		vec3 pos = 2.0*stp+.5;
		for ( int i=0; i < 10; i++ )
		{
			float z = Noise(ivec2(pos.xy)).x;
			z = fract(z-offset);
			float d = 50.0*z-pos.z;
			float w = max(0.0, 1.0-8.0*length(fract(pos.xy)-.5));
			vec3 c = max(vec3(0), vec3(1.0-abs(d+speed2*.5)*ispeed,1.0-abs(d)*ispeed,1.0-abs(d-speed2*.5)*ispeed));
			col += 1.5*(1.0-z)*c*w*w;
			pos += stp;
		}
		gl_FragColor = vec4(col,1.0);
	}
  );
}

void setup_screensaver_noise(CUBE_STATE_T *state)
{
  state->fbwidth = 960; state->fbheight = 540;
  state->effect_fshader_source = TO_STRING(
	precision lowp float;
	varying vec2 vTextureCoord;
	uniform highp float iGlobalTime;
	uniform vec3 iResolution;
	uniform vec2 iMouse;
	uniform sampler2D iChannel0;

	float     time=iGlobalTime*0.1;
	float pi = 3.14159265;

	float rand(vec2 co)
	{
	  return fract(sin(dot(co.xy, vec2(12.9898,78.233))) * 43758.5453);
	}

	void main()
	{
	  float r = rand(vec2(1.0*iGlobalTime+vTextureCoord.x, 3.0*iGlobalTime+vTextureCoord.y));
	  float g = rand(vec2(2.0*iGlobalTime+vTextureCoord.x, 2.0*iGlobalTime+vTextureCoord.y));
	  float b = rand(vec2(3.0*iGlobalTime+vTextureCoord.x, 1.0*iGlobalTime+vTextureCoord.y));
	  gl_FragColor = vec4(r, g, b, 1.0);
	}
  );
}

void setup_screensaver_warp(CUBE_STATE_T *state)
{
  state->texwidth = state->width;
  state->texheight = state->height;
  state->fbwidth = 800;
  state->fbheight = 450;
  state->effect_texture_data = create_framebuffer_tex(state->texwidth, state->texheight);

  state->effect_fshader_source = TO_STRING(
	precision lowp float;
	varying vec2 vTextureCoord;
	uniform highp float iGlobalTime;
	uniform vec3 iResolution;
	uniform vec2 iMouse;
	uniform sampler2D iChannel0;

	float     time=iGlobalTime*0.1;
	const float pi = 3.14159265;
	const float sin_4000 = 0.6427876097;
	const float cos_4000 = 0.7660444431;
	const float sin_6000 = -0.8660254038;
	const float cos_6000 = -0.5;

	void main()
	{
	  float s = sin(time);
	  float c = cos(time);
	  float s2 = 2.0*s*c;
	  float c2 = 1.0-2.0*s*s;
	  float s3 = s2*c + c2*s;
	  float c3 = c2*c - s2*s;
	  float ss = s2*cos_4000 + c2*sin_4000;
	  float cc = c3*cos_6000 - s3*sin_6000;
	  vec2 offset2   = vec2(6.0*sin(time*1.1),              3.0*cos(time*1.1));
	  vec2 oldPos = vTextureCoord.xy - vec2(0.5, 0.5);
	  vec2 newPos = vec2(oldPos.x * c2 - oldPos.y * s2,
		             oldPos.y * c2 + oldPos.x * s2);
	  newPos = newPos*(1.0+0.2*s3) - offset2;
	  vec2 temp = newPos;
	  float beta = sin(temp.y*2.0 + time*8.0);
	  newPos.x = temp.x + 0.4*beta;
	  newPos.y = temp.y - 0.4*beta;
	  gl_FragColor = texture2D(iChannel0, newPos);
	}
  );
}

#ifdef STANDALONE

typedef struct
{
   uint32_t screen_width;
   uint32_t screen_height;
// OpenGL|ES objects
   EGLDisplay display;
   EGLSurface surface;
   EGLContext context;
} EGL_STATE_T;

/***********************************************************
 * Name: init_ogl
 *
 * Arguments:
 *       CUBE_STATE_T *state - holds OGLES model info
 *
 * Description: Sets the display, OpenGL|ES context and screen stuff
 *
 * Returns: void
 *
 ***********************************************************/
static void init_ogl(EGL_STATE_T *state)
{
   int32_t success = 0;
   EGLBoolean result;
   EGLint num_config;

   static EGL_DISPMANX_WINDOW_T nativewindow;

   DISPMANX_ELEMENT_HANDLE_T dispman_element;
   DISPMANX_DISPLAY_HANDLE_T dispman_display;
   DISPMANX_UPDATE_HANDLE_T dispman_update;
   VC_RECT_T dst_rect;
   VC_RECT_T src_rect;

   static const EGLint attribute_list[] =
   {
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_NONE
   };
   
   static const EGLint context_attributes[] = 
   {
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE
   };
   EGLConfig config;

   // get an EGL display connection
   state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   assert(state->display!=EGL_NO_DISPLAY);
   check();

   // initialize the EGL display connection
   result = eglInitialize(state->display, NULL, NULL);
   assert(EGL_FALSE != result);
   check();

   // get an appropriate EGL frame buffer configuration
   result = eglChooseConfig(state->display, attribute_list, &config, 1, &num_config);
   assert(EGL_FALSE != result);
   check();

   // get an appropriate EGL frame buffer configuration
   result = eglBindAPI(EGL_OPENGL_ES_API);
   assert(EGL_FALSE != result);
   check();

   // create an EGL rendering context
   state->context = eglCreateContext(state->display, config, EGL_NO_CONTEXT, context_attributes);
   assert(state->context!=EGL_NO_CONTEXT);
   check();

   // create an EGL window surface
   success = graphics_get_display_size(0 /* LCD */, &state->screen_width, &state->screen_height);
   assert( success >= 0 );

   dst_rect.x = 0;
   dst_rect.y = 0;
   dst_rect.width = state->screen_width;
   dst_rect.height = state->screen_height;
      
   src_rect.x = 0;
   src_rect.y = 0;
   src_rect.width = state->screen_width << 16;
   src_rect.height = state->screen_height << 16;        

   dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
   dispman_update = vc_dispmanx_update_start( 0 );
         
   dispman_element = vc_dispmanx_element_add ( dispman_update, dispman_display,
      0/*layer*/, &dst_rect, 0/*src*/,
      &src_rect, DISPMANX_PROTECTION_NONE, 0 /*alpha*/, 0/*clamp*/, 0/*transform*/);
      
   nativewindow.element = dispman_element;
   nativewindow.width = state->screen_width;
   nativewindow.height = state->screen_height;
   vc_dispmanx_update_submit_sync( dispman_update );
      
   check();

   state->surface = eglCreateWindowSurface( state->display, config, &nativewindow, NULL );
   assert(state->surface != EGL_NO_SURFACE);
   check();

   // connect the context to the surface
   result = eglMakeCurrent(state->display, state->surface, state->surface, state->context);
   assert(EGL_FALSE != result);
   check();
}

int main ()
{
   int terminate = 0;
   CUBE_STATE_T _state, *state=&_state;
   EGL_STATE_T _eglstate, *eglstate=&_eglstate;

   // Clear application state
   memset( eglstate, 0, sizeof( *eglstate ) );
   bcm_host_init();
   // Start OGLES
   init_ogl(eglstate);
again:
   memset( state, 0, sizeof( *state ) );
   state->verbose = 1;
   state->width = eglstate->screen_width;
   state->height = eglstate->screen_height;
   setup_screensaver_default(state);
   setup_screensaver_warp(state);
   screensaver_init(state);
   screensaver_init_shaders(state);

   int frames = 0;
   uint64_t ts = GetTimeStamp();
   while (!terminate)
   {
      screensaver_update(state);
      screensaver_render(state);
      eglSwapBuffers(eglstate->display, eglstate->surface);
      check();

      frames++;
      uint64_t ts2 = GetTimeStamp();
      if (ts2 - ts > 1e6)
      {
         printf("%d fps (%.3f)\n", frames, state->time);
         ts += 1e6;
         frames = 0;
         //break;
      }
   }
   screensaver_deinit_shaders(state);
   goto again;
   return 0;
}
#endif

