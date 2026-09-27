// Dragon logo intro, adapted from the N64 Doom port (src/i_intro.c), itself
// adapted from lambertjamesd/n64brew2025's src/intro/logo.c (logo_libdragon),
// which in turn credits:
//
//   logo_libdragon and logo_n64brew sourced from the N64brew-GameJam2024
//   repository
//
//   Copyright (c) 2024 N64brew
//
//   Permission is hereby granted, free of charge, to any person obtaining a
//   copy of this software and associated documentation files (the
//   "Software"), to deal in the Software without restriction, including
//   without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to
//   permit persons to whom the Software is furnished to do so, subject to
//   the following conditions:
//
//   The above copyright notice and this permission notice shall be included
//   in all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// The animation was laid out in 640x480 design-space coordinates; the game runs
// at 320x240, so everything is simply drawn at half scale. Like the Doom port,
// the dragon roar of the original is left out: the animation is silent.
#include "intro_n64.h"

#include <libdragon.h>

namespace Intro_N64
{
	enum
	{
		SCREEN_WIDTH  = 320,
		SCREEN_HEIGHT = 240,
	};
	static const float c_scale = (float)SCREEN_HEIGHT / 480.0f;

	static float sxf(float v) { return v * c_scale; }
	static float syf(float v) { return v * c_scale; }
	static int sx(float v) { return (int)sxf(v); }
	static int sy(float v) { return (int)syf(v); }

