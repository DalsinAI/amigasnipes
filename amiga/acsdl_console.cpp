/* Snipes' text screen, keyboard and clock for AmigaOS 3.x, on acsdl12.
 *
 * Upstream's SDL 2 build (sdl/console.cpp, keyboard.cpp, timer.cpp) draws
 * the 40x25 text screen from a second thread with SDL2_ttf. Here there is
 * one thread: the game calls SleepTimeslice() whenever it waits (for the
 * next 18.2 Hz tick, for a key, for a line of input) and PollKeyboard()
 * once a frame, and both read the events and, at most every 20 ms, draw
 * the cells that changed straight into acsdl12's 8-bit 320x200 screen
 * with an 8x8 code page 437 font (cp437_font.h), as a CGA in its 40-column
 * text mode does. The palette is the CGA's 16 colours; a cell's attribute
 * byte is foreground in its low four bits and background in its high four,
 * as upstream draws it.
 *
 * The AmiCradle Team. Under the same terms as Snipes. */

#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "config.h"
#include "../Snipes.h"
#include "../macros.h"
#include "../console.h"
#include "../keyboard.h"
#include "../timer.h"
#include "cp437_font.h"

#define DEFAULT_TEXT_COLOR 0x7
#define TILE_W 8
#define TILE_H 8
#define SCREEN_W (WINDOW_WIDTH  * TILE_W)
#define SCREEN_H (WINDOW_HEIGHT * TILE_H)
#define FRAME_MS 20
#define CURSOR_TILE 0xFFFF /* never a real cell, so the cursor's cell is redrawn */

static MazeTile Screen[WINDOW_HEIGHT][WINDOW_WIDTH];
static Uint16   Shown [WINDOW_HEIGHT][WINDOW_WIDTH]; /* chr | color << 8 last drawn */
static SDL_Surface *VideoSurface;

static BYTE OutputTextColor = DEFAULT_TEXT_COLOR;
static Uint OutputCursorX = 0;
static Uint OutputCursorY = 0;
static bool OutputCursorVisible = true;
static Uint CursorShownX = ~0u, CursorShownY = ~0u;
static Uint32 LastFrameTicks;
static bool Paused = false;

#define InputBufferSize 16 // as in PC BIOS
static char InputBuffer[InputBufferSize];
static Uint InputBufferReadIndex = 0, InputBufferWriteIndex = 0;
static bool TextInputActive = false;

static const SDL_Color ScreenColors[16] =
{
	{ 0x00, 0x00, 0x00, 0 }, { 0x00, 0x00, 0xAA, 0 }, { 0x00, 0xAA, 0x00, 0 }, { 0x00, 0xAA, 0xAA, 0 },
	{ 0xAA, 0x00, 0x00, 0 }, { 0xAA, 0x00, 0xAA, 0 }, { 0xAA, 0x55, 0x00, 0 }, { 0xAA, 0xAA, 0xAA, 0 },
	{ 0x55, 0x55, 0x55, 0 }, { 0x55, 0x55, 0xFF, 0 }, { 0x55, 0xFF, 0x55, 0 }, { 0x55, 0xFF, 0xFF, 0 },
	{ 0xFF, 0x55, 0x55, 0 }, { 0xFF, 0x55, 0xFF, 0 }, { 0xFF, 0xFF, 0x55, 0 }, { 0xFF, 0xFF, 0xFF, 0 },
};

static void PumpEvents();
static void Present(bool force);

/* ------------------------------------------------------------- screen */

void WriteTextMem(Uint count, WORD row, WORD column, MazeTile *src)
{
	MazeTile* dst = &Screen[row][column];
	for (Uint i=0; i<count; i++)
	{
		if ((size_t)(dst - &Screen[0][0]) >= WINDOW_WIDTH*WINDOW_HEIGHT)
			break; // Out of bounds
		*dst++ = src[i];
	}
}

void outputText(BYTE color, WORD count, WORD row, WORD column, const char *src)
{
	MazeTile* dst = &Screen[row][column];
	for (Uint i=0; i<count; i++)
	{
		if ((size_t)(dst - &Screen[0][0]) >= WINDOW_WIDTH*WINDOW_HEIGHT)
			break; // Out of bounds
		*dst++ = MazeTile(color, src[i]);
	}
}

void outputNumber(BYTE color, bool zeroPadding, WORD count, WORD row, WORD column, Uint number)
{
	char textbuf[strlength("4294967295")+1];
	sprintf(textbuf, zeroPadding ? "%0*u" : "%*u", count, number);
	outputText(color, count, row, column, textbuf);
}

void EraseBottomTwoLines()
{
	for (Uint y = WINDOW_HEIGHT-2; y < WINDOW_HEIGHT; y++)
		for (Uint x = 0; x < WINDOW_WIDTH; x++)
			Screen[y][x] = MazeTile(7, ' ');
}

