#include "DisplayDevice_Amiga.h"
#include "../../tracker/amiga/AmigaApplication.h"
#include "Graphics.h"

DisplayDevice_Amiga::DisplayDevice_Amiga(AmigaApplication * app)
: app(app)
{
}

DisplayDevice_Amiga::~DisplayDevice_Amiga()
{

}

PPGraphicsAbstract*
DisplayDevice_Amiga::open()
{
    return NULL;
}

void
DisplayDevice_Amiga::close()
{

}

void
DisplayDevice_Amiga::update()
{

}

void
DisplayDevice_Amiga::update(const PPRect &r)
{

}

void
DisplayDevice_Amiga::setSize(const PPSize& size)
{

}

void
DisplayDevice_Amiga::setTitle(const PPSystemString& title)
{
    app->setWindowTitle(title.getStrBuffer());
}

bool
DisplayDevice_Amiga::goFullScreen(bool b)
{
    return false;
}

PPSize
DisplayDevice_Amiga::getDisplayResolution() const
{
    return PPSize();
}

void
DisplayDevice_Amiga::shutDown()
{

}

void
DisplayDevice_Amiga::signalWaitState(bool b, const PPColor& color)
{

}

void
DisplayDevice_Amiga::setMouseCursor(MouseCursorTypes type)
{

}
