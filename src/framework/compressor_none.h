/*
===========================================================================

Uncompressed idFile forwarding layer and base for the bit-stream codecs.

===========================================================================
*/

#ifndef __COMPRESSOR_NONE_H__
#define __COMPRESSOR_NONE_H__

#include "Compressor.h"

class idCompressor_None : public idCompressor {
public:
					idCompressor_None( void );

	void			Init( idFile* f, bool compress, int wordLength );
	void			FinishCompress( void );
	float			GetCompressionRatio( void ) const;

	const char*		GetName( void );
	const char*		GetFullPath( void );
	int				Read( void* outData, int outLength );
	int				Write( const void* inData, int inLength );
	int				Length( void );
	ID_TIME_T		Timestamp( void );
	int				Tell( void );
	void			ForceFlush( void );
	void			Flush( void );
	int				Seek( long offset, fsOrigin_t origin );

protected:
	idFile*			file;
	bool			compress;
};

#if defined( _M_IX86 )
static_assert( sizeof( idCompressor_None ) == 0x0c, "idCompressor_None layout drift" );
#endif

#endif /* !__COMPRESSOR_NONE_H__ */
