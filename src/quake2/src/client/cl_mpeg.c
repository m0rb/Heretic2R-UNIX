//
// cl_mpeg.c -- MPEG-1 cinematic backend (based on PL_MPEG, https://github.com/phoboslab/pl_mpeg)
//
// For the Loki Games release mpg files (MPEG-1 video + MP2 audio)
//
// Heretic2R UNIX port by morb
//

#include "client.h"
#include "cl_mpeg.h"

#ifndef _WIN32
#include "../unix/compat.h"
#endif

#include <pl_mpeg/pl_mpeg.h>

extern refexport_t re;

typedef struct
{
	plm_t*   plm;
	int      width;
	int      height;
	byte*    rgba;          // Latest decoded frame, width*height*4.
	qboolean have_frame;
	int      last_realtime; // cls.realtime baseline for the playback clock.
	int16_t* abuf;          // Scratch for float -> int16 audio conversion.
	int      abuf_cap;      // Capacity of abuf, in int16 samples.
} mpeg_state_t;

static mpeg_state_t m;

// Decoded video frame: lazily init the renderer (dimensions are only known once
// the first frame is decoded) and convert to RGBA.
static void MPEG_OnVideo(plm_t* plm, plm_frame_t* frame, void* user)
{
	if (m.rgba == NULL)
	{
		m.width = frame->width;
		m.height = frame->height;
		m.rgba = malloc((size_t)m.width * m.height * 4);
		// PL_MPEG's RGBA convert writes only R/G/B and never the alpha byte, so
		// prime alpha to fully opaque once. The cinematic blits through
		// Draw_Render(), which alpha-blends + alpha-tests; leaving alpha as malloc
		// garbage. Masks out pixels in a funky fixed pattern (vertical-comb corruption).
		memset(m.rgba, 0xFF, (size_t)m.width * m.height * 4);
		re.DrawInitCinematicRGBA(m.width, m.height);
	}

	plm_frame_to_rgba(frame, m.rgba, m.width * 4);
	m.have_frame = true;
}

// Decoded audio frame: PL_MPEG hands us float, stereo-interleaved samples (mono
// sources are duplicated to both channels). Convert to int16 and feed the sound
// mixer's raw stream, same as Smacker and OGG paths.
static void MPEG_OnAudio(plm_t* plm, plm_samples_t* samples, void* user)
{
	const int total = (int)samples->count * 2; // Stereo interleaved.

	if (m.abuf_cap < total)
	{
		m.abuf = realloc(m.abuf, (size_t)total * sizeof(int16_t));
		m.abuf_cap = total;
	}

	for (int i = 0; i < total; i++)
	{
		float f = samples->interleaved[i];
		f = (f > 1.0f) ? 1.0f : (f < -1.0f ? -1.0f : f);
		m.abuf[i] = (int16_t)(f * 32767.0f);
	}

	se.RawSamples((int)samples->count, (uint)plm_get_samplerate(m.plm),
		sizeof(int16_t), 2, (const byte*)m.abuf, Cvar_VariableValue("s_volume"));
}

qboolean MPEG_Open(const char* fullpath)
{
	memset(&m, 0, sizeof(m));

	m.plm = plm_create_with_filename(fullpath);
	if (m.plm == NULL)
		return false;

	// Loki's mpgs carry a MPEG-PS header that declares zero video streams.
	// plm_probe() ignores the header and scans packet start codes to find them.
	if (!plm_probe(m.plm, 2 * 1024 * 1024) || plm_get_num_video_streams(m.plm) < 1)
	{
		Com_Printf("MPEG_Open: no video stream found in '%s'\n", fullpath);
		plm_destroy(m.plm);
		m.plm = NULL;
		return false;
	}

	plm_set_video_decode_callback(m.plm, MPEG_OnVideo, NULL);
	plm_set_audio_decode_callback(m.plm, MPEG_OnAudio, NULL);
	plm_set_audio_enabled(m.plm, true);

	// Prime the first frame so dimensions are known and the renderer is set up.
	const float fps = (float)plm_get_framerate(m.plm);
	const double step = (fps > 0.0f) ? (1.0 / (double)fps) : (1.0 / 30.0);
	int guard = 0;
	while (!m.have_frame && !plm_has_ended(m.plm) && guard++ < 1000)
		plm_decode(m.plm, step);

	if (!m.have_frame)
	{
		Com_Printf("MPEG_Open: failed to decode first frame of '%s'\n", fullpath);
		MPEG_Shutdown();
		return false;
	}

	m.last_realtime = cls.realtime;
	return true;
}

void MPEG_Shutdown(void)
{
	if (m.rgba != NULL)        // Renderer cinematic state was initialized.
		re.DrawCloseCinematic();
	if (m.plm != NULL)
		plm_destroy(m.plm);

	free(m.rgba);
	free(m.abuf);
	memset(&m, 0, sizeof(m));
}

void MPEG_Run(void)
{
	if (m.plm == NULL)
		return;

	// Pause while a menu or the console is up: hold the clock so playback resumes
	// from where it left off.
	if (cls.key_dest != key_game)
	{
		m.last_realtime = cls.realtime;
		return;
	}

	double delta = (double)(cls.realtime - m.last_realtime) / 1000.0;
	m.last_realtime = cls.realtime;

	if (delta <= 0.0)
		return;
	if (delta > 0.25)
		delta = 0.25; // Clamp after a load hitch to avoid a burst of audio. 

	plm_decode(m.plm, delta);

	if (plm_has_ended(m.plm))
		SCR_FinishCinematic(); // Sends 'nextserver' and calls SCR_StopCinematic().
}

void MPEG_Draw(void)
{
	if (m.plm != NULL && m.have_frame)
		re.DrawCinematicRGBA(m.rgba);
}
