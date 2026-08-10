/*
===========================================================================

Seeded MD5 implementation used by the PunkBuster integration.  The digest
rounds are RFC 1321 MD5; PunkBuster varies the initial state with the caller's
pseudo-random seed.

===========================================================================
*/

#include "../../framework/precompiled.h"
#include "pbmd5.h"

static unsigned char PADDING[ 64 ] = {
	0x80
};

static ID_INLINE unsigned long RotateLeft( unsigned long value, unsigned long bits ) {
	return ( value << bits ) | ( value >> ( 32 - bits ) );
}

static ID_INLINE unsigned long F( unsigned long x, unsigned long y, unsigned long z ) {
	return ( x & y ) | ( ~x & z );
}

static ID_INLINE unsigned long G( unsigned long x, unsigned long y, unsigned long z ) {
	return ( x & z ) | ( y & ~z );
}

static ID_INLINE unsigned long H( unsigned long x, unsigned long y, unsigned long z ) {
	return x ^ y ^ z;
}

static ID_INLINE unsigned long I( unsigned long x, unsigned long y, unsigned long z ) {
	return y ^ ( x | ~z );
}

#define FF( a, b, c, d, x, s, ac ) \
	( a ) += F( ( b ), ( c ), ( d ) ) + ( x ) + (unsigned long)( ac ); \
	( a ) = RotateLeft( ( a ), ( s ) ); \
	( a ) += ( b )

#define GG( a, b, c, d, x, s, ac ) \
	( a ) += G( ( b ), ( c ), ( d ) ) + ( x ) + (unsigned long)( ac ); \
	( a ) = RotateLeft( ( a ), ( s ) ); \
	( a ) += ( b )

#define HH( a, b, c, d, x, s, ac ) \
	( a ) += H( ( b ), ( c ), ( d ) ) + ( x ) + (unsigned long)( ac ); \
	( a ) = RotateLeft( ( a ), ( s ) ); \
	( a ) += ( b )

#define II( a, b, c, d, x, s, ac ) \
	( a ) += I( ( b ), ( c ), ( d ) ) + ( x ) + (unsigned long)( ac ); \
	( a ) = RotateLeft( ( a ), ( s ) ); \
	( a ) += ( b )

