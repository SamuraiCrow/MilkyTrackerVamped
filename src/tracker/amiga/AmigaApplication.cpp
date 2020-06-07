#include "AmigaApplication.h"
#include "Amiga_KeyTranslation.h"
#include "PPUI.h"
#include "DisplayDevice_Amiga.h"
#include "Screen.h"
#include "Tracker.h"
#include "PPMutex.h"
#include "PPSystem_POSIX.h"
#include "PPPath_POSIX.h"
#include "PlayerMaster.h"
#include "../../milkyplay/drivers/amiga/AudioDriver_Amiga.h"

#include <proto/picasso96.h>
#include <clib/picasso96_protos.h>

PPMutex* globalMutex = NULL;

static pp_uint32
getVerticalBeamPosition() {
    struct vpos {
        pp_uint32 :13;
        pp_uint32 vpos:11;
        pp_uint32 hpos:8;
    };
    return ((struct vpos*) 0xdff004)->vpos;
}

AmigaApplication::AmigaApplication()
: cpuType(0)
, hasFPU(false)
, hasAMMX(false)
, bpp(16)
, noSplash(false)
, running(false)
, loadFile(NULL)
, tracker(NULL)
, trackerScreen(NULL)
, displayDevice(NULL)
, fullScreen(false)
, vbSignal(-1)
, vbCount(0)
, mouseLeftSeconds(0)
, mouseLeftMicros(0)
, mouseLeftDown(false)
, mouseLeftVBStart(0)
, mouseRightSeconds(0)
, mouseRightMicros(0)
, mouseRightDown(false)
, mouseRightVBStart(0)
, mousePosition(PPPoint(-1, -1))
, keyQualifierShiftPressed(false)
, keyQualifierCtrlPressed(false)
, keyQualifierAltPressed(false)
, trackerStartUpFinished(false)
, showAlert(false)
{
    strcpy(currentTitle, "");
    strcpy(currentAlert, "");
    globalMutex = new PPMutex();
}

AmigaApplication::~AmigaApplication()
{
    delete globalMutex;
}

struct Screen * AmigaApplication::getScreen() const
{
    if(fullScreen)
        return screen;
    return pubScreen;
}

