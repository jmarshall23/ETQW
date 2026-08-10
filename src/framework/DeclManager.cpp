// Copyright (C) 2007 Id Software, Inc.
//
// Enemy Territory: Quake Wars declaration manager reconstruction.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "DeclManagerLocal.h"
#include "../decllib/declTemplate.h"

static const unsigned int INVALID_BINARY_SOURCE_OFFSET = ~0u;

namespace {

class declExpandedTextSetter {
public:
	explicit declExpandedTextSetter( idStr* destination_ ) : destination( destination_ ) {}

	void operator()( const char* text, const int length ) const {
		if ( destination == NULL ) {
			return;
		}
		if ( text == NULL || length <= 0 ) {
			destination->Clear();
			return;
		}
		*destination = idStr( text, 0, length );
	}

private:
	idStr* destination;
};

}

idCVar idDeclManagerLocal::decl_show(
	"decl_show",
	"0",
	CVAR_SYSTEM | CVAR_INTEGER,
	"set to 1 to print parses, 2 to also print references, 3 only prints out of level load, 4 only prints out of level load but also prints references",
	0.0f,
	4.0f,
	idCmdSystem::ArgCompletion_Integer< 0, 4 >
);

idCVar idDeclManagerLocal::decl_usageLog(
	"decl_usageLog",
	"0",
	CVAR_SYSTEM | CVAR_BOOL,
	"creates a log of all declarations touched"
);

idDeclManagerLocal declManagerLocal;
idDeclManager* declManager = &declManagerLocal;

/*
===============================================================================

	Small private records

===============================================================================
*/

declFileDependency_t::declFileDependency_t( void ) :
	timestamp( 0 ),
	dirty( false ) {
}

declFileDependency_t::declFileDependency_t( const char* name ) :
	fileName( name ),
	timestamp( 0 ),
	dirty( false ) {
}

idDeclFolder::idDeclFolder( const char* folderName, const char* fileExtension ) :
	folder( folderName ),
	extension( fileExtension ),
	referenceCount( 1 ),
	scannedForBinaries( false ) {
}

idDeclManagerLocal::declTypeInfo_t::declTypeInfo_t( void ) :
	declType( NULL ),
	refCount( 0 ) {
}

/*
===============================================================================

	idDeclFile

===============================================================================
*/

idDeclFile::idDeclFile( const char* name ) :
	fileName( name ),
	timestamp( 0 ),
	checksum( 0 ),
	fileSize( 0 ),
	numLines( 0 ),
	isBinary( false ),
	decls( NULL ) {
}

idDeclFile::~idDeclFile( void ) {
	dependencies.Clear();
	includeDependencies.Clear();
}

void idDeclFile::Reload( bool force ) {
	if ( !force && timestamp != 0 && fileSystem != NULL ) {
		unsigned int newTimestamp = 0;
		if ( fileSystem->ReadFile( fileName, NULL, &newTimestamp ) >= 0 && newTimestamp == timestamp ) {
			bool dependencyChanged = false;
			for ( int i = 0; i < dependencies.Num(); i++ ) {
				declFileDependency_t* dependency = dependencies[ i ];
				if ( dependency != NULL && fileSystem->GetTimestamp( dependency->fileName ) != dependency->timestamp ) {
					dependencyChanged = true;
					break;
				}
			}
			if ( !dependencyChanged ) {
				return;
			}
		}
	}

	LoadAndParse();
}

int idDeclFile::LoadAndParse( void ) {
	if ( fileSystem == NULL ) {
		return 0;
	}

	char* buffer = NULL;
	const int length = fileSystem->ReadFile( fileName, reinterpret_cast< void** >( &buffer ), &timestamp );
	if ( length < 0 || buffer == NULL ) {
		common->Warning( "couldn't load decl file '%s'", fileName.c_str() );
		return 0;
	}

	common->DPrintf( "...loading '%s'\n", fileName.c_str() );

	idParser src;
	src.SetFlags( DECL_LEXER_FLAGS );
	if ( !src.LoadMemory( buffer, length, fileName ) ) {
		common->Warning( "couldn't parse decl file '%s'", fileName.c_str() );
		fileSystem->FreeFile( buffer );
		return 0;
	}

	dependencies.Clear();
	includeDependencies.Clear();

	for ( idDeclLocal* decl = decls; decl != NULL; decl = decl->nextInFile ) {
		decl->flags.redefinedInReload = false;
	}

	checksum = MD5_BlockChecksum( buffer, length );
	fileSize = length;

	// Keep dependencies found while scanning a declaration body separate from
	// includes that precede declarations at file scope.  Decl parsers replay
	// the latter before parsing their extracted source text, while body-local
	// includes are already present at their original position in that text.
	src.PushDependencies();

	idToken token;
	while ( true ) {
		const int startMarker = src.GetFileOffset();
		const int sourceLine = src.GetLineNum();

		if ( !src.ReadToken( &token ) ) {
			break;
		}

		idDeclTypeInterface* declType = declManagerLocal.GetDeclType( token.c_str() );
		if ( declType == NULL ) {
			if ( token == "{" ) {
				src.Warning( "Missing decl name" );
				src.SkipBracedSection( false );
			} else {
				src.Warning( "Unknown decl type '%s'", token.c_str() );
				idToken skippedName;
				idToken brace;
				if ( src.ReadToken( &skippedName ) && src.ReadToken( &brace ) && brace == "{" ) {
					src.SkipBracedSection( false );
				}
			}
			continue;
		}

		idToken nameToken;
		if ( !src.ReadToken( &nameToken ) ) {
			src.Warning( "Type without definition at end of file" );
			break;
		}
		if ( nameToken == "{" ) {
			src.Warning( "Missing decl name" );
			src.SkipBracedSection( false );
			continue;
		}

		int dependencyIndex = src.GetCurrentDependency();
		const char* dependencyName = NULL;
		while ( ( dependencyName = src.GetNextDependency( dependencyIndex ) ) != NULL ) {
			includeDependencies.AddUnique( dependencyName );
		}

		src.PushDependencies();

		// Export sections are consumed by the model-export tool directly.  The
		// retail decl loader skips their bodies and never installs their author
		// labels (for example "hauser") as declarations.
		if ( declType->SkipParsing() ) {
			if ( !src.SkipBracedSection( true ) ) {
				src.Warning( "Unexpected end of file" );
			}
			src.PopDependencies();
			continue;
		}

		idToken brace;
		if ( !src.ReadToken( &brace ) ) {
			src.Warning( "Type without definition at end of file" );
			src.PopDependencies();
			break;
		}
		if ( brace != "{" ) {
			src.Warning( "Expecting '{' but found '%s'", brace.c_str() );
			src.PopDependencies();
			continue;
		}

		if ( !src.SkipBracedSection( false ) ) {
			src.Warning( "Unexpected end of file in %s '%s'", declType->GetName(), nameToken.c_str() );
			src.PopDependencies();
			break;
		}

		const int endMarker = src.GetFileOffset();
		if ( endMarker < startMarker || endMarker > length ) {
			src.Warning( "Invalid source range in %s '%s'", declType->GetName(), nameToken.c_str() );
			src.PopDependencies();
			continue;
		}

		idDeclLocal* newDecl = declManagerLocal.FindTypeWithoutParsing( declType, nameToken.c_str(), false );
		bool reparse = false;
		if ( newDecl != NULL ) {
			if ( newDecl->sourceFile != NULL &&
					( newDecl->sourceFile != this || newDecl->flags.redefinedInReload ) ) {
				src.Warning(
					"%s '%s' previously defined at %s:%i",
					declType->GetName(),
					nameToken.c_str(),
					newDecl->sourceFile->fileName.c_str(),
					newDecl->sourceLine
				);
				src.PopDependencies();
				continue;
			}
			reparse = newDecl->declState != DS_UNPARSED;
		} else {
			newDecl = declManagerLocal.FindTypeWithoutParsing( declType, nameToken.c_str(), true );
			newDecl->nextInFile = decls;
			decls = newDecl;
		}

		newDecl->flags.redefinedInReload = true;
		newDecl->SetTextLocal( buffer + startMarker, endMarker - startMarker, true );
		newDecl->sourceFile = this;
		newDecl->sourceTextOffset = startMarker;
		newDecl->sourceTextLength = endMarker - startMarker;
		newDecl->sourceLine = sourceLine;
		newDecl->declState = DS_UNPARSED;
		newDecl->fileDependencies.Clear();

		dependencyIndex = src.GetCurrentDependency();
		while ( ( dependencyName = src.GetNextDependency( dependencyIndex ) ) != NULL ) {
			newDecl->AddIncludeDependency( dependencyName );
		}
		for ( int i = 0; i < includeDependencies.Num(); i++ ) {
			newDecl->AddIncludeDependency( includeDependencies[ i ] );
		}

		if ( reparse ) {
			newDecl->ParseLocal();
			declType->OnReload( newDecl->self );
		}

		src.PopDependencies();
	}

	int dependencyIndex = src.GetCurrentDependency();
	const char* dependencyName = NULL;
	while ( ( dependencyName = src.GetNextDependency( dependencyIndex ) ) != NULL ) {
		includeDependencies.AddUnique( dependencyName );
	}
	src.PopDependencies();

	numLines = src.GetLineNum();
	fileSystem->FreeFile( buffer );

	for ( idDeclLocal* decl = decls; decl != NULL; decl = decl->nextInFile ) {
		if ( !decl->flags.redefinedInReload ) {
			decl->MakeDefault();
			decl->sourceTextOffset = fileSize;
			decl->sourceTextLength = 0;
			decl->sourceLine = numLines;
		}
	}

	return checksum;
}

