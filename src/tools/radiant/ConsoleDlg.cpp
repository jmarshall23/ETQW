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

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "qe3.h"
#include "Radiant.h"
#include "ConsoleDlg.h"


// CConsoleDlg dialog

IMPLEMENT_DYNCREATE(CConsoleDlg, CDialog)
CConsoleDlg::CConsoleDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CConsoleDlg::IDD)
{
    currentHistoryPosition = -1;
    currentCommand = "";
	saveCurrentCommand = true;
}

CConsoleDlg::~CConsoleDlg()
{
}

void CConsoleDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EDIT_CONSOLE, editConsole);
	DDX_Control(pDX, IDC_EDIT_INPUT, editInput);
}

void CConsoleDlg::AddText( const char *msg ) {
	idStr work;

	work = msg;
	work.RemoveColors();
	work = CEntityDlg::TranslateString( work.c_str() );
	consoleStr += work;
	const int textLimit = editConsole.GetSafeHwnd() != NULL ? (int)editConsole.GetLimitText() : 1024 * 1024;
	if ( consoleStr.Length() > textLimit ) {
		consoleStr = consoleStr.Right( (int)( textLimit * 0.75f ) );
	}
	if ( editConsole.GetSafeHwnd() != NULL ) {
		editConsole.SetWindowText( consoleStr.c_str() );
		editConsole.SetSel( consoleStr.Length(), consoleStr.Length() );
	}
}

idStr CConsoleDlg::NavigateHistory( int direction, const char *inProgress ) {
	if ( direction < 0 ) {
		if ( saveCurrentCommand ) {
			currentCommand = inProgress != NULL ? inProgress : "";
			saveCurrentCommand = false;
		}
		if ( consoleHistory.Num() == 0 ) {
			return currentCommand;
		}
		currentHistoryPosition = idMath::ClampInt( 0, consoleHistory.Num() - 1, currentHistoryPosition );
		idStr result = consoleHistory[currentHistoryPosition];
		if ( currentHistoryPosition > 0 ) {
			currentHistoryPosition--;
		}
		return result;
	}

	if ( currentHistoryPosition < consoleHistory.Num() - 1 ) {
		currentHistoryPosition++;
		return consoleHistory[currentHistoryPosition];
	}
	saveCurrentCommand = true;
	idStr result = currentCommand;
	currentCommand.Clear();
	return result;
}


BEGIN_MESSAGE_MAP(CConsoleDlg, CDialog)
	ON_WM_SIZE()
	ON_WM_SETFOCUS()
	ON_WM_ACTIVATE()
END_MESSAGE_MAP()


// CConsoleDlg message handlers

void CConsoleDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialog::OnSize(nType, cx, cy);

	if (editInput.GetSafeHwnd() == NULL) {
		return;
	}

	CRect rect, crect;
	GetWindowRect(rect);
	editInput.GetWindowRect(crect);

	editInput.SetWindowPos(NULL, 4, rect.Height() - 4 - crect.Height(), rect.Width() - 8, crect.Height(), SWP_SHOWWINDOW);
	editConsole.SetWindowPos(NULL, 4, 4, rect.Width() - 8, rect.Height() - crect.Height() - 8, SWP_SHOWWINDOW);
}

