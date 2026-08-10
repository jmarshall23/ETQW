// Copyright (C) 2007 Id Software, Inc.
//
// Fixed-frequency Huffman compressor used by the ETQW network channels.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "compressor_none.h"

namespace {

static const int HUFFMAN_SYMBOLS = 256;
static const int HUFFMAN_CODE_WORDS = 8;
static const int HUFFMAN_BUFFER_SIZE = 1024;

struct huffmanCode_s {
	unsigned int	bits[ HUFFMAN_CODE_WORDS ];
	int				numBits;
};

struct huffmanNode_s {
	int				symbol;
	int				frequency;
	huffmanNode_s*	next;
	huffmanNode_s*	children[ 2 ];
};

class idCompressor_FixedHuffman : public idCompressor_None {
public:
	explicit				idCompressor_FixedHuffman( int* frequencies );
	virtual					~idCompressor_FixedHuffman( void );

	virtual void			Init( idFile* f, bool compress, int wordLength );
	virtual void			FinishCompress( void );
	virtual float			GetCompressionRatio( void ) const;

	virtual int				Read( void* outData, int outLength );
	virtual int				Write( const void* inData, int inLength );

	void					BuildFrequencies( const char* data, int size );
	int					GetBitsRead( void ) const;

private:
	void					WriteBits( unsigned int value, int numBits );
	int					ReadBit( void );
	void					BuildHuffmanCode_r( huffmanNode_s* node, huffmanCode_s code );
	void					FreeHuffmanTree_r( huffmanNode_s* node );
	void					SetupHuffman( void );
	static void			InsertNode( huffmanNode_s*& list, huffmanNode_s* node );

