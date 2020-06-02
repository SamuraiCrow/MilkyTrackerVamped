#include "AmigaApplication.h"
#include "PPUI.h"
#include "DisplayDevice_Amiga.h"
#include "Screen.h"
#include "Tracker.h"
#include "PPMutex.h"
#include "PPSystem_POSIX.h"
#include "PPPath_POSIX.h"
#include "PlayerMaster.h"
#include "../../milkyplay/drivers/amiga/AudioDriver_Amiga.h"

PPMutex* globalMutex = NULL;

AmigaApplication::AmigaApplication()
: cpuType(0)
, hasFPU(false)
, hasAMMX(false)
, bpp(16)
, noSplash(false)
, running(false)
, loadFile(NULL)
, tracker(NULL)
, displayDevice(NULL)
, fullScreen(false)
, vbSignal(-1)
, vbCount(0)
, mouseLeftSeconds(0)
, mouseLeftMicros(0)
, mouseRightSeconds(0)
, mouseRightMicros(0)
{
    strcpy(currentTitle, "");
    globalMutex = new PPMutex();
    irqVerticalBlank = (struct Interrupt *) AllocMem(sizeof(struct Interrupt), MEMF_PUBLIC | MEMF_CLEAR);
}

AmigaApplication::~AmigaApplication()
{
    FreeMem(irqVerticalBlank, sizeof(struct Interrupt));
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
    strcpy(currentTitle, title);
    SetWindowTitles(window, currentTitle, 0);
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
                WA_DepthGadget  , TRUE,
                WA_CloseGadget  , TRUE,
                WA_Activate     , TRUE,
                WA_IDCMP        , IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY,
                TAG_DONE);

            if(window) {
                displayDevice = new DisplayDevice_Amiga(this);
                if(displayDevice->init()) {
                    displayDevice->allowForUpdates(false);

                    screen = new PPScreen(displayDevice, tracker);
                    tracker->setScreen(screen);

                    tracker->startUp(noSplash);
                } else {
                    fprintf(stderr, "Could not init display device!\n");
                    ret = 3;
                }
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

        // Setup IPC VBISR<->loop
        task = FindTask(NULL);
        vbSignal = AllocSignal(-1);
        if(vbSignal != -1) {
            vbMask = 1L << vbSignal;

            // Create interrupt for buffering
            irqVerticalBlank->is_Node.ln_Type = NT_INTERRUPT;
            irqVerticalBlank->is_Node.ln_Pri = 127;
            irqVerticalBlank->is_Node.ln_Name = (char *) "mt-vb-irq";
            irqVerticalBlank->is_Data = this;
            irqVerticalBlank->is_Code = (void(*)()) verticalBlankService;
            AddIntServer(INTB_VERTB, irqVerticalBlank);
        } else {
            fprintf(stderr, "Could not alloc signal for VB<->loop IPC!\n");
            ret = 4;
        }
    }

    return ret;
}

pp_int32
AmigaApplication::verticalBlankService(register AmigaApplication * that __asm("a1"))
{
    return that->verticalBlank();
}

pp_int32
AmigaApplication::verticalBlank()
{
    vbCount++;
    Signal(task, vbMask);

    return 0;
}

void AmigaApplication::transformMouseCoordinates(PPPoint& p)
{
    p.x -= window->BorderLeft;
    p.y -= window->BorderTop;
}

void AmigaApplication::loop()
{
    struct IntuiMessage * msg;
    struct MsgPort * port = window->UserPort;
    ULONG portMask = 1L << port->mp_SigBit;

    running = true;

    // Initial screen update
    displayDevice->allowForUpdates(true);
    displayDevice->update();

    while(running) {
		ULONG signal = Wait(vbMask | portMask);

        if(signal & portMask) {
            while((msg = (struct IntuiMessage *) GetMsg(port))) {
                switch(msg->Class) {
                case IDCMP_CLOSEWINDOW:
                    running = false;
                    break;
                case IDCMP_MOUSEMOVE:
                    {
                        PPPoint point(msg->MouseX, msg->MouseY);
                        transformMouseCoordinates(point);
                        PPEvent mouseMoveEvent(eMouseMoved, &point, sizeof(PPPoint));
                        raiseEventSynchronized(&mouseMoveEvent);
                    }
                    break;
                case IDCMP_MOUSEBUTTONS:
                    switch (msg->Code)
                    {
                    case IECODE_LBUTTON:
                        {
                            PPPoint point(msg->MouseX, msg->MouseY);
                            transformMouseCoordinates(point);
                            if(DoubleClick(mouseLeftSeconds, mouseLeftMicros, msg->Seconds, msg->Micros)) {
                                PPEvent mouseDownEvent(eLMouseDoubleClick, &point, sizeof (PPPoint));
                                raiseEventSynchronized(&mouseDownEvent);
                            } else {
                                PPEvent mouseDownEvent(eLMouseDown, &point, sizeof (PPPoint));
                                raiseEventSynchronized(&mouseDownEvent);

                                mouseLeftSeconds  = msg->Seconds;
                                mouseLeftMicros   = msg->Micros;
                                mouseRightSeconds = 0;
                                mouseRightMicros  = 0;
                            }
                        }
                        break;
                    case IECODE_LBUTTON | IECODE_UP_PREFIX:
                        {
                            PPPoint point(msg->MouseX, msg->MouseY);
                            transformMouseCoordinates(point);
                            PPEvent mouseUpEvent(eLMouseUp, &point, sizeof (PPPoint));
                            raiseEventSynchronized(&mouseUpEvent);
                        }
                        break;
                    case IECODE_RBUTTON:
                        {
                            PPPoint point(msg->MouseX, msg->MouseY);
                            transformMouseCoordinates(point);
                            if(DoubleClick(mouseRightSeconds, mouseRightMicros, msg->Seconds, msg->Micros)) {
                                PPEvent mouseDownEvent(eRMouseDoubleClick, &point, sizeof (PPPoint));
                                raiseEventSynchronized(&mouseDownEvent);
                            } else {
                                PPEvent mouseDownEvent(eRMouseDown, &point, sizeof (PPPoint));
                                raiseEventSynchronized(&mouseDownEvent);

                                mouseLeftSeconds  = 0;
                                mouseLeftMicros   = 0;
                                mouseRightSeconds = msg->Seconds;
                                mouseRightMicros  = msg->Micros;
                            }
                        }
                        break;
                    case IECODE_RBUTTON | IECODE_UP_PREFIX:
                        {
                            PPPoint point(msg->MouseX, msg->MouseY);
                            transformMouseCoordinates(point);
                            PPEvent mouseUpEvent(eRMouseUp, &point, sizeof (PPPoint));
                            raiseEventSynchronized(&mouseUpEvent);
                        }
                        break;
                    }
                    break;
                }

                ReplyMsg((struct Message *) msg);
            }

            displayDevice->setSize(windowSize);
        }

        if(signal & vbMask) {
            if(!(vbCount & 1)) {
                PPEvent timerEvent(eTimer);
			    raiseEventSynchronized(&timerEvent);
            }

            AudioDriverInterface_Amiga * driverInterface = (AudioDriverInterface_Amiga *) tracker->playerMaster->getCurrentDriver();
            if(driverInterface) {
                driverInterface->bufferAudio();
            }

            displayDevice->flush();
        }
    }
}

int AmigaApplication::stop()
{
	PPEvent event(eAppQuit);
	raiseEventSynchronized(&event);

    RemIntServer(INTB_VERTB, irqVerticalBlank);
    if(vbSignal >= 0) {
        FreeSignal(vbSignal);
    }

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

