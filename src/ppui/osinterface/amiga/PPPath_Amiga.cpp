/*
 *  ppui/osinterface/amiga/PPPath_Amiga.cpp
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

#include "PPPath_Amiga.h"
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>

static BPTR currentDir = NULL;

extern BPTR GetProgramDirLock();

#define PPMAX_DIR_PATH 1024

void PPPathEntry_Amiga::create(const PPSystemString& path, const PPSystemString& name)
{
	this->name = name;
	PPSystemString fullPath = path;

	fullPath.append(name);

    printf("%s(%s, %s)\n", __PRETTY_FUNCTION__, path.getStrBuffer(), name.getStrBuffer());

	/*struct stat file_status;

	if (::stat(fullPath, &file_status) == 0)
	{
		size = file_status.st_size;

		if (S_ISDIR(file_status.st_mode))
			type = Directory;
		//if (S_ISLNK(file_status.st_mode))
		//	printf("foo.txt is a symbolic link\n");
		if (S_ISCHR(file_status.st_mode))
			type = Hidden;
		if (S_ISBLK(file_status.st_mode))
			type = Hidden;
		if (S_ISFIFO(file_status.st_mode))
			type = Hidden;
		if (S_ISSOCK(file_status.st_mode))
			type = Hidden;
		if (S_ISREG(file_status.st_mode))
			type = File;
	}
	else
	{ 	type = Nonexistent;
	}*/
}

bool PPPathEntry_Amiga::isHidden() const
{
	return PPPathEntry::isHidden();
}

PPPath_Amiga::PPPath_Amiga()
{
	current = getCurrent();
}

PPPath_Amiga::PPPath_Amiga(const PPSystemString& path)
: current(path)
{
}

const PPSystemString PPPath_Amiga::getCurrent()
{
	STRPTR cwd = (STRPTR) AllocMem(PPMAX_DIR_PATH+1, MEMF_CLEAR);
  	PPSystemString path("PROGDIR:");

    if(!currentDir) {
        currentDir = GetProgramDirLock();
    }

    if(NameFromLock(currentDir, cwd, PPMAX_DIR_PATH)) {
        path = cwd;
    }

	return path;
}

bool PPPath_Amiga::change(const PPSystemString& path)
{
	return (bool) SetCurrentDirName(path.getStrBuffer());
}

bool PPPath_Amiga::stepInto(const PPSystemString& directory)
{
	return false;
}

const PPPathEntry* PPPath_Amiga::getFirstEntry()
{
	return getNextEntry();
}

const PPPathEntry* PPPath_Amiga::getNextEntry()
{
	return NULL;
}

bool PPPath_Amiga::canGotoHome() const
{
	return true;
}

void PPPath_Amiga::gotoHome()
{
    SetCurrentDirName("PROGDIR:");
}

bool PPPath_Amiga::canGotoRoot() const
{
	return true;
}

void PPPath_Amiga::gotoRoot()
{
}

bool PPPath_Amiga::canGotoParent() const
{
	return true;
}

void PPPath_Amiga::gotoParent()
{
}

char PPPath_Amiga::getPathSeparatorAsASCII() const
{
	return '/';
}

const PPSystemString PPPath_Amiga::getPathSeparator() const
{
	return PPSystemString(getPathSeparatorAsASCII());
}

bool PPPath_Amiga::fileExists(const PPSystemString& fileName) const
{
	return false;
}


