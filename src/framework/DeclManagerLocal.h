// Copyright (C) 2007 Id Software, Inc.
//
// Private declaration-manager types reconstructed from the ETQW PDB.

#ifndef __DECLMANAGERLOCAL_H__
#define __DECLMANAGERLOCAL_H__

#include "DeclManager.h"

class idDeclManagerLocal;

/*
================
declFileDependency_t
================
*/
class declFileDependency_t {
public:
							declFileDependency_t( void );
	explicit				declFileDependency_t( const char* name );

	idStr					fileName;
	unsigned int			timestamp;
	bool					dirty;
};

/*
================
idDeclFolder
================
*/
class idDeclFolder {
public:
							idDeclFolder( const char* folderName, const char* fileExtension );

	idStr					folder;
	idStr					extension;
	int						referenceCount;
	bool					scannedForBinaries;
};

class idDeclFile;

/*
================
idDeclLocal
================
*/
class idDeclLocal : public idDeclBase {
	friend class idDeclFile;
	friend class idDeclManagerLocal;

public:
							idDeclLocal( void );
	virtual					~idDeclLocal( void );

	virtual const char*		GetName( void ) const;
	virtual qhandle_t		GetType( void ) const;
	virtual declState_t		GetState( void ) const;
	virtual bool			IsValid( void ) const;
	virtual void			Invalidate( void );
	virtual void			Reload( void );
	virtual void			EnsureNotPurged( void );
	virtual void			ReParse( void );
	virtual int				Index( void ) const;
	virtual int				GetLineNum( void ) const;
	virtual int				GetFileOffset( void ) const;
	virtual int				GetFileLength( void ) const;
	virtual const char*		GetFileName( void ) const;
	virtual void			GetText( char* text ) const;
	virtual int				GetTextLength( void ) const;
	virtual void			SetBinarySource( const byte* source, int length );
	virtual void			GetBinarySource( byte*& source, int& length ) const;
	virtual void			FreeSourceBuffer( byte* buffer ) const;
	virtual bool			HasBinaryBuffer( void ) const;
	virtual void			SetText( const char* text );
	virtual bool			ReplaceSourceFileText( void );
	virtual bool			SourceFileChanged( void ) const;
	virtual void			MakeDefault( void );
	virtual bool			EverReferenced( void ) const;
	virtual bool			SetDefaultText( void );
	virtual const char*		DefaultDefinition( void ) const;
	virtual bool			Parse( const char* text, const int textLength );
	virtual void			FreeData( void );
	virtual size_t			Size( void ) const;
	virtual void			List( void ) const;
	virtual void			Dot( void ) const;
	virtual void			Print( void ) const;
	virtual void			InvalidateAndDiscard( void );
	virtual const idStrList&	GetIncludeDependencies( void ) const;
	virtual const idStrList*	GetFileLevelIncludeDependencies( void ) const;

private:
	void					AddIncludeDependency( const char* file );
	void					SetBinarySourceDirect( const byte* source, int length );
	void					AllocateSelf( void );
	void					ParseLocal( void );
	void					Purge( void );
	void					SetTextDirect( const char* text, const int length );
	void					SetTextLocal( const char* text, const int length, bool updateChecksum );

private:
	idDecl*					self;
	idStr					name;
	char*					textSource;
	int						textLength;
	int						binarySourceLength;
	idFile_Memory*			binarySource;
	unsigned int			binarySourceOffset;
	idDeclFile*				sourceFile;
	int						sourceTextOffset;
	int						sourceTextLength;
	int						sourceLine;
	int						checksum;
	qhandle_t				type;
	declState_t				declState;
	int						index;
	idStrList				fileDependencies;

	struct declFlags_t {
		unsigned int		parsedOutsideLevelLoad	: 1;
		unsigned int		everReferenced			: 1;
		unsigned int		referencedThisLevel		: 1;
		unsigned int		redefinedInReload		: 1;
		unsigned int		reserved				: 28;
	} flags;

	idDeclLocal*			nextInFile;
};

/*
================
idDeclFile
================
*/
class idDeclFile {
	friend class idDeclLocal;
	friend class idDeclManagerLocal;

public:
	explicit				idDeclFile( const char* fileName );
							~idDeclFile( void );

	void					Reload( bool force );
	int						LoadAndParse( void );
	const idStrList&		GetIncludeDependencies( void ) const;
	void					MakeBinaryFilename( void );

private:
	int						LoadAndParseBinary( idFile* binaryFile );

public:
	idStr					fileName;
	idStr					binaryFileName;
	unsigned int			timestamp;
	int						checksum;
	int						fileSize;
	int						numLines;
	bool					isBinary;
	idDeclLocal*			decls;
	idListGranularityOne< declFileDependency_t* > dependencies;
	idStrList				includeDependencies;
};

/*
================
idDeclManagerLocal
================
*/
class idDeclManagerLocal : public idDeclManager {
	friend class idDeclFile;
	friend class idDeclLocal;

public:
	struct declTypeData_t {
		idHashIndex			hashTable;
		idList< idDeclLocal* > linearList;
	};

	struct declTypeInfo_t {
							declTypeInfo_t( void );
		idDeclTypeInterface*	declType;
		declTypeData_t		data;
		int					refCount;
	};

	struct declFileDependencies_t {
		idHashIndex			hash;
		idListGranularityOne< declFileDependency_t* > list;
	};

public:
							idDeclManagerLocal( void );
	virtual					~idDeclManagerLocal( void );

