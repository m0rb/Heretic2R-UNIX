//
// menu_video.c
//
// Copyright 1998 Raven Software
//

#include "menu_video.h"
#include "menu_main.h"
#include "client/client.h"
#include "win32/dll_io/vid_dll.h"
#include "../unix/vid_dll.h"
#include "../unix/compat.h"

cvar_t* m_banner_video;

cvar_t* m_item_driver; // "Renderer"
cvar_t* m_item_vidmode; // "Video resolution"
cvar_t* m_item_display; // "Display"
cvar_t* m_item_fullscreen; // "Fullscreen"
cvar_t* m_item_rate; // "Refresh Rate"
cvar_t* m_item_target_fps; //mxd. "Target FPS"
cvar_t* m_item_texfilter; // "Texture Filter"
cvar_t* m_item_anisotropic; // "Anisotropic"
cvar_t* m_item_antialias; // "Antialiasing"
cvar_t* m_item_gamma;
cvar_t* m_item_brightness;
cvar_t* m_item_contrast;
cvar_t* m_item_minlight; // YQ2
cvar_t* m_item_detail;
cvar_t* m_item_vsync;        // YQ2
cvar_t* m_item_consolescale; // YQ2
cvar_t* m_item_hudscale;     // YQ2
cvar_t* m_item_menuscale;    // YQ2

static float m_gamma;
static float m_brightness;
static float m_contrast;
static float m_minlight; //mxd. gl_minlight when entering menu.

static menuframework_t s_video_menu;

static menulist_t s_ref_list;
static menulist_t s_mode_list;
static menulist_t s_display_list;
static menulist_t s_fullscreen_list;
static menulist_t s_rate_list;
static menulist_t s_target_fps_list; //mxd
static menulist_t s_texfilter_list;
static menulist_t s_anisotropic_list;
static menulist_t s_msaa_list;
static menuslider_t s_gamma_slider;
static menuslider_t s_brightness_slider;
static menuslider_t s_contrast_slider;
static menuslider_t s_minlight_slider; // YQ2
static menuslider_t s_detail_slider;
static menulist_t s_vsync_list;        // YQ2
static menulist_t s_consolescale_list; // YQ2
static menulist_t s_hudscale_list;     // YQ2
static menulist_t s_menuscale_list;    // YQ2

static const char* ref_list_titles[MAX_REFLIBS];
static int initial_reflib_index; // vid_ref index when entering menu.

#define MAX_DISPLAYED_VIDMODES	64 //mxd. This is kinda ugly, since vid_modes array itself is dynamically allocated...
#define MAX_DISPLAYS 8
static const char* vid_mode_titles[MAX_DISPLAYED_VIDMODES];
static int initial_vid_mode; // vid_mode when entering menu.
static int initial_fullscreen; // vid_fullscreen when entering menu.
static int initial_displayindex;
static int initial_rate;
static int initial_msaa;

static const int aniso_values[] = { 0, 2, 4, 8, 16 };
static const int msaa_values[]  = { 0, 2, 4, 8 };
static const int rate_values[]  = { -1, 60, 120, 144, 165, 240 };
static const char* texfilter_modes[] = { "GL_NEAREST", "GL_LINEAR_MIPMAP_NEAREST", "GL_LINEAR_MIPMAP_LINEAR" };

static int IndexOfValue(const int* vals, int count, int v)
{
	for (int i = 0; i < count; i++)
		if (vals[i] == v)
			return i;
	return 0;
}

#pragma region ========================== MENU ITEM CALLBACKS ==========================

static void UpdateTargetFPSFunc(void* self) //mxd
{
	const menulist_t* list = (menulist_t*)self;
	Cvar_SetValue("vid_maxfps", (float)(list->curvalue + 1) * 30.0f);
}

static void UpdateAnisotropicFunc(void* self) // YQ2
{
	const menulist_t* list = (menulist_t*)self;
	Cvar_SetValue("r_anisotropic", (float)aniso_values[list->curvalue]);
}

static void UpdateTexFilterFunc(void* self)
{
	const menulist_t* list = (menulist_t*)self;
	Cvar_Set("gl_texturemode", (char*)texfilter_modes[list->curvalue]);
}

static void UpdateGammaFunc(void* self) // H2
{
	const menuslider_t* slider = (menuslider_t*)self;
	Cvar_SetValue("vid_gamma", (16.0f - slider->curvalue) / 16.0f);
}

