/* Snipes' PC speaker for AmigaOS 3.x, on acsdl12.
 *
 * The game plays one square wave at a time, its pitch given as the PC's
 * timer divisor (1193182 Hz / freqnum), as upstream's sdl/sound.cpp does.
 * Here the wave is made in acsdl12's SDL_OpenAudio callback (the mixer's
 * music hook) at 22050 Hz, 16-bit mono, with a 32-bit phase accumulator
 * instead of upstream's fmod() for each sample: a change of pitch carries
 * on from the same phase, as upstream's does. Without AHI the game plays
 * on silently.
 *
 * The AmiCradle Team. Under the same terms as Snipes. */

#include <stdio.h>
#include <SDL.h>
#include "config.h"
#include "../sound.h"

#define TONE_SAMPLE_RATE 22050
#define WAVE_BUFFER_LENGTH 512
#define TONE_AMPLITUDE 0x1000

static volatile Uint32 PhaseStep; /* 0: silence */
static Uint32 Phase;
static Uint32 SampleRate = TONE_SAMPLE_RATE;
static bool AudioOpen;

static void SoundCallback(void *, Uint8 *_stream, int _length)
{
	Sint16 *stream = (Sint16*)_stream;
	Uint length = (Uint)_length / 2;
	Uint32 step = PhaseStep;

	if (step == 0)
		for (Uint i=0; i<length; i++)
			stream[i] = 0;
	else
		for (Uint i=0; i<length; i++)
		{
			stream[i] = (Phase & 0x80000000u) ? TONE_AMPLITUDE : -TONE_AMPLITUDE;
			Phase += step;
		}
}

void PlayTone(Uint freqnum)
{
	if (!freqnum)
	{
		PhaseStep = 0;
		return;
	}
	double freq = (13125000. / 11) / freqnum;
	double step = freq * 4294967296. / SampleRate;
	PhaseStep = step >= 2147483648. ? 0x7FFFFFFFu : (Uint32)step;
}

void ClearSound()
{
	PhaseStep = 0;
}

int OpenSound()
{
	SDL_AudioSpec desiredSpec, obtainedSpec;

	desiredSpec.freq = TONE_SAMPLE_RATE;
	desiredSpec.format = AUDIO_S16SYS;
	desiredSpec.channels = 1;
	desiredSpec.samples = WAVE_BUFFER_LENGTH;
	desiredSpec.callback = SoundCallback;
	desiredSpec.userdata = NULL;

	if (SDL_OpenAudio(&desiredSpec, &obtainedSpec) != 0)
	{
		fprintf(stderr, "No sound (SDL_OpenAudio: %s)\n", SDL_GetError());
		return 0; /* play on without it */
	}
	if (obtainedSpec.freq > 0)
		SampleRate = obtainedSpec.freq;
	AudioOpen = true;
	SDL_PauseAudio(0);
	return 0;
}

void CloseSound()
{
	if (AudioOpen)
		SDL_CloseAudio();
	AudioOpen = false;
}
