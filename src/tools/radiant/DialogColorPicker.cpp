#include "RadiantPch.h"
#pragma hdrstop

#include "DialogColorPicker.h"

bool DoNewColor( int *red, int *green, int *blue, float *overBright,
	void ( *update )( float, float, float, float ) ) {
	CDialogColorPicker dialog( RGB( *red, *green, *blue ) );
	dialog.UpdateParent = update;
	if ( dialog.DoModal() != IDOK ) {
		return false;
	}
	const COLORREF color = dialog.GetColor();
	*red = GetRValue( color );
	*green = GetGValue( color );
	*blue = GetBValue( color );
	*overBright = dialog.GetOverBright();
	return true;
}