static void UpdateBrightnessFunc(void* self)
{
	const menuslider_t* slider = (menuslider_t*)self;
	Cvar_SetValue("vid_brightness", slider->curvalue / 16.0f);
}

static void UpdateContrastFunc(void* self) // H2
{
	const menuslider_t* slider = (menuslider_t*)self;
	Cvar_SetValue("vid_contrast", slider->curvalue / 16.0f);
}

static void UpdateMinlightFunc(void* self) // YQ2
{
	const menuslider_t* slider = (menuslider_t*)self;
	Cvar_SetValue("gl_minlight", slider->curvalue * 4.0f);
}

static void UpdateDetailFunc(void* self) // H2
{
	const menuslider_t* slider = (menuslider_t*)self;
	Cvar_SetValue("r_detail", slider->curvalue);
}

static void UpdateVSyncFunc(void* self) // YQ2
{
	const menulist_t* list = (menulist_t*)self;
	Cvar_SetValue("r_vsync", (float)list->curvalue);
}

// Scale list: index 0 = Auto (-1), indices 1-4 = 1x-4x.
static void UpdateConsoleScaleFunc(void* self) // YQ2
{
	const menulist_t* list = (menulist_t*)self;
	Cvar_SetValue("r_consolescale", list->curvalue == 0 ? -1.0f : (float)list->curvalue);
}

static void UpdateHUDScaleFunc(void* self) // YQ2
{
	const menulist_t* list = (menulist_t*)self;
	Cvar_SetValue("r_hudscale", list->curvalue == 0 ? -1.0f : (float)list->curvalue);
}

static void UpdateMenuScaleFunc(void* self) // YQ2
{
	const menulist_t* list = (menulist_t*)self;
	Cvar_SetValue("r_menuscale", list->curvalue == 0 ? -1.0f : (float)list->curvalue);
}

static void ApplyChanges(const qboolean close_menu) //mxd. +close_menu arg.
{
	if (initial_vid_mode != s_mode_list.curvalue)
	{
		Cvar_SetValue("vid_mode", (float)s_mode_list.curvalue);
		initial_vid_mode = s_mode_list.curvalue;
		vid_restart_required = true;
	}

	if (initial_fullscreen != s_fullscreen_list.curvalue)
	{
		Cvar_SetValue("vid_fullscreen", (float)s_fullscreen_list.curvalue);
		initial_fullscreen = s_fullscreen_list.curvalue;
		vid_restart_required = true;
	}

	if (initial_displayindex != s_display_list.curvalue)
	{
		Cvar_SetValue("vid_displayindex", (float)s_display_list.curvalue);
		initial_displayindex = s_display_list.curvalue;
		vid_restart_required = true;
	}

	if (initial_rate != rate_values[s_rate_list.curvalue])
	{
		Cvar_SetValue("vid_rate", (float)rate_values[s_rate_list.curvalue]);
		initial_rate = rate_values[s_rate_list.curvalue];
		vid_restart_required = true;
	}

	if (initial_msaa != msaa_values[s_msaa_list.curvalue])
	{
		Cvar_SetValue("r_msaa_samples", (float)msaa_values[s_msaa_list.curvalue]);
		initial_msaa = msaa_values[s_msaa_list.curvalue];
		vid_restart_required = true;
	}

	if (initial_reflib_index != s_ref_list.curvalue)
	{
		Cvar_Set("vid_ref", reflib_infos[s_ref_list.curvalue].id);
		initial_reflib_index = s_ref_list.curvalue;
		vid_restart_required = true;
	}

	if ((int)m_minlight != (int)m_gl_minlight->value) // YQ2
		vid_restart_required = true;

	if (vid_restart_required)
	{
		M_ForceMenuOff();
		if (cls.state != ca_active)
			M_Menu_Main_f();
		return;
	}

	if (close_menu)
	{
		//mxd. These don't require vid_restart, but we still need to update ALL textures in RI_BeginFrame() AFTER menu is closed.
		// (only it_pic/it_sky textures are updated by R_GammaAffect() when menus are open (for performance reasons)).
		if (m_gamma != vid_gamma->value || m_brightness != vid_brightness->value || m_contrast != vid_contrast->value)
			Cvar_SetValue("vid_textures_refresh_required", 1.0f);

		M_PopMenu();
	}
}

