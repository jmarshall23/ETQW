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
#include "afxwin.h"


// CConsoleDlg dialog

class CConsoleDlg : public CDialog
{
	DECLARE_DYNCREATE(CConsoleDlg)

public:
	CConsoleDlg(CWnd* pParent = NULL);   // standard constructor
	virtual ~CConsoleDlg();

// Dialog Data
	enum { IDD = IDD_DIALOG_CONSOLE };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
public:
	CEdit editConsole;
	CEdit editInput;
	void AddText(const char *msg);
	void SetConsoleText ( const idStr& text );
	void ExecuteCommand ( const idStr& cmd = "" );
	idStr NavigateHistory( int direction, const char *inProgress );
	const char *GetConsoleText() const { return consoleStr.c_str(); }
	
	idStr consoleStr;
    idStrList consoleHistory;
    idStr currentCommand;
    int currentHistoryPosition;
	bool saveCurrentCommand;

	afx_msg void OnSize(UINT nType, int cx, int cy);
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
};
