// Copyright (C) 2007 Id Software, Inc.
//


#include "../../../idlib/precompiled.h"
#pragma hdrstop

#include "../../sys_local.h"

class idKeyboardWin32 : public idKeyboard {
public:
	virtual bool		Init() { return true; }
	virtual void	Shutdown() {}
	virtual void	Activate() {}
	virtual void	Deactivate() {}
	virtual int		PollInputEvents( bool ) { return 0; }
	virtual int		ReturnInputEvent( const int, keyNum_t&, bool& ) { return 0; }
	virtual void	EndInputEvents() {}

	virtual keyNum_t ConvertScanToKey( unsigned int scanCode ) const;
	virtual keyNum_t ConvertCharToKey( char ch ) const;
	virtual char	ConvertScanToChar( unsigned int scanCode ) const;
	virtual unsigned int ConvertCharToScan( char ch ) const;
	virtual char	ConvertKeyToChar( const keyNum_t keyNum ) const;
	virtual bool	IsConsoleKey( const sdSysEvent& event ) const;

private:
	static const unsigned int vkToKeyNum[ 256 ];
	static const unsigned int keyNumToVk[ K_NUM_KEYS ];
};

const unsigned int idKeyboardWin32::vkToKeyNum[ 256 ] = {
	0, 0, 0, 0, 0, 0, 0, 0, 8, 9, 0, 0, 169, 13, 0, 0,
	142, 141, 140, 131, 129, 0, 0, 0, 0, 0, 0, 27, 0, 0, 0, 0,
	32, 146, 145, 148, 147, 135, 133, 136, 134, 0, 0, 0, 250, 143, 144, 0,
	48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 0, 0, 0, 0, 0, 0,
	0, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
	112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 137, 138, 139, 0, 0,
	175, 171, 172, 173, 168, 169, 170, 165, 166, 167, 181, 179, 174, 178, 176, 177,
	149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	180, 130, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	142, 252, 141, 253, 140, 251, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 59, 43, 44, 45, 46, 47,
	96, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 91, 92, 93, 39, 0,
	0, 0, 225, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const unsigned int idKeyboardWin32::keyNumToVk[ K_NUM_KEYS ] = {
	0, 0, 0, 0, 0, 0, 0, 0, 8, 9, 0, 0, 0, 13, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 27, 0, 0, 0, 0,
	32, 0, 0, 0, 0, 0, 0, 222, 0, 0, 0, 187, 188, 189, 190, 191,
	48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 0, 186, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 219, 220, 221, 0, 0,
	192, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
	80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 0, 0, 0, 0, 0,
	0, 20, 145, 19, 0, 38, 134, 135, 136, 91, 92, 93, 18, 17, 16, 45,
	46, 34, 33, 36, 35, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122,
	123, 124, 125, 126, 0, 96, 104, 105, 100, 101, 102, 97, 98, 99, 108, 96,
	110, 111, 109, 107, 144, 106, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 226, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 44, 165, 161, 163, 0
};

static idKeyboardWin32 keyboard;

idKeyboard& idSysLocal::Keyboard() {
	return keyboard;
}

keyNum_t idKeyboardWin32::ConvertScanToKey( unsigned int scanCode ) const {
	const HKL layout = GetKeyboardLayout( 0 );
	return static_cast< keyNum_t >( vkToKeyNum[ MapVirtualKeyExA( scanCode, MAPVK_VSC_TO_VK_EX, layout ) & 0xff ] );
}

keyNum_t idKeyboardWin32::ConvertCharToKey( char ch ) const {
	const HKL layout = GetKeyboardLayout( 0 );
	return static_cast< keyNum_t >( vkToKeyNum[ static_cast< unsigned char >( VkKeyScanExA( ch, layout ) ) ] );
}

char idKeyboardWin32::ConvertScanToChar( unsigned int scanCode ) const {
	const HKL layout = GetKeyboardLayout( 0 );
	const UINT virtualKey = MapVirtualKeyExA( scanCode, MAPVK_VSC_TO_VK_EX, layout );
	const int value = static_cast< int >( MapVirtualKeyExA( virtualKey, MAPVK_VK_TO_CHAR, layout ) );
	return value < 0 ? '\0' : static_cast< char >( value );
}

unsigned int idKeyboardWin32::ConvertCharToScan( char ch ) const {
	const HKL layout = GetKeyboardLayout( 0 );
	const unsigned char virtualKey = static_cast< unsigned char >( VkKeyScanExA( ch, layout ) );
	return MapVirtualKeyExA( virtualKey, MAPVK_VK_TO_VSC, layout );
}

char idKeyboardWin32::ConvertKeyToChar( const keyNum_t keyNum ) const {
	if ( static_cast< unsigned int >( keyNum ) >= K_NUM_KEYS ) {
		return '\0';
	}
	const int value = static_cast< int >( MapVirtualKeyExA( keyNumToVk[ keyNum ], MAPVK_VK_TO_CHAR, GetKeyboardLayout( 0 ) ) );
	return value < 0 ? '\0' : static_cast< char >( value );
}

bool idKeyboardWin32::IsConsoleKey( const sdSysEvent& event ) const {
	return ( event.IsKeyEvent() || event.IsCharEvent() ) && event.GetScanCode() == 41;
}

