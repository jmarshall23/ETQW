/*
===========================================================================

Seeded MD5 interface used by the PunkBuster integration.

===========================================================================
*/

#ifndef __PBMD5_H__
#define __PBMD5_H__

typedef struct MD5_CTX {
	unsigned long	i[ 2 ];
	unsigned long	buf[ 4 ];
	unsigned char	in[ 64 ];
	unsigned char	digest[ 16 ];
} MD5_CTX;

void MD5Init( MD5_CTX* mdContext, unsigned long pseudoRandomNumber );
void MD5Update( MD5_CTX* mdContext, unsigned char* inBuf, unsigned int inLen );
void MD5Final( MD5_CTX* mdContext );

#endif /* !__PBMD5_H__ */
