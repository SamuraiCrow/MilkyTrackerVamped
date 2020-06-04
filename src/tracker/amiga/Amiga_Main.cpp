
/* *  tracker/amiga/CGX_Main.cpp
 *
 *  Copyright 2017 Marlon Beijer
 *
 *  This file is part of Milkytracker.
 *
 *  Milkytracker is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Milkytracker is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Milkytracker.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
 *  CGX_Main.cpp
 *  MilkyTracker CybergraphX (RTG) Amiga front end
 *
 *  Created by Marlon Beijer on 17.09.17.
 *
 */

#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif
#if defined(__AMIGA__) || defined(__amigaos4__)
#	ifndef AFB_68080
#		define AFB_68080 10
#	endif
#	ifndef AFF_68080
#		define AFF_68080 (1<<AFB_68080)
#	endif
#	if defined(WARPOS) && !defined(AFF68060)
#		define AFF_68060 1
#	endif
#endif
#if defined(__AMIGA__) || defined(WARPUP) || defined(__WARPOS__) || defined(AROS) || defined(__amigaos4__) || defined(__morphos__)
#	include "amigaversion.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>

//#include <SDL.h>
//#include "SDL_KeyTranslation.h"
#include "AmigaApplication.h"
// ---------------------------- Tracker includes ----------------------------
#include "PPUI.h"
#include "DisplayDevice_Amiga.h"
#include "Screen.h"
#include "Tracker.h"
#include "PPMutex.h"
#include "PPSystem_POSIX.h"
#include "PPPath_POSIX.h"
// --------------------------------------------------------------------------

// Amiga specifics
extern struct ExecBase * SysBase;

struct Library * IntuitionBase = NULL;
struct Library * KeymapBase = NULL;
struct Library * GfxBase = NULL;
struct Library * P96Base = NULL;
struct Library * VampireBase = NULL;

int	cpuType = 0;
bool hasFPU = false;
bool hasAMMX = false;

struct Device * TimerBase = NULL;
static struct IORequest timereq;

static AmigaApplication * app = NULL;

static pp_uint32 lmyTime;
static PPPoint llastClickPosition = PPPoint(0, 0);
static pp_uint16 lClickCount = 0;

static pp_uint32 rmyTime;
static PPPoint rlastClickPosition = PPPoint(0, 0);
static pp_uint16 rClickCount = 0;

static bool lMouseDown = false;
static pp_uint32 lButtonDownStartTime;

static bool rMouseDown = false;
static pp_uint32 rButtonDownStartTime;

static pp_uint32 timerTicker = 0;

static PPPoint p;

static bool exitDone;

static struct timeval startTime;

void QueryKeyModifiers() {
	if(!app)
		return;

	if(app->isShiftPressed())
		setKeyModifier(KeyModifierSHIFT);
	else
		clearKeyModifier(KeyModifierSHIFT);

	if(app->isCtrlPressed())
		setKeyModifier(KeyModifierCTRL);
	else
		clearKeyModifier(KeyModifierCTRL);

	if(app->isAltPressed())
		setKeyModifier(KeyModifierALT);
	else
		clearKeyModifier(KeyModifierALT);
}

