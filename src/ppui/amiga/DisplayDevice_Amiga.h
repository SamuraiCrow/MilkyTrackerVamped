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

#include <vector>

class AmigaApplication;
class PPMutex;

class DisplayDevice_Amiga : public PPDisplayDeviceBase
{
	enum ScreenMode
	{
		INVALID = 0,
		AGA_C2P_8,
		RTG_WINDOWED_8,
		RTG_WINDOWED_16,
		SAGA_PIP_8,
		SAGA_PIP_16,
		SAGA_DIRECT
	};
private:
	AmigaApplication * 	app;
	bool                useRTGWindowed;
	bool				useSAGAPiP;
	pp_int32            width;
	pp_int32            height;
	pp_int32            pitch;
	pp_uint32           dbPage;
	ScreenMode          screenMode;
	PPMutex *           drawMutex;
	std::vector<PPRect> drawCommands;

	struct RastPort * 	rastPort;
	struct Window *   	window;
	struct Screen * 	pubScreen;

	void *              unalignedOffScreenBuffer;
	void *              alignedOffScreenBuffer;

	void *              unalignedScreenBuffer[2];
	void *              alignedScreenBuffer[2];

	void * 				allocMemAligned(pp_uint32 size, void ** aligned);

public:
	void                flush();

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
	virtual bool 	init();

	virtual	void	setTitle(const PPSystemString& title);

	virtual	bool	goFullScreen(bool b);

	virtual	void	shutDown();
	virtual	void	signalWaitState(bool b, const PPColor& color);

	virtual	void	setMouseCursor(MouseCursorTypes type);
};

#endif // __DISPLAYDEVICE_AMIGA_H__