#pragma endregion

void VID_PreMenuInit(void)
{
	//mxd. Refresher library titles.
	for (int i = 0; i < num_reflib_infos; i++)
	{
		ref_list_titles[i] = reflib_infos[i].title;

		if (Q_stricmp(vid_ref->string, reflib_infos[i].id) == 0)
		{
			initial_reflib_index = i;
			s_ref_list.curvalue = i;
		}
	}

	ref_list_titles[num_reflib_infos] = NULL;

	//mxd. Window resolution labels.
	for (int i = 0; i < min(MAX_DISPLAYED_VIDMODES, num_vid_modes); i++)
		vid_mode_titles[i] = vid_modes[i].description;

	vid_mode_titles[num_vid_modes] = NULL;

	if (vid_mode == NULL)
	{
		vid_mode = Cvar_Get("vid_mode", "0", 0);
		vid_restart_required = true;
	}

	initial_vid_mode = (int)vid_mode->value; //mxd
	initial_fullscreen = ClampI((int)Cvar_VariableValue("vid_fullscreen"), 0, 2);
	initial_displayindex = (int)Cvar_VariableValue("vid_displayindex");
	initial_rate = (int)Cvar_VariableValue("vid_rate");
	initial_msaa = (int)Cvar_VariableValue("r_msaa_samples");

	if (scr_viewsize == NULL)
		scr_viewsize = Cvar_Get("viewsize", "100", CVAR_ARCHIVE);
}