const idStrList& idDeclFile::GetIncludeDependencies( void ) const {
	return includeDependencies;
}

void idDeclFile::MakeBinaryFilename( void ) {
	idStr extension;
	fileName.ExtractFileExtension( extension );
	if ( extension.Length() > 0 ) {
		extension = "b" + extension;
	} else {
		extension = "b";
	}

	binaryFileName = fileName;
	binaryFileName.SetFileExtension( extension );
	binaryFileName = va( "generated/declb/%s", binaryFileName.c_str() );
}

int idDeclFile::LoadAndParseBinary( idFile* binaryFile ) {
	// Binary table/header restoration belongs with the remaining decllib
	// binary serializers. Text declarations remain the authoritative path.
	(void)binaryFile;
	return 0;
}

/*
===============================================================================

	idDeclLocal

===============================================================================
*/

idDeclLocal::idDeclLocal( void ) :
	self( NULL ),
	name( "unnamed" ),
	textSource( NULL ),
	textLength( 0 ),
	binarySourceLength( 0 ),
	binarySource( NULL ),
	binarySourceOffset( INVALID_BINARY_SOURCE_OFFSET ),
	sourceFile( NULL ),
	sourceTextOffset( 0 ),
	sourceTextLength( 0 ),
	sourceLine( 0 ),
	checksum( 0 ),
	type( -1 ),
	declState( DS_UNPARSED ),
	index( 0 ),
	nextInFile( NULL ) {
	memset( &flags, 0, sizeof( flags ) );
}

idDeclLocal::~idDeclLocal( void ) {
	if ( self != NULL ) {
		delete self;
		self = NULL;
	}
	Mem_Free( textSource );
	textSource = NULL;
	if ( binarySource != NULL && fileSystem != NULL ) {
		fileSystem->CloseFile( binarySource );
		binarySource = NULL;
	}
}

const char* idDeclLocal::GetName( void ) const {
	return name.c_str();
}

qhandle_t idDeclLocal::GetType( void ) const {
	return type;
}

declState_t idDeclLocal::GetState( void ) const {
	return declState;
}

bool idDeclLocal::IsValid( void ) const {
	return declState != DS_UNPARSED;
}

void idDeclLocal::Invalidate( void ) {
	declState = DS_UNPARSED;
}

void idDeclLocal::InvalidateAndDiscard( void ) {
	flags.parsedOutsideLevelLoad = false;
	flags.everReferenced = false;
	flags.referencedThisLevel = false;
	declState = DS_UNPARSED;
}

void idDeclLocal::Reload( void ) {
	if ( sourceFile != NULL ) {
		sourceFile->Reload( false );
	}
}

void idDeclLocal::EnsureNotPurged( void ) {
	if ( declState == DS_UNPARSED ) {
		ParseLocal();
	}
}

void idDeclLocal::ReParse( void ) {
	declState = DS_UNPARSED;
	ParseLocal();
}

int idDeclLocal::Index( void ) const {
	return index;
}

int idDeclLocal::GetLineNum( void ) const {
	return sourceLine;
}

int idDeclLocal::GetFileOffset( void ) const {
	return sourceTextOffset;
}

int idDeclLocal::GetFileLength( void ) const {
	return sourceTextLength;
}

const char* idDeclLocal::GetFileName( void ) const {
	return sourceFile != NULL ? sourceFile->fileName.c_str() : "*implicit*";
}

void idDeclLocal::GetText( char* text ) const {
	if ( textSource == NULL ) {
		text[ 0 ] = '\0';
		return;
	}
	memcpy( text, textSource, textLength + 1 );
}

int idDeclLocal::GetTextLength( void ) const {
	return textLength;
}

void idDeclLocal::SetBinarySourceDirect( const byte* source, int length ) {
	if ( binarySource == NULL && fileSystem != NULL ) {
		binarySource = fileSystem->OpenMemoryFile( GetName() );
	}
	if ( binarySource == NULL ) {
		return;
	}

	binarySource->Clear();
	if ( length > 0 ) {
		binarySource->SetGranularity( length );
		binarySource->Write( source, length );
	}
	binarySource->MakeReadOnly();
	binarySourceLength = length;
	binarySourceOffset = INVALID_BINARY_SOURCE_OFFSET;
}

void idDeclLocal::SetBinarySource( const byte* source, int length ) {
	SetBinarySourceDirect( source, length );
}

