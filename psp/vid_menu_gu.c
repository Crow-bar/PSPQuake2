/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "../client/client.h"
#include "../client/qmenu.h"

extern cvar_t *vid_gamma;
extern cvar_t *scr_viewsize;
extern cvar_t *scr_drawfps;

/*
====================================================================

MENU INTERACTION

====================================================================
*/
static menuframework_s s_video_menu;

static menuslider_s s_screensize_slider;
static menuslider_s s_brightness_slider;
static menulist_s   s_drawfps_list;
static menulist_s   s_vsync_list;
static menuslider_s s_tq_slider;
static menulist_s   s_dynamic_lighting_list;
static menulist_s   s_light_glow_list;
static menulist_s   s_simple_particles_list;
static menuslider_s s_particle_size_slider;

static void ScreenSizeCallback (void *unused)
{
	Cvar_SetValue ("viewsize", s_screensize_slider.curvalue * 10);
}

static void BrightnessCallback (void *unused)
{
	float gamma = (0.8 - (s_brightness_slider.curvalue / 10.0 - 0.5)) + 0.5;

	Cvar_SetValue ("vid_gamma", gamma);
}

static void DrawFpsCallback (void *unused)
{
	Cvar_SetValue ("scr_drawfps", s_drawfps_list.curvalue);
}

static void VSyncCallback (void *unused)
{
	Cvar_SetValue ("gl_vsync", s_vsync_list.curvalue);
}

static void TgCallback (void *unused)
{
	Cvar_SetValue ("gl_picmip", 3 - s_tq_slider.curvalue);
}

static void DynamicLightingCallback (void *unused)
{
	Cvar_SetValue ("gl_dynamic", s_dynamic_lighting_list.curvalue);
}

static void LightGlowCallback (void *unused)
{
	Cvar_SetValue ("gl_flashblend", s_light_glow_list.curvalue);
}

static void SimpleParticlesCallback (void *unused)
{
	Cvar_SetValue ("gl_particle_point", s_simple_particles_list.curvalue);

	s_particle_size_slider.generic.flags = (s_simple_particles_list.curvalue > 0) ? QMF_INACTIVE : 0;
}

static void ParticleSizeCallback (void *unused)
{
	Cvar_SetValue ("gl_particle_scale", s_particle_size_slider.curvalue * 0.1);
}

