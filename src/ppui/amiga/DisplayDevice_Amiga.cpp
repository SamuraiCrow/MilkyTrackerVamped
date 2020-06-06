#include "DisplayDevice_Amiga.h"
#include "../../tracker/amiga/AmigaApplication.h"
#include "Graphics.h"
#include "PPMutex.h"

#include <intuition/intuitionbase.h>
#include <proto/picasso96.h>
#include <clib/picasso96_protos.h>

extern struct IntuitionBase * IntuitionBase;

DisplayDevice_Amiga::DisplayDevice_Amiga(AmigaApplication * app)
: app(app)
, screenMode(INVALID)
{
    width = app->getWindowSize().width;
    height = app->getWindowSize().height;
    size.width = width;
    size.height = height;
    screen = app->getScreen();
    window = app->getWindow();
    rastPort = window->RPort;
    useRTGFullscreen = app->isFullScreen() && app->getBpp() == 16;
    useRTGWindowed = !app->isFullScreen() && app->getBpp() == 16 && GetBitMapAttr(rastPort->BitMap, BMA_DEPTH) == 16;
    useSAGADirectFB = app->isAMMX() && useRTGFullscreen;
    useSAGAPiP = app->isAMMX() && useRTGWindowed;
    pitch = width * app->getBpp() >> 3;
    drawMutex = new PPMutex();
}

DisplayDevice_Amiga::~DisplayDevice_Amiga()
{
    if(useRTGWindowed) {
        if(useSAGAPiP) {
            WRITE16(SAGA_PIP_PIXFMT,   0);
            WRITE16(SAGA_PIP_X0,       0);
            WRITE16(SAGA_PIP_Y0,       0);
            WRITE16(SAGA_PIP_X1,       0);
            WRITE16(SAGA_PIP_Y1,       0);
            WRITE16(SAGA_PIP_COLORKEY, 0);
            WRITE32(SAGA_PIP_BPLPTR,   0);

            V_FreeExpansionPort(V_PIP);
        }

        for(int i = 0; i < 2; i++) {
            FreeMem(unalignedScreenBuffer[i], (pitch * height) + 16);
        }
        FreeMem(unalignedOffScreenBuffer, (pitch * height) + 16);
    }

    delete drawMutex;
}

void *
DisplayDevice_Amiga::allocMemAligned(pp_uint32 size, void ** aligned)
{
    void * b = (void *) AllocMem((pitch * height) + 16, MEMF_FAST | MEMF_CLEAR);
    if(!b) {
        return NULL;
    }

    // And align
    void * a = (void *) (((pp_uint32) b + 15) & ~ (pp_uint32) 0xf);
    memset(a, 0, pitch * height);
    *aligned = a;

    return b;
}

bool
DisplayDevice_Amiga::init()
{
    screenMode = INVALID;

    // Detect display mode
    if(useRTGWindowed) {
        screenMode = RTG_WINDOWED_16;

        if(useSAGAPiP) {
            void * allocator;

            if (allocator = V_AllocExpansionPort(V_PIP, (UBYTE *) "MilkyTracker")) {
                fprintf(stderr, "SAGA PiP already in use! (by: %s)\n", allocator);
                return false;
            }

            screenMode = SAGA_PIP_16;
        }

        currentGraphics = new PPGraphics_16BIT(width, height, 0, NULL);
	    currentGraphics->lock = true;
    } else if(useRTGFullscreen) {
        screenMode = RTG_FULLSCREEN_16;

        currentGraphics = new PPGraphics_16BIT(width, height, 0, NULL);
	    currentGraphics->lock = true;
    }

    if(screenMode != INVALID) {
        // Allocate offscreen buffer
        void * b = allocMemAligned(pitch * height, &alignedOffScreenBuffer);
        if(!b) {
            fprintf(stderr, "Could not allocate enough memory for off-screen buffer %ld\n");
            return false;
        }
        unalignedOffScreenBuffer = b;

        // Allocate screen buffers
        for(int i = 0; i < 2; i++) {
            b = allocMemAligned(pitch * height, &alignedScreenBuffer[i]);
            if(!b) {
                fprintf(stderr, "Could not allocate enough memory for screen buffer %ld\n", i);
                return false;
            }

            unalignedScreenBuffer[i] = b;
        }

        // Start with DB page 0
        dbPage = 0;

        // And activate display mode
        if(screenMode == SAGA_PIP_16) {
            WRITE16(SAGA_PIP_COLORKEY, 0);
            WRITE16(SAGA_PIP_PIXFMT, SAGAF_RGB16);
            WRITE32(SAGA_PIP_BPLPTR, (ULONG) alignedScreenBuffer[1]);
        }

        return true;
    }

    fprintf(stderr, "Requested display mode not supported!\n");
    return false;
}