void idDeclLocal::GetBinarySource( byte*& source, int& length ) const {
	source = NULL;
	length = 0;

	if ( binarySource != NULL && binarySourceLength > 0 ) {
		source = static_cast< byte* >( Mem_Alloc( binarySourceLength ) );
		memcpy( source, binarySource->GetDataPtr(), binarySourceLength );
		length = binarySourceLength;
		return;
	}

	if ( binarySourceOffset != INVALID_BINARY_SOURCE_OFFSET ) {
		common->Warning( "%s: external binary decl buffers are not restored yet", GetName() );
	}
}

void idDeclLocal::FreeSourceBuffer( byte* buffer ) const {
	Mem_Free( buffer );
}

bool idDeclLocal::HasBinaryBuffer( void ) const {
	return binarySourceOffset != INVALID_BINARY_SOURCE_OFFSET || binarySource != NULL;
}

void idDeclLocal::SetText( const char* text ) {
	if ( text == NULL ) {
		SetTextLocal( "", 0, true );
	} else {
		SetTextLocal( text, idStr::Length( text ), true );
	}
}

void idDeclLocal::SetTextDirect( const char* text, const int length ) {
	SetTextLocal( text, length, false );
}

void idDeclLocal::SetTextLocal( const char* text, const int length, bool updateChecksum ) {
	Mem_Free( textSource );
	textSource = static_cast< char* >( Mem_Alloc( length + 1 ) );
	if ( length > 0 ) {
		memcpy( textSource, text, length );
	}
	textSource[ length ] = '\0';
	textLength = length;
	if ( updateChecksum ) {
		checksum = MD5_BlockChecksum( text, length );
	}
}

bool idDeclLocal::ReplaceSourceFileText( void ) {
	if ( sourceFile == NULL || fileSystem == NULL ) {
		common->Warning( "Can't save implicit declaration '%s'", GetName() );
		return false;
	}

	const int oldFileLength = sourceFile->fileSize;
	const int newFileLength = oldFileLength - sourceTextLength + textLength;
	char* buffer = static_cast< char* >( Mem_Alloc( Max( oldFileLength, newFileLength ) + 1 ) );

	if ( oldFileLength > 0 ) {
		idFile* input = fileSystem->OpenFileRead( GetFileName() );
		if ( input == NULL ||
				input->Length() != sourceFile->fileSize ||
				input->Timestamp() != sourceFile->timestamp ) {
			if ( input != NULL ) {
				fileSystem->CloseFile( input );
			}
			Mem_Free( buffer );
			common->Warning( "The file '%s' has been modified outside the engine", GetFileName() );
			return false;
		}
		input->Read( buffer, oldFileLength );
		fileSystem->CloseFile( input );
		if ( MD5_BlockChecksum( buffer, oldFileLength ) != sourceFile->checksum ) {
			Mem_Free( buffer );
			common->Warning( "The file '%s' has been modified outside the engine", GetFileName() );
			return false;
		}
	}

	memmove(
		buffer + sourceTextOffset + textLength,
		buffer + sourceTextOffset + sourceTextLength,
		oldFileLength - sourceTextOffset - sourceTextLength
	);
	if ( textLength > 0 ) {
		memcpy( buffer + sourceTextOffset, textSource, textLength );
	}

	idFile* output = fileSystem->OpenFileWrite( GetFileName(), "fs_devpath" );
	if ( output == NULL ) {
		Mem_Free( buffer );
		common->Warning( "Couldn't open '%s' for writing", GetFileName() );
		return false;
	}
	output->Write( buffer, newFileLength );
	fileSystem->CloseFile( output );

	sourceFile->fileSize = newFileLength;
	sourceFile->checksum = MD5_BlockChecksum( buffer, newFileLength );
	fileSystem->ReadFile( GetFileName(), NULL, &sourceFile->timestamp );
	Mem_Free( buffer );

	for ( idDeclLocal* decl = sourceFile->decls; decl != NULL; decl = decl->nextInFile ) {
		if ( decl != this && decl->sourceTextOffset > sourceTextOffset ) {
			decl->sourceTextOffset += textLength - sourceTextLength;
		}
	}
	sourceTextLength = textLength;
	return true;
}

bool idDeclLocal::SourceFileChanged( void ) const {
	if ( sourceFile == NULL || sourceFile->fileSize <= 0 || fileSystem == NULL ) {
		return false;
	}

	unsigned int newTimestamp = 0;
	const int newLength = fileSystem->ReadFile( GetFileName(), NULL, &newTimestamp );
	return newLength != sourceFile->fileSize || newTimestamp != sourceFile->timestamp;
}

void idDeclLocal::MakeDefault( void ) {
	static int recursionLevel = 0;

	declManagerLocal.MediaPrint( "DEFAULTED\n" );
	declState = DS_DEFAULTED;
	AllocateSelf();
	if ( self == NULL ) {
		return;
	}

	const char* defaultText = self->DefaultDefinition();
	if ( ++recursionLevel > 100 ) {
		common->FatalError( "idDecl::MakeDefault: bad DefaultDefinition(): %s", defaultText );
	}

	self->FreeData();
	self->Parse( defaultText, idStr::Length( defaultText ) );
	--recursionLevel;
}

bool idDeclLocal::EverReferenced( void ) const {
	return flags.everReferenced != 0;
}

bool idDeclLocal::SetDefaultText( void ) {
	return false;
}

const char* idDeclLocal::DefaultDefinition( void ) const {
	return "{ }";
}

bool idDeclLocal::Parse( const char* text, const int length ) {
	if ( text == NULL ) {
		return false;
	}
	idParser src;
	src.SetFlags( DECL_LEXER_FLAGS );
	if ( !src.LoadMemory( text, length, GetFileName(), GetLineNum() ) ) {
		return false;
	}
	idToken brace;
	if ( !src.SkipUntilString( "{", &brace ) ) {
		return false;
	}
	return src.SkipBracedSection( false ) != 0;
}

void idDeclLocal::FreeData( void ) {
}

size_t idDeclLocal::Size( void ) const {
	return sizeof( *this ) + name.Allocated() + fileDependencies.Allocated();
}

void idDeclLocal::List( void ) const {
	common->Printf( "%s %s %i\n", GetName(), GetFileName(), GetLineNum() );
}

void idDeclLocal::Dot( void ) const {
	common->Printf( "\"%s:%s\";\n", declManagerLocal.GetDeclTypeName( type ), GetName() );
}

void idDeclLocal::Print( void ) const {
}

const idStrList& idDeclLocal::GetIncludeDependencies( void ) const {
	return fileDependencies;
}

const idStrList* idDeclLocal::GetFileLevelIncludeDependencies( void ) const {
	return sourceFile != NULL ? &sourceFile->includeDependencies : NULL;
}

void idDeclLocal::AddIncludeDependency( const char* file ) {
	if ( file != NULL && file[ 0 ] != '\0' ) {
		fileDependencies.AddUnique( file );
	}
}

