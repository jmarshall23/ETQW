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
#include "DlgEvent.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CDlgEvent dialog


CDlgEvent::CDlgEvent(CWnd* pParent /*=NULL*/)
	: CDialog(CDlgEvent::IDD, pParent)
{
	//{{AFX_DATA_INIT(CDlgEvent)
	m_strParm = _T("");
	m_event = 0;
	//}}AFX_DATA_INIT
}


void CDlgEvent::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CDlgEvent)
	DDX_Text(pDX, IDC_EDIT_PARAM, m_strParm);
	DDX_Radio(pDX, IDC_RADIO_EVENT, m_event);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CDlgEvent, CDialog)
	//{{AFX_MSG_MAP(CDlgEvent)
		// NOTE: the ClassWizard will add message map macros here
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CDlgEvent message handlers
