#ifndef __ETQW_RADIANT_DIALOG_NAME_H__
#define __ETQW_RADIANT_DIALOG_NAME_H__

#include "../../sys/win32/rc/Common_resource.h"

class DialogName : public CDialog {
public:
	DialogName( const char *caption, CWnd *parent = NULL );
	enum { IDD = IDD_NEWNAME };
	CString m_strName;

protected:
	virtual void DoDataExchange( CDataExchange *exchange );
	virtual BOOL OnInitDialog();
	CString m_strCaption;
	DECLARE_MESSAGE_MAP()
};

#endif
