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

#ifndef ZCLIP_H
#define ZCLIP_H

// I don't like doing macros without braces and with whitespace, but the compiler moans if I do these differently,
//	and since they're only for use within glColor3f() calls anyway then this is ok...  (that's my excuse anyway)
//
#define ZCLIP_COLOUR		1.0f, 0.0f, 1.0f
#define ZCLIP_COLOUR_DIM	0.8f, 0.0f, 0.8f


class CZClip
{
public:
	CZClip();
	~CZClip();

	int		GetTop(void);
	int		GetBottom(void);
	void	SetTop(int iNewZ);
	void	SetBottom(int iNewZ);
	void	Reset(void);
	bool	IsEnabled(void);
	bool	Enable(bool bOnOff);
	void	Paint(void);

protected:
	void	Legalise(void);

	bool	m_bEnabled;
	int		m_iZClipTop;
	int		m_iZClipBottom;
};


#endif	// #ifndef ZCLIP_H


///////////// eof ///////////////


