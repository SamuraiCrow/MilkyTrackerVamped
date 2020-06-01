#ifndef TRACKER_AMIGA_APPLICATION_H
#define TRACKER_AMIGA_APPLICATION_H

#include "BasicTypes.h"

class Tracker;
class DisplayDevice_Amiga;
class PPScreen;
class PPEvent;

class AmigaApplication
{
private:
    pp_uint32 bpp;
    bool noSplash;
    char * loadFile;
    Tracker * tracker;
    DisplayDevice_Amiga * displayDevice;
    PPScreen * screen;
    PPSize windowSize;
    bool fullScreen;
    PPSystemString oldCwd;

    struct Window * window;
    struct Screen * pubScreen;

    int load(char * loadFile);
    void raiseEventSynchronized(PPEvent * event);
public:
    int start();
    void loop();
    int stop();

    const PPSize& getWindowSize() const { return windowSize; }
    pp_uint32 getBpp() const { return bpp; }

    void setBpp(pp_uint32 bpp) { this->bpp = bpp; }
    void setNoSplash(bool noSplash) { this->noSplash = noSplash; }
    void setLoadFile(char * loadFile) { this->loadFile = loadFile; }
    void setWindowTitle(const char * title);

    AmigaApplication();
    ~AmigaApplication();
};

#endif /* TRACKER_AMIGA_APPLICATION_H */