	// The retail class hides the base class' compression flag.  DIA places
	// this derived flag at 0x0c and the frequency pointer at 0x10.
	bool				compress;
	int*				huffmanFrequencies;
	huffmanCode_s		huffmanCodes[ HUFFMAN_SYMBOLS ];
	huffmanNode_s*		huffmanTree;
	int				maxHuffmanBits;
	bool				huffmanTreeReady;
	byte				treePadding[ 3 ];
	int				totalUncompressedLength;
	int				totalCompressedLength;
	byte				buffer[ HUFFMAN_BUFFER_SIZE ];
	int				maxBytes;
	int				bitCount;
};

idCompressor_FixedHuffman::idCompressor_FixedHuffman( int* frequencies ) :
	compress( false ),
	huffmanFrequencies( frequencies ),
	huffmanTree( NULL ),
	maxHuffmanBits( 0 ),
	huffmanTreeReady( false ),
	totalUncompressedLength( 0 ),
	totalCompressedLength( 0 ),
	maxBytes( 0 ),
	bitCount( 0 ) {
	memset( treePadding, 0, sizeof( treePadding ) );
	memset( huffmanCodes, 0, sizeof( huffmanCodes ) );
	memset( buffer, 0, sizeof( buffer ) );
}

idCompressor_FixedHuffman::~idCompressor_FixedHuffman( void ) {
	if ( huffmanTree != NULL ) {
		FreeHuffmanTree_r( huffmanTree );
		huffmanTree = NULL;
	}
}

float idCompressor_FixedHuffman::GetCompressionRatio( void ) const {
	if ( totalUncompressedLength == 0 ) {
		return 0.0f;
	}
	return static_cast< float >( totalUncompressedLength - totalCompressedLength ) * 100.0f /
		static_cast< float >( totalUncompressedLength );
}

void idCompressor_FixedHuffman::InsertNode( huffmanNode_s*& list, huffmanNode_s* node ) {
	huffmanNode_s* previous = NULL;
	huffmanNode_s* current = list;
	while ( current != NULL && node->frequency > current->frequency ) {
		previous = current;
		current = current->next;
	}
	if ( previous == NULL ) {
		node->next = list;
		list = node;
	} else {
		node->next = current;
		previous->next = node;
	}
}

void idCompressor_FixedHuffman::BuildHuffmanCode_r( huffmanNode_s* node, huffmanCode_s code ) {
	if ( node->symbol != -1 ) {
		huffmanCodes[ node->symbol ] = code;
		return;
	}

	const int bit = code.numBits;
	++code.numBits;
	if ( code.numBits > maxHuffmanBits ) {
		maxHuffmanBits = code.numBits;
	}
	BuildHuffmanCode_r( node->children[ 0 ], code );
	code.bits[ bit >> 5 ] |= 1u << ( bit & 31 );
	BuildHuffmanCode_r( node->children[ 1 ], code );
}

void idCompressor_FixedHuffman::FreeHuffmanTree_r( huffmanNode_s* node ) {
	if ( node == NULL ) {
		return;
	}
	if ( node->symbol == -1 ) {
		FreeHuffmanTree_r( node->children[ 0 ] );
		FreeHuffmanTree_r( node->children[ 1 ] );
	}
	delete node;
}

void idCompressor_FixedHuffman::SetupHuffman( void ) {
	if ( huffmanTreeReady ) {
		return;
	}

	huffmanNode_s* list = NULL;
	for ( int i = 0; i < HUFFMAN_SYMBOLS; ++i ) {
		huffmanNode_s* node = new huffmanNode_s;
		node->symbol = i;
		node->frequency = huffmanFrequencies[ i ];
		node->next = NULL;
		node->children[ 0 ] = node->children[ 1 ] = NULL;
		InsertNode( list, node );
	}

	for ( int i = HUFFMAN_SYMBOLS - 1; i > 0; --i ) {
		huffmanNode_s* left = list;
		huffmanNode_s* right = left->next;
		list = right->next;

		huffmanNode_s* parent = new huffmanNode_s;
		parent->symbol = -1;
		parent->frequency = left->frequency + right->frequency;
		parent->next = NULL;
		parent->children[ 0 ] = left;
		parent->children[ 1 ] = right;
		InsertNode( list, parent );
	}

	huffmanCode_s code;
	memset( &code, 0, sizeof( code ) );
	memset( huffmanCodes, 0, sizeof( huffmanCodes ) );
	maxHuffmanBits = 0;
	huffmanTree = list;
	BuildHuffmanCode_r( huffmanTree, code );
	huffmanTreeReady = true;
}

void idCompressor_FixedHuffman::Init( idFile* f, bool encode, int inWordLength ) {
	if ( huffmanFrequencies == NULL ) {
		common->Error( "idCompressor_FixedHuffman::Init: no huffman frequency table set" );
		return;
	}
	file = f;
	compress = encode;
	(void)inWordLength;
	totalUncompressedLength = 0;
	totalCompressedLength = 0;
	memset( buffer, 0, sizeof( buffer ) );
	maxBytes = 0;
	bitCount = 0;
	if ( !huffmanTreeReady ) {
		SetupHuffman();
	}
}

void idCompressor_FixedHuffman::WriteBits( unsigned int value, int numBits ) {
	if ( bitCount + numBits >= HUFFMAN_BUFFER_SIZE * 8 ) {
		const int bytes = bitCount >> 3;
		const byte lastByte = bytes < HUFFMAN_BUFFER_SIZE ? buffer[ bytes ] : 0;
		if ( bytes > 0 ) {
			const int written = file->Write( buffer, bytes );
			if ( written != bytes ) {
				common->Error( "idCompressor_FixedHuffman::WriteBits: short write %d %d", bytes, written );
			}
			totalCompressedLength += bytes;
		}
		memset( buffer, 0, sizeof( buffer ) );
		bitCount -= bytes * 8;
		maxBytes = HUFFMAN_BUFFER_SIZE;
		buffer[ 0 ] = lastByte;
	}

	while ( numBits > 0 ) {
		const int bitOffset = bitCount & 7;
		int chunkBits = 8 - bitOffset;
		if ( chunkBits > numBits ) {
			chunkBits = numBits;
		}
		buffer[ bitCount >> 3 ] |= static_cast< byte >( ( value & ( ( 1u << chunkBits ) - 1u ) ) << bitOffset );
		value >>= chunkBits;
		numBits -= chunkBits;
		bitCount += chunkBits;
	}
}

int idCompressor_FixedHuffman::ReadBit( void ) {
	if ( bitCount == maxBytes * 8 ) {
		maxBytes = file->Read( buffer, sizeof( buffer ) );
		if ( maxBytes <= 0 ) {
			common->Error( "idCompressor_FixedHuffman::ReadBit: read past end of input" );
			return 0;
		}
		totalCompressedLength += maxBytes;
		bitCount = 0;
	}
	const int result = ( buffer[ bitCount >> 3 ] >> ( bitCount & 7 ) ) & 1;
	++bitCount;
	return result;
}

int idCompressor_FixedHuffman::GetBitsRead( void ) const {
	return bitCount + 8 * totalCompressedLength - 8 * maxBytes;
}

int idCompressor_FixedHuffman::Read( void* outData, int outLength ) {
	if ( compress || outLength <= 0 ) {
		return 0;
	}
	const int start = totalCompressedLength + ( bitCount >> 3 ) - maxBytes;
	byte* output = static_cast< byte* >( outData );
	for ( int i = 0; i < outLength; ++i ) {
		huffmanNode_s* node = huffmanTree;
		while ( node->symbol == -1 ) {
			node = node->children[ ReadBit() ];
		}
		output[ i ] = static_cast< byte >( node->symbol );
	}
	return totalCompressedLength + ( bitCount >> 3 ) - maxBytes - start;
}

int idCompressor_FixedHuffman::Write( const void* inData, int inLength ) {
	if ( !compress || inLength <= 0 ) {
		return 0;
	}
	totalUncompressedLength += inLength;
	const int start = totalCompressedLength + ( bitCount >> 3 );
	const byte* input = static_cast< const byte* >( inData );
	for ( int i = 0; i < inLength; ++i ) {
		const huffmanCode_s& code = huffmanCodes[ input[ i ] ];
		int word = 0;
		for ( ; word < ( code.numBits >> 5 ); ++word ) {
			WriteBits( code.bits[ word ], 32 );
		}
		if ( code.numBits & 31 ) {
			WriteBits( code.bits[ word ], code.numBits & 31 );
		}
	}
	return totalCompressedLength + ( bitCount >> 3 ) - start;
}

void idCompressor_FixedHuffman::FinishCompress( void ) {
	if ( !compress ) {
		return;
	}
	int bytes = bitCount >> 3;
	if ( bitCount & 7 ) {
		++bytes;
	}
	const int written = file->Write( buffer, bytes );
	if ( written != bytes ) {
		common->Error( "idCompressor_FixedHuffman::FinishCompress: short write %d %d", bytes, written );
	}
	totalCompressedLength += bytes;
	memset( buffer, 0, sizeof( buffer ) );
	maxBytes = 0;
	bitCount = 0;
}

void idCompressor_FixedHuffman::BuildFrequencies( const char* data, int size ) {
	for ( int i = 0; i < size; ++i ) {
		int& frequency = huffmanFrequencies[ static_cast< byte >( data[ i ] ) ];
		if ( ++frequency == 0x7fffffff ) {
			common->Error( "idCompressor_FixedHuffman::BuildFrequencies: overflow" );
		}
	}
}

#if defined( _M_IX86 )
static_assert( sizeof( idCompressor_FixedHuffman ) == 0x2830, "idCompressor_FixedHuffman layout drift" );
#endif

} // namespace

idCompressor* idCompressor::AllocFixedHuffman( int* huffmanFrequencies ) {
	return new idCompressor_FixedHuffman( huffmanFrequencies );
}