	virtual void			Init( void );
	virtual void			Shutdown( void );
	virtual void			Reload( bool force, const char* dir = NULL );
	virtual void			BeginLevelLoad( void );
	virtual void			EndLevelLoad( void );
	virtual void			FinishBuild( void );
	virtual idTokenCache&	GetGlobalTokenCache( void );
	virtual bool			HasGlobalTokenCache( void ) const;
	virtual void			RegisterDeclType( idDeclTypeInterface* type );
	virtual void			UnregisterDeclType( idDeclTypeInterface* type );
	virtual void			RegisterDeclFolder( const char* folder, const char* extension );
	virtual void			UnregisterDeclFolder( const char* folder, const char* extension );
	virtual void			FinishedRegistering( void );
	virtual int				GetChecksum( void ) const;
	virtual int				GetNumDeclTypes( void ) const;
	virtual const idDecl*	FindType( qhandle_t typeHandle, const char* name, bool makeDefault = true );
	virtual bool			TypeExists( qhandle_t typeHandle );
	virtual int				GetNumDecls( qhandle_t typeHandle );
	virtual const idDecl*	DeclByIndex( qhandle_t typeHandle, int index, bool forceParse = true );
	virtual void			DotType( const idCmdArgs& args, const char* typeName );
	virtual void			ListType( const idCmdArgs& args, const char* typeName );
	virtual void			PrintType( const idCmdArgs& args, const char* typeName );
	virtual idDecl*			CreateNewDecl( qhandle_t typeHandle, const char* name, const char* fileName );
	virtual void			MediaPrint( const char* fmt, ... );
	virtual void			WritePrecacheCommands( idFile* f );
	virtual void			CacheFromDict( const idDict& dict );
	virtual idDeclTypeInterface* GetDeclType( const char* typeName ) const;
	virtual idDeclTypeInterface* GetDeclType( qhandle_t typeHandle ) const;
	virtual qhandle_t		GetDeclTypeHandle( const char* typeName ) const;
	virtual const char*		GetDeclTypeName( qhandle_t typeHandle ) const;
	virtual void			AddDependency( const idDecl* decl, const idDecl* dependency );
	virtual void			AddDependency( const idDecl* decl, const char* fileName );
	virtual void			AddDependencies( const idDecl* decl, const idParser& parser );
	virtual bool			EvaluateTemplates(
								idDecl* decl,
								const char* srcText,
								sdFunctions::sdCallable< void( const char*, const int ) > setTextFunc,
								bool stripComments = true );

	static void				MakeNameCanonical( const char* name, char* result, int maxLength );

private:
	bool					DeclHandleIsValid( qhandle_t typeHandle ) const;
	void					FreeType( qhandle_t typeHandle );
	idDeclFile*				GetFile( const char* fileName ) const;
	idDeclLocal*			FindTypeWithoutParsing(
								idDeclTypeInterface* type,
								const char* name,
								bool makeDefault );
	declFileDependency_t*	GetDependency( const char* fileName, bool create );

	static void				ListDecls_f( const idCmdArgs& args );
	static void				ReloadDecls_f( const idCmdArgs& args );
	static void				ReparseDecls_f( const idCmdArgs& args );
	static void				TouchDecl_f( const idCmdArgs& args );

private:
	idList< declTypeInfo_t* > declTypes;
	idHashIndex				declTypeHash;
	idList< idDeclFolder* > declFolders;
	idList< idDeclFile* >	loadedFiles;
	idHashIndex				loadedFileHash;
	declFileDependencies_t	fileDependencies;
	idCompressor*			binaryDataCompressor;
	idCompressor*			binaryTokenCompressor;
	idFile_Memory*			globalTokenCacheMemory;
	int						globalTokenCacheLength;
	idTokenCache			globalTokenCache;

	// The PDB places twenty 8-byte idDeclTypeTemplate instances here.  Their
	// concrete decllib classes are being restored in their own PDB source
	// files; retaining the space keeps this private class ABI-correct while
	// that work is in progress.
	byte					builtinDeclTypeStorage[ 20 * 8 ];

	int						checksum;
	int						indent;
	bool					insideLevelLoad;
	bool					hasReachedLevelLoad;

	static idCVar			decl_show;
	static idCVar			decl_usageLog;
};

#if defined( _WIN32 ) && !defined( _WIN64 )
static_assert( sizeof( declFileDependency_t ) == 0x28, "declFileDependency_t must match the ETQW PDB" );
static_assert( sizeof( idDeclFolder ) == 0x48, "idDeclFolder must match the ETQW PDB" );
static_assert( sizeof( idDeclLocal ) == 0x74, "idDeclLocal must match the ETQW PDB" );
static_assert( sizeof( idDeclFile ) == 0x78, "idDeclFile must match the ETQW PDB" );
static_assert( sizeof( idDeclManagerLocal::declTypeData_t ) == 0x28, "declTypeData_t must match the ETQW PDB" );
static_assert( sizeof( idDeclManagerLocal::declTypeInfo_t ) == 0x30, "declTypeInfo_t must match the ETQW PDB" );
static_assert( sizeof( idDeclManagerLocal ) == 0x170, "idDeclManagerLocal must match the ETQW PDB" );
#endif

extern idDeclManagerLocal declManagerLocal;

#endif /* !__DECLMANAGERLOCAL_H__ */