	void play()
	{
		const color_t RED = RGBA32(221, 46, 26, 255);
		const color_t WHITE = RGBA32(255, 255, 255, 255);

		sprite_t* d1 = sprite_load("rom:/intro/dragon1.sprite");
		sprite_t* d2 = sprite_load("rom:/intro/dragon2.sprite");
		sprite_t* d3 = sprite_load("rom:/intro/dragon3.sprite");
		sprite_t* d4 = sprite_load("rom:/intro/dragon4.sprite");
		if (!d1 || !d2 || !d3 || !d4)
		{
			// The intro is optional: skip it if the sprites are missing.
			if (d1) { sprite_free(d1); }
			if (d2) { sprite_free(d2); }
			if (d3) { sprite_free(d3); }
			if (d4) { sprite_free(d4); }
			return;
		}

		display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
		rdpq_init();

		float angle1 = 3.2f, angle2 = 1.9f, angle3 = 0.9f;
		float scale1 = 0.0f, scale2 = 0.4f, scale3 = 0.8f, scroll4 = 400.0f;
		const uint32_t ms0 = get_ticks_ms();
		// Translation offset of the animation (simplifies centering).
		const int X0 = 10, Y0 = 30;

		while (true)
		{
			// Animation parts:
			// 0: rotate dragon head
			// 1: rotate dragon body and tail, scale up
			// 2: scroll dragon logo
			// 3: fade out
			const uint32_t tt = get_ticks_ms() - ms0;
			int animPart;
			if (tt < 1000) { animPart = 0; }
			else if (tt < 1500) { animPart = 1; }
			else if (tt < 4000) { animPart = 2; }
			else if (tt < 5000) { animPart = 3; }
			else { break; }

			// Update animation parameters using quadratic ease-out.
			angle1 -= angle1 * 0.04f; if (angle1 < 0.010f) { angle1 = 0; }
			if (animPart >= 1)
			{
				angle2 -= angle2 * 0.06f; if (angle2 < 0.01f) { angle2 = 0; }
				angle3 -= angle3 * 0.06f; if (angle3 < 0.01f) { angle3 = 0; }
				scale2 -= scale2 * 0.06f; if (scale2 < 0.01f) { scale2 = 0; }
				scale3 -= scale3 * 0.06f; if (scale3 < 0.01f) { scale3 = 0; }
			}
			if (animPart >= 2)
			{
				scroll4 -= scroll4 * 0.08f;
			}

			// Colors for the fade out.
			color_t red = RED;
			color_t white = WHITE;
			if (animPart >= 3)
			{
				red.a = 255 - (tt - 4000) * 255 / 1000;
				white.a = 255 - (tt - 4000) * 255 / 1000;
			}

			surface_t* fb = display_get();
			rdpq_attach_clear(fb, NULL);

			// To simulate the dragon jumping out, the head is scissored so that
			// it appears as it moves; initially also horizontally, so that the
			// head tail is not visible on the right.
			if (angle1 > 1.0f) { rdpq_set_scissor(sx(0), sy(0), sx(X0 + 300), sy(Y0 + 240)); }
			else { rdpq_set_scissor(sx(0), sy(0), sx(640), sy(Y0 + 240)); }

			// Dragon head.
			rdpq_set_mode_standard();
			rdpq_mode_alphacompare(1);
			rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
			rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,PRIM),(TEX0,0,PRIM,0)));
			rdpq_set_prim_color(red);
			rdpq_blitparms_t headParms = {};
			headParms.theta = angle1;
			headParms.scale_x = (scale1 + 1) * c_scale;
			headParms.scale_y = (scale1 + 1) * c_scale;
			headParms.cx = 176;
			headParms.cy = 171;
			rdpq_sprite_blit(d1, sx(X0 + 216), sy(Y0 + 205), &headParms);

			rdpq_set_scissor(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

			// Black rectangle with an alpha gradient, covering the head tail.
			rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
			rdpq_mode_dithering(DITHER_NOISE_NOISE);
			float vtx[4][6] =
			{
				//  x,                y,               r,g,b,a
				{ sxf(X0 + 0),   syf(Y0 + 180), 0,0,0,0 },
				{ sxf(X0 + 200), syf(Y0 + 180), 0,0,0,0 },
				{ sxf(X0 + 200), syf(Y0 + 240), 0,0,0,1 },
				{ sxf(X0 + 0),   syf(Y0 + 240), 0,0,0,1 },
			};
			rdpq_triangle(&TRIFMT_SHADE, vtx[0], vtx[1], vtx[2]);
			rdpq_triangle(&TRIFMT_SHADE, vtx[0], vtx[2], vtx[3]);

			if (animPart >= 1)
			{
				// Dragon body and tail, faded in.
				rdpq_set_mode_standard();
				rdpq_mode_alphacompare(1);
				rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
				rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,PRIM),(TEX0,0,PRIM,0)));

				color_t color = red;
				color.r *= 1 - scale3; color.g *= 1 - scale3; color.b *= 1 - scale3;
				rdpq_set_prim_color(color);

				rdpq_blitparms_t bodyParms = {};
				bodyParms.theta = angle2;
				bodyParms.scale_x = (1 - scale2) * c_scale;
				bodyParms.scale_y = (1 - scale2) * c_scale;
				bodyParms.cx = 145;
				bodyParms.cy = 113;
				rdpq_sprite_blit(d2, sx(X0 + 246), sy(Y0 + 230), &bodyParms);

				rdpq_blitparms_t tailParms = {};
				tailParms.theta = -angle3;
				tailParms.scale_x = (1 - scale3) * c_scale;
				tailParms.scale_y = (1 - scale3) * c_scale;
				tailParms.cx = 91;
				tailParms.cy = 24;
				rdpq_sprite_blit(d3, sx(X0 + 266), sy(Y0 + 256), &tailParms);
			}

			// Scrolling logo.
			if (animPart >= 2)
			{
				rdpq_set_prim_color(white);
				rdpq_blitparms_t logoParms = {};
				logoParms.scale_x = c_scale;
				logoParms.scale_y = c_scale;
				rdpq_sprite_blit(d4, sx(X0 + 161 + (int)scroll4), sy(Y0 + 182), &logoParms);
			}

			rdpq_detach_show();
		}

		rspq_wait();
		sprite_free(d1);
		sprite_free(d2);
		sprite_free(d3);
		sprite_free(d4);
		// The render backend initializes both again.
		rdpq_close();
		display_close();
	}
}