void idDeclLocal::AllocateSelf( void ) {
	if ( self != NULL ) {
		return;
	}

	idDeclTypeInterface* declType = declManagerLocal.GetDeclType( type );
	if ( declType == NULL ) {
		common->FatalError( "idDeclLocal::AllocateSelf: invalid decl type %i", type );
		return;
	}

	self = declType->Alloc();
	if ( self == NULL ) {
		common->FatalError( "idDeclLocal::AllocateSelf: allocator for '%s' returned NULL", declType->GetName() );
		return;
	}
	self->base = this;
}

void idDeclLocal::ParseLocal( void ) {
	AllocateSelf();
	if ( self == NULL ) {
		return;
	}

	idDeclTypeInterface* declType = declManagerLocal.GetDeclType( type );
	if ( declType == NULL ) {
		MakeDefault();
		return;
	}

	self->FreeData();
	declManagerLocal.MediaPrint( "parsing %s %s\n", declType->GetName(), name.c_str() );

	bool generatedDefaultText = false;
	if ( textSource == NULL && !HasBinaryBuffer() ) {
		generatedDefaultText = self->SetDefaultText();
	}

	declManagerLocal.indent++;
	if ( textSource == NULL && !HasBinaryBuffer() ) {
		MakeDefault();
		declManagerLocal.indent--;
		return;
	}

	declState = DS_PARSED;
	bool parsed = true;
	if ( !declType->SkipParsing() ) {
		if ( HasBinaryBuffer() ) {
			parsed = self->Parse( NULL, 0 );
		} else {
			idStr expandedText;
			const char* parseText = textSource;
			int parseLength = textLength;
			// Retail only runs the template evaluator when the declaration
			// actually contains a useTemplate directive.  Besides avoiding
			// unnecessary work, this is significant because template evaluation
			// strips comments and idStr::StripComments treats URL-like values such
			// as "decl://skin" as C++ comments.
			if ( declType->AllowTemplateEvaluation() &&
				idStr::FindText( textSource, "useTemplate", false ) >= 0 ) {
				declExpandedTextSetter setter( &expandedText );
				sdFunctions::sdCallable< void( const char*, const int ) > callback( setter );
				parsed = declManagerLocal.EvaluateTemplates( self, textSource, callback, true );
				if ( parsed ) {
					parseText = expandedText.c_str();
					parseLength = expandedText.Length();
				}
			}
			if ( parsed ) {
				parsed = self->Parse( parseText, parseLength );
			}
		}
	}

	if ( !parsed ) {
		MakeDefault();
		common->Warning(
			"idDeclLocal::ParseLocal failed to parse decl '%s' in file '%s' line %i",
			GetName(),
			GetFileName(),
			GetLineNum()
		);
	}

	if ( generatedDefaultText ) {
		Mem_Free( textSource );
		textSource = NULL;
		textLength = 0;
	}

	declType->PostParse( self );
	declManagerLocal.indent--;
}

void idDeclLocal::Purge( void ) {
	if ( flags.parsedOutsideLevelLoad ) {
		return;
	}
	flags.referencedThisLevel = false;
	MakeDefault();
	declState = DS_UNPARSED;
}

/*
===============================================================================

	idDeclManagerLocal

===============================================================================
*/

idDeclManagerLocal::idDeclManagerLocal( void ) :
	binaryDataCompressor( NULL ),
	binaryTokenCompressor( NULL ),
	globalTokenCacheMemory( NULL ),
	globalTokenCacheLength( 0 ),
	checksum( 0 ),
	indent( 0 ),
	insideLevelLoad( false ),
	hasReachedLevelLoad( false ) {
	memset( builtinDeclTypeStorage, 0, sizeof( builtinDeclTypeStorage ) );
}

idDeclManagerLocal::~idDeclManagerLocal( void ) {
}

void idDeclManagerLocal::Init( void ) {
	common->Printf( "----- Initializing Decls -----\n" );

	insideLevelLoad = false;
	hasReachedLevelLoad = false;
	checksum = 0;
	indent = 0;

	binaryDataCompressor = idCompressor::AllocHuffman();
	binaryTokenCompressor = idCompressor::AllocHuffman();
	if ( fileSystem != NULL ) {
		globalTokenCacheMemory = fileSystem->OpenMemoryFile( "globalTokenCache" );
	}
	globalTokenCacheLength = 0;

	// The PDB-owned engine decl types must exist before the game DLL registers
	// callbacks and initializes its typed decl wrappers.
	Decl_RegisterBuiltinTypes( this );

	if ( cmdSystem != NULL ) {
		cmdSystem->AddCommand( "listDecls", ListDecls_f, CMD_FL_SYSTEM, "lists all decls" );
		cmdSystem->AddCommand( "reloadDecls", ReloadDecls_f, CMD_FL_SYSTEM | CMD_FL_CHEAT, "reloads decls" );
		cmdSystem->AddCommand( "reparseDecls", ReparseDecls_f, CMD_FL_SYSTEM | CMD_FL_CHEAT, "reparses decls" );
		cmdSystem->AddCommand( "touch", TouchDecl_f, CMD_FL_SYSTEM | CMD_FL_CHEAT, "touches a decl" );
	}

	common->Printf( "------------------------------\n" );
}

void idDeclManagerLocal::Shutdown( void ) {
	if ( cmdSystem != NULL ) {
		cmdSystem->RemoveCommand( "listDecls" );
		cmdSystem->RemoveCommand( "reloadDecls" );
		cmdSystem->RemoveCommand( "reparseDecls" );
		cmdSystem->RemoveCommand( "touch" );
	}

	for ( int i = 0; i < declTypes.Num(); i++ ) {
		FreeType( i );
		delete declTypes[ i ];
	}
	declTypes.Clear();
	declTypeHash.Clear();

	loadedFiles.DeleteContents( true );
	loadedFileHash.Clear();
	declFolders.DeleteContents( true );

	fileDependencies.list.DeleteContents( true );
	fileDependencies.hash.Clear();

	globalTokenCache.Clear();
	if ( globalTokenCacheMemory != NULL && fileSystem != NULL ) {
		fileSystem->CloseFile( globalTokenCacheMemory );
	}
	globalTokenCacheMemory = NULL;
	globalTokenCacheLength = 0;

	delete binaryTokenCompressor;
	binaryTokenCompressor = NULL;
	delete binaryDataCompressor;
	binaryDataCompressor = NULL;
}

void idDeclManagerLocal::Reload( bool force, const char* dir ) {
	for ( int i = 0; i < loadedFiles.Num(); i++ ) {
		if ( dir != NULL && dir[ 0 ] != '\0' &&
				idStr::IcmpnPath( loadedFiles[ i ]->fileName, dir, idStr::Length( dir ) ) != 0 ) {
			continue;
		}
		loadedFiles[ i ]->Reload( force );
	}
	checksum = GetChecksum();
}

void idDeclManagerLocal::BeginLevelLoad( void ) {
	hasReachedLevelLoad = true;
	insideLevelLoad = true;

	for ( int i = 0; i < declTypes.Num(); i++ ) {
		declTypeInfo_t* info = declTypes[ i ];
		if ( info == NULL || info->declType == NULL ) {
			continue;
		}
		for ( int j = 0; j < info->data.linearList.Num(); j++ ) {
			idDeclLocal* decl = info->data.linearList[ j ];
			if ( !decl->flags.parsedOutsideLevelLoad && decl->declState != DS_UNPARSED ) {
				decl->flags.referencedThisLevel = false;
				decl->Purge();
			}
		}
	}
}