DWORD ReadTextFromConsole(char buffer[], DWORD bufsize)
{
	TextInputActive = true;

	DWORD numread = 0;
	while (true)
	{
		while (InputBufferReadIndex == InputBufferWriteIndex)
		{
			got_ctrl_break = forfeit_match;
			if (forfeit_match)
				goto done;
			SleepTimeslice();
		}
		char c = InputBuffer[InputBufferReadIndex];
		InputBufferReadIndex = (InputBufferReadIndex+1) % InputBufferSize;
		if (c == '\n')
		{
			WriteTextToConsole("\r\n", 2);
			break;
		}
		else
		if (c == '\b')
		{
			if (numread)
			{
				WriteTextToConsole(&c, 1);
				numread--;
			}
		}
		else
		{
			if (numread < bufsize)
			{
				WriteTextToConsole(&c, 1);
				buffer[numread] = c;
				numread++;
			}
		}
	}

done:
	TextInputActive = false;
	return numread;
}

void SetConsoleOutputTextColor(WORD wAttributes)
{
	OutputTextColor = (BYTE)wAttributes;
}

void WriteTextToConsole(char const *text, size_t length)
{
	for (Uint n=0; n<length; n++)
	{
		switch (text[n])
		{
		case '\r':
			OutputCursorX = 0;
			break;
		case '\n':
			OutputCursorY++;
			break;
		case '\b':
			if (OutputCursorX)
			{
				OutputCursorX--;
				outputText(OutputTextColor, 1, OutputCursorY, OutputCursorX, " ");
			}
			break;
		default:
			outputText(OutputTextColor, 1, OutputCursorY, OutputCursorX, text+n);
			OutputCursorX++;
			break;
		}

		if (OutputCursorY == WINDOW_HEIGHT) // Scroll down
		{
			memmove(&Screen[0], &Screen[1], sizeof(Screen[0]) * (WINDOW_HEIGHT-1));
			OutputCursorY--;
		}
	}
}

void OpenDirectConsole()
{
	OutputCursorVisible = false;
}

void CloseDirectConsole(Uint lineNum)
{
	OutputCursorVisible = true;
	OutputCursorX = 0;
	OutputCursorY = lineNum;
	OutputTextColor = DEFAULT_TEXT_COLOR;
}

void ClearConsole()
{
	OutputCursorX = 0;
	OutputCursorY = 0;
	OutputTextColor = DEFAULT_TEXT_COLOR;

	for (Uint y = 0; y < WINDOW_HEIGHT; y++)
		for (Uint x = 0; x < WINDOW_WIDTH; x++)
			Screen[y][x].color = DEFAULT_TEXT_COLOR;
}

static void DrawCell(Uint x, Uint y, BYTE chr, BYTE fg, BYTE bg)
{
	Uint8 *dst = (Uint8*)VideoSurface->pixels + y * TILE_H * VideoSurface->pitch + x * TILE_W;
	const unsigned char *glyph = Cp437Font[chr];
	for (Uint row = 0; row < TILE_H; row++, dst += VideoSurface->pitch)
	{
		unsigned bits = glyph[row];
		for (Uint col = 0; col < TILE_W; col++, bits <<= 1)
			dst[col] = (bits & 0x80) ? fg : bg;
	}
}

static void Present(bool force)
{
	if (!VideoSurface)
		return;
	Uint32 now = SDL_GetTicks();
	if (!force && now - LastFrameTicks < FRAME_MS)
		return;
	LastFrameTicks = now;

	/* The cursor blinks as upstream's does: a solid block, 250 ms on, 250 off. */
	bool cursorOn = OutputCursorVisible && now % 500 < 250 &&
	                OutputCursorX < WINDOW_WIDTH && OutputCursorY < WINDOW_HEIGHT;
	Uint cursorX = cursorOn ? OutputCursorX : ~0u;
	Uint cursorY = cursorOn ? OutputCursorY : ~0u;

	bool changed = false;
	if (SDL_LockSurface(VideoSurface) != 0)
		return;
	for (Uint y = 0; y < WINDOW_HEIGHT; y++)
		for (Uint x = 0; x < WINDOW_WIDTH; x++)
		{
			MazeTile tile = Screen[y][x];
			bool isCursor = x == cursorX && y == cursorY;
			Uint16 key = isCursor ? CURSOR_TILE : (Uint16)(tile.chr | tile.color << 8);
			if (Shown[y][x] == key && !(isCursor && (cursorX != CursorShownX || cursorY != CursorShownY)))
				continue;
			Shown[y][x] = key;
			changed = true;
			if (isCursor)
				DrawCell(x, y, 0xDB, OutputTextColor & 0xF, 0);
			else
				DrawCell(x, y, tile.chr, tile.color & 0xF, tile.color >> 4);
		}
	SDL_UnlockSurface(VideoSurface);
	CursorShownX = cursorX;
	CursorShownY = cursorY;
	if (changed)
		SDL_UpdateRect(VideoSurface, 0, 0, 0, 0);
}

