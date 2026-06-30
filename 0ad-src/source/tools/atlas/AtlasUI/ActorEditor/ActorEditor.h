/* Copyright (C) 2025 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "tools/atlas/AtlasObject/AtlasObject.h"
#include "tools/atlas/AtlasUI/CustomControls/Windows/AtlasWindow.h"

#include <wx/defs.h>
#include <wx/event.h>

class ActorEditorListCtrl;
class wxCheckBox;
class wxComboBox;
class wxWindow;

class ActorEditor : public AtlasWindow
{
	enum
	{
		ID_CreateEntity = ID_Custom
	};

public:
	ActorEditor(wxWindow* parent);

private:
	void OnCreateEntity(wxCommandEvent& event);

protected:
	AtObj FreezeData();
	void ThawData(AtObj& in);

	AtObj ExportData();
	void ImportData(AtObj& in);

	// TODO: er, what's the difference between freeze/thaw and export/import?
	// Why is the code duplicated between them?

	virtual wxString GetDefaultOpenDirectory();

private:
	ActorEditorListCtrl* m_ActorEditorListCtrl;

	wxCheckBox* m_CastShadows;
	wxCheckBox* m_Float;
	wxComboBox* m_Material;

	// Some data is not modifiable in the editor
	// but should be persisted so for convenience keep a copy of the last loaded file.
	AtObj m_Actor;

	DECLARE_EVENT_TABLE();
};