BOOL CConsoleDlg::PreTranslateMessage(MSG* pMsg)
{

	if (pMsg->hwnd == editInput.GetSafeHwnd()) {
		if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE ) {
			Select_Deselect();
			g_pParentWnd->SetFocus ();
			return TRUE;
		}

		if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN ) {
			ExecuteCommand();
			return TRUE;
		}

		if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE ) {
			if (pMsg->wParam == VK_ESCAPE) {
				g_pParentWnd->GetCamera()->SetFocus();
				Select_Deselect();
			}

			return TRUE;
		}

		if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_UP ) {     
            //save off the current in-progress command so we can get back to it
			if ( saveCurrentCommand == true ) {
                CString str;
                editInput.GetWindowText ( str );
                currentCommand = str.GetBuffer ( 0 );
				saveCurrentCommand = false;
	}

			if ( consoleHistory.Num () > 0 ) {
				editInput.SetWindowText ( consoleHistory[currentHistoryPosition] );
            
				int selLocation = consoleHistory[currentHistoryPosition].Length ();
				editInput.SetSel ( selLocation , selLocation + 1);
}

			if ( currentHistoryPosition > 0) {
                --currentHistoryPosition;
            }

			return TRUE;
		}

		if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_DOWN ) {  
            int selLocation = 0;
            if ( currentHistoryPosition < consoleHistory.Num () - 1 ) {
                ++currentHistoryPosition;
                editInput.SetWindowText ( consoleHistory[currentHistoryPosition] );
                selLocation = consoleHistory[currentHistoryPosition].Length ();
            }
            else {
                editInput.SetWindowText ( currentCommand );
                selLocation = currentCommand.Length ();
				currentCommand.Clear ();
				saveCurrentCommand = true;
            }
                        
            editInput.SetSel ( selLocation , selLocation + 1);

			return TRUE;
		}
		if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_TAB ) {  
			common->Printf ( "Command History\n----------------\n" );
			for ( int i = 0 ; i < consoleHistory.Num ();i++ )
{
				common->Printf ( "[cmd %d]:  %s\n" , i , consoleHistory[i].c_str() );
			}
			common->Printf ( "----------------\n" );
		}
		if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_NEXT) {  
			editConsole.LineScroll ( 10 );	
		}

		if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_PRIOR ) {  
			editConsole.LineScroll ( -10 );	
		}

		if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_HOME ) {  
			editConsole.LineScroll ( -editConsole.GetLineCount() );	
		}

		if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_END ) {  
			editConsole.LineScroll ( editConsole.GetLineCount() );	
		}
	}

	return CDialog::PreTranslateMessage(pMsg);
}

void CConsoleDlg::OnSetFocus(CWnd* pOldWnd) {
	CDialog::OnSetFocus(pOldWnd);
	editInput.SetFocus();
}

void CConsoleDlg::SetConsoleText ( const idStr& text ) {
	editInput.Clear ();
	editInput.SetWindowText ( text.c_str() );
}

void CConsoleDlg::ExecuteCommand ( const idStr& cmd ) {
	CString str;
	if ( cmd.Length() > 0 ) {
		str = cmd;
	}
	else {
		editInput.GetWindowText(str);
	}

	if ( str != "" ) {			
		editInput.SetWindowText("");
		common->Printf("%s\n", str.GetBuffer(0));		

		//avoid adding multiple identical commands in a row
		int index = consoleHistory.Num ();

		if ( index == 0 || str.GetBuffer(0) != consoleHistory[index-1]) {					
			//keep the history to 16 commands, removing the oldest command
			if ( consoleHistory.Num () > 16 ) {
				consoleHistory.RemoveIndex ( 0 );
			}
			currentHistoryPosition = consoleHistory.Append ( str.GetBuffer (0) );    
		}
		else {
			currentHistoryPosition = consoleHistory.Num () - 1;
		}

		currentCommand.Clear ();

		bool propogateCommand = true;

		//process some of our own special commands
		if ( str.CompareNoCase ( "clear" ) == 0) {
			consoleStr.Clear();
			if ( editConsole.GetSafeHwnd() != NULL ) {
				editConsole.SetWindowText( "" );
			}
		}
		else if ( str.CompareNoCase ( "edit" ) == 0) {
			propogateCommand = false;
		}
		if ( propogateCommand ) {
			cmdSystem->BufferCommandText( CMD_EXEC_NOW, str );
		}

		Sys_UpdateWindows(W_ALL);
	}
}

void CConsoleDlg::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized)
{
	CDialog::OnActivate(nState, pWndOther, bMinimized);

	if ( nState == WA_ACTIVE || nState == WA_CLICKACTIVE )
	{
		editInput.SetFocus();
	}	
}
