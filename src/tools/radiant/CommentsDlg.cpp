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
#include "CommentsDlg.h"


// CCommentsDlg dialog

IMPLEMENT_DYNAMIC(CCommentsDlg, CDialog)
CCommentsDlg::CCommentsDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CCommentsDlg::IDD, pParent)
	, strName(_T(""))
	, strPath(_T(""))
	, strComments(_T(""))
{
}

CCommentsDlg::~CCommentsDlg()
{
}

void CCommentsDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Text(pDX, IDC_EDIT_NAME, strName);
	DDX_Text(pDX, IDC_EDIT_PATH, strPath);
	DDX_Text(pDX, IDC_EDIT_COMMENTS, strComments);
}


BEGIN_MESSAGE_MAP(CCommentsDlg, CDialog)
END_MESSAGE_MAP()

	
// CCommentsDlg message handlers
