/*
 *  ppui/osinterface/amiga/PPMessageBox_Amiga.cpp
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

#include "PPMessageBox.h"
#include "DialogFileSelector.h"

PPMessageBox::ReturnCodes PPMessageBox::runModal()
{
	// Convert texts
	char* captionASCIIZ = this->caption.toASCIIZ();
	char* contentASCIIZ = this->content.toASCIIZ();
	PPString caption(captionASCIIZ);
	PPString content(contentASCIIZ);
	delete[] captionASCIIZ;
	delete[] contentASCIIZ;

	// Create a message box (the message box will invoke the responder)
	PPDialogBase* dialog = new PPDialogBase(screen, NULL, PP_DEFAULT_ID, caption, content);

	ReturnCodes result = PPModalDialog::ReturnCodeCANCEL; // SDL_runModalLoop(screen, dialog);

	delete dialog;

	return result;
}
