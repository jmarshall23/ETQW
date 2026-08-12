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

#ifndef __CMDLIB__
#define __CMDLIB__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>


int		LoadFile( const char *filename, void **bufferptr );
void 	DefaultExtension( char *path, char *extension );
void 	DefaultPath( char *path, char *basepath );
void 	StripFilename( char *path );
void 	StripExtension( char *path );

// error and printf functions
typedef void (PFN_ERR)( const char *pFormat, ... );
typedef void (PFN_PRINTF)( const char *pFormat, ... );
typedef void (PFN_ERR_NUM)( int nNum, const char *pFormat, ... );
typedef void (PFN_PRINTF_NUM)( int nNum, const char *pFormat, ... );

void Error( const char *pFormat, ... );
void Printf( const char *pFormat, ... );
void ErrorNum( int n, const char *pFormat, ... );
void PrintfNum( int n, const char *pFormat, ... );

void SetErrorHandler( PFN_ERR pe );
void SetPrintfHandler( PFN_PRINTF pe );
void SetErrorHandlerNum( PFN_ERR_NUM pe );
void SetPrintfHandlerNum( PFN_PRINTF_NUM pe );

#endif /* !__CMDLIB__ */