/*
static void translateMouseUpEvent(pp_int32 mouseButton, pp_int32 localMouseX, pp_int32 localMouseY) {
	// @todo
	if (mouseButton == SDL_BUTTON_WHEELDOWN) {
		TMouseWheelEventParams mouseWheelParams;
		mouseWheelParams.pos.x = localMouseX;
		mouseWheelParams.pos.y = localMouseY;
		mouseWheelParams.deltaX = -1;
		mouseWheelParams.deltaY = -1;

		PPEvent myEvent(eMouseWheelMoved, &mouseWheelParams, sizeof (mouseWheelParams));
		RaiseEventSerialized(&myEvent);
	} else if (mouseButton == SDL_BUTTON_WHEELUP) {
		TMouseWheelEventParams mouseWheelParams;
		mouseWheelParams.pos.x = localMouseX;
		mouseWheelParams.pos.y = localMouseY;
		mouseWheelParams.deltaX = 1;
		mouseWheelParams.deltaY = 1;

		PPEvent myEvent(eMouseWheelMoved, &mouseWheelParams, sizeof (mouseWheelParams));
		RaiseEventSerialized(&myEvent);
	} else if (mouseButton > 2 || !mouseButton)
		return;

	// -----------------------------
	if (mouseButton == 1) {
		lClickCount++;

		if (lClickCount >= 4) {
			pp_uint32 deltat = PPGetTickCount() - lmyTime;

			if (deltat < 500) {
				p.x = localMouseX;
				p.y = localMouseY;
				if (abs(p.x - llastClickPosition.x) < 4 &&
						abs(p.y - llastClickPosition.y) < 4) {
					PPEvent myEvent(eLMouseDoubleClick, &p, sizeof (PPPoint));
					RaiseEventSerialized(&myEvent);
				}
			}

			lClickCount = 0;
		}

		p.x = localMouseX;
		p.y = localMouseY;
		PPEvent myEvent(eLMouseUp, &p, sizeof (PPPoint));
		RaiseEventSerialized(&myEvent);
		lMouseDown = false;
	} else if (mouseButton == 2) {
		rClickCount++;

		if (rClickCount >= 4) {
			pp_uint32 deltat = PPGetTickCount() - rmyTime;

			if (deltat < 500) {
				p.x = localMouseX;
				p.y = localMouseY;
				if (abs(p.x - rlastClickPosition.x) < 4 && abs(p.y - rlastClickPosition.y) < 4) {
					PPEvent myEvent(eRMouseDoubleClick, &p, sizeof (PPPoint));
					RaiseEventSerialized(&myEvent);
				}
			}

			rClickCount = 0;
		}

		p.x = localMouseX;
		p.y = localMouseY;
		PPEvent myEvent(eRMouseUp, &p, sizeof (PPPoint));
		RaiseEventSerialized(&myEvent);
		rMouseDown = false;
	}
}

static void translateKeyDownEvent(const SDL_Event& event) {
	SDL_keysym keysym = event.key.keysym;

	// ALT+RETURN = Fullscreen toggle
	if (keysym.sym == SDLK_RETURN && (keysym.mod & KMOD_LALT)) {
		PPEvent myEvent(eFullScreen);
		RaiseEventSerialized(&myEvent);
		return;
	}

	pp_uint16 character = event.key.keysym.unicode;

	pp_uint16 chr[3] = {toVK(keysym), toSC(keysym), character};

#ifndef NOT_PC_KB
	// Hack for azerty keyboards (num keys are shifted, so we use the scancodes)
	if (stdKb) {
		if (chr[1] >= 2 && chr[1] <= 10)
			chr[0] = chr[1] + 47; // 1-9
		else if (chr[1] == 11)
			chr[0] = 48; // 0
	}
#endif

	PPEvent myEvent(eKeyDown, &chr, sizeof (chr));
	RaiseEventSerialized(&myEvent);

	if (character == 127) character = VK_BACK;

	if (character >= 32 && character <= 127) {
		PPEvent myEvent2(eKeyChar, &character, sizeof (character));
		RaiseEventSerialized(&myEvent2);
	}
}

static void translateKeyUpEvent(const SDL_Event& event) {
	SDL_keysym keysym = event.key.keysym;

	pp_uint16 character = event.key.keysym.unicode;

	pp_uint16 chr[3] = {toVK(keysym), toSC(keysym), character};

#ifndef NOT_PC_KB
	if (stdKb) {
		if (chr[1] >= 2 && chr[1] <= 10)
			chr[0] = chr[1] + 47;
		else if (chr[1] == 11)
			chr[0] = 48;
	}
#endif

	PPEvent myEvent(eKeyUp, &chr, sizeof (chr));
	RaiseEventSerialized(&myEvent);
}

void processSDLEvents(const SDL_Event& event) {
	pp_uint32 mouseButton = 0;

	switch (event.type) {
		case SDL_KEYDOWN:
			translateKeyDownEvent(event);
			break;

		case SDL_KEYUP:
			translateKeyUpEvent(event);
			break;
	}
}

/*
void exitSDLEventLoop(bool serializedEventInvoked) {
	PPEvent event(eAppQuit);
	RaiseEventSerialized(&event);

	// it's necessary to make this mutex lock because the SDL modal event loop
	// used in the modal dialogs expects modal dialogs to be invoked by
	// events within these mutex lock calls
	if (!serializedEventInvoked)
		globalMutex->lock();

	bool res = myTracker->shutDown();

	if (!serializedEventInvoked)
		globalMutex->unlock();

	if (res)
		exitDone = 1;
}*/

pp_uint32 PPGetTickCount() {
    struct timeval endTime;

	GetSysTime(&endTime);
    SubTime(&endTime, &startTime);

    return endTime.tv_secs * 1000 + endTime.tv_micro / 1000;
}

