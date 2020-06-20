
/* *  tracker/amiga/CGX_Main.cpp
 *
 *  Copyright 2017 Marlon Beijer
 *  Copyright 2020 neoman
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
 *  MilkyTracker AGA/RTG Amiga front end
 *
 *  Created by Marlon Beijer on 17.09.17.
 *  Rewritten by neoman as full AGA/RTG port without SDL in 2020
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

#include "AmigaApplication.h"
#include "PPUI.h"

extern struct ExecBase * SysBase;

struct Library * IntuitionBase = NULL;
struct Library * KeymapBase = NULL;
struct Library * GfxBase = NULL;
struct Library * P96Base = NULL;
struct Library * VampireBase = NULL;
struct Library * IconBase = NULL;

int	cpuType = 0;
bool hasFPU = false;
bool hasAMMX = false;

struct Device * TimerBase = NULL;
static struct IORequest timereq;
static struct timeval startTime;

static AmigaApplication * app = NULL;

void PrintStackSize() {
	struct Task * task = FindTask(NULL);
	ULONG currentstack = (ULONG) task->tc_SPUpper - (ULONG) task->tc_SPLower;
	printf("Current stack: %lu\n", currentstack);
}

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

pp_uint32 PPGetTickCount() {
    struct timeval endTime;

	GetSysTime(&endTime);
    SubTime(&endTime, &startTime);

    return endTime.tv_secs * 1000 + endTime.tv_micro / 1000;
}

bool QueryClassicBrowser(bool currentSetting) {
	// For fullscreen mode, always use the classic browser on Amiga
	if(app && app->isFullScreen()) {
		return true;
	}
	return currentSetting;
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
	EasyStruct easyStruct;
	bool fromWorkbench;
	char exePath[256] = {0};
	BPTR dirLock, oldDir;
	struct DiskObject * diskObj;
	bool choseFullscreenByToolType = false;

	app = new AmigaApplication();
	app->setCpuType(cpuType);
	app->setHasFPU(hasFPU);
	app->setHasAMMX(hasAMMX);

	// Get program path from WBStartup/argv
   	fromWorkbench = argc == 0;
	if(fromWorkbench) {
   		struct WBArg * workbenchArgs = ((struct WBStartup *) argv)->sm_ArgList;
		dirLock = workbenchArgs->wa_Lock;
		strncpy(exePath, (char *) workbenchArgs->wa_Name, 255);
		oldDir = CurrentDir(dirLock);
	} else {
		strncpy(exePath, argv[0], 255);
	}

	// Process tool types
	if(diskObj = GetDiskObject(exePath)) {
		char * toolTypeVal;

		if(toolTypeVal = (char *) FindToolType(diskObj->do_ToolTypes, (STRPTR) "FULLSCREEN")) {
			app->setFullScreen(*toolTypeVal == '1');
			choseFullscreenByToolType = true;
		}

		FreeDiskObject(diskObj);
	}

	if(fromWorkbench) {
		CurrentDir(oldDir);
	}

	// Ask to run in fullscreen mode
	if(!choseFullscreenByToolType) {
		easyStruct.es_StructSize = sizeof(struct EasyStruct);
		easyStruct.es_Flags = 0;
		easyStruct.es_Title = (UBYTE *) "MilkyTracker";
		easyStruct.es_TextFormat = (UBYTE *) "Do you want to run in fullscreen mode?";
		easyStruct.es_GadgetFormat = (UBYTE *) "Yes|No";

		if(EasyRequest(NULL, &easyStruct, NULL) == 1) {
			app->setFullScreen(true);
		}
	}

	// @todo Add new splash screen
	app->setNoSplash(true);

	// And start
	if(ret = app->start()) {
		fprintf(stderr, "Starting tracker failed! (ret = %ld)\n", ret);
		app->stop();
	} else {
		do {
			app->loop();
		} while(app->stop() > 0);
	}

	delete app;

	return ret;
}

int main2(int argc, char * argv[])
{
	int ret = 0;

	// Check hardware
	if(!checkHardware()) {
		return 1;
	}

	// Open libraries and boot application
    if(KeymapBase = OpenLibrary("keymap.library", 39)) {
		if(IntuitionBase = OpenLibrary("intuition.library", 39)) {
			if(IconBase = OpenLibrary("icon.library", 37)) {
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
						fprintf(stderr, "Could not open %s V2! This program needs RTG installed.\n", P96NAME);
						ret = 1;
					}
					CloseLibrary(GfxBase);
				} else {
					fprintf(stderr, "Could not open graphics.library V39!\n");
					ret = 1;
				}
				CloseLibrary(IconBase);
			} else {
				fprintf(stderr, "Could not open icon.library V37!\n");
				ret = 1;
			}
			CloseLibrary(IntuitionBase);
		}  else {
			fprintf(stderr, "Could not open intuition.library V39!\n");
			ret = 1;
		}
		CloseLibrary(KeymapBase);
	} else {
		fprintf(stderr, "Could not open keymap.library V39!\n");
		ret = 1;
	}

	return ret;
}
