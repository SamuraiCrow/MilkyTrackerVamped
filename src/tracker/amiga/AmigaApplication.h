#ifndef TRACKER_AMIGA_APPLICATION_H
#define TRACKER_AMIGA_APPLICATION_H

#include "BasicTypes.h"

#include <exec/exec.h>
#include <intuition/intuition.h>

#include <vampire/saga.h>
#include <vampire/vampire.h>
#include <proto/vampire.h>

#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#include <clib/graphics_protos.h>
#include <clib/keymap_protos.h>
#include <clib/timer_protos.h>
#include <clib/intuition_protos.h>
#include <clib/picasso96_protos.h>
#include <clib/vampire_protos.h>

#include <hardware/intbits.h>

class Tracker;
class DisplayDevice_Amiga;
class PPScreen;
class PPEvent;

class AmigaApplication
{
private:
    int                     cpuType;
    bool                    hasFPU;
    bool                    hasAMMX;
    pp_uint32               bpp;
    bool                    noSplash;
    bool                    running;
    bool                    fullScreen;
    bool                    trackerStartUpFinished;
    char *                  loadFile;
    bool                    showAlert;

    Tracker *               tracker;
    DisplayDevice_Amiga *   displayDevice;
    PPScreen *              trackerScreen;
    PPSize                  windowSize;
    PPSystemString          oldCwd;
    char                    currentTitle[256];
    char                    currentAlert[256];

    struct Window *         window;
    struct Screen *         screen;
    struct Task *           task;

    struct Interrupt *      irqVerticalBlank;
    pp_int32                vbSignal;
    pp_uint32               vbMask;
    pp_uint32               vbCount;

    PPPoint                 mousePosition;

    pp_uint32               mouseLeftSeconds;
    pp_uint32               mouseLeftMicros;
    bool                    mouseLeftDown;
    pp_uint32               mouseLeftVBStart;

    pp_uint32               mouseRightSeconds;
    pp_uint32               mouseRightMicros;
    bool                    mouseRightDown;
    pp_uint32               mouseRightVBStart;

    bool                    keyQualifierShiftPressed;
    bool                    keyQualifierCtrlPressed;
    bool                    keyQualifierAltPressed;

    int                     load(char * loadFile);
    void                    raiseEventSynchronized(PPEvent * event);
    void                    resetScreenAlert();

    void                    setMousePosition(pp_int32 x, pp_int32 y);

    static pp_int32         verticalBlankService(register AmigaApplication * that __asm("a1"));
    pp_int32                verticalBlank();
public:
    int                     start();
    void                    loop();
    int                     stop();

    bool                    isFullScreen() const { return fullScreen; }
    bool                    isAMMX() const { return hasAMMX; }

    struct Screen *         getScreen() const { return screen; }
    struct Window *         getWindow() const { return window; }

    const PPSize&           getWindowSize() const { return windowSize; }
    pp_uint32               getBpp() const { return bpp; }

    void                    setRunning(bool running) { this->running = running; }
    void                    setFullScreen(bool fullScreen) { this->fullScreen = fullScreen; }
	void                    setCpuType(int cpuType) { this->cpuType = cpuType; }
	void                    setHasFPU(bool hasFPU) { this->hasFPU = hasFPU; }
	void                    setHasAMMX(bool hasAMMX) { this->hasAMMX = hasAMMX; }
    void                    setBpp(pp_uint32 bpp) { this->bpp = bpp; }
    void                    setNoSplash(bool noSplash) { this->noSplash = noSplash; }
    void                    setLoadFile(char * loadFile) { this->loadFile = loadFile; }
    void                    setWindowTitle(const char * title);
    void                    setScreenAlert(const char * title);

    bool                    isShiftPressed() const { return keyQualifierShiftPressed; };
    bool                    isCtrlPressed() const { return keyQualifierCtrlPressed; };
    bool                    isAltPressed() const { return keyQualifierAltPressed; };

    AmigaApplication();
    ~AmigaApplication();
};

#endif /* TRACKER_AMIGA_APPLICATION_H */