/*
** VID_MenuInit
*/
void VID_MenuInit (void)
{
	static const char *yesno_names[] = {"no", "yes", 0};
	static const char *vsync_names[] = {"no", "yes", "adaptive", 0};
	static const char *glow_names[]  = {"dlm only", "dlm + glow", "glow only", 0};

	if (!scr_viewsize)
		scr_viewsize = Cvar_Get ("viewsize", "100", CVAR_ARCHIVE);

	if (!vid_gamma)
		vid_gamma = Cvar_Get ("vid_gamma", "1.0", CVAR_ARCHIVE);

	if (!scr_drawfps)
		scr_drawfps = Cvar_Get ("scr_drawfps", "0", CVAR_ARCHIVE);

	s_video_menu.x      = viddef.width * 0.50;
	s_video_menu.nitems = 0;

	s_screensize_slider.generic.type     = MTYPE_SLIDER;
	s_screensize_slider.generic.x        = 0;
	s_screensize_slider.generic.y        = 20;
	s_screensize_slider.generic.name     = "Screen size";
	s_screensize_slider.generic.callback = ScreenSizeCallback;
	s_screensize_slider.minvalue         = 3;
	s_screensize_slider.maxvalue         = 12;
	s_screensize_slider.curvalue         = scr_viewsize->value / 10;

	s_brightness_slider.generic.type     = MTYPE_SLIDER;
	s_brightness_slider.generic.x        = 0;
	s_brightness_slider.generic.y        = 30;
	s_brightness_slider.generic.name     = "Brightness";
	s_brightness_slider.generic.callback = BrightnessCallback;
	s_brightness_slider.minvalue         = 5;
	s_brightness_slider.maxvalue         = 13;
	s_brightness_slider.curvalue         = (1.3 - vid_gamma->value + 0.5) * 10;

	/***/

	s_drawfps_list.generic.type     = MTYPE_SPINCONTROL;
	s_drawfps_list.generic.x        = 0;
	s_drawfps_list.generic.y        = 50;
	s_drawfps_list.generic.name     = "Draw FPS";
	s_drawfps_list.generic.callback = DrawFpsCallback;
	s_drawfps_list.itemnames        = yesno_names;
	s_drawfps_list.curvalue         = scr_drawfps->value;

	s_vsync_list.generic.type     = MTYPE_SPINCONTROL;
	s_vsync_list.generic.x        = 0;
	s_vsync_list.generic.y        = 60;
	s_vsync_list.generic.name     = "VSync";
	s_vsync_list.generic.callback = VSyncCallback;
	s_vsync_list.itemnames        = vsync_names;
	s_vsync_list.curvalue         = Cvar_VariableValue ("gl_vsync");

	s_tq_slider.generic.type     = MTYPE_SLIDER;
	s_tq_slider.generic.x        = 0;
	s_tq_slider.generic.y        = 70;
	s_tq_slider.generic.name     = "Texture quality";
	s_tq_slider.generic.callback = TgCallback;
	s_tq_slider.minvalue         = 0;
	s_tq_slider.maxvalue         = 3;
	s_tq_slider.curvalue         = 3 - Cvar_VariableValue ("gl_picmip");

	s_dynamic_lighting_list.generic.type     = MTYPE_SPINCONTROL;
	s_dynamic_lighting_list.generic.x        = 0;
	s_dynamic_lighting_list.generic.y        = 80;
	s_dynamic_lighting_list.generic.name     = "Dynamic lighting";
	s_dynamic_lighting_list.generic.callback = DynamicLightingCallback;
	s_dynamic_lighting_list.itemnames        = yesno_names;
	s_dynamic_lighting_list.curvalue         = Cvar_VariableValue ("gl_dynamic");

	s_light_glow_list.generic.type     = MTYPE_SPINCONTROL;
	s_light_glow_list.generic.x        = 0;
	s_light_glow_list.generic.y        = 90;
	s_light_glow_list.generic.name     = "Light glow effect";
	s_light_glow_list.generic.callback = LightGlowCallback;
	s_light_glow_list.itemnames        = glow_names;
	s_light_glow_list.curvalue         = Cvar_VariableValue ("gl_flashblend");

	s_simple_particles_list.generic.type     = MTYPE_SPINCONTROL;
	s_simple_particles_list.generic.x        = 0;
	s_simple_particles_list.generic.y        = 100;
	s_simple_particles_list.generic.name     = "Simple particles";
	s_simple_particles_list.generic.callback = SimpleParticlesCallback;
	s_simple_particles_list.itemnames        = yesno_names;
	s_simple_particles_list.curvalue         = Cvar_VariableValue ("gl_particle_point");

	s_particle_size_slider.generic.type     = MTYPE_SLIDER;
	s_particle_size_slider.generic.flags    = (s_simple_particles_list.curvalue > 0) ? QMF_INACTIVE : 0;
	s_particle_size_slider.generic.x        = 0;
	s_particle_size_slider.generic.y        = 110;
	s_particle_size_slider.generic.name     = "Particle size";
	s_particle_size_slider.generic.callback = ParticleSizeCallback;
	s_particle_size_slider.minvalue         = 1;
	s_particle_size_slider.maxvalue         = 10;
	s_particle_size_slider.curvalue         = Cvar_VariableValue ("gl_particle_scale") * 10;

	Menu_AddItem (&s_video_menu, (void *)&s_screensize_slider);
	Menu_AddItem (&s_video_menu, (void *)&s_brightness_slider);
	Menu_AddItem (&s_video_menu, (void *)&s_drawfps_list);
	Menu_AddItem (&s_video_menu, (void *)&s_vsync_list);
	Menu_AddItem (&s_video_menu, (void *)&s_tq_slider);
	Menu_AddItem (&s_video_menu, (void *)&s_dynamic_lighting_list);
	Menu_AddItem (&s_video_menu, (void *)&s_light_glow_list);
	Menu_AddItem (&s_video_menu, (void *)&s_simple_particles_list);
	Menu_AddItem (&s_video_menu, (void *)&s_particle_size_slider);

	Menu_Center (&s_video_menu);

	s_video_menu.x -= 8;
}

/*
================
VID_MenuDraw
================
*/
void VID_MenuDraw (void)
{
	int w, h;

	/*
	** draw the banner
	*/
	re.DrawGetPicSize (&w, &h, "m_banner_video");
	re.DrawPic (viddef.width / 2 - w / 2, viddef.height / 2 - 110, "m_banner_video");

	/*
	** move cursor to a reasonable starting position
	*/
	Menu_AdjustCursor (&s_video_menu, 1);

	/*
	** draw the menu
	*/
	Menu_Draw (&s_video_menu);
}

/*
================
VID_MenuKey
================
*/
const char *VID_MenuKey (int key)
{
	extern const char *Default_MenuKey (menuframework_s * m, int key);
	return Default_MenuKey (&s_video_menu, key);
}