void AmigaApplication::raiseEventSynchronized(PPEvent * event)
{
    if(!tracker || !trackerScreen)
        return;

    globalMutex->lock();
    {
        trackerScreen->raiseEvent(event);
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
	PPEvent eventDrop(eFileDragDropped, &strPtr, sizeof(PPSystemString*));
	raiseEventSynchronized(&eventDrop);

    // And confirm
    pp_uint16 chr[3] = {VK_RETURN, 0, 0};
    PPEvent eventConfirm(eKeyDown, &chr, sizeof(chr));
    raiseEventSynchronized(&eventConfirm);

    // Restore path
    path.change(newCwd);
}

void AmigaApplication::setWindowTitle(const char * title)
{
    if(!fullScreen) {
        strcpy(currentTitle, title);
        SetWindowTitles(window, currentTitle, (CONST_STRPTR) (UBYTE *) ~0);
    }
}

void AmigaApplication::resetScreenAlert()
{
    if(showAlert) {
        if(fullScreen) {
            ShowTitle(screen, FALSE);
        } else {
            SetWindowTitles(window, currentTitle, (CONST_STRPTR) (UBYTE *) ~0);
        }
        showAlert = false;
    }
}

void AmigaApplication::setScreenAlert(const char * title)
{
    strcpy(currentAlert, title);
    if(fullScreen) {
        ShowTitle(screen, TRUE);
        SetWindowTitles(window, (CONST_STRPTR) (UBYTE *) ~0, currentAlert);
        DisplayBeep(screen);
    } else {
        SetWindowTitles(window, currentAlert, (CONST_STRPTR) (UBYTE *) ~0);
        DisplayBeep(NULL);
    }
    showAlert = true;
}

int AmigaApplication::start()
{
    int ret = 0;

    // Store old path
    PPPath_POSIX path;
    oldCwd = path.getCurrent();

    // Startup tracker
    globalMutex->lock();
    {
        tracker = new Tracker();

        if (pubScreen = LockPubScreen(NULL)) {
            if (!fullScreen)
                fullScreen = tracker->getFullScreenFlagFromDatabase();

            if(fullScreen) {
                windowSize.width = pubScreen->Width;
                windowSize.height = pubScreen->Height;
            } else {
                windowSize = tracker->getWindowSizeFromDatabase();
            }

            if(fullScreen) {
                ULONG displayId = p96BestModeIDTags(
                    P96BIDTAG_NominalWidth,     windowSize.width,
                    P96BIDTAG_NominalHeight,    windowSize.height,
                    P96BIDTAG_Depth,            bpp,
                    TAG_DONE);
                if(displayId == INVALID_ID) {
                    fprintf(stderr, "Cannot find best screen mode (%ldx%ldx%ld)!\n", windowSize.width, windowSize.height, bpp);
                    ret = 5;
                }

                if(!ret) {
                    screen = p96OpenScreenTags(
                        P96SA_DisplayID             , displayId,
                        P96SA_Left                  , 0,
                        P96SA_Top                   , 0,
                        P96SA_Width                 , windowSize.width,
                        P96SA_Height                , windowSize.height,
                        P96SA_Depth                 , bpp,
                        P96SA_DetailPen             , 0,
                        P96SA_BlockPen              , 1,
                        P96SA_Quiet                 , FALSE,
                        P96SA_Type                  , CUSTOMSCREEN,
                        P96SA_RGBFormat             , RGBFB_R5G6B5,
                        P96SA_BitMap                , FALSE,
                        P96SA_ConstantBytesPerRow   , TRUE,
                        P96SA_AutoScroll            , FALSE,
                        P96SA_Exclusive             , TRUE,
                        P96SA_ShowTitle             , FALSE,
                        TAG_DONE);
                    if(screen) {
                        window = OpenWindowTags(NULL,
                            WA_CustomScreen  , (APTR) screen,
                            WA_Left          , 0,
                            WA_Top           , 0,
                            WA_InnerWidth    , windowSize.width,
                            WA_InnerHeight   , windowSize.height,
                            WA_Title         , (APTR) (!fullScreen ? "Loading MilkyTracker" : NULL),
                            WA_Borderless    , TRUE,
                            WA_Backdrop      , TRUE,
                            WA_Activate      , TRUE,
                            WA_ReportMouse   , TRUE,
                            WA_NoCareRefresh , TRUE,
                            WA_RMBTrap       , TRUE,
                            WA_IDCMP         , IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY,
                            TAG_DONE);
                        if(!window) {
                            fprintf(stderr, "Could not create window!\n");
                            ret = 4;
                        }
                    } else {
                        fprintf(stderr, "Could not create P96 screen!\n");
                        ret = 6;
                    }
                }
            } else {
                window = OpenWindowTags(NULL,
                    WA_CustomScreen  , (APTR) pubScreen,
                    WA_Left          , (pubScreen->Width - windowSize.width) / 2,
                    WA_Top           , (pubScreen->Height - windowSize.height) / 2,
                    WA_InnerWidth    , windowSize.width,
                    WA_InnerHeight   , windowSize.height,
                    WA_Title         , (APTR) "Loading MilkyTracker ...",
                    WA_DragBar       , TRUE,
                    WA_DepthGadget   , TRUE,
                    WA_CloseGadget   , TRUE,
                    WA_Activate      , TRUE,
                    WA_ReportMouse   , TRUE,
                    WA_NoCareRefresh , TRUE,
                    WA_RMBTrap       , TRUE,
                    WA_IDCMP         , IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY,
                    TAG_DONE);
                if(!window) {
                    fprintf(stderr, "Could not create window!\n");
                    ret = 4;
                }
            }
        } else {
            fprintf(stderr, "Could not get public screen!\n");
            ret = 1;
        }

        if(!ret) {
            displayDevice = new DisplayDevice_Amiga(this);
            if(displayDevice->init()) {
                displayDevice->allowForUpdates(false);

                trackerScreen = new PPScreen(displayDevice, tracker);
                tracker->setScreen(trackerScreen);

                tracker->startUp(noSplash);

                trackerStartUpFinished = true;
            } else {
                fprintf(stderr, "Could not init display device!\n");
                ret = 3;
            }
        } else {
            fprintf(stderr, "Could not init Intuition objects!\n");
            ret = 2;
        }
    }
    globalMutex->unlock();

    if(!ret) {
        // And load initially if been passed
        if(loadFile != NULL) {
            ret = load(loadFile);
        }

        if(!ret) {
            // Setup IPC VBISR<->loop
            task = FindTask(NULL);
            vbSignal = AllocSignal(-1);
            if(vbSignal != -1) {
                vbMask = 1L << vbSignal;

                // Create interrupt for buffering
                irqVerticalBlank = (struct Interrupt *) AllocMem(sizeof(struct Interrupt), MEMF_PUBLIC | MEMF_CLEAR);
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

void
AmigaApplication::setMousePosition(pp_int32 x, pp_int32 y)
{
    mousePosition.x = x - window->BorderLeft;
    mousePosition.y = y - window->BorderTop;
}

void
AmigaApplication::loop()
{
    struct MsgPort * port = window->UserPort;
    ULONG portMask = 1L << port->mp_SigBit;
    struct InputEvent ie = {0};

    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_SubClass = 0;

    running = true;

    // Focus window
    ActivateWindow(window);

    // Initial screen update
    displayDevice->allowForUpdates(true);
    displayDevice->update();

    while(running) {
		ULONG signal = Wait(vbMask | portMask);

        // Prebuffering audio has first priority
        if(signal & vbMask) {
            AudioDriverInterface_Amiga * driverInterface = (AudioDriverInterface_Amiga *) tracker->playerMaster->getCurrentDriver();
            if(driverInterface) {
                driverInterface->bufferAudio();
            }
        }

        // After that handle Intuition messages
        if(signal & portMask) {
            struct IntuiMessage * msg;

            while((msg = (struct IntuiMessage *) GetMsg(port))) {
                switch(msg->Class) {
                case IDCMP_CLOSEWINDOW:
                    running = false;
                    break;
                case IDCMP_RAWKEY:
                    {
                        UBYTE buffer[8];
                        APTR eventptr;
                        int actual;
                        AmigaKeyInputData key;
                        bool keyUp = false;

                        keyQualifierShiftPressed = (msg->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? true : false;
                        keyQualifierCtrlPressed = (msg->Qualifier & IEQUALIFIER_CONTROL) ? true : false;
                        keyQualifierAltPressed = (msg->Qualifier & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) ? true : false;

                        switch(msg->Code) {
                        case 0x7a:
                        case 0x7b:
                            {
                                TMouseWheelEventParams mouseWheelParams;

                                mouseWheelParams.pos.x = mousePosition.x;
                                mouseWheelParams.pos.y = mousePosition.y;
                                mouseWheelParams.deltaX = msg->Code == 0x7a ? 1 : -1;
                                mouseWheelParams.deltaY = msg->Code == 0x7a ? 1 : -1;

                                PPEvent mouseWheelEvent(eMouseWheelMoved, &mouseWheelParams, sizeof (mouseWheelParams));
		                        raiseEventSynchronized(&mouseWheelEvent);
                            }
                        default:
                            key.code = msg->Code;
                            key.qual = msg->Qualifier;
                            key.sym = -1;

                            ie.ie_Code = msg->Code;
                            ie.ie_Qualifier = msg->Qualifier & ~(IEQUALIFIER_CONTROL | IEQUALIFIER_LALT | IEQUALIFIER_RALT);
                            ie.ie_EventAddress = (APTR *) *((ULONG *)msg->IAddress);

                            actual = MapRawKey(&ie, (STRPTR) buffer, 8, NULL);
                            if(actual == 1) {
                                key.sym = *buffer;
                            }

                            //printf("Raw key data: code=$%04x qualifier=$%04lx sym=$%04x / %c\n", msg->Code, msg->Qualifier, key.sym, key.sym);

                            keyUp = key.code >= 0x80;
                            if(keyUp)
                                key.code &= 0x80;

                            pp_uint16 chr[3] = {toVK(key), toSC(key), key.sym == -1 ? 0 : key.sym};

                            //printf("Translated key data: vk=$%04x, sc=$%04x, chr=$%04x\n", chr[0], chr[1], chr[2]);

                            if(keyUp) {
                                PPEvent keyUpEvent(eKeyUp, &chr, sizeof(chr));
                                raiseEventSynchronized(&keyUpEvent);
                            } else {
                                PPEvent keyDownEvent(eKeyDown, &chr, sizeof(chr));
                                raiseEventSynchronized(&keyDownEvent);

                                if (key.sym >= 0x20 && key.sym <= 0x7f) {
                                    pp_uint8 character = (pp_uint8) key.sym;
                                    PPEvent keyCharEvent(eKeyChar, &character, sizeof(pp_uint8));
                                    raiseEventSynchronized(&keyCharEvent);
                                }
                            }
                        }
                    }
                    break;
                case IDCMP_MOUSEMOVE:
                    {
                        setMousePosition(msg->MouseX, msg->MouseY);

                        if(mouseLeftDown) {
                            PPEvent mouseDragEvent(eLMouseDrag, &mousePosition, sizeof(PPPoint));
                            raiseEventSynchronized(&mouseDragEvent);
                        } else if(mouseRightDown) {
                            PPEvent mouseDragEvent(eRMouseDrag, &mousePosition, sizeof(PPPoint));
                            raiseEventSynchronized(&mouseDragEvent);
                        } else {
                            PPEvent mouseMoveEvent(eMouseMoved, &mousePosition, sizeof(PPPoint));
                            raiseEventSynchronized(&mouseMoveEvent);
                        }
                    }
                    break;
                case IDCMP_MOUSEBUTTONS:
                    setMousePosition(msg->MouseX, msg->MouseY);

                    switch (msg->Code)
                    {
                    case IECODE_LBUTTON:
                        {
                            resetScreenAlert();

                            if(DoubleClick(mouseLeftSeconds, mouseLeftMicros, msg->Seconds, msg->Micros)) {
                                PPEvent mouseDownEvent(eLMouseDoubleClick, &mousePosition, sizeof(PPPoint));
                                raiseEventSynchronized(&mouseDownEvent);
                            }

                            PPEvent mouseDownEvent(eLMouseDown, &mousePosition, sizeof(PPPoint));
                            raiseEventSynchronized(&mouseDownEvent);

                            mouseLeftSeconds  = msg->Seconds;
                            mouseLeftMicros   = msg->Micros;
                            mouseLeftDown     = true;
                            mouseLeftVBStart  = vbCount;
                            mouseRightSeconds = 0;
                            mouseRightMicros  = 0;
                            mouseRightDown    = false;
                        }
                        break;
                    case IECODE_LBUTTON | IECODE_UP_PREFIX:
                        {
                            PPEvent mouseUpEvent(eLMouseUp, &mousePosition, sizeof(PPPoint));
                            raiseEventSynchronized(&mouseUpEvent);
                            mouseLeftDown = false;
                        }
                        break;
                    case IECODE_RBUTTON:
                        {
                            if(DoubleClick(mouseRightSeconds, mouseRightMicros, msg->Seconds, msg->Micros)) {
                                PPEvent mouseDownEvent(eRMouseDoubleClick, &mousePosition, sizeof(PPPoint));
                                raiseEventSynchronized(&mouseDownEvent);
                            }

                            PPEvent mouseDownEvent(eRMouseDown, &mousePosition, sizeof(PPPoint));
                            raiseEventSynchronized(&mouseDownEvent);

                            mouseLeftSeconds  = 0;
                            mouseLeftMicros   = 0;
                            mouseLeftDown     = false;
                            mouseRightSeconds = msg->Seconds;
                            mouseRightMicros  = msg->Micros;
                            mouseRightDown    = true;
                            mouseRightVBStart = vbCount;
                        }
                        break;
                    case IECODE_RBUTTON | IECODE_UP_PREFIX:
                        {
                            PPEvent mouseUpEvent(eRMouseUp, &mousePosition, sizeof(PPPoint));
                            raiseEventSynchronized(&mouseUpEvent);
                            mouseRightDown = false;
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

            if(mouseLeftDown && (vbCount - mouseLeftVBStart) > 25) {
                PPEvent mouseRepeatEvent(eLMouseRepeat, &mousePosition, sizeof(PPPoint));
                raiseEventSynchronized(&mouseRepeatEvent);
            } else if(mouseRightDown && (vbCount - mouseRightVBStart) > 25) {
                PPEvent mouseRepeatEvent(eRMouseRepeat, &mousePosition, sizeof(PPPoint));
                raiseEventSynchronized(&mouseRepeatEvent);
            }

            // And draw the screen at last (@todo check if we have enough VBTime left for that)
            displayDevice->flush();
        }
    }
}

int AmigaApplication::stop()
{
	PPEvent event(eAppQuit);
	raiseEventSynchronized(&event);

    // Stop tracker if possible
    if(trackerStartUpFinished) {
        if(!tracker->shutDown())
            return 1;
    }

    // Destroy display resources
    if(irqVerticalBlank != NULL) {
        RemIntServer(INTB_VERTB, irqVerticalBlank);
        FreeMem(irqVerticalBlank, sizeof(struct Interrupt));
    }
    if(vbSignal >= 0) {
        FreeSignal(vbSignal);
    }

    // Destroy tracker resources
    delete screen;
    delete displayDevice;
    delete tracker;

    // Clean Intuition UI
    if(window)
        CloseWindow(window);
    if(screen)
        CloseScreen(screen);
    if(pubScreen)
        UnlockPubScreen(0, screen);

    return 0;
}