static void VID_MenuInit(void)
{
	static const char* target_fps_names[] = { "30", "60", "90", "120", "240", NULL }; //mxd
	static const char* vsync_names[]        = { "Off", "On", "Adaptive", NULL };        // YQ2
	static const char* scale_names[]        = { "Auto", "1x", "2x", "3x", "4x", NULL }; // YQ2
	static const char* fullscreen_names[]   = { "Windowed", "Fullscreen", "Borderless", NULL };
	static const char* rate_names[]         = { "Auto", "60", "120", "144", "165", "240", NULL };
	static const char* texfilter_names[]    = { "Nearest", "Bilinear", "Trilinear", NULL };
	static const char* anisotropic_names[]  = { "Off", "2x", "4x", "8x", "16x", NULL };
	static const char* msaa_names[]         = { "Off", "2x", "4x", "8x", NULL };

	static char display_bufs[MAX_DISPLAYS][16];
	static const char* display_names[MAX_DISPLAYS + 1];

	static char name_driver[MAX_QPATH];
	static char name_vidmode[MAX_QPATH];
	static char name_display[MAX_QPATH];
	static char name_fullscreen[MAX_QPATH];
	static char name_rate[MAX_QPATH];
	static char name_target_fps[MAX_QPATH]; //mxd
	static char name_texfilter[MAX_QPATH];
	static char name_anisotropic[MAX_QPATH];
	static char name_msaa[MAX_QPATH];
	static char name_gamma[MAX_QPATH];
	static char name_brightness[MAX_QPATH];
	static char name_contrast[MAX_QPATH];
	static char name_minlight[MAX_QPATH]; // YQ2
	static char name_detail[MAX_QPATH];
	static char name_vsync[MAX_QPATH];        // YQ2
	static char name_consolescale[MAX_QPATH]; // YQ2
	static char name_hudscale[MAX_QPATH];     // YQ2
	static char name_menuscale[MAX_QPATH];    // YQ2

	VID_PreMenuInit();

	m_gamma = Cvar_VariableValue("vid_gamma");
	m_brightness = Cvar_VariableValue("vid_brightness");
	m_contrast = Cvar_VariableValue("vid_contrast");
	m_minlight = Cvar_VariableValue("gl_minlight"); // YQ2

	s_video_menu.nitems = 0;

	Cvar_SetValue("r_detail", Clamp(m_r_detail->value, 0.0f, 3.0f));
	Cvar_SetValue("gl_minlight", Clamp(m_gl_minlight->value, 0.0f, 32.0f)); //mxd
	Cvar_SetValue("vid_maxfps", Clamp(vid_maxfps->value, 30.0f, 240.0f)); //mxd

	const int ndisplays = (VID_GetNumDisplays() < MAX_DISPLAYS) ? VID_GetNumDisplays() : MAX_DISPLAYS;
	for (int i = 0; i < ndisplays; i++)
	{
		Com_sprintf(display_bufs[i], sizeof(display_bufs[i]), "%i", i + 1);
		display_names[i] = display_bufs[i];
	}
	display_names[ndisplays] = NULL;

	int y = 0;

	Com_sprintf(name_driver, sizeof(name_driver), "\x02%s", m_item_driver->string);
	s_ref_list.generic.type = MTYPE_SPINCONTROL;
	s_ref_list.generic.x = 0;
	s_ref_list.generic.y = y; y += 40;
	s_ref_list.generic.name = name_driver;
	s_ref_list.generic.width = re.BF_Strlen(name_driver);
	s_ref_list.curvalue = initial_reflib_index;
	s_ref_list.itemnames = ref_list_titles;

	Com_sprintf(name_vidmode, sizeof(name_vidmode), "\x02%s", m_item_vidmode->string);
	s_mode_list.generic.type = MTYPE_SPINCONTROL;
	s_mode_list.generic.x = 0;
	s_mode_list.generic.y = y; y += 40;
	s_mode_list.generic.name = name_vidmode;
	s_mode_list.generic.width = re.BF_Strlen(name_vidmode);
	s_mode_list.curvalue = initial_vid_mode;
	s_mode_list.itemnames = vid_mode_titles;

	Com_sprintf(name_display, sizeof(name_display), "\x02%s", m_item_display->string);
	s_display_list.generic.type = MTYPE_SPINCONTROL;
	s_display_list.generic.x = 0;
	s_display_list.generic.y = y; if (ndisplays > 1) y += 26;
	s_display_list.generic.name = name_display;
	s_display_list.generic.width = re.BF_Strlen(name_display);
	s_display_list.generic.flags = QMF_SINGLELINE;
	s_display_list.itemnames = display_names;
	s_display_list.curvalue = ClampI(initial_displayindex, 0, ndisplays - 1);

	Com_sprintf(name_fullscreen, sizeof(name_fullscreen), "\x02%s", m_item_fullscreen->string);
	s_fullscreen_list.generic.type = MTYPE_SPINCONTROL;
	s_fullscreen_list.generic.x = 0;
	s_fullscreen_list.generic.y = y; y += 26;
	s_fullscreen_list.generic.name = name_fullscreen;
	s_fullscreen_list.generic.width = re.BF_Strlen(name_fullscreen);
	s_fullscreen_list.generic.flags = QMF_SINGLELINE;
	s_fullscreen_list.itemnames = fullscreen_names;
	s_fullscreen_list.curvalue = initial_fullscreen;

	Com_sprintf(name_rate, sizeof(name_rate), "\x02%s", m_item_rate->string);
	s_rate_list.generic.type = MTYPE_SPINCONTROL;
	s_rate_list.generic.x = 0;
	s_rate_list.generic.y = y; y += 26;
	s_rate_list.generic.name = name_rate;
	s_rate_list.generic.width = re.BF_Strlen(name_rate);
	s_rate_list.generic.flags = QMF_SINGLELINE;
	s_rate_list.itemnames = rate_names;
	s_rate_list.curvalue = IndexOfValue(rate_values, 6, initial_rate);

	Com_sprintf(name_target_fps, sizeof(name_target_fps), "\x02%s", m_item_target_fps->string);
	s_target_fps_list.generic.type = MTYPE_SPINCONTROL;
	s_target_fps_list.generic.x = 0;
	s_target_fps_list.generic.y = y; y += 26;
	s_target_fps_list.generic.name = name_target_fps;
	s_target_fps_list.generic.width = re.BF_Strlen(name_target_fps);
	s_target_fps_list.generic.flags = QMF_SINGLELINE;
	s_target_fps_list.generic.callback = UpdateTargetFPSFunc;
	s_target_fps_list.itemnames = target_fps_names;
	s_target_fps_list.curvalue = (int)(vid_maxfps->value / 30.0f) - 1;

	Com_sprintf(name_gamma, sizeof(name_gamma), "\x02%s", m_item_gamma->string);
	s_gamma_slider.generic.type = MTYPE_SLIDER;
	s_gamma_slider.generic.flags = QMF_SELECT_SOUND;
	s_gamma_slider.generic.x = 0;
	s_gamma_slider.generic.y = y; y += 40;
	s_gamma_slider.generic.name = name_gamma;
	s_gamma_slider.generic.width = re.BF_Strlen(name_gamma);
	s_gamma_slider.generic.callback = UpdateGammaFunc;
	s_gamma_slider.minvalue = 0.0f;
	s_gamma_slider.maxvalue = 16.0f;
	s_gamma_slider.curvalue = 16.0f - vid_gamma->value * 16.0f;

	Com_sprintf(name_brightness, sizeof(name_brightness), "\x02%s", m_item_brightness->string);
	s_brightness_slider.generic.type = MTYPE_SLIDER;
	s_brightness_slider.generic.flags = QMF_SELECT_SOUND;
	s_brightness_slider.generic.x = 0;
	s_brightness_slider.generic.y = y; y += 40;
	s_brightness_slider.generic.name = name_brightness;
	s_brightness_slider.generic.width = re.BF_Strlen(name_brightness);
	s_brightness_slider.generic.callback = UpdateBrightnessFunc;
	s_brightness_slider.minvalue = 0.0f;
	s_brightness_slider.maxvalue = 16.0f;
	s_brightness_slider.curvalue = vid_brightness->value * 16.0f;

	Com_sprintf(name_contrast, sizeof(name_contrast), "\x02%s", m_item_contrast->string);
	s_contrast_slider.generic.type = MTYPE_SLIDER;
	s_contrast_slider.generic.flags = QMF_SELECT_SOUND;
	s_contrast_slider.generic.x = 0;
	s_contrast_slider.generic.y = y; y += 40;
	s_contrast_slider.generic.name = name_contrast;
	s_contrast_slider.generic.width = re.BF_Strlen(name_contrast);
	s_contrast_slider.generic.callback = UpdateContrastFunc;
	s_contrast_slider.minvalue = 1.6f;
	s_contrast_slider.maxvalue = 14.4f;
	s_contrast_slider.curvalue = vid_contrast->value * 16.0f;

	// YQ2
	Com_sprintf(name_minlight, sizeof(name_minlight), "\x02%s", m_item_minlight->string);
	s_minlight_slider.generic.type = MTYPE_SLIDER;
	s_minlight_slider.generic.flags = QMF_SELECT_SOUND;
	s_minlight_slider.generic.x = 0;
	s_minlight_slider.generic.y = y; y += 40;
	s_minlight_slider.generic.name = name_minlight;
	s_minlight_slider.generic.width = re.BF_Strlen(name_minlight);
	s_minlight_slider.generic.callback = UpdateMinlightFunc;
	s_minlight_slider.minvalue = 0.0f;
	s_minlight_slider.maxvalue = 8.0f;
	s_minlight_slider.curvalue = m_gl_minlight->value * 0.25f;

	Com_sprintf(name_detail, sizeof(name_detail), "\x02%s", m_item_detail->string);
	s_detail_slider.generic.type = MTYPE_SLIDER;
	s_detail_slider.generic.flags = QMF_SELECT_SOUND; //mxd. QMF_SELECT_SOUND flag was missing in original version.
	s_detail_slider.generic.x = 0;
	s_detail_slider.generic.y = y; y += 40;
	s_detail_slider.generic.name = name_detail;
	s_detail_slider.generic.width = re.BF_Strlen(name_detail);
	s_detail_slider.generic.callback = UpdateDetailFunc;
	s_detail_slider.minvalue = 0.0f;
	s_detail_slider.maxvalue = 3.0f;
	s_detail_slider.curvalue = m_r_detail->value; //mxd. Original version used Cvar_VariableValue("r_detail") here.

	Com_sprintf(name_texfilter, sizeof(name_texfilter), "\x02%s", m_item_texfilter->string);
	s_texfilter_list.generic.type = MTYPE_SPINCONTROL;
	s_texfilter_list.generic.x = 0;
	s_texfilter_list.generic.y = y; y += 26;
	s_texfilter_list.generic.name = name_texfilter;
	s_texfilter_list.generic.width = re.BF_Strlen(name_texfilter);
	s_texfilter_list.generic.flags = QMF_SINGLELINE;
	s_texfilter_list.generic.callback = UpdateTexFilterFunc;
	s_texfilter_list.itemnames = texfilter_names;
	s_texfilter_list.curvalue = 1;
	for (int i = 0; i < 3; i++)
		if (Q_stricmp(Cvar_VariableString("gl_texturemode"), texfilter_modes[i]) == 0)
		{
			s_texfilter_list.curvalue = i;
			break;
		}

	Com_sprintf(name_anisotropic, sizeof(name_anisotropic), "\x02%s", m_item_anisotropic->string);
	s_anisotropic_list.generic.type = MTYPE_SPINCONTROL;
	s_anisotropic_list.generic.x = 0;
	s_anisotropic_list.generic.y = y; y += 26;
	s_anisotropic_list.generic.name = name_anisotropic;
	s_anisotropic_list.generic.width = re.BF_Strlen(name_anisotropic);
	s_anisotropic_list.generic.flags = QMF_SINGLELINE;
	s_anisotropic_list.generic.callback = UpdateAnisotropicFunc;
	s_anisotropic_list.itemnames = anisotropic_names;
	s_anisotropic_list.curvalue = IndexOfValue(aniso_values, 5, (int)Cvar_VariableValue("r_anisotropic"));

	Com_sprintf(name_msaa, sizeof(name_msaa), "\x02%s", m_item_antialias->string);
	s_msaa_list.generic.type = MTYPE_SPINCONTROL;
	s_msaa_list.generic.x = 0;
	s_msaa_list.generic.y = y; y += 26;
	s_msaa_list.generic.name = name_msaa;
	s_msaa_list.generic.width = re.BF_Strlen(name_msaa);
	s_msaa_list.generic.flags = QMF_SINGLELINE;
	s_msaa_list.itemnames = msaa_names;
	s_msaa_list.curvalue = IndexOfValue(msaa_values, 4, initial_msaa);

	Com_sprintf(name_vsync, sizeof(name_vsync), "\x02%s", m_item_vsync->string);
	s_vsync_list.generic.type = MTYPE_SPINCONTROL;
	s_vsync_list.generic.x = 0;
	s_vsync_list.generic.y = y; y += 26;
	s_vsync_list.generic.name = name_vsync;
	s_vsync_list.generic.width = re.BF_Strlen(name_vsync);
	s_vsync_list.generic.flags = QMF_SINGLELINE;
	s_vsync_list.generic.callback = UpdateVSyncFunc;
	s_vsync_list.itemnames = vsync_names;
	s_vsync_list.curvalue = ClampI((int)Cvar_VariableValue("r_vsync"), 0, 2); // YQ2

	Com_sprintf(name_consolescale, sizeof(name_consolescale), "\x02%s", m_item_consolescale->string);
	s_consolescale_list.generic.type = MTYPE_SPINCONTROL;
	s_consolescale_list.generic.x = 0;
	s_consolescale_list.generic.y = y; y += 26;
	s_consolescale_list.generic.name = name_consolescale;
	s_consolescale_list.generic.width = re.BF_Strlen(name_consolescale);
	s_consolescale_list.generic.flags = QMF_SINGLELINE;
	s_consolescale_list.generic.callback = UpdateConsoleScaleFunc;
	s_consolescale_list.itemnames = scale_names;
	// cvar -1 = Auto (index 0); 1-4 = 1x-4x (indices 1-4)
	s_consolescale_list.curvalue = (r_consolescale->value < 0) ? 0 : ClampI((int)r_consolescale->value, 1, 4); // YQ2

	Com_sprintf(name_hudscale, sizeof(name_hudscale), "\x02%s", m_item_hudscale->string);
	s_hudscale_list.generic.type = MTYPE_SPINCONTROL;
	s_hudscale_list.generic.x = 0;
	s_hudscale_list.generic.y = y; y += 26;
	s_hudscale_list.generic.name = name_hudscale;
	s_hudscale_list.generic.width = re.BF_Strlen(name_hudscale);
	s_hudscale_list.generic.flags = QMF_SINGLELINE;
	s_hudscale_list.generic.callback = UpdateHUDScaleFunc;
	s_hudscale_list.itemnames = scale_names;
	s_hudscale_list.curvalue = (r_hudscale->value < 0) ? 0 : ClampI((int)r_hudscale->value, 1, 4); // YQ2

	Com_sprintf(name_menuscale, sizeof(name_menuscale), "\x02%s", m_item_menuscale->string);
	s_menuscale_list.generic.type = MTYPE_SPINCONTROL;
	s_menuscale_list.generic.x = 0;
	s_menuscale_list.generic.y = y; y += 26;
	s_menuscale_list.generic.name = name_menuscale;
	s_menuscale_list.generic.width = re.BF_Strlen(name_menuscale);
	s_menuscale_list.generic.flags = QMF_SINGLELINE;
	s_menuscale_list.generic.callback = UpdateMenuScaleFunc;
	s_menuscale_list.itemnames = scale_names;
	s_menuscale_list.curvalue = (r_menuscale->value < 0) ? 0 : ClampI((int)r_menuscale->value, 1, 4); // YQ2

	Menu_AddItem(&s_video_menu, &s_ref_list);
	Menu_AddItem(&s_video_menu, &s_mode_list);
	if (ndisplays > 1)
		Menu_AddItem(&s_video_menu, &s_display_list);
	Menu_AddItem(&s_video_menu, &s_fullscreen_list);
	Menu_AddItem(&s_video_menu, &s_rate_list);
	Menu_AddItem(&s_video_menu, &s_target_fps_list); //mxd
	Menu_AddItem(&s_video_menu, &s_gamma_slider);
	Menu_AddItem(&s_video_menu, &s_brightness_slider);
	Menu_AddItem(&s_video_menu, &s_contrast_slider);
	Menu_AddItem(&s_video_menu, &s_minlight_slider); // YQ2
	Menu_AddItem(&s_video_menu, &s_detail_slider);
	Menu_AddItem(&s_video_menu, &s_texfilter_list);
	Menu_AddItem(&s_video_menu, &s_anisotropic_list);
	Menu_AddItem(&s_video_menu, &s_msaa_list);
	Menu_AddItem(&s_video_menu, &s_vsync_list);        // YQ2
	Menu_AddItem(&s_video_menu, &s_consolescale_list); // YQ2
	Menu_AddItem(&s_video_menu, &s_hudscale_list);     // YQ2
	Menu_AddItem(&s_video_menu, &s_menuscale_list);    // YQ2

	Menu_Center(&s_video_menu);
}