static void Transform( unsigned long* in, unsigned long* buf ) {
	unsigned long a = buf[ 0 ];
	unsigned long b = buf[ 1 ];
	unsigned long c = buf[ 2 ];
	unsigned long d = buf[ 3 ];

	FF( a, b, c, d, in[  0 ],  7, 0xd76aa478UL );
	FF( d, a, b, c, in[  1 ], 12, 0xe8c7b756UL );
	FF( c, d, a, b, in[  2 ], 17, 0x242070dbUL );
	FF( b, c, d, a, in[  3 ], 22, 0xc1bdceeeUL );
	FF( a, b, c, d, in[  4 ],  7, 0xf57c0fafUL );
	FF( d, a, b, c, in[  5 ], 12, 0x4787c62aUL );
	FF( c, d, a, b, in[  6 ], 17, 0xa8304613UL );
	FF( b, c, d, a, in[  7 ], 22, 0xfd469501UL );
	FF( a, b, c, d, in[  8 ],  7, 0x698098d8UL );
	FF( d, a, b, c, in[  9 ], 12, 0x8b44f7afUL );
	FF( c, d, a, b, in[ 10 ], 17, 0xffff5bb1UL );
	FF( b, c, d, a, in[ 11 ], 22, 0x895cd7beUL );
	FF( a, b, c, d, in[ 12 ],  7, 0x6b901122UL );
	FF( d, a, b, c, in[ 13 ], 12, 0xfd987193UL );
	FF( c, d, a, b, in[ 14 ], 17, 0xa679438eUL );
	FF( b, c, d, a, in[ 15 ], 22, 0x49b40821UL );

	GG( a, b, c, d, in[  1 ],  5, 0xf61e2562UL );
	GG( d, a, b, c, in[  6 ],  9, 0xc040b340UL );
	GG( c, d, a, b, in[ 11 ], 14, 0x265e5a51UL );
	GG( b, c, d, a, in[  0 ], 20, 0xe9b6c7aaUL );
	GG( a, b, c, d, in[  5 ],  5, 0xd62f105dUL );
	GG( d, a, b, c, in[ 10 ],  9, 0x02441453UL );
	GG( c, d, a, b, in[ 15 ], 14, 0xd8a1e681UL );
	GG( b, c, d, a, in[  4 ], 20, 0xe7d3fbc8UL );
	GG( a, b, c, d, in[  9 ],  5, 0x21e1cde6UL );
	GG( d, a, b, c, in[ 14 ],  9, 0xc33707d6UL );
	GG( c, d, a, b, in[  3 ], 14, 0xf4d50d87UL );
	GG( b, c, d, a, in[  8 ], 20, 0x455a14edUL );
	GG( a, b, c, d, in[ 13 ],  5, 0xa9e3e905UL );
	GG( d, a, b, c, in[  2 ],  9, 0xfcefa3f8UL );
	GG( c, d, a, b, in[  7 ], 14, 0x676f02d9UL );
	GG( b, c, d, a, in[ 12 ], 20, 0x8d2a4c8aUL );

	HH( a, b, c, d, in[  5 ],  4, 0xfffa3942UL );
	HH( d, a, b, c, in[  8 ], 11, 0x8771f681UL );
	HH( c, d, a, b, in[ 11 ], 16, 0x6d9d6122UL );
	HH( b, c, d, a, in[ 14 ], 23, 0xfde5380cUL );
	HH( a, b, c, d, in[  1 ],  4, 0xa4beea44UL );
	HH( d, a, b, c, in[  4 ], 11, 0x4bdecfa9UL );
	HH( c, d, a, b, in[  7 ], 16, 0xf6bb4b60UL );
	HH( b, c, d, a, in[ 10 ], 23, 0xbebfbc70UL );
	HH( a, b, c, d, in[ 13 ],  4, 0x289b7ec6UL );
	HH( d, a, b, c, in[  0 ], 11, 0xeaa127faUL );
	HH( c, d, a, b, in[  3 ], 16, 0xd4ef3085UL );
	HH( b, c, d, a, in[  6 ], 23, 0x04881d05UL );
	HH( a, b, c, d, in[  9 ],  4, 0xd9d4d039UL );
	HH( d, a, b, c, in[ 12 ], 11, 0xe6db99e5UL );
	HH( c, d, a, b, in[ 15 ], 16, 0x1fa27cf8UL );
	HH( b, c, d, a, in[  2 ], 23, 0xc4ac5665UL );

	II( a, b, c, d, in[  0 ],  6, 0xf4292244UL );
	II( d, a, b, c, in[  7 ], 10, 0x432aff97UL );
	II( c, d, a, b, in[ 14 ], 15, 0xab9423a7UL );
	II( b, c, d, a, in[  5 ], 21, 0xfc93a039UL );
	II( a, b, c, d, in[ 12 ],  6, 0x655b59c3UL );
	II( d, a, b, c, in[  3 ], 10, 0x8f0ccc92UL );
	II( c, d, a, b, in[ 10 ], 15, 0xffeff47dUL );
	II( b, c, d, a, in[  1 ], 21, 0x85845dd1UL );
	II( a, b, c, d, in[  8 ],  6, 0x6fa87e4fUL );
	II( d, a, b, c, in[ 15 ], 10, 0xfe2ce6e0UL );
	II( c, d, a, b, in[  6 ], 15, 0xa3014314UL );
	II( b, c, d, a, in[ 13 ], 21, 0x4e0811a1UL );
	II( a, b, c, d, in[  4 ],  6, 0xf7537e82UL );
	II( d, a, b, c, in[ 11 ], 10, 0xbd3af235UL );
	II( c, d, a, b, in[  2 ], 15, 0x2ad7d2bbUL );
	II( b, c, d, a, in[  9 ], 21, 0xeb86d391UL );

	buf[ 0 ] += a;
	buf[ 1 ] += b;
	buf[ 2 ] += c;
	buf[ 3 ] += d;
}

