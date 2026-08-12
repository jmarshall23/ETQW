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
#include "CurveDlg.h"


// CCurveDlg dialog

IMPLEMENT_DYNAMIC(CCurveDlg, CDialog)
CCurveDlg::CCurveDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CCurveDlg::IDD, pParent)
{
}

CCurveDlg::~CCurveDlg()
{
}

void CCurveDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_COMBO_CURVES, comboCurve);
}

void CCurveDlg::OnOK() {
	UpdateData(TRUE);
	CString str;
	comboCurve.GetWindowText( str );
	strCurveType = str;
	CDialog::OnOK();
}

BEGIN_MESSAGE_MAP(CCurveDlg, CDialog)
END_MESSAGE_MAP()


// CCurveDlg message handlers