static void VID_MenuDraw(void)
{
	char title[MAX_QPATH];

	// Draw menu BG.
	Menu_DrawBG("book/back/b_conback8.bk", cls.m_menuscale);

	if (cls.m_menualpha == 0.0f)
		return;

	// Draw menu title.
	Com_sprintf(title, sizeof(title), "\x03%s", m_banner_video->string);
	const int x = M_GetMenuLabelX(re.BF_Strlen(title));
	const int y = M_GetMenuOffsetY(&s_video_menu);
	re.DrawBigFont(x, y, title, cls.m_menualpha);

	s_video_menu.x = M_GetMenuLabelX(s_video_menu.width);
	Menu_AdjustCursor(&s_video_menu, 1);
	Menu_Draw(&s_video_menu);
}

static const char* VID_MenuKey(const int key)
{
	if (cls.m_menustate != MS_OPENED)
		return NULL;

	switch (key)
	{
		case K_ENTER:
		case K_KP_ENTER:
			ApplyChanges(false);
			return SND_MENU_ENTER;

		case K_ESCAPE:
			ApplyChanges(true);
			return SND_MENU_CLOSE;

		case K_UPARROW:
		case K_KP_UPARROW:
			s_video_menu.cursor--;
			Menu_AdjustCursor(&s_video_menu, -1);
			return SND_MENU_SELECT;

		case K_DOWNARROW:
		case K_KP_DOWNARROW:
			s_video_menu.cursor++;
			Menu_AdjustCursor(&s_video_menu, 1);
			return SND_MENU_SELECT;

		case K_LEFTARROW:
		case K_KP_LEFTARROW:
			//mxd. Original logic calls se.StopAllSounds_Sounding() here - no longer needed.
			return (Menu_SlideItem(&s_video_menu, -1) ? SND_MENU_TOGGLE : NULL); //mxd. Add sound.

		case K_RIGHTARROW:
		case K_KP_RIGHTARROW:
			//mxd. Original logic calls se.StopAllSounds_Sounding() here - no longer needed.
			return (Menu_SlideItem(&s_video_menu, 1) ? SND_MENU_TOGGLE : NULL); //mxd. Add sound.

		default:
			break;
	}

	return NULL;
}

// Q2 counterpart
void M_Menu_Video_f(void)
{
	VID_MenuInit();
	M_PushMenu(VID_MenuDraw, VID_MenuKey);
}