/*
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
#ifndef __DISPLAYDEVICE_AMIGA_H__
#define __DISPLAYDEVICE_AMIGA_H__

#include "BasicTypes.h"
#include "DisplayDeviceBase.h"

class AmigaApplication;

class DisplayDevice_Amiga : public PPDisplayDeviceBase
{
private:
	AmigaApplication * app;

public:
	DisplayDevice_Amiga(AmigaApplication * app);
	virtual ~DisplayDevice_Amiga();

	// --- PPDisplayDeviceBase ------------------------------------------------
public:
	virtual	PPGraphicsAbstract*	open();
	virtual	void 	close();

	virtual	void 	update();
	virtual	void	update(const PPRect&r);

	virtual	void	setSize(const PPSize& size);

	virtual	bool 	supportsScaling() const { return false; }

	// --- ex. PPWindow -------------------------------------------------------
public:
	virtual	void	setTitle(const PPSystemString& title);

	virtual	bool	goFullScreen(bool b);

	virtual	PPSize	getDisplayResolution() const;

	virtual	void	shutDown();
	virtual	void	signalWaitState(bool b, const PPColor& color);

	virtual	void	setMouseCursor(MouseCursorTypes type);
};

#endif // __DISPLAYDEVICE_AMIGA_H__
