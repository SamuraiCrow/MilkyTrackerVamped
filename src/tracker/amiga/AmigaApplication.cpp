#include "AmigaApplication.h"
#include "PPUI.h"
#include "DisplayDevice_Amiga.h"
#include "Screen.h"
#include "Tracker.h"
#include "PPMutex.h"
#include "PPSystem_POSIX.h"
#include "PPPath_POSIX.h"

#include <intuition/intuition.h>
#include <clib/exec_protos.h>
#include <clib/intuition_protos.h>

PPMutex* globalMutex = NULL;

AmigaApplication::AmigaApplication()
: bpp(16)
, noSplash(false)
, loadFile(NULL)
{
    globalMutex = new PPMutex();
}

AmigaApplication::~AmigaApplication()
{
    delete globalMutex;
}

void AmigaApplication::raiseEventSynchronized(PPEvent * event)
{
    if(!tracker || !screen)
        return;

    globalMutex->lock();
    {
        screen->raiseEvent(event);
    }
    globalMutex->unlock();
}

int AmigaApplication::load(char * loadFile)
{
	PPPath_POSIX path;
    PPSystemString newCwd = path.getCurrent();

    // Change to old path
    path.change(oldCwd);

    // "Drop" file to load onto MilkyTracker @todo
    PPSystemString finalFile(loadFile);
	PPSystemString* strPtr = &finalFile;
	PPEvent eventDrop(eFileDragDropped, &strPtr, sizeof (PPSystemString*));
	raiseEventSynchronized(&eventDrop);

    // And confirm
    pp_uint16 chr[3] = {VK_RETURN, 0, 0};
    PPEvent eventConfirm(eKeyDown, &chr, sizeof (chr));
    raiseEventSynchronized(&eventConfirm);

    // Restore path
	path.change(newCwd);
}

void AmigaApplication::setWindowTitle(const char * title)
{
    SetWindowTitles(window, title, 0);
}

int AmigaApplication::start()
{
    int ret = 0;

    // Store old path
	PPPath_POSIX path;
    oldCwd = path.getCurrent();

    // Get public screen
    if (!(pubScreen = LockPubScreen(NULL))) {
        fprintf(stderr, "Could not get public screen!\n");
        ret = 1;
    }

    if(!ret) {
        // Startup tracker
        globalMutex->lock();
        {
            tracker = new Tracker();

            windowSize = tracker->getWindowSizeFromDatabase();
            if (!fullScreen)
                fullScreen = tracker->getFullScreenFlagFromDatabase();

            window = OpenWindowTags(NULL,
                WA_CustomScreen , (APTR) pubScreen,
                WA_Left         , (pubScreen->Width - windowSize.width) / 2,
                WA_Top          , (pubScreen->Height - windowSize.height) / 2,
                WA_InnerWidth   , windowSize.width,
                WA_InnerHeight  , windowSize.height,
                WA_Title        , (APTR) "Loading MilkyTracker ...",
                WA_DragBar      , TRUE,
                WA_DepthGadget	, TRUE,
                WA_CloseGadget	, TRUE,
                WA_Activate     , TRUE,
                WA_IDCMP        , IDCMP_CLOSEWINDOW | IDCMP_RAWKEY,
                TAG_DONE);

            if(window) {
                displayDevice = new DisplayDevice_Amiga(this);
                displayDevice->init();

                screen = new PPScreen(displayDevice, tracker);
                tracker->setScreen(screen);

                tracker->startUp(noSplash);
            } else {
                fprintf(stderr, "Could not create window!\n");
                ret = 2;
            }
        }
        globalMutex->unlock();

        if(!ret) {
            // And load initially if been passed
            if(loadFile != NULL) {
                ret = load(loadFile);
            }
        }
    }

    return ret;
}

void AmigaApplication::loop()
{
    bool running = true;
    struct IntuiMessage * msg;
    struct MsgPort * port = window->UserPort;

    while(running) {
        Wait(1L << port->mp_SigBit);

        while((msg = (struct IntuiMessage *) GetMsg(window->UserPort))) {
            switch(msg->Class) {
            case IDCMP_CLOSEWINDOW:
                running = false;
                break;
            }

            ReplyMsg((struct Message *) msg);
        }
    }
}

int AmigaApplication::stop()
{
	PPEvent event(eAppQuit);
	raiseEventSynchronized(&event);

    // Stop tracker
    globalMutex->lock();
    {
        tracker->shutDown();

        delete screen;
        delete displayDevice;
        delete tracker;
    }
    globalMutex->unlock();

    // Clean Intuition UI
    if(window)
        CloseWindow(window);
    if(pubScreen)
        UnlockPubScreen(0, pubScreen);


    return 0;
}

