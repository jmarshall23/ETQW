#include "RadiantPch.h"
#pragma hdrstop

#include "DialogName.h"

DialogName::DialogName( const char *caption, CWnd *parent ) :
	CDialog( DialogName::IDD, parent ), m_strName( _T( "" ) ), m_strCaption( caption ) {
}

void DialogName::DoDataExchange( CDataExchange *exchange ) {
	CDialog::DoDataExchange( exchange );
	DDX_Text( exchange, IDC_TOOLS_EDITNAME, m_strName );
}

BOOL DialogName::OnInitDialog() {
	CDialog::OnInitDialog();
	SetWindowText( m_strCaption );
	return TRUE;
}

BEGIN_MESSAGE_MAP( DialogName, CDialog )
END_MESSAGE_MAP()