void idDeclManagerLocal::EndLevelLoad( void ) {
	insideLevelLoad = false;
}

void idDeclManagerLocal::FinishBuild( void ) {
	// Text declarations are complete. Binary output is restored alongside
	// the missing decllib serializers so that format version 3 remains exact.
}

idTokenCache& idDeclManagerLocal::GetGlobalTokenCache( void ) {
	return globalTokenCache;
}

bool idDeclManagerLocal::HasGlobalTokenCache( void ) const {
	return globalTokenCache.Num() != 0;
}

void idDeclManagerLocal::RegisterDeclType( idDeclTypeInterface* typeInterface ) {
	if ( typeInterface == NULL ) {
		return;
	}

	for ( int i = 0; i < declTypes.Num(); i++ ) {
		declTypeInfo_t* info = declTypes[ i ];
		if ( info != NULL && info->declType == typeInterface ) {
			info->refCount++;
			typeInterface->OnRegister( i );
			return;
		}
	}

	declTypeInfo_t* info = new declTypeInfo_t;
	info->declType = typeInterface;
	info->refCount = 1;
	const int handle = declTypes.Append( info );
	declTypeHash.Add( declTypeHash.GenerateKey( typeInterface->GetName(), false ), handle );
	typeInterface->OnRegister( handle );
}

void idDeclManagerLocal::UnregisterDeclType( idDeclTypeInterface* typeInterface ) {
	if ( typeInterface == NULL ) {
		return;
	}

	for ( int i = 0; i < declTypes.Num(); i++ ) {
		declTypeInfo_t* info = declTypes[ i ];
		if ( info == NULL || info->declType != typeInterface ) {
			continue;
		}
		if ( --info->refCount <= 0 ) {
			FreeType( i );
			typeInterface->OnRegister( -1 );
		}
		return;
	}
}

void idDeclManagerLocal::RegisterDeclFolder( const char* folderName, const char* extension ) {
	if ( folderName == NULL || extension == NULL || fileSystem == NULL ) {
		return;
	}

	idDeclFolder* declFolder = NULL;
	for ( int i = 0; i < declFolders.Num(); i++ ) {
		if ( declFolders[ i ]->folder.IcmpPath( folderName ) == 0 &&
				declFolders[ i ]->extension.Icmp( extension ) == 0 ) {
			declFolder = declFolders[ i ];
			declFolder->referenceCount++;
			break;
		}
	}
	if ( declFolder == NULL ) {
		declFolder = new idDeclFolder( folderName, extension );
		declFolders.Append( declFolder );
	}

	idFileList* fileList = fileSystem->ListFilesTree( declFolder->folder, declFolder->extension, true );
	if ( fileList == NULL ) {
		return;
	}

	for ( int i = 0; i < fileList->GetNumFiles(); i++ ) {
		idStr fileName = fileList->GetFile( i );
		fileName.BackSlashesToSlashes();

		idDeclFile* file = GetFile( fileName );
		if ( file == NULL ) {
			file = new idDeclFile( fileName );
			const int fileIndex = loadedFiles.Append( file );
			loadedFileHash.Add( loadedFileHash.GenerateKey( fileName, false ), fileIndex );
		}
		file->LoadAndParse();
	}

	fileSystem->FreeFileList( fileList );
	checksum = GetChecksum();
}

void idDeclManagerLocal::UnregisterDeclFolder( const char* folderName, const char* extension ) {
	for ( int i = 0; i < declFolders.Num(); i++ ) {
		idDeclFolder* folder = declFolders[ i ];
		if ( folder->folder.IcmpPath( folderName ) != 0 || folder->extension.Icmp( extension ) != 0 ) {
			continue;
		}
		if ( --folder->referenceCount <= 0 ) {
			delete folder;
			declFolders.RemoveIndex( i );
		}
		return;
	}
}

void idDeclManagerLocal::FinishedRegistering( void ) {
	for ( int i = 0; i < declFolders.Num(); i++ ) {
		declFolders[ i ]->scannedForBinaries = true;
	}
}

int idDeclManagerLocal::GetChecksum( void ) const {
	idList< int > checksumData;
	for ( int i = 0; i < declTypes.Num(); i++ ) {
		const declTypeInfo_t* info = declTypes[ i ];
		if ( info == NULL || info->declType == NULL || info->declType->SkipChecksum() ) {
			continue;
		}
		for ( int j = 0; j < info->data.linearList.Num(); j++ ) {
			const idDeclLocal* decl = info->data.linearList[ j ];
			if ( decl->sourceFile == NULL ) {
				continue;
			}
			checksumData.Append( j );
			checksumData.Append( decl->checksum );
		}
	}
	if ( checksumData.Num() == 0 ) {
		return 0;
	}
	return MD5_BlockChecksum( checksumData.Begin(), checksumData.Num() * sizeof( checksumData[ 0 ] ) );
}

int idDeclManagerLocal::GetNumDeclTypes( void ) const {
	return declTypes.Num();
}

const idDecl* idDeclManagerLocal::FindType( qhandle_t typeHandle, const char* requestedName, bool makeDefault ) {
	idDeclTypeInterface* typeInterface = GetDeclType( typeHandle );
	if ( typeInterface == NULL ) {
		return NULL;
	}

	const char* name = requestedName;
	if ( name == NULL || name[ 0 ] == '\0' ) {
		if ( !makeDefault ) {
			return NULL;
		}
		common->Warning(
			"idDeclManager::FindType: supplied an empty name for type '%s' while expecting a default",
			typeInterface->GetName()
		);
		name = "_emptyName";
	}

	idDeclLocal* decl = FindTypeWithoutParsing( typeInterface, name, makeDefault );
	if ( decl == NULL ) {
		return NULL;
	}

	decl->AllocateSelf();
	if ( decl->declState == DS_UNPARSED ) {
		decl->ParseLocal();
		decl->flags.parsedOutsideLevelLoad = !insideLevelLoad;
	}
	decl->flags.everReferenced = true;
	decl->flags.referencedThisLevel = true;
	return decl->self;
}

bool idDeclManagerLocal::TypeExists( qhandle_t typeHandle ) {
	return DeclHandleIsValid( typeHandle );
}

int idDeclManagerLocal::GetNumDecls( qhandle_t typeHandle ) {
	if ( !DeclHandleIsValid( typeHandle ) ) {
		return 0;
	}
	return declTypes[ typeHandle ]->data.linearList.Num();
}

