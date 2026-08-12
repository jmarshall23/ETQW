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
#if !defined(AFX_CAMERATARGETDLG_H__DE6597C1_1F63_4835_8949_5D2D5F208C6B__INCLUDED_)
#define AFX_CAMERATARGETDLG_H__DE6597C1_1F63_4835_8949_5D2D5F208C6B__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// CameraTargetDlg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CCameraTargetDlg dialog

class CCameraTargetDlg : public CDialog
{
// Construction
public:
	CCameraTargetDlg(CWnd* pParent = NULL);   // standard constructor

// Dialog Data
	//{{AFX_DATA(CCameraTargetDlg)
	enum { IDD = IDD_DLG_CAMERATARGET };
	int		m_nType;
	CString	m_strName;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CCameraTargetDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CCameraTargetDlg)
	afx_msg void OnPopupNewcameraFixed();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_CAMERATARGETDLG_H__DE6597C1_1F63_4835_8949_5D2D5F208C6B__INCLUDED_)