int OpenConsole()
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
	{
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	VideoSurface = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 8, SDL_SWSURFACE | SDL_HWPALETTE | SDL_FULLSCREEN);
	if (!VideoSurface)
	{
		fprintf(stderr, "SDL_SetVideoMode: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}
	SDL_WM_SetCaption("Snipes", "Snipes");
	SDL_ShowCursor(SDL_DISABLE);
	SDL_Color palette[256];
	memset(palette, 0, sizeof(palette));
	memcpy(palette, ScreenColors, sizeof(ScreenColors));
	SDL_SetPalette(VideoSurface, SDL_LOGPAL | SDL_PHYSPAL, palette, 0, 256);
	SDL_FillRect(VideoSurface, NULL, 0);
	SDL_EnableUNICODE(1);
	SDL_EnableKeyRepeat(0, 0);
	memset(Shown, 0xFF, sizeof(Shown)); /* draw every cell the first time */
	for (Uint y = 0; y < WINDOW_HEIGHT; y++)
		for (Uint x = 0; x < WINDOW_WIDTH; x++)
			Screen[y][x] = MazeTile(DEFAULT_TEXT_COLOR, ' ');
	LastFrameTicks = SDL_GetTicks();
	Present(true);
	return 0;
}

void CloseConsole()
{
	Present(true);
	VideoSurface = NULL;
	SDL_Quit();
}

/* ----------------------------------------------------------- keyboard */

static BYTE KeyState[SDLK_LAST];
#ifdef USE_SCANCODES_FOR_LETTER_KEYS
static BYTE KeyScanState[128];       /* by Amiga raw key: the fire keys stay where they are on any layout */
enum { RAW_W = 0x11, RAW_A = 0x20, RAW_S = 0x21, RAW_D = 0x22, RAW_X = 0x32 };
#endif
static SDLMod ModifierState;
static bool AnyKeyPressed = false;

void ClearKeyboard()
{
	memset(KeyState, 0, sizeof(KeyState));
#ifdef USE_SCANCODES_FOR_LETTER_KEYS
	memset(KeyScanState, 0, sizeof(KeyScanState));
#endif
	ModifierState = KMOD_NONE;
	InputBufferReadIndex = InputBufferWriteIndex = 0;
}

static void PushInput(char c)
{
	Uint next = (InputBufferWriteIndex+1) % InputBufferSize;
	if (next == InputBufferReadIndex)
		return; // full, as the BIOS buffer would beep
	InputBuffer[InputBufferWriteIndex] = c;
	InputBufferWriteIndex = next;
}

static void HandleKey(const SDL_KeyboardEvent *e)
{
	SDLKey sym = e->keysym.sym;
	ModifierState = e->keysym.mod;
	if ((unsigned)sym >= SDLK_LAST)
		return;
	if (e->type == SDL_KEYDOWN)
	{
		if (!KeyState[sym])
		{
			switch (sym)
			{
			case SDLK_LCTRL: case SDLK_RCTRL:
			case SDLK_LSHIFT: case SDLK_RSHIFT:
			case SDLK_LALT: case SDLK_RALT:
			case SDLK_LMETA: case SDLK_RMETA:
			case SDLK_LSUPER: case SDLK_RSUPER:
			case SDLK_CAPSLOCK:
				break;
			default:
				AnyKeyPressed = true;
				break;
			}
		}
		KeyState[sym] |= 1;
#ifdef USE_SCANCODES_FOR_LETTER_KEYS
		KeyScanState[e->keysym.scancode & 0x7F] |= 1;
#endif

		if (sym == SDLK_F1)
			sound_enabled ^= true;
		else
		if (sym == SDLK_F2)
			shooting_sound_enabled ^= true;
		else
		if (sym == SDLK_ESCAPE)
			forfeit_match = true;
		else
		if (sym == SDLK_c && (e->keysym.mod & KMOD_CTRL) && !(e->keysym.mod & KMOD_ALT))
			forfeit_match = true;
		else
		if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER)
			PushInput('\n');
		else
		if (sym == SDLK_BACKSPACE)
			PushInput('\b');
		else
		if (TextInputActive && e->keysym.unicode >= 0x20 && e->keysym.unicode < 0x7F)
			PushInput((char)e->keysym.unicode);
	}
	else
	{
		KeyState[sym] &= ~1;
#ifdef USE_SCANCODES_FOR_LETTER_KEYS
		KeyScanState[e->keysym.scancode & 0x7F] &= ~1;
#endif
	}
}