static bool checkHardware()
{
	cpuType = 0;
	hasFPU = true;
	hasAMMX = false;

#if !defined(__amigaos4__) && !defined(MORPHOS) && !defined(WARPOS) && defined(__AMIGA__)

	if ((SysBase->AttnFlags & AFF_68080) != 0)
		cpuType = 68080;
	else if ((SysBase->AttnFlags & AFF_68060) != 0)
		cpuType = 68060;
	else if ((SysBase->AttnFlags & AFF_68040) != 0)
		cpuType = 68040;
	else if ((SysBase->AttnFlags & AFF_68030) != 0)
		cpuType = 68030;
	else if ((SysBase->AttnFlags & AFF_68020) != 0)
		cpuType = 68020;
	else if ((SysBase->AttnFlags & AFF_68010) != 0)
		cpuType = 68010;
	else
		cpuType = 68000;

	if ((SysBase->AttnFlags & AFF_FPU40) != 0)
		hasFPU = true;
	if (cpuType == 68080)
		hasAMMX = true;
#endif

	return hasFPU;
}

static int boot(int argc, char * argv[])
{
	int ret;
	app = new AmigaApplication();

	app->setCpuType(cpuType);
	app->setHasFPU(hasFPU);
	app->setHasAMMX(hasAMMX);

	// Parse command line (@todo use ToolTypes)
	while (argc > 1) {
		--argc;
		if (strcmp(argv[argc - 1], "-bpp") == 0) {
			app->setBpp(atoi(argv[argc]));
			--argc;
		} else if (strcmp(argv[argc], "-nosplash") == 0) {
			app->setNoSplash(true);
		} else if (strcmp(argv[argc], "-fullscreen") == 0) {
			app->setNoSplash(false);
		} else {
			if (argv[argc][0] == '-') {
				fprintf(stderr, "Usage: %s [-bpp N] [-fullscreen] [-nosplash] [-recvelocity]\n", argv[0]);
				return 1;
			} else {
				app->setLoadFile(argv[argc]);
			}
		}
	}

	// And start
	if(ret = app->start()) {
		fprintf(stderr, "Starting tracker failed! (ret = %ld)\n", ret);
	} else {
		app->loop();

		if(ret = app->stop()) {
			fprintf(stderr, "Stopping tracker failed! (ret = %ld)\n", ret);
		}
	}

	delete app;

	return ret;
}

int main(int argc, char * argv[])
{
	int ret = 0;

	// Check hardware
	if(!checkHardware()) {
		return 1;
	}

	// Open libraries and boot application
    if(KeymapBase = OpenLibrary("keymap.library", 39)) {
		if(IntuitionBase = OpenLibrary("intuition.library", 39)) {
			if(GfxBase = OpenLibrary("graphics.library", 39)) {
				if(P96Base = OpenLibrary(P96NAME, 2)) {
					BYTE err = OpenDevice("timer.device", 0, &timereq, 0);
					if(err == 0) {
						TimerBase = timereq.io_Device;
						GetSysTime(&startTime);

						if(hasAMMX) {
							if(!(VampireBase = (struct Library *) OpenResource(V_VAMPIRENAME))) {
								fprintf(stderr, "Could not find vampire.resource!\n");
								ret = 2;
							} else if(VampireBase->lib_Version < 45) {
								fprintf(stderr, "Vampire.resource version needs to be 45 or higher!\n");
								ret = 2;
							} else if(V_EnableAMMX(V_AMMX_V2) == VRES_ERROR) {
								fprintf(stderr, "Cannot enable AMMX V2+!\n");
								ret = 2;
							}
						}

						if(!ret) {
							ret = boot(argc, argv);
						}

						CloseDevice(&timereq);
					} else {
						fprintf(stderr, "Could not open timer.device! (err = %ld)\n", err);
						ret = 1;
					}
					CloseLibrary(P96Base);
				} else {
					fprintf(stderr, "Could not open %s V2!\n", P96NAME);
					ret = 1;
				}
				CloseLibrary(GfxBase);
			} else {
				fprintf(stderr, "Could not open graphics.library V39!\n");
				ret = 1;
			}
			CloseLibrary(IntuitionBase);
		}  else {
			fprintf(stderr, "Could not open intuition.library V39!\n");
			ret = 1;
		}
	} else {
		fprintf(stderr, "Could not open keymap.library V39!\n");
		ret = 1;
	}

	return ret;
}