void MD5Init( MD5_CTX* mdContext, unsigned long pseudoRandomNumber ) {
	mdContext->i[ 0 ] = 0;
	mdContext->i[ 1 ] = 0;
	mdContext->buf[ 0 ] = 11 * pseudoRandomNumber + 0x67452301UL;
	mdContext->buf[ 1 ] = 71 * pseudoRandomNumber + 0xefcdab89UL;
	mdContext->buf[ 2 ] = 37 * pseudoRandomNumber + 0x98badcfeUL;
	mdContext->buf[ 3 ] = 97 * pseudoRandomNumber + 0x10325476UL;
}

void MD5Update( MD5_CTX* mdContext, unsigned char* inBuf, unsigned int inLen ) {
	unsigned long in[ 16 ];
	unsigned int mdi = ( mdContext->i[ 0 ] >> 3 ) & 0x3f;

	if ( ( mdContext->i[ 0 ] + ( (unsigned long)inLen << 3 ) ) < mdContext->i[ 0 ] ) {
		mdContext->i[ 1 ]++;
	}
	mdContext->i[ 0 ] += (unsigned long)inLen << 3;
	mdContext->i[ 1 ] += (unsigned long)inLen >> 29;

	while ( inLen-- ) {
		mdContext->in[ mdi++ ] = *inBuf++;
		if ( mdi == 0x40 ) {
			for ( unsigned int i = 0, ii = 0; i < 16; i++, ii += 4 ) {
				in[ i ] = (unsigned long)mdContext->in[ ii ] |
					( (unsigned long)mdContext->in[ ii + 1 ] << 8 ) |
					( (unsigned long)mdContext->in[ ii + 2 ] << 16 ) |
					( (unsigned long)mdContext->in[ ii + 3 ] << 24 );
			}
			Transform( in, mdContext->buf );
			mdi = 0;
		}
	}
}

void MD5Final( MD5_CTX* mdContext ) {
	unsigned long in[ 16 ];
	unsigned long bits[ 2 ];
	unsigned int mdi = ( mdContext->i[ 0 ] >> 3 ) & 0x3f;
	unsigned int padLen = mdi < 56 ? 56 - mdi : 120 - mdi;

	bits[ 0 ] = mdContext->i[ 0 ];
	bits[ 1 ] = mdContext->i[ 1 ];
	MD5Update( mdContext, PADDING, padLen );

	for ( unsigned int i = 0, ii = 0; i < 14; i++, ii += 4 ) {
		in[ i ] = (unsigned long)mdContext->in[ ii ] |
			( (unsigned long)mdContext->in[ ii + 1 ] << 8 ) |
			( (unsigned long)mdContext->in[ ii + 2 ] << 16 ) |
			( (unsigned long)mdContext->in[ ii + 3 ] << 24 );
	}
	in[ 14 ] = bits[ 0 ];
	in[ 15 ] = bits[ 1 ];
	Transform( in, mdContext->buf );

	for ( unsigned int i = 0, ii = 0; i < 4; i++, ii += 4 ) {
		mdContext->digest[ ii ] = (unsigned char)( mdContext->buf[ i ] & 0xff );
		mdContext->digest[ ii + 1 ] = (unsigned char)( ( mdContext->buf[ i ] >> 8 ) & 0xff );
		mdContext->digest[ ii + 2 ] = (unsigned char)( ( mdContext->buf[ i ] >> 16 ) & 0xff );
		mdContext->digest[ ii + 3 ] = (unsigned char)( ( mdContext->buf[ i ] >> 24 ) & 0xff );
	}
}

#undef FF
#undef GG
#undef HH
#undef II
