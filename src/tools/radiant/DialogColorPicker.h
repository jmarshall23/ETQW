#ifndef __ETQW_RADIANT_DIALOG_COLOR_PICKER_H__
#define __ETQW_RADIANT_DIALOG_COLOR_PICKER_H__

class CDialogColorPicker : public CColorDialog {
public:
	explicit CDialogColorPicker( COLORREF color, CWnd *parent = NULL ) :
		CColorDialog( color, CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR, parent ),
		overBright( 1.0f ), UpdateParent( NULL ) {
	}

	COLORREF GetColor() const { return CColorDialog::GetColor(); }
	float GetOverBright() const { return overBright; }
	void ( *UpdateParent )( float r, float g, float b, float a );

	virtual INT_PTR DoModal() {
		const INT_PTR result = CColorDialog::DoModal();
		if ( result == IDOK && UpdateParent != NULL ) {
			const COLORREF color = GetColor();
			UpdateParent( GetRValue( color ) / 255.0f, GetGValue( color ) / 255.0f,
				GetBValue( color ) / 255.0f, overBright );
		}
		return result;
	}

private:
	float overBright;
};

bool DoNewColor( int *red, int *green, int *blue, float *overBright,
	void ( *update )( float, float, float, float ) = NULL );

#endif
