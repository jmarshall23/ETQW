/*
===========================================================================

DarklightNG Source Code
Copyright (C) 2026 - Justin Marshall(aka IceColdDuke).

This file is part of the DarklightNG GPL source code.
This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

DarklightNG is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DarklightNG is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

===========================================================================
*/
#pragma once
#include "afxcmn.h"

#include "entitydlg.h"
#include "ConsoleDlg.h"
#include "TabsDlg.h"


// CInspectorDialog dialog

class CInspectorDialog : public CTabsDlg
{
	//DECLARE_DYNAMIC(CInspectorDialog)w

public:
	CInspectorDialog(CWnd* pParent = NULL);   // standard constructor
	virtual ~CInspectorDialog();

// Dialog Data
	enum { IDD = IDD_DIALOG_INSPECTORS };

protected:
	bool initialized;
	unsigned int dockedTabs;
	int activeMode;

	DECLARE_MESSAGE_MAP()
public:
	virtual BOOL OnInitDialog();
	void AssignModel ();
	CTabCtrl tabInspector;
	//idGLConsoleWidget consoleWnd;
	CConsoleDlg consoleWnd;
	CNewTexWnd texWnd;
	CDialogTextures mediaDlg;
	CEntityDlg entityDlg;
	void SetMode(int mode, bool updateTabs = true);
	int GetMode() const { return activeMode; }
	void UpdateEntitySel(eclass_t *ent);
	void UpdateSelectedEntity();
	void FillClassList();
	bool GetSelectAllCriteria(idStr &key, idStr &val);

	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	afx_msg void OnClose();
	virtual BOOL PreTranslateMessage(MSG* pMsg);

	void SetDockedTabs ( bool docked , int ID );	
};

extern CInspectorDialog *g_Inspectors;