const idDecl* idDeclManagerLocal::DeclByIndex( qhandle_t typeHandle, int declIndex, bool forceParse ) {
	if ( !DeclHandleIsValid( typeHandle ) ) {
		return NULL;
	}

	declTypeData_t& data = declTypes[ typeHandle ]->data;
	if ( declIndex < 0 || declIndex >= data.linearList.Num() ) {
		common->Warning( "idDeclManager::DeclByIndex: index %i out of range for '%s'", declIndex, GetDeclTypeName( typeHandle ) );
		return NULL;
	}

	idDeclLocal* decl = data.linearList[ declIndex ];
	decl->AllocateSelf();
	if ( forceParse && decl->declState == DS_UNPARSED ) {
		decl->ParseLocal();
	}
	return decl->self;
}

void idDeclManagerLocal::DotType( const idCmdArgs& args, const char* typeName ) {
	idDeclTypeInterface* typeInterface = GetDeclType( typeName );
	if ( typeInterface == NULL ) {
		common->Printf( "Invalid decl type '%s'\n", typeName );
		return;
	}

	const bool all = args.Argc() > 1 && idStr::Icmp( args.Argv( 1 ), "all" ) == 0;
	declTypeData_t& data = declTypes[ typeInterface->GetHandle() ]->data;
	common->Printf( "digraph G {\n" );
	for ( int i = 0; i < data.linearList.Num(); i++ ) {
		idDeclLocal* decl = data.linearList[ i ];
		if ( all || decl->flags.referencedThisLevel ) {
			decl->AllocateSelf();
			decl->self->Dot();
		}
	}
	common->Printf( "}\n" );
}

void idDeclManagerLocal::ListType( const idCmdArgs& args, const char* typeName ) {
	idDeclTypeInterface* typeInterface = GetDeclType( typeName );
	if ( typeInterface == NULL ) {
		common->Printf( "Invalid decl type '%s'\n", typeName );
		return;
	}

	const char* option = args.Argc() > 1 ? args.Argv( 1 ) : "";
	const bool all = idStr::Icmp( option, "all" ) == 0 || idStr::Icmp( option, "reparseall" ) == 0;
	const bool ever = idStr::Icmp( option, "ever" ) == 0;
	const bool reparse = idStr::Icmp( option, "reparseall" ) == 0;
	declTypeData_t& data = declTypes[ typeInterface->GetHandle() ]->data;

	common->Printf( "--------------------\n" );
	int printed = 0;
	for ( int i = 0; i < data.linearList.Num(); i++ ) {
		idDeclLocal* decl = data.linearList[ i ];
		if ( reparse ) {
			decl->ReParse();
		}
		if ( !all && decl->declState == DS_UNPARSED ) {
			continue;
		}
		if ( !all && !( ever ? decl->flags.everReferenced : decl->flags.referencedThisLevel ) ) {
			continue;
		}

		common->Printf(
			"%c%c%4i: ",
			decl->flags.referencedThisLevel ? '*' : ( decl->flags.everReferenced ? '.' : ' ' ),
			decl->declState == DS_DEFAULTED ? 'D' : ' ',
			decl->index
		);
		if ( decl->declState == DS_UNPARSED ) {
			common->Printf( "%s %s line %i\n", decl->GetName(), decl->GetFileName(), decl->GetLineNum() );
		} else {
			decl->self->List();
		}
		printed++;
	}
	common->Printf( "--------------------\n" );
	common->Printf( "%i of %i %s\n", printed, data.linearList.Num(), typeInterface->GetName() );
}

void idDeclManagerLocal::PrintType( const idCmdArgs& args, const char* typeName ) {
	if ( args.Argc() < 2 ) {
		common->Printf( "USAGE: print<decl type> <decl name>\n" );
		return;
	}

	idDeclTypeInterface* typeInterface = GetDeclType( typeName );
	if ( typeInterface == NULL ) {
		common->Printf( "Invalid decl type '%s'\n", typeName );
		return;
	}

	idDeclLocal* decl = FindTypeWithoutParsing( typeInterface, args.Argv( 1 ), false );
	if ( decl == NULL ) {
		common->Printf( "%s '%s' not found.\n", typeInterface->GetName(), args.Argv( 1 ) );
		return;
	}

	common->Printf( "%s %s:\n", typeInterface->GetName(), decl->GetName() );
	common->Printf( "source: %s:%i\n----------\n", decl->GetFileName(), decl->GetLineNum() );
	common->Printf( "%s\n----------\n", decl->textSource != NULL ? decl->textSource : "NO SOURCE" );
	if ( decl->declState == DS_UNPARSED ) {
		common->Printf( "Unparsed.\n" );
	} else if ( decl->declState == DS_DEFAULTED ) {
		common->Printf( "<DEFAULTED>\n" );
	} else {
		common->Printf( "Parsed.\n" );
	}
	if ( decl->self != NULL ) {
		decl->self->Print();
	}
}

idDecl* idDeclManagerLocal::CreateNewDecl( qhandle_t typeHandle, const char* requestedName, const char* requestedFileName ) {
	idDeclTypeInterface* typeInterface = GetDeclType( typeHandle );
	if ( typeInterface == NULL || requestedName == NULL || requestedFileName == NULL ) {
		return NULL;
	}

	idDeclLocal* existing = FindTypeWithoutParsing( typeInterface, requestedName, false );
	if ( existing != NULL ) {
		existing->AllocateSelf();
		return existing->self;
	}

	char canonicalName[ MAX_STRING_CHARS ];
	MakeNameCanonical( requestedName, canonicalName, sizeof( canonicalName ) );

	idStr fileName = requestedFileName;
	fileName.BackSlashesToSlashes();
	idDeclFile* file = GetFile( fileName );
	if ( file == NULL ) {
		file = new idDeclFile( fileName );
		const int fileIndex = loadedFiles.Append( file );
		loadedFileHash.Add( loadedFileHash.GenerateKey( fileName, false ), fileIndex );
	}

	idDeclLocal* decl = FindTypeWithoutParsing( typeInterface, canonicalName, true );
	decl->AllocateSelf();

	const idStr definition = decl->self->DefaultDefinition();
	idStr declText = typeInterface->GetName();
	declText += " ";
	declText += canonicalName;
	declText += " ";
	declText += definition;

	decl->SetTextLocal( declText, declText.Length(), true );
	decl->sourceFile = file;
	decl->sourceTextOffset = file->fileSize;
	decl->sourceTextLength = 0;
	decl->sourceLine = file->numLines;
	decl->nextInFile = file->decls;
	file->decls = decl;
	decl->ParseLocal();
	return decl->self;
}

void idDeclManagerLocal::MediaPrint( const char* fmt, ... ) {
	const int show = decl_show.GetInteger();
	if ( show == 0 || ( show > 2 && insideLevelLoad ) ) {
		return;
	}

	for ( int i = 0; i < indent; i++ ) {
		common->Printf( "    " );
	}

	va_list args;
	va_start( args, fmt );
	char buffer[ 1024 ];
	idStr::vsnPrintf( buffer, sizeof( buffer ), fmt, args );
	va_end( args );
	buffer[ sizeof( buffer ) - 1 ] = '\0';
	common->Printf( "%s", buffer );
}