PPGraphicsAbstract*
DisplayDevice_Amiga::open()
{
	if (!isEnabled())
		return NULL;

	if (currentGraphics->lock) {
		currentGraphics->lock = false;

        static_cast<PPGraphicsFrameBuffer*>(currentGraphics)->setBufferProperties(pitch, (pp_uint8 *) alignedOffScreenBuffer);

		return currentGraphics;
	}

    return NULL;
}

void
DisplayDevice_Amiga::close()
{
	currentGraphics->lock = true;
}

void
DisplayDevice_Amiga::update()
{
	update(PPRect(0, 0, width, height));
}

void
DisplayDevice_Amiga::update(const PPRect &r)
{
	if(!isUpdateAllowed() || !isEnabled())
		return;

    drawMutex->lock();
    {
        drawCommands.push_back(r);
    }
    drawMutex->unlock();
}

void
DisplayDevice_Amiga::flush()
{
    drawMutex->lock();

    if(drawCommands.size() > 0) {
        pp_uint16 * ps = (pp_uint16 *) alignedOffScreenBuffer;

        if(screenMode == SAGA_PIP_16) {
            // Draw rects from off-screen to on-screen buffers
            for(int i = 0; i < 2; i++) {
                pp_uint16 * pd = (pp_uint16 *) alignedScreenBuffer[i];

                for(std::vector<PPRect>::iterator rect = drawCommands.begin(); rect != drawCommands.end(); ++rect) {
                    register pp_uint32 rw = rect->width();
                    pp_uint32 off = rect->y1 * width + rect->x1;
                    pp_uint16 * s = ps + off;
                    pp_uint16 * d = pd + off;

                    // Copy line by line
                    for(int y = 0; y < rect->height(); y++) {
                        memcpy(d, s, rw * 2); // @todo
                        s += width;
                        d += width;
                    }
                }
            }

		    WRITE32(SAGA_PIP_BPLPTR, (ULONG) alignedScreenBuffer[dbPage]);
            dbPage ^= 1;
        } else {
            struct RenderInfo renderInfo;

            renderInfo.BytesPerRow = pitch;
            renderInfo.Memory = ps;
            renderInfo.pad = 0;
            renderInfo.RGBFormat = RGBFB_R5G6B5;

            p96WritePixelArray(&renderInfo, 0, 0, rastPort, window->BorderLeft, window->BorderTop, width, height);
            WaitBlit();
        }

        drawCommands.clear();
    }

    drawMutex->unlock();
}

void
DisplayDevice_Amiga::setSize(const PPSize& size)
{
    if(screenMode == SAGA_PIP_16) {
        ULONG x0 = 0, y0 = 0;
        ULONG x1 = 0, y1 = 0;

        if (screen == IntuitionBase->FirstScreen) {
            x0 = SAGA_PIP_DELTAX + window->LeftEdge + screen->LeftEdge + window->BorderLeft + 2;
            y0 = SAGA_PIP_DELTAY + window->TopEdge + screen->TopEdge + window->BorderTop;

            if ((x0 + width - 16 - 64) < screen->Width) {
                x1 = x0 + width;
                y1 = y0 + height;
            } else {
                x0 = 0;
                y0 = 0;
            }
        }

        WRITE16(SAGA_PIP_X0, x0);
        WRITE16(SAGA_PIP_Y0, y0);
        WRITE16(SAGA_PIP_X1, x1);
        WRITE16(SAGA_PIP_Y1, y1);
    }
}

void
DisplayDevice_Amiga::setTitle(const PPSystemString& title)
{
    app->setWindowTitle(title.getStrBuffer());
}

void
DisplayDevice_Amiga::setAlert(const PPSystemString& title)
{
    app->setScreenAlert(title.getStrBuffer());
}

bool
DisplayDevice_Amiga::goFullScreen(bool b)
{
    return false;
}

void
DisplayDevice_Amiga::shutDown()
{
    app->setRunning(false);
}

void
DisplayDevice_Amiga::signalWaitState(bool b, const PPColor& color)
{
	setMouseCursor(b ? MouseCursorTypeWait : MouseCursorTypeStandard);
}

void
DisplayDevice_Amiga::setMouseCursor(MouseCursorTypes type)
{
	currentCursorType = type;

	switch (type)
	{
    case MouseCursorTypeStandard:
        break;
    case MouseCursorTypeResizeLeft:
        break;
    case MouseCursorTypeResizeRight:
        break;
    case MouseCursorTypeHand:
        break;
    case MouseCursorTypeWait:
        break;
	}
}

PPSize
DisplayDevice_Amiga::getDisplayResolution() const
{
    return PPSize(width, height);
}