static void PumpEvents()
{
	SDL_Event e;
	while (SDL_PollEvent(&e))
	{
		switch (e.type)
		{
		case SDL_QUIT:
			instant_quit = forfeit_match = got_ctrl_break = true;
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			HandleKey(&e.key);
			break;
		case SDL_ACTIVEEVENT:
			if (e.active.state & (SDL_APPINPUTFOCUS | SDL_APPACTIVE))
			{
				Paused = !e.active.gain;
				if (Paused)
					ClearKeyboard(); // keys let go while away would stick
			}
			break;
		default:
			break;
		}
	}
}

Uint PollKeyboard()
{
	PumpEvents();
	Present(false);
	while (Paused && !forfeit_match)
		SleepTimeslice();

	Uint state = 0;
	if (KeyState[SDLK_RIGHT]) state |= KEYSTATE_MOVE_RIGHT;
	if (KeyState[SDLK_LEFT ]) state |= KEYSTATE_MOVE_LEFT;
	if (KeyState[SDLK_DOWN ]) state |= KEYSTATE_MOVE_DOWN;
	if (KeyState[SDLK_UP   ]) state |= KEYSTATE_MOVE_UP;
	/* The Amiga's keypad has no Num Lock: its digits always move. */
	if (KeyState[SDLK_KP6]) state |= KEYSTATE_MOVE_RIGHT;
	if (KeyState[SDLK_KP4]) state |= KEYSTATE_MOVE_LEFT;
	if (KeyState[SDLK_KP5]) state |= KEYSTATE_MOVE_DOWN;
	if (KeyState[SDLK_KP2]) state |= KEYSTATE_MOVE_DOWN;
	if (KeyState[SDLK_KP8]) state |= KEYSTATE_MOVE_UP;
#ifndef USE_SCANCODES_FOR_LETTER_KEYS
	if (KeyState[SDLK_d]) state |= KEYSTATE_FIRE_RIGHT;
	if (KeyState[SDLK_a]) state |= KEYSTATE_FIRE_LEFT;
	if (KeyState[SDLK_s]) state |= KEYSTATE_FIRE_DOWN;
	if (KeyState[SDLK_x]) state |= KEYSTATE_FIRE_DOWN;
	if (KeyState[SDLK_w]) state |= KEYSTATE_FIRE_UP;
#else
	if (KeyScanState[RAW_D]) state |= KEYSTATE_FIRE_RIGHT;
	if (KeyScanState[RAW_A]) state |= KEYSTATE_FIRE_LEFT;
	if (KeyScanState[RAW_S]) state |= KEYSTATE_FIRE_DOWN;
	if (KeyScanState[RAW_X]) state |= KEYSTATE_FIRE_DOWN;
	if (KeyScanState[RAW_W]) state |= KEYSTATE_FIRE_UP;
#endif
	spacebar_state = KeyState[SDLK_SPACE];
	if ((state & (KEYSTATE_MOVE_RIGHT | KEYSTATE_MOVE_LEFT)) == (KEYSTATE_MOVE_RIGHT | KEYSTATE_MOVE_LEFT) ||
		(state & (KEYSTATE_MOVE_DOWN  | KEYSTATE_MOVE_UP  )) == (KEYSTATE_MOVE_DOWN  | KEYSTATE_MOVE_UP  ))
		state &= ~(KEYSTATE_MOVE_RIGHT | KEYSTATE_MOVE_LEFT | KEYSTATE_MOVE_DOWN | KEYSTATE_MOVE_UP);
	if ((state & (KEYSTATE_FIRE_RIGHT | KEYSTATE_FIRE_LEFT)) == (KEYSTATE_FIRE_RIGHT | KEYSTATE_FIRE_LEFT) ||
		(state & (KEYSTATE_FIRE_DOWN  | KEYSTATE_FIRE_UP  )) == (KEYSTATE_FIRE_DOWN  | KEYSTATE_FIRE_UP  ))
		state &= ~(KEYSTATE_FIRE_RIGHT | KEYSTATE_FIRE_LEFT | KEYSTATE_FIRE_DOWN | KEYSTATE_FIRE_UP);
	fast_forward = KeyState[SDLK_f];
	return state;
}

void WaitForKeyPress()
{
	AnyKeyPressed = false;
	while (!AnyKeyPressed && !instant_quit)
		SleepTimeslice();
}

int OpenKeyboard()
{
	ClearKeyboard();
	return 0;
}

void CloseKeyboard()
{
}

/* -------------------------------------------------------------- clock */

WORD GetTickCountWord()
{
	/* The PC's 18.2 Hz BIOS tick, from the 1.19 MHz timer: 13125000/11/65536. */
	return (WORD)(SDL_GetTicks() * 13125uLL / (11*65535));
}

int OpenTimer()
{
	return 0;
}

void CloseTimer()
{
}

void SleepTimeslice()
{
	PumpEvents();
	Present(false);
	SDL_Delay(1);
}