void idDeclManagerLocal::WritePrecacheCommands( idFile* f ) {
	if ( f == NULL ) {
		return;
	}
	for ( int i = 0; i < declTypes.Num(); i++ ) {
		declTypeInfo_t* info = declTypes[ i ];
		if ( info == NULL || info->declType == NULL ) {
			continue;
		}
		for ( int j = 0; j < info->data.linearList.Num(); j++ ) {
			idDeclLocal* decl = info->data.linearList[ j ];
			if ( decl->flags.referencedThisLevel ) {
				f->Printf( "touch %s %s\n", info->declType->GetName(), decl->GetName() );
			}
		}
	}
}

void idDeclManagerLocal::CacheFromDict( const idDict& dict ) {
	for ( int i = 0; i < declTypes.Num(); i++ ) {
		declTypeInfo_t* info = declTypes[ i ];
		if ( info != NULL && info->declType != NULL ) {
			info->declType->CacheFromDict( dict );
		}
	}
}

idDeclTypeInterface* idDeclManagerLocal::GetDeclType( const char* typeName ) const {
	if ( typeName == NULL ) {
		return NULL;
	}
	const int hash = declTypeHash.GenerateKey( typeName, false );
	for ( int i = declTypeHash.GetFirst( hash ); i != idHashIndex::NULL_INDEX; i = declTypeHash.GetNext( i ) ) {
		declTypeInfo_t* info = declTypes[ i ];
		if ( info != NULL && info->declType != NULL && idStr::Icmp( info->declType->GetName(), typeName ) == 0 ) {
			return info->declType;
		}
	}
	return NULL;
}

idDeclTypeInterface* idDeclManagerLocal::GetDeclType( qhandle_t typeHandle ) const {
	return DeclHandleIsValid( typeHandle ) ? declTypes[ typeHandle ]->declType : NULL;
}

qhandle_t idDeclManagerLocal::GetDeclTypeHandle( const char* typeName ) const {
	idDeclTypeInterface* typeInterface = GetDeclType( typeName );
	return typeInterface != NULL ? typeInterface->GetHandle() : -1;
}

const char* idDeclManagerLocal::GetDeclTypeName( qhandle_t typeHandle ) const {
	idDeclTypeInterface* typeInterface = GetDeclType( typeHandle );
	return typeInterface != NULL ? typeInterface->GetName() : "Unknown";
}

void idDeclManagerLocal::AddDependency( const idDecl* decl, const idDecl* dependency ) {
	if ( decl == NULL || dependency == NULL ) {
		common->Warning( "idDeclManagerLocal::AddDependency: NULL declaration" );
		return;
	}
	AddDependency( decl, dependency->GetFileName() );
}

void idDeclManagerLocal::AddDependency( const idDecl* decl, const char* fileName ) {
	if ( decl == NULL || fileName == NULL ) {
		return;
	}

	idDeclLocal* local = static_cast< idDeclLocal* >( decl->base );
	idDeclFile* declFile = local->sourceFile;
	if ( declFile == NULL || declFile->fileName.IcmpPath( fileName ) == 0 ) {
		return;
	}

	declFileDependency_t* dependency = GetDependency( fileName, true );
	if ( dependency != NULL ) {
		declFile->dependencies.AddUnique( dependency );
	}
}

void idDeclManagerLocal::AddDependencies( const idDecl* decl, const idParser& parser ) {
	int dependencyIndex = 0;
	const char* dependencyName = NULL;
	while ( ( dependencyName = parser.GetNextDependency( dependencyIndex ) ) != NULL ) {
		AddDependency( decl, dependencyName );
	}
}

bool idDeclManagerLocal::EvaluateTemplates(
	idDecl* decl,
	const char* srcText,
	sdFunctions::sdCallable< void( const char*, const int ) > setTextFunc,
	bool stripComments ) {
	if ( srcText == NULL || !setTextFunc.IsValid() ) {
		return false;
	}

	idStr source = srcText;
	if ( stripComments ) {
		source.StripComments();
	}

	const int templateType = GetDeclTypeHandle( declTemplateIdentifier );
	if ( templateType < 0 ) {
		setTextFunc( source.c_str(), source.Length() );
		return idStr::FindText( source.c_str(), "useTemplate", false ) < 0;
	}

	const int maximumIterations = 100;
	for ( int iteration = 0; iteration < maximumIterations; iteration++ ) {
		const int start = idStr::FindText( source.c_str(), "useTemplate", false );
		if ( start < 0 ) {
			setTextFunc( source.c_str(), source.Length() );
			return true;
		}

		idLexer lexer;
		lexer.SetFlags( DECL_LEXER_FLAGS | LEXFL_NOFATALERRORS );
		if ( !lexer.LoadMemory(
				source.c_str() + start,
				source.Length() - start,
				decl != NULL ? decl->GetFileName() : "templateEvaluation",
				decl != NULL ? decl->GetLineNum() : 1 ) ) {
			return false;
		}

		idToken token;
		if ( !lexer.ReadToken( &token ) || token.Icmp( "useTemplate" ) != 0 ) {
			return false;
		}
		if ( !lexer.ReadToken( &token ) ) {
			lexer.Warning( "EvaluateTemplates: missing template name" );
			return false;
		}

		const sdDeclTemplate* declTemplate = static_cast< const sdDeclTemplate* >(
			FindType( templateType, token.c_str(), false )
		);
		if ( declTemplate == NULL ) {
			lexer.Warning(
				"Could not find template \"%s\" while parsing %s",
				token.c_str(),
				decl != NULL ? decl->GetName() : "<unknown>"
			);
			return false;
		}
		if ( decl != NULL ) {
			AddDependency( decl, declTemplate );
		}
		if ( !lexer.ExpectTokenString( "<" ) ) {
			return false;
		}

		idStrList arguments;
		while ( lexer.ReadToken( &token ) ) {
			if ( token == ">" ) {
				break;
			}
			if ( token == "," ) {
				continue;
			}
			arguments.Append( token );
		}
		if ( token != ">" ) {
			lexer.Warning( "EvaluateTemplates: unexpected end of template argument list" );
			return false;
		}

		idStr evaluated;
		if ( !declTemplate->Evaluate( arguments, evaluated ) ) {
			lexer.Warning(
				"Template evaluation failed while loading '%s'",
				decl != NULL ? decl->GetName() : "<unknown>"
			);
			return false;
		}

		const int end = start + lexer.GetFileOffset();
		idStr replacement = source.Left( start );
		replacement.Append( evaluated );
		replacement.Append( source.Mid( end, source.Length() - end ) );
		source.Swap( replacement );
	}

	common->Warning(
		"EvaluateTemplates: reached max iterations while evaluating '%s'",
		decl != NULL ? decl->GetName() : "<unknown>"
	);
	return false;
}

void idDeclManagerLocal::MakeNameCanonical( const char* name, char* result, int maxLength ) {
	if ( result == NULL || maxLength <= 0 ) {
		return;
	}
	if ( name == NULL ) {
		result[ 0 ] = '\0';
		return;
	}

	int lastDot = -1;
	int i = 0;
	for ( ; i < maxLength - 1 && name[ i ] != '\0'; i++ ) {
		const char c = name[ i ];
		if ( c == '\\' ) {
			result[ i ] = '/';
		} else if ( c == '.' ) {
			lastDot = i;
			result[ i ] = c;
		} else {
			result[ i ] = idStr::ToLower( c );
		}
	}
	result[ i ] = '\0';
	if ( lastDot >= 0 ) {
		result[ lastDot ] = '\0';
	}
}

