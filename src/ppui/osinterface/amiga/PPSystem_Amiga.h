/*
 *  ppui/osinterface/amiga/PPSystem_Amiga.h
 *
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

#ifndef SYSTEM_AMIGA_H
#define SYSTEM_AMIGA_H

#include "../../../milkyplay/MilkyPlayCommon.h"

class System
{
private:
	static SYSCHAR buffer[];

public:
	static const SYSCHAR* getTempFileName();

	static const SYSCHAR* getConfigFileName();

	static void msleep(int msecs);
};

#endif

