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
#include "afxwin.h"


// CPreviewDlg dialog

struct CommentedItem {
	idStr Name;
	idStr Path;
	idStr Comments;
};

class CPreviewDlg : public CDialog
{
public:
	enum {MODELS, GUIS, SOUNDS, MATERIALS, SCRIPTS, SOUNDPARENT, WAVES, PARTICLES, MODELPARENT, GUIPARENT, COMMENTED, SKINS};
	CPreviewDlg(CWnd* pParent = NULL);   // standard constructor
	virtual ~CPreviewDlg();
	void SetMode( int mode, const char *preSelect = NULL );
	void RebuildTree( const char *data );
	void SetDisablePreview( bool b ) {
		disablePreview = b;
	}
	
	idStr mediaName;
	int returnCode;

	bool Waiting();
	void SetModal();
// Dialog Data
	enum { IDD = IDD_DIALOG_PREVIEW };
private:
	DECLARE_DYNAMIC(CPreviewDlg)

	CTreeCtrl treeMedia;
	CEdit editInfo;
	HTREEITEM commentItem;
	CImageList m_image;
	idGLDrawable m_testDrawable;
	idGLDrawableMaterial m_drawMaterial;
	idGLDrawableModel m_drawModel;
	idGLWidget wndPreview;
	idHashTable<HTREEITEM> quickTree;
	idList<CommentedItem> items;
	virtual BOOL OnInitDialog();
	int currentMode;
	void AddCommentedItems();
	idStr data;
	bool disablePreview;

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	void BuildTree();
	void AddStrList(const char *root, const idStrList &list, int type);
	void AddSounds(bool rootItems);
	void AddMaterials(bool rootItems);
	void AddParticles(bool rootItems);
	void AddSkins( bool rootItems );
	
	DECLARE_MESSAGE_MAP()

public:
	afx_msg void OnTvnSelchangedTreeMedia(NMHDR *pNMHDR, LRESULT *pResult);
	virtual BOOL Create(LPCTSTR lpszTemplateName, CWnd* pParentWnd = NULL);
protected:
	virtual void OnCancel();
	virtual void OnOK();
	virtual void OnShowWindow( BOOL bShow, UINT status );
public:
	afx_msg void OnBnClickedButtonReload();
	afx_msg void OnBnClickedButtonAdd();
	afx_msg void OnBnClickedButtonPlay();
};