bool idDeclManagerLocal::DeclHandleIsValid( qhandle_t typeHandle ) const {
	return typeHandle >= 0 &&
		typeHandle < declTypes.Num() &&
		declTypes[ typeHandle ] != NULL &&
		declTypes[ typeHandle ]->declType != NULL;
}

void idDeclManagerLocal::FreeType( qhandle_t typeHandle ) {
	if ( !DeclHandleIsValid( typeHandle ) ) {
		return;
	}

	declTypeInfo_t* info = declTypes[ typeHandle ];
	for ( int fileIndex = 0; fileIndex < loadedFiles.Num(); fileIndex++ ) {
		idDeclLocal* previous = NULL;
		idDeclLocal* decl = loadedFiles[ fileIndex ]->decls;
		while ( decl != NULL ) {
			idDeclLocal* next = decl->nextInFile;
			if ( decl->type == typeHandle ) {
				if ( previous != NULL ) {
					previous->nextInFile = next;
				} else {
					loadedFiles[ fileIndex ]->decls = next;
				}
			} else {
				previous = decl;
			}
			decl = next;
		}
	}

	info->data.linearList.DeleteContents( true );
	info->data.hashTable.Clear();
	info->declType = NULL;
	info->refCount = 0;
}

idDeclFile* idDeclManagerLocal::GetFile( const char* fileName ) const {
	if ( fileName == NULL ) {
		return NULL;
	}
	const int hash = loadedFileHash.GenerateKey( fileName, false );
	for ( int i = loadedFileHash.GetFirst( hash ); i != idHashIndex::NULL_INDEX; i = loadedFileHash.GetNext( i ) ) {
		if ( loadedFiles[ i ]->fileName.IcmpPath( fileName ) == 0 ) {
			return loadedFiles[ i ];
		}
	}
	return NULL;
}

idDeclLocal* idDeclManagerLocal::FindTypeWithoutParsing(
	idDeclTypeInterface* typeInterface,
	const char* requestedName,
	bool makeDefault ) {
	if ( typeInterface == NULL || requestedName == NULL ) {
		return NULL;
	}

	const qhandle_t typeHandle = typeInterface->GetHandle();
	if ( !DeclHandleIsValid( typeHandle ) ) {
		return NULL;
	}

	char canonicalName[ MAX_STRING_CHARS ];
	MakeNameCanonical( requestedName, canonicalName, sizeof( canonicalName ) );

	declTypeData_t& data = declTypes[ typeHandle ]->data;
	const int hash = data.hashTable.GenerateKey( canonicalName, false );
	for ( int i = data.hashTable.GetFirst( hash ); i != idHashIndex::NULL_INDEX; i = data.hashTable.GetNext( i ) ) {
		if ( data.linearList[ i ]->name.Icmp( canonicalName ) == 0 ) {
			const int show = decl_show.GetInteger();
			if ( show == 2 || show == 4 ) {
				MediaPrint( "referencing %s %s\n", typeInterface->GetName(), requestedName );
			}
			return data.linearList[ i ];
		}
	}

	if ( !makeDefault ) {
		return NULL;
	}

	idDeclLocal* decl = new idDeclLocal;
	decl->name = canonicalName;
	decl->type = typeHandle;
	decl->index = data.linearList.Append( decl );
	data.hashTable.Add( hash, decl->index );
	return decl;
}

declFileDependency_t* idDeclManagerLocal::GetDependency( const char* fileName, bool create ) {
	const int hash = fileDependencies.hash.GenerateKey( fileName, false );
	for ( int i = fileDependencies.hash.GetFirst( hash );
			i != idHashIndex::NULL_INDEX;
			i = fileDependencies.hash.GetNext( i ) ) {
		if ( fileDependencies.list[ i ]->fileName.IcmpPath( fileName ) == 0 ) {
			return fileDependencies.list[ i ];
		}
	}

	if ( !create ) {
		return NULL;
	}

	declFileDependency_t* dependency = new declFileDependency_t( fileName );
	if ( fileSystem != NULL ) {
		dependency->timestamp = fileSystem->GetTimestamp( fileName );
	}
	const int dependencyIndex = fileDependencies.list.Append( dependency );
	fileDependencies.hash.Add( hash, dependencyIndex );
	return dependency;
}

void idDeclManagerLocal::ListDecls_f( const idCmdArgs& args ) {
	(void)args;
	int totalDecls = 0;
	int totalBytes = 0;
	for ( int i = 0; i < declManagerLocal.declTypes.Num(); i++ ) {
		declTypeInfo_t* info = declManagerLocal.declTypes[ i ];
		if ( info == NULL || info->declType == NULL ) {
			continue;
		}
		int bytes = 0;
		for ( int j = 0; j < info->data.linearList.Num(); j++ ) {
			bytes += static_cast< int >( info->data.linearList[ j ]->Size() );
			if ( info->data.linearList[ j ]->self != NULL ) {
				bytes += static_cast< int >( info->data.linearList[ j ]->self->Size() );
			}
		}
		common->Printf( "%4ik %4i %s\n", bytes >> 10, info->data.linearList.Num(), info->declType->GetName() );
		totalDecls += info->data.linearList.Num();
		totalBytes += bytes;
	}
	common->Printf(
		"%i total decls in %i decl files, %iKB in structures\n",
		totalDecls,
		declManagerLocal.loadedFiles.Num(),
		totalBytes >> 10
	);
}

void idDeclManagerLocal::ReloadDecls_f( const idCmdArgs& args ) {
	const bool force = args.Argc() > 1 && idStr::Icmp( args.Argv( 1 ), "all" ) == 0;
	declManagerLocal.Reload( force );
}

void idDeclManagerLocal::ReparseDecls_f( const idCmdArgs& args ) {
	(void)args;
	for ( int i = 0; i < declManagerLocal.declTypes.Num(); i++ ) {
		declTypeInfo_t* info = declManagerLocal.declTypes[ i ];
		if ( info == NULL || info->declType == NULL ) {
			continue;
		}
		for ( int j = 0; j < info->data.linearList.Num(); j++ ) {
			if ( info->data.linearList[ j ]->declState != DS_UNPARSED ) {
				info->data.linearList[ j ]->ReParse();
			}
		}
	}
}

void idDeclManagerLocal::TouchDecl_f( const idCmdArgs& args ) {
	if ( args.Argc() != 3 ) {
		common->Printf( "usage: touch <type> <name>\n" );
		return;
	}
	const qhandle_t handle = declManagerLocal.GetDeclTypeHandle( args.Argv( 1 ) );
	if ( handle < 0 || declManagerLocal.FindType( handle, args.Argv( 2 ), false ) == NULL ) {
		common->Printf( "%s '%s' not found\n", args.Argv( 1 ), args.Argv( 2 ) );
	}
}
