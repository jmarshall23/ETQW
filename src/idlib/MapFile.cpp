// Copyright (C) 2007 Id Software, Inc.
//

#include "precompiled.h"
#pragma hdrstop

/*
===============
FloatCRC
===============
*/
ID_INLINE unsigned int FloatCRC( float f ) {
	return *(unsigned int *)&f;
}

/*
===============
StringCRC
===============
*/
ID_INLINE unsigned int StringCRC( const char *str ) {
	unsigned int i, crc;
	const unsigned char *ptr;

	crc = 0;
	ptr = reinterpret_cast<const unsigned char*>(str);
	for ( i = 0; str[i]; i++ ) {
		crc ^= str[i] << (i & 3);
	}
	return crc;
}

/*
=================
ComputeAxisBase

WARNING : special case behaviour of atan2(y,x) <-> atan(y/x) might not be the same everywhere when x == 0
rotation by (0,RotY,RotZ) assigns X to normal
=================
*/
static void ComputeAxisBase( const idVec3 &normal, idVec3 &texS, idVec3 &texT ) {
	float RotY, RotZ;
	idVec3 n;

	// do some cleaning
	n[0] = ( idMath::Fabs( normal[0] ) < 1e-6f ) ? 0.0f : normal[0];
	n[1] = ( idMath::Fabs( normal[1] ) < 1e-6f ) ? 0.0f : normal[1];
	n[2] = ( idMath::Fabs( normal[2] ) < 1e-6f ) ? 0.0f : normal[2];

	RotY = -atan2( n[2], idMath::Sqrt( n[1] * n[1] + n[0] * n[0]) );
	RotZ = atan2( n[1], n[0] );

	// rotate (0,1,0) and (0,0,1) to compute texS and texT
	texS[0] = -sin( RotZ );
	texS[1] = cos( RotZ );
	texS[2] = 0;
	// the texT vector is along -Z ( T texture coorinates axis )
	texT[0] = -sin( RotY ) * cos( RotZ );
	texT[1] = -sin( RotY ) * sin( RotZ );
	texT[2] = -cos( RotY );
}

/*
=================
idMapBrushSide::GetTextureVectors
=================
*/
void idMapBrushSide::GetTextureVectors( idVec4 v[2] ) const {
	int i;
	idVec3 texX, texY;

	ComputeAxisBase( plane.Normal(), texX, texY );
	for ( i = 0; i < 2; i++ ) {
		v[i][0] = texX[0] * texMat[i][0] + texY[0] * texMat[i][1];
		v[i][1] = texX[1] * texMat[i][0] + texY[1] * texMat[i][1];
		v[i][2] = texX[2] * texMat[i][0] + texY[2] * texMat[i][1];
		v[i][3] = texMat[i][2] + ( origin * v[i].ToVec3() );
	}
}

/*
=================
idMapPatch::Parse
=================
*/
idMapPatch *idMapPatch::Parse( idLexer &src, const idVec3 &origin, bool patchDef3, float version ) {
	float		info[7];
	idDrawVert *vert;
	idToken		token;
	int			i, j;

	if ( !src.ExpectTokenString( "{" ) ) {
		return NULL;
	}

	// read the material (we had an implicit 'textures/' in the old format...)
	if ( !src.ReadToken( &token ) ) {
		src.Error( "idMapPatch::Parse: unexpected EOF" );
		return NULL;
	}

	// Parse it
	if (patchDef3) {
		if ( !src.Parse1DMatrix( 7, info ) ) {
			src.Error( "idMapPatch::Parse: unable to Parse patchDef3 info" );
			return NULL;
		}
	} else {
		if ( !src.Parse1DMatrix( 5, info ) ) {
			src.Error( "idMapPatch::Parse: unable to parse patchDef2 info" );
			return NULL;
		}
	}

	idMapPatch *patch = new idMapPatch( info[0], info[1] );
	patch->SetSize( info[0], info[1] );

	patch->SetMaterial( token );

	if ( patchDef3 ) {
		patch->SetHorzSubdivisions( info[2] );
		patch->SetVertSubdivisions( info[3] );
		patch->SetExplicitlySubdivided( true );
	}

	if ( patch->GetWidth() < 0 || patch->GetHeight() < 0 ) {
		src.Error( "idMapPatch::Parse: bad size" );
		delete patch;
		return NULL;
	}

	// these were written out in the wrong order, IMHO
	if ( !src.ExpectTokenString( "(" ) ) {
		src.Error( "idMapPatch::Parse: bad patch vertex data" );
		delete patch;
		return NULL;
	}
	for ( j = 0; j < patch->GetWidth(); j++ ) {
		if ( !src.ExpectTokenString( "(" ) ) {
			src.Error( "idMapPatch::Parse: bad vertex row data" );
			delete patch;
			return NULL;
		}
		for ( i = 0; i < patch->GetHeight(); i++ ) {
			float v[5];

			if ( !src.Parse1DMatrix( 5, v ) ) {
				src.Error( "idMapPatch::Parse: bad vertex column data" );
				delete patch;
				return NULL;
			}

			vert = &((*patch)[i * patch->GetWidth() + j]);
			vert->xyz[0] = v[0] - origin[0];
			vert->xyz[1] = v[1] - origin[1];
			vert->xyz[2] = v[2] - origin[2];
			vert->SetST( v[3], v[4] );
		}
		if ( !src.ExpectTokenString( ")" ) ) {
			delete patch;
			src.Error( "idMapPatch::Parse: unable to parse patch control points" );
			return NULL;
		}
	}
	if ( !src.ExpectTokenString( ")" ) ) {
		src.Error( "idMapPatch::Parse: unable to parse patch control points, no closure" );
		delete patch;
		return NULL;
	}

	//-------------------------------------------------------------------------
	// here we may have to jump over brush epairs ( only used in editor )
	src.ReadToken( &token );
	do {
		// the token should be a key string for a key/value pair
		if ( token.type != TT_STRING ) {
			break;
		}

		idStr key = token;

		if ( !src.ReadTokenOnLine( &token ) || token.type != TT_STRING ) {
			src.Error( "idMapPatch::Parse: expected epair value string not found" );
			return NULL;
		}

		patch->epairs.Set( key, token );

		// try to read the next key
		if ( !src.ReadToken( &token ) ) {
			src.Error( "idMapPatch::Parse: unexpected EOF" );
		}
	} while (1);

	src.UnreadToken( &token );
	//-------------------------------------------------------------------------

	if ( !src.ExpectTokenString( "}" ) || !src.ExpectTokenString( "}" ) ) {
		src.Error( "idMapPatch::Parse: unable to parse patch control points, no closure" );
		delete patch;
		return NULL;
	}

	return patch;
}

/*
============
idMapPatch::Write
============
*/
bool idMapPatch::Write( idStr& buffer, int primitiveNum, const idVec3 &origin ) const {
	int i, j;
	const idDrawVert *v;
	idVec2 st;

	if ( GetExplicitlySubdivided() ) {
		buffer += va( "// primitive %d\n{\n patchDef3\n {\n", primitiveNum );
		buffer += va( "  \"%s\"\n", GetMaterial());
		buffer += va( "  ( %d %d %d %d 0 0 0 )\n", GetWidth(), GetHeight(), GetHorzSubdivisions(), GetVertSubdivisions());
	} else {
		buffer += va( "// primitive %d\n{\n patchDef2\n {\n", primitiveNum );
		buffer += va( "  \"%s\"\n", GetMaterial());
		buffer += va( "  ( %d %d 0 0 0 )\n",  GetWidth(), GetHeight());
	}

	buffer += va( "  (\n" );
	for ( i = 0; i < GetWidth(); i++ ) {
		buffer += va( "   ( " );
		for ( j = 0; j < GetHeight(); j++ ) {
			v = &verts[ j * GetWidth() + i ];
			st = v->GetST();
			buffer += va( " ( %f %f %f %f %f )", v->xyz[0] + origin[0],
				v->xyz[1] + origin[1], v->xyz[2] + origin[2], st[0], st[1] );
		}
		buffer += va( " )\n" );
	}
	buffer += va( "  )\n" );

	// write patch epairs
	for ( i = 0; i < epairs.GetNumKeyVals(); i++) {
		buffer += va( "  \"%s\" \"%s\"\n", epairs.GetKeyVal(i)->GetKey().c_str(), epairs.GetKeyVal(i)->GetValue().c_str());
	}

	buffer += va( " }\n}\n" );

	return true;
}

/*
============
idMapPatch::Write
============
*/
bool idMapPatch::Write( idFile *fp, int primitiveNum, const idVec3 &origin ) const {
	int i, j;
	const idDrawVert *v;
	idVec2 st;

	if ( GetExplicitlySubdivided() ) {
		fp->WriteFloatString( "// primitive %d\n{\n patchDef3\n {\n", primitiveNum );
		fp->WriteFloatString( "  \"%s\"\n", GetMaterial());
		fp->WriteFloatString( "  ( %d %d %d %d 0 0 0 )\n", GetWidth(), GetHeight(), GetHorzSubdivisions(), GetVertSubdivisions());
	} else {
		fp->WriteFloatString( "// primitive %d\n{\n patchDef2\n {\n", primitiveNum );
		fp->WriteFloatString( "  \"%s\"\n", GetMaterial());
		fp->WriteFloatString( "  ( %d %d 0 0 0 )\n",  GetWidth(), GetHeight());
	}

	fp->WriteFloatString( "  (\n" );
	for ( i = 0; i < GetWidth(); i++ ) {
		fp->WriteFloatString( "   ( " );
		for ( j = 0; j < GetHeight(); j++ ) {
			v = &verts[ j * GetWidth() + i ];
			st = v->GetST();
			fp->WriteFloatString( " ( %f %f %f %f %f )", v->xyz[0] + origin[0],
								v->xyz[1] + origin[1], v->xyz[2] + origin[2], st[0], st[1] );
		}
		fp->WriteFloatString( " )\n" );
	}
	fp->WriteFloatString( "  )\n" );
	
	// write patch epairs
	for ( i = 0; i < epairs.GetNumKeyVals(); i++) {
		fp->WriteFloatString( "  \"%s\" \"%s\"\n", epairs.GetKeyVal(i)->GetKey().c_str(), epairs.GetKeyVal(i)->GetValue().c_str());
	}

	fp->WriteFloatString( " }\n}\n" );

	return true;
}

/*
===============
idMapPatch::GetGeometryCRC
===============
*/
unsigned int idMapPatch::GetGeometryCRC( void ) const {
	int i, j;
	unsigned int crc;

	crc = GetHorzSubdivisions() ^ GetVertSubdivisions();
	for ( i = 0; i < GetWidth(); i++ ) {
		for ( j = 0; j < GetHeight(); j++ ) {
			crc ^= FloatCRC( verts[j * GetWidth() + i].xyz.x );
			crc ^= FloatCRC( verts[j * GetWidth() + i].xyz.y );
			crc ^= FloatCRC( verts[j * GetWidth() + i].xyz.z );
		}
	}

	crc ^= StringCRC( GetMaterial() );

	return crc;
}

/*
=================
idMapBrush::Parse
=================
*/
idMapBrush *idMapBrush::Parse( idLexer &src, const idVec3 &origin, bool newFormat, float version ) {
	int i;
	idVec3 planepts[3];
	idToken token;
	idList<idMapBrushSide*> sides;
	idMapBrushSide	*side;
	idDict epairs;

	if ( !src.ExpectTokenString( "{" ) ) {
		return NULL;
	}

	do {
		if ( !src.ReadToken( &token ) ) {
			src.Error( "idMapBrush::Parse: unexpected EOF" );
			sides.DeleteContents( true );
			return NULL;
		}
		if ( token == "}" ) {
			break;
		}

		// here we may have to jump over brush epairs ( only used in editor )
		do {
			// if token is a brace
			if ( token == "(" ) {
				break;
			}
			// the token should be a key string for a key/value pair
			if ( token.type != TT_STRING ) {
				src.Error( "idMapBrush::Parse: unexpected %s, expected ( or epair key string", token.c_str() );
				sides.DeleteContents( true );
				return NULL;
			}

			idStr key = token;

			if ( !src.ReadTokenOnLine( &token ) || token.type != TT_STRING ) {
				src.Error( "idMapBrush::Parse: expected epair value string not found" );
				sides.DeleteContents( true );
				return NULL;
			}

			epairs.Set( key, token );

			// try to read the next key
			if ( !src.ReadToken( &token ) ) {
				src.Error( "idMapBrush::Parse: unexpected EOF" );
				sides.DeleteContents( true );
				return NULL;
			}
		} while (1);

		src.UnreadToken( &token );

		side = new idMapBrushSide();
		sides.Append(side);

		if ( newFormat ) {
			if ( !src.Parse1DMatrix( 4, side->plane.ToFloatPtr() ) ) {
				src.Error( "idMapBrush::Parse: unable to read brush side plane definition" );
				sides.DeleteContents( true );
				return NULL;
			}
		} else {
			// read the three point plane definition
			if (!src.Parse1DMatrix( 3, planepts[0].ToFloatPtr() ) ||
				!src.Parse1DMatrix( 3, planepts[1].ToFloatPtr() ) ||
				!src.Parse1DMatrix( 3, planepts[2].ToFloatPtr() ) ) {
				src.Error( "idMapBrush::Parse: unable to read brush side plane definition" );
				sides.DeleteContents( true );
				return NULL;
			}

			planepts[0] -= origin;
			planepts[1] -= origin;
			planepts[2] -= origin;

			side->plane.FromPoints( planepts[0], planepts[1], planepts[2] );
		}

		// read the texture matrix
		// this is odd, because the texmat is 2D relative to default planar texture axis
		if ( !src.Parse2DMatrix( 2, 3, side->texMat[0].ToFloatPtr() ) ) {
			src.Error( "idMapBrush::Parse: unable to read brush side texture matrix" );
			sides.DeleteContents( true );
			return NULL;
		}
		side->origin = origin;
		
		// read the material
		if ( !src.ReadTokenOnLine( &token ) ) {
			src.Error( "idMapBrush::Parse: unable to read brush side material" );
			sides.DeleteContents( true );
			return NULL;
		}

		// we had an implicit 'textures/' in the old format...
		side->material = token;

		// Q2 allowed override of default flags and values, but we don't any more
		if ( src.ReadTokenOnLine( &token ) ) {
			if ( src.ReadTokenOnLine( &token ) ) {
				if ( src.ReadTokenOnLine( &token ) ) {
				}
			}
		}
	} while( 1 );

	if ( !src.ExpectTokenString( "}" ) ) {
		sides.DeleteContents( true );
		return NULL;
	}

	idMapBrush *brush = new idMapBrush();
	for ( i = 0; i < sides.Num(); i++ ) {
		brush->AddSide( sides[i] );
	}

	brush->epairs = epairs;

	return brush;
}

/*
=================
idMapBrush::ParseQ3
=================
*/
idMapBrush *idMapBrush::ParseQ3( idLexer &src, const idVec3 &origin ) {
	int i, shift[2], rotate;
	float scale[2];
	idVec3 planepts[3];
	idToken token;
	idList<idMapBrushSide*> sides;
	idMapBrushSide	*side;
	idDict epairs;

	do {
		if ( src.CheckTokenString( "}" ) ) {
			break;
		}

		side = new idMapBrushSide();
		sides.Append( side );

		// read the three point plane definition
		if (!src.Parse1DMatrix( 3, planepts[0].ToFloatPtr() ) ||
			!src.Parse1DMatrix( 3, planepts[1].ToFloatPtr() ) ||
			!src.Parse1DMatrix( 3, planepts[2].ToFloatPtr() ) ) {
			src.Error( "idMapBrush::ParseQ3: unable to read brush side plane definition" );
			sides.DeleteContents( true );
			return NULL;
		}

		planepts[0] -= origin;
		planepts[1] -= origin;
		planepts[2] -= origin;

		side->plane.FromPoints( planepts[0], planepts[1], planepts[2] );

		// read the material
		if ( !src.ReadTokenOnLine( &token ) ) {
			src.Error( "idMapBrush::ParseQ3: unable to read brush side material" );
			sides.DeleteContents( true );
			return NULL;
		}

		// we have an implicit 'textures/' in the old format
		side->material = "textures/" + token;

		// read the texture shift, rotate and scale
		shift[0] = src.ParseInt();
		shift[1] = src.ParseInt();
		rotate = src.ParseInt();
		scale[0] = src.ParseFloat();
		scale[1] = src.ParseFloat();
		side->texMat[0] = idVec3( 0.03125f, 0.0f, 0.0f );
		side->texMat[1] = idVec3( 0.0f, 0.03125f, 0.0f );
		side->origin = origin;
		
		// Q2 allowed override of default flags and values, but we don't any more
		if ( src.ReadTokenOnLine( &token ) ) {
			if ( src.ReadTokenOnLine( &token ) ) {
				if ( src.ReadTokenOnLine( &token ) ) {
				}
			}
		}
	} while( 1 );

	idMapBrush *brush = new idMapBrush();
	for ( i = 0; i < sides.Num(); i++ ) {
		brush->AddSide( sides[i] );
	}

	brush->epairs = epairs;

	return brush;
}

/*
============
idMapBrush::Write
============
*/
bool idMapBrush::Write( idStr& buffer, int primitiveNum, const idVec3 &origin ) const {
	int i;
	idMapBrushSide *side;

	buffer += va( "// primitive %d\n{\n brushDef3\n {\n", primitiveNum );

	// write brush epairs
	for ( i = 0; i < epairs.GetNumKeyVals(); i++) {
		buffer += va( "  \"%s\" \"%s\"\n", epairs.GetKeyVal(i)->GetKey().c_str(), epairs.GetKeyVal(i)->GetValue().c_str());
	}

	// write brush sides
	for ( i = 0; i < GetNumSides(); i++ ) {
		side = GetSide( i );
		buffer += va( "  ( %f %f %f %f ) ", side->plane[0], side->plane[1], side->plane[2], side->plane[3] );
		buffer += va( "( ( %f %f %f ) ( %f %f %f ) ) \"%s\" 0 0 0\n",
			side->texMat[0][0], side->texMat[0][1], side->texMat[0][2],
			side->texMat[1][0], side->texMat[1][1], side->texMat[1][2],
			side->material.c_str() );
	}

	buffer += va( " }\n}\n" );

	return true;
}

/*
============
idMapBrush::Write
============
*/
bool idMapBrush::Write( idFile *fp, int primitiveNum, const idVec3 &origin ) const {
	int i;
	idMapBrushSide *side;

	fp->WriteFloatString( "// primitive %d\n{\n brushDef3\n {\n", primitiveNum );

	// write brush epairs
	for ( i = 0; i < epairs.GetNumKeyVals(); i++) {
		fp->WriteFloatString( "  \"%s\" \"%s\"\n", epairs.GetKeyVal(i)->GetKey().c_str(), epairs.GetKeyVal(i)->GetValue().c_str());
	}

	// write brush sides
	for ( i = 0; i < GetNumSides(); i++ ) {
		side = GetSide( i );
		fp->WriteFloatString( "  ( %f %f %f %f ) ", side->plane[0], side->plane[1], side->plane[2], side->plane[3] );
		fp->WriteFloatString( "( ( %f %f %f ) ( %f %f %f ) ) \"%s\" 0 0 0\n",
							side->texMat[0][0], side->texMat[0][1], side->texMat[0][2],
								side->texMat[1][0], side->texMat[1][1], side->texMat[1][2],
									side->material.c_str() );
	}

	fp->WriteFloatString( " }\n}\n" );

	return true;
}

/*
===============
idMapBrush::GetGeometryCRC
===============
*/
unsigned int idMapBrush::GetGeometryCRC( void ) const {
	int i, j;
	idMapBrushSide *mapSide;
	unsigned int crc;

	crc = 0;
	for ( i = 0; i < GetNumSides(); i++ ) {
		mapSide = GetSide(i);
		for ( j = 0; j < 4; j++ ) {
			crc ^= FloatCRC( mapSide->GetPlane()[j] );
		}
		crc ^= StringCRC( mapSide->GetMaterial() );
	}

	return crc;
}

/*
================
idMapEntity::Parse
================
*/
idMapEntity *idMapEntity::Parse( idLexer &src, bool worldSpawn, float version ) {
	idToken	token;
	idMapEntity *mapEnt;
	idMapPatch *mapPatch;
	idMapBrush *mapBrush;
	bool worldent;
	idVec3 origin;
	double v1, v2, v3;

	if ( !src.ReadToken(&token) ) {
		return NULL;
	}

	if ( token != "{" ) {
		src.Error( "idMapEntity::Parse: { not found, found %s", token.c_str() );
		return NULL;
	}

	mapEnt = new idMapEntity();

	if ( worldSpawn ) {
		mapEnt->primitives.Resize( 1024, 256 );
	}

	origin.Zero();
	worldent = false;
	do {
		if ( !src.ReadToken(&token) ) {
			src.Error( "idMapEntity::Parse: EOF without closing brace" );
			return NULL;
		}
		if ( token == "}" ) {
			break;
		}

		if ( token == "{" ) {
			// parse a brush or patch
			if ( !src.ReadToken( &token ) ) {
				src.Error( "idMapEntity::Parse: unexpected EOF" );
				return NULL;
			}

			if ( worldent ) {
				origin.Zero();
			}

			// if is it a brush: brush, brushDef, brushDef2, brushDef3
			if ( token.Icmpn( "brush", 5 ) == 0 ) {
				mapBrush = idMapBrush::Parse( src, origin, ( !token.Icmp( "brushDef2" ) || !token.Icmp( "brushDef3" ) ), version );
				if ( !mapBrush ) {
					return NULL;
				}
				mapEnt->AddPrimitive( mapBrush );
			}
			// if is it a patch: patchDef2, patchDef3
			else if ( token.Icmpn( "patch", 5 ) == 0 ) {
				mapPatch = idMapPatch::Parse( src, origin, !token.Icmp( "patchDef3" ), version );
				if ( !mapPatch ) {
					return NULL;
				}
				mapEnt->AddPrimitive( mapPatch );
			}
			// assume it's a brush in Q3 or older style
			else {
				src.UnreadToken( &token );
				mapBrush = idMapBrush::ParseQ3( src, origin );
				if ( !mapBrush ) {
					return NULL;
				}
				mapEnt->AddPrimitive( mapBrush );
			}
		} else {
			idStr key, value;

			// parse a key / value pair
			key = token;
			src.ReadTokenOnLine( &token );
			value = token;

			// strip trailing spaces that sometimes get accidentally
			// added in the editor
			value.StripTrailingWhiteSpace();
			key.StripTrailingWhiteSpace();

			mapEnt->epairs.Set( key, value );

			if ( !idStr::Icmp( key, "origin" ) ) {
				// scanf into doubles, then assign, so it is idVec size independent
				v1 = v2 = v3 = 0;
				sscanf( value, "%lf %lf %lf", &v1, &v2, &v3 );
				origin.x = v1;
				origin.y = v2;
				origin.z = v3;
			}
			else if ( !idStr::Icmp( key, "classname" ) && !idStr::Icmp( value, "worldspawn" ) ) {
				worldent = true;
			}
		}
	} while( 1 );

	return mapEnt;
}

/*
================
idMapEntity::ParseActions

Parse and save the bot's actions
================
*/
idMapEntity *idMapEntity::ParseActions( idLexer &src ) {
	idToken	token;
	idMapEntity *mapEnt;

	if ( !src.ReadToken( &token ) ) {
		return false;
	}

	if ( token != "{" ) {
		src.Error( "idMapEntity::ParseActions: { not found, found %s", token.c_str() );
		return false;
	}

	mapEnt = new idMapEntity();

	do {
		if ( !src.ReadToken(&token) ) {
			src.Error( "idMapEntity::ParseActions: EOF without closing brace" );
			return NULL;
		}

		if ( token == "}" ) {
			break;
		}

		idStr key, value;

		// parse a key / value pair
		key = token;
		src.ReadTokenOnLine( &token );
		value = token;

		// strip trailing spaces that sometimes get accidentally
		// added in the editor
		value.StripTrailingWhiteSpace();
		key.StripTrailingWhiteSpace();

		mapEnt->epairs.Set( key, value );
	} while ( 1 );

	return mapEnt;
}

/*
============
idMapEntity::Write
============
*/
bool idMapEntity::Write( idStr& buffer, int entityNum ) const {
	int i;
	idMapPrimitive *mapPrim;
	idVec3 origin;

	buffer += va( "// entity %d\n{\n", entityNum );

	// write entity epairs
	for ( i = 0; i < epairs.GetNumKeyVals(); i++) {
		buffer += va( "\"%s\" \"%s\"\n", epairs.GetKeyVal(i)->GetKey().c_str(), epairs.GetKeyVal(i)->GetValue().c_str());
	}

	epairs.GetVector( "origin", "0 0 0", origin );

	// write pritimives
	for ( i = 0; i < GetNumPrimitives(); i++ ) {
		mapPrim = GetPrimitive( i );

		switch( mapPrim->GetType() ) {
			case idMapPrimitive::TYPE_BRUSH:
				static_cast<idMapBrush*>(mapPrim)->Write( buffer, i, origin );
				break;
			case idMapPrimitive::TYPE_PATCH:
				static_cast<idMapPatch*>(mapPrim)->Write( buffer, i, origin );
				break;
		}
	}

	buffer += va( "}\n" );

	return true;
}

/*
============
idMapEntity::Write
============
*/
bool idMapEntity::Write( idFile *fp, int entityNum ) const {
	int i;
	idMapPrimitive *mapPrim;
	idVec3 origin;

	fp->WriteFloatString( "// entity %d\n{\n", entityNum );

	// write entity epairs
	for ( i = 0; i < epairs.GetNumKeyVals(); i++) {
		fp->WriteFloatString( "\"%s\" \"%s\"\n", epairs.GetKeyVal(i)->GetKey().c_str(), epairs.GetKeyVal(i)->GetValue().c_str());
	}

	epairs.GetVector( "origin", "0 0 0", origin );

	// write pritimives
	for ( i = 0; i < GetNumPrimitives(); i++ ) {
		mapPrim = GetPrimitive( i );

		switch( mapPrim->GetType() ) {
			case idMapPrimitive::TYPE_BRUSH:
				static_cast<idMapBrush*>(mapPrim)->Write( fp, i, origin );
				break;
			case idMapPrimitive::TYPE_PATCH:
				static_cast<idMapPatch*>(mapPrim)->Write( fp, i, origin );
				break;
		}
	}

	fp->WriteFloatString( "}\n" );

	return true;
}

/*
===============
idMapEntity::RemovePrimitiveData
===============
*/
void idMapEntity::RemovePrimitiveData() {
	primitives.DeleteContents(true);
}

/*
===============
idMapEntity::GetGeometryCRC
===============
*/
unsigned int idMapEntity::GetGeometryCRC( void ) const {
	int i;
	unsigned int crc;
	idMapPrimitive	*mapPrim;

	crc = 0;
	for ( i = 0; i < GetNumPrimitives(); i++ ) {
		mapPrim = GetPrimitive( i );

		switch( mapPrim->GetType() ) {
			case idMapPrimitive::TYPE_BRUSH:
				crc ^= static_cast<idMapBrush*>(mapPrim)->GetGeometryCRC();
				break;
			case idMapPrimitive::TYPE_PATCH:
				crc ^= static_cast<idMapPatch*>(mapPrim)->GetGeometryCRC();
				break;
		}
	}

	return crc;
}

/*
===============
idMapFile::Parse
===============
*/
bool idMapFile::ParseBuffer( const idStr& buffer, const idStr& name, bool moveFuncGroups ) {
	// no string concatenation for epairs and allow path names for materials
	idLexer src( LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES );
	idToken token;
	idStr fullName;
	idMapEntity *mapEnt;
	int i, j, k;

	hasPrimitiveData = false;

	src.LoadMemory( buffer.c_str(), buffer.Length(), name.c_str() );

	version = OLD_MAP_VERSION;
	fileTime = src.GetFileTime();
	entities.DeleteContents( true );

	if ( src.CheckTokenString( "Version" ) ) {
		src.ReadTokenOnLine( &token );
		version = token.GetFloatValue();
	}

	while( 1 ) {
		mapEnt = idMapEntity::Parse( src, ( entities.Num() == 0 ), version );
		if ( !mapEnt ) {
			break;
		}
		entities.Append( mapEnt );
	}

	SetGeometryCRC();
	// if the map has a worldspawn
	if ( entities.Num() ) {

		// "removeEntities" "classname" can be set in the worldspawn to remove all entities with the given classname
		const idKeyValue *removeEntities = entities[0]->epairs.MatchPrefix( "removeEntities", NULL );
		while ( removeEntities ) {
			RemoveEntities( removeEntities->GetValue() );
			removeEntities = entities[0]->epairs.MatchPrefix( "removeEntities", removeEntities );
		}

		// "overrideMaterial" "material" can be set in the worldspawn to reset all materials
		idStr material;
		if ( entities[0]->epairs.GetString( "overrideMaterial", "", material ) ) {
			for ( i = 0; i < entities.Num(); i++ ) {
				mapEnt = entities[i];
				for ( j = 0; j < mapEnt->GetNumPrimitives(); j++ ) {
					idMapPrimitive *mapPrimitive = mapEnt->GetPrimitive( j );
					switch( mapPrimitive->GetType() ) {
						case idMapPrimitive::TYPE_BRUSH: {
							idMapBrush *mapBrush = static_cast<idMapBrush *>(mapPrimitive);
							for ( k = 0; k < mapBrush->GetNumSides(); k++ ) {
								mapBrush->GetSide( k )->SetMaterial( material );
							}
							break;
														 }
						case idMapPrimitive::TYPE_PATCH: {
							static_cast<idMapPatch *>(mapPrimitive)->SetMaterial( material );
							break;
														 }
					}
				}
			}
		}

		// force all entities to have a name key/value pair
		if ( entities[0]->epairs.GetBool( "forceEntityNames" ) ) {
			for ( i = 1; i < entities.Num(); i++ ) {
				mapEnt = entities[i];
				if ( !mapEnt->epairs.FindKey( "name" ) ) {
					mapEnt->epairs.Set( "name", va( "%s%d", mapEnt->epairs.GetString( "classname", "forcedName" ), i ) );
				}
			}
		}

		// move the primitives of any func_group entities to the worldspawn
		if ( moveFuncGroups && entities[0]->epairs.GetBool( "moveFuncGroups" ) ) {
			for ( i = 1; i < entities.Num(); i++ ) {
				mapEnt = entities[i];
				if ( idStr::Icmp( mapEnt->epairs.GetString( "classname" ), "func_group" ) == 0 ) {
					// transform primitives into worldspawn space
					idMapPrimitive *mapPrim;
					idVec3 translationVec;
					mapEnt->epairs.GetVector( "origin", "", translationVec );
					for ( j = 0; j < mapEnt->GetNumPrimitives(); j++ ) {
						mapPrim = mapEnt->GetPrimitive( j );
						if ( mapPrim->GetType() == idMapPrimitive::TYPE_PATCH ) {
							static_cast<idMapPatch*>(mapPrim)->TranslateSelf( translationVec );
						}
						if ( mapPrim->GetType() == idMapPrimitive::TYPE_BRUSH ) {
							for ( k = 0; k < static_cast<idMapBrush*>(mapPrim)->GetNumSides(); k++ ) {
								idMapBrushSide *side;
								side = static_cast<idMapBrush*>(mapPrim)->GetSide( k );

								idPlane &plane = side->GetPlane();
								plane.TranslateSelf( translationVec );

								side->TranslateSelf( -translationVec );
							}
						}
					}
					entities[0]->primitives.Append( mapEnt->primitives );
					mapEnt->primitives.Clear();
				}
			}

			// we have no need anymore for the func_group entities
			for ( i = entities.Num() - 1; i > 0; i-- ) {
				mapEnt = entities[i];
				if ( idStr::Icmp( mapEnt->epairs.GetString( "classname" ), "func_group" ) == 0 ) {
					entities.RemoveIndex( i );
				}
			}
		}
	}

	hasPrimitiveData = true;
	return true;
}

/*
===============================================================================

	Enemy Territory: Quake Wars .world source support

	The public ETQW idMapFile source only contains the older brushDef parser even
	though the editor source levels use the WorldEdit sdWorldFile container.  The
	compiler and Radiant both consume idMapFile, so translate the editable world
	container into the existing idMapEntity/idMapBrush representation here.

===============================================================================
*/

struct etqwWorldTransform_t {
	idVec3	translation;
	idMat3	rotation;
	bool	hasTranslation;
	bool	hasRotation;

	etqwWorldTransform_t() : hasTranslation( false ), hasRotation( false ) {
		translation.Zero();
		rotation.Identity();
	}
};

static bool ETQWParseWorldDictionary( idLexer &src, idDict &dictionary ) {
	if ( !src.ExpectTokenString( "{" ) ) {
		return false;
	}
	idToken key;
	while ( src.ReadToken( &key ) ) {
		if ( key == "}" ) {
			return true;
		}

		idToken valueToken;
		if ( !src.ReadToken( &valueToken ) ) {
			return false;
		}
		if ( valueToken == "(" ) {
			idStr value;
			int depth = 1;
			while ( depth > 0 && src.ReadToken( &valueToken ) ) {
				if ( valueToken == "(" ) {
					depth++;
				} else if ( valueToken == ")" ) {
					depth--;
					if ( depth == 0 ) {
						break;
					}
				}
				if ( !value.IsEmpty() ) {
					value += " ";
				}
				value += valueToken.c_str();
			}
			dictionary.Set( key.c_str(), value.c_str() );
		} else {
			dictionary.Set( key.c_str(), valueToken.c_str() );
		}
	}
	return false;
}

static bool ETQWParseWorldTransform( idLexer &src, etqwWorldTransform_t &transform ) {
	if ( !src.ExpectTokenString( "{" ) ) {
		return false;
	}
	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}
		if ( token.Icmp( "translation" ) == 0 ) {
			if ( !src.Parse1DMatrix( 3, transform.translation.ToFloatPtr() ) ) {
				return false;
			}
			transform.hasTranslation = true;
		} else if ( token.Icmp( "rotation" ) == 0 ) {
			if ( !src.Parse2DMatrix( 3, 3, transform.rotation[0].ToFloatPtr() ) ) {
				return false;
			}
			transform.hasRotation = true;
		} else {
			idToken value;
			if ( !src.ReadToken( &value ) ) {
				return false;
			}
			if ( value == "{" ) {
				src.SkipBracedSection( false );
			} else if ( value == "}" ) {
				src.UnreadToken( &value );
			}
		}
	}
	return false;
}

static bool ETQWSkipWorldValue( idLexer &src );

static bool ETQWParseWorldReference( idLexer &src,
	etqwWorldTransform_t &transform, idStr &url ) {
	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return !url.IsEmpty();
		}
		if ( token.Icmp( "sdTransform" ) == 0 ) {
			if ( !ETQWParseWorldTransform( src, transform ) ) {
				return false;
			}
		} else if ( token.Icmp( "url" ) == 0 ) {
			idToken value;
			if ( !src.ReadToken( &value ) ) {
				return false;
			}
			url = value.c_str();
		} else if ( !ETQWSkipWorldValue( src ) ) {
			return false;
		}
	}
	return false;
}

static bool ETQWReadWorldHolderBody( idLexer &src ) {
	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token.Icmp( "groups" ) == 0 ) {
			if ( !src.SkipBracedSection() ) {
				return false;
			}
			continue;
		}
		if ( token == "{" ) {
			return true;
		}
		src.UnreadToken( &token );
		return false;
	}
	return false;
}

static bool ETQWSkipWorldValue( idLexer &src ) {
	idToken value;
	if ( !src.ReadToken( &value ) ) {
		return false;
	}
	if ( value == "{" ) {
		return src.SkipBracedSection( false ) != 0;
	}
	if ( value == "(" ) {
		int depth = 1;
		while ( depth > 0 && src.ReadToken( &value ) ) {
			if ( value == "(" ) depth++;
			else if ( value == ")" ) depth--;
		}
	}
	return true;
}

static bool ETQWParseWorldFaces( idLexer &src, idMapBrush &brush ) {
	if ( !src.ExpectTokenString( "{" ) ) {
		idLib::common->Printf( "ETQW world parse: faces missing opening brace\n" );
		return false;
	}
	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			return true;
		}
		if ( token.Icmp( "plane" ) != 0 ) {
			if ( !ETQWSkipWorldValue( src ) ) {
				return false;
			}
			continue;
		}

		idMapBrushSide *side = new idMapBrushSide();
		idPlane plane;
		idVec3 textureMatrix[2];
		float textureBounds[4];
		if ( !src.Parse1DMatrix( 4, plane.ToFloatPtr() ) ||
			!src.ExpectTokenString( "matrix" ) ||
			!src.Parse2DMatrix( 2, 3, textureMatrix[0].ToFloatPtr() ) ||
			!src.ExpectTokenString( "texBounds" ) ||
			!src.Parse2DMatrix( 2, 2, textureBounds ) ) {
			idLib::common->Printf( "ETQW world parse: invalid brush face near line %d\n", src.GetLineNum() );
			delete side;
			return false;
		}
		idToken material;
		if ( !src.ReadTokenOnLine( &material ) ) {
			idLib::common->Printf( "ETQW world parse: brush face has no material near line %d\n", src.GetLineNum() );
			delete side;
			return false;
		}
		side->SetPlane( plane );
		side->SetTextureMatrix( textureMatrix );
		side->SetMaterial( material.c_str() );
		brush.AddSide( side );
	}
	return false;
}

static idMapBrush *ETQWParseWorldBrush( idLexer &src ) {
	idMapBrush *brush = new idMapBrush();
	etqwWorldTransform_t transform;
	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			for ( int sideIndex = 0; sideIndex < brush->GetNumSides(); sideIndex++ ) {
				idMapBrushSide *side = brush->GetSide( sideIndex );
				idPlane plane = side->GetPlane();
				if ( transform.hasRotation ) {
					plane.RotateSelf( vec3_origin, transform.rotation );
				}
				if ( transform.hasTranslation ) {
					plane.TranslateSelf( transform.translation );
					side->TranslateSelf( transform.translation );
				}
				side->SetPlane( plane );
			}
			return brush;
		}
		if ( token.Icmp( "sdDictionary" ) == 0 ) {
			if ( !ETQWParseWorldDictionary( src, brush->epairs ) ) {
				idLib::common->Printf( "ETQW world parse: invalid brush dictionary near line %d\n", src.GetLineNum() );
				break;
			}
		} else if ( token.Icmp( "sdTransform" ) == 0 ) {
			if ( !ETQWParseWorldTransform( src, transform ) ) {
				idLib::common->Printf( "ETQW world parse: invalid brush transform near line %d\n", src.GetLineNum() );
				break;
			}
		} else if ( token.Icmp( "faces" ) == 0 ) {
			if ( !ETQWParseWorldFaces( src, *brush ) ) {
				idLib::common->Printf( "ETQW world parse: invalid brush faces near line %d\n", src.GetLineNum() );
				break;
			}
		} else if ( !ETQWSkipWorldValue( src ) ) {
			idLib::common->Printf( "ETQW world parse: invalid brush field '%s' near line %d\n", token.c_str(), src.GetLineNum() );
			break;
		}
	}
	delete brush;
	return NULL;
}

static idMapPatch *ETQWParseWorldPatch( idLexer &src ) {
	etqwWorldTransform_t transform;
	idDict patchDictionary;
	idStr material = "_default";
	int horizontalSubdivisions = 0;
	int verticalSubdivisions = 0;
	bool explicitSubdivisions = false;
	idMapPatch *patch = NULL;
	idToken token;

	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			if ( patch == NULL ) {
				return NULL;
			}
			patch->SetMaterial( material.c_str() );
			patch->SetHorzSubdivisions( horizontalSubdivisions );
			patch->SetVertSubdivisions( verticalSubdivisions );
			patch->SetExplicitlySubdivided( explicitSubdivisions );
			patch->epairs = patchDictionary;
			for ( int vertexIndex = 0;
				vertexIndex < patch->GetWidth() * patch->GetHeight();
				++vertexIndex ) {
				idVec3 &position = ( *patch )[ vertexIndex ].xyz;
				if ( transform.hasRotation ) {
					position *= transform.rotation;
				}
				if ( transform.hasTranslation ) {
					position += transform.translation;
				}
			}
			return patch;
		}
		if ( token.Icmp( "sdDictionary" ) == 0 ) {
			if ( !ETQWParseWorldDictionary( src, patchDictionary ) ) {
				break;
			}
		} else if ( token.Icmp( "sdTransform" ) == 0 ) {
			if ( !ETQWParseWorldTransform( src, transform ) ) {
				break;
			}
		} else if ( token.Icmp( "material" ) == 0 ) {
			idToken value;
			if ( !src.ReadToken( &value ) ) {
				break;
			}
			material = value.c_str();
		} else if ( token.Icmp( "useExplicitSubdivisions" ) == 0 ) {
			idToken value;
			if ( !src.ReadToken( &value ) ) {
				break;
			}
			explicitSubdivisions = value.GetIntValue() != 0;
		} else if ( token.Icmp( "explicitSubdivisions" ) == 0 ) {
			float subdivisions[ 2 ];
			if ( !src.Parse1DMatrix( 2, subdivisions ) ) {
				break;
			}
			horizontalSubdivisions = static_cast< int >( subdivisions[ 0 ] );
			verticalSubdivisions = static_cast< int >( subdivisions[ 1 ] );
			explicitSubdivisions = true;
		} else if ( token.Icmp( "controlPoints" ) == 0 ) {
			float dimensions[ 2 ];
			if ( !src.Parse1DMatrix( 2, dimensions ) ) {
				break;
			}
			const int width = static_cast< int >( dimensions[ 0 ] );
			const int height = static_cast< int >( dimensions[ 1 ] );
			if ( width <= 0 || height <= 0 || width > 1024 || height > 1024 ||
				!src.ExpectTokenString( "{" ) ||
				!src.ExpectTokenString( "(" ) ) {
				break;
			}
			delete patch;
			patch = new idMapPatch( width, height );
			patch->SetSize( width, height );
			bool valid = true;
			for ( int column = 0; valid && column < width; ++column ) {
				valid = src.ExpectTokenString( "(" );
				for ( int row = 0; valid && row < height; ++row ) {
					float point[ 5 ];
					valid = src.Parse1DMatrix( 5, point );
					if ( valid ) {
						idDrawVert &vertex = ( *patch )[ row * width + column ];
						vertex.xyz.Set( point[ 0 ], point[ 1 ], point[ 2 ] );
						vertex.SetST( point[ 3 ], point[ 4 ] );
					}
				}
				if ( valid ) {
					valid = src.ExpectTokenString( ")" );
				}
			}
			if ( !valid || !src.ExpectTokenString( ")" ) ||
				!src.ExpectTokenString( "}" ) ) {
				break;
			}
		} else if ( !ETQWSkipWorldValue( src ) ) {
			break;
		}
	}

	delete patch;
	return NULL;
}

static bool ETQWParseWorldPrimitives( idLexer &src, idMapEntity &entity ) {
	if ( !src.ExpectTokenString( "{" ) ) {
		return false;
	}
	idToken type;
	while ( src.ReadToken( &type ) ) {
		if ( type == "}" ) {
			return true;
		}
		idToken primitiveName;
		if ( !src.ReadToken( &primitiveName ) ) {
			return false;
		}
		if ( !ETQWReadWorldHolderBody( src ) ) {
			continue;
		}
		if ( type.Icmp( "sdPrimitiveBrush" ) == 0 ) {
			idMapBrush *brush = ETQWParseWorldBrush( src );
			if ( brush == NULL ) {
				idLib::common->Printf( "ETQW world parse: failed brush '%s' near line %d\n", primitiveName.c_str(), src.GetLineNum() );
				return false;
			}
			entity.AddPrimitive( brush );
		} else if ( type.Icmp( "sdPrimitivePatch" ) == 0 ) {
			idMapPatch *patch = ETQWParseWorldPatch( src );
			if ( patch == NULL ) {
				idLib::common->Printf( "ETQW world parse: failed patch '%s' near line %d\n",
					primitiveName.c_str(), src.GetLineNum() );
				return false;
			}
			entity.AddPrimitive( patch );
		} else {
			src.SkipBracedSection( false );
		}
	}
	return false;
}

static idMapEntity *ETQWParseWorldEntity( idLexer &src, const char *entityName ) {
	idMapEntity *entity = new idMapEntity();
	entity->epairs.Set( "name", entityName );
	etqwWorldTransform_t transform;
	idToken token;
	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			if ( transform.hasTranslation ) {
				entity->epairs.Set( "origin", va( "%g %g %g", transform.translation.x,
					transform.translation.y, transform.translation.z ) );
			}
			if ( transform.hasRotation ) {
				entity->epairs.Set( "rotation", va( "%g %g %g %g %g %g %g %g %g",
					transform.rotation[0][0], transform.rotation[0][1], transform.rotation[0][2],
					transform.rotation[1][0], transform.rotation[1][1], transform.rotation[1][2],
					transform.rotation[2][0], transform.rotation[2][1], transform.rotation[2][2] ) );
			}
			if ( entity->epairs.GetString( "classname", "" )[0] == '\0' ) {
				entity->epairs.Set( "classname", idStr::Icmp( entityName, "worldspawn" ) == 0 ? "worldspawn" : "func_static" );
			}
			return entity;
		}
		if ( token.Icmp( "sdDictionary" ) == 0 ) {
			if ( !ETQWParseWorldDictionary( src, entity->epairs ) ) break;
		} else if ( token.Icmp( "sdTransform" ) == 0 ) {
			if ( !ETQWParseWorldTransform( src, transform ) ) break;
		} else if ( token.Icmp( "classname" ) == 0 ) {
			idToken className;
			if ( !src.ReadToken( &className ) ) break;
			entity->epairs.Set( "classname", className.c_str() );
		} else if ( token.Icmp( "primitives" ) == 0 ) {
			if ( !ETQWParseWorldPrimitives( src, *entity ) ) {
				idLib::common->Printf( "ETQW world parse: invalid primitives for '%s' near line %d\n", entityName, src.GetLineNum() );
				break;
			}
		} else if ( !ETQWSkipWorldValue( src ) ) {
			idLib::common->Printf( "ETQW world parse: invalid entity field '%s' for '%s' near line %d\n", token.c_str(), entityName, src.GetLineNum() );
			break;
		}
	}
	delete entity;
	return NULL;
}

void idMapFile::TransformWorldReferenceEntity( idMapEntity *entity,
	const idVec3 &translation, const idMat3 &rotation ) {
	if ( entity == NULL ) {
		return;
	}

	const bool isWorld = idStr::Icmp( entity->epairs.GetString( "classname" ),
		"worldspawn" ) == 0;
	idVec3 origin;
	entity->epairs.GetVector( "origin", "0 0 0", origin );
	origin = isWorld ? vec3_origin : origin * rotation + translation;
	entity->epairs.Set( "origin", va( "%g %g %g", origin.x, origin.y,
		origin.z ) );

	idMat3 localRotation;
	if ( entity->epairs.GetMatrix( "rotation", NULL, localRotation ) ) {
		localRotation *= rotation;
	} else {
		localRotation = rotation;
	}
	if ( !isWorld && !localRotation.IsIdentity() ) {
		entity->epairs.Set( "rotation", va( "%g %g %g %g %g %g %g %g %g",
			localRotation[0][0], localRotation[0][1], localRotation[0][2],
			localRotation[1][0], localRotation[1][1], localRotation[1][2],
			localRotation[2][0], localRotation[2][1], localRotation[2][2] ) );
	}

	const idVec3 primitiveTranslation = isWorld ? translation : vec3_origin;
	for ( int primitiveIndex = 0;
		primitiveIndex < entity->GetNumPrimitives(); ++primitiveIndex ) {
		idMapPrimitive *primitive = entity->GetPrimitive( primitiveIndex );
		if ( primitive->GetType() == idMapPrimitive::TYPE_BRUSH ) {
			idMapBrush *brush = static_cast< idMapBrush* >( primitive );
			for ( int sideIndex = 0; sideIndex < brush->GetNumSides(); ++sideIndex ) {
				idMapBrushSide *side = brush->GetSide( sideIndex );
				idPlane plane = side->GetPlane();
				plane.RotateSelf( vec3_origin, rotation );
				plane.TranslateSelf( primitiveTranslation );
				side->SetPlane( plane );
				side->TranslateSelf( primitiveTranslation );
			}
		} else if ( primitive->GetType() == idMapPrimitive::TYPE_PATCH ) {
			idMapPatch *patch = static_cast< idMapPatch* >( primitive );
			for ( int vertexIndex = 0;
				vertexIndex < patch->GetWidth() * patch->GetHeight(); ++vertexIndex ) {
				( *patch )[ vertexIndex ].xyz =
					( *patch )[ vertexIndex ].xyz * rotation + primitiveTranslation;
			}
		}
	}
}

bool idMapFile::AppendWorldReference( const char *url,
	const idVec3 &translation, const idMat3 &rotation,
	const char *instanceName ) {
	if ( url == NULL || url[0] == '\0' ) {
		return false;
	}
	static int referenceDepth = 0;
	if ( referenceDepth >= 16 ) {
		idLib::common->Warning( "ETQW world reference nesting is too deep at %s",
			url );
		return false;
	}

	idStr referencePath = url;
	const char *fileSystemPrefix = "filesystem://";
	if ( !idStr::Icmpn( referencePath, fileSystemPrefix,
		strlen( fileSystemPrefix ) ) ) {
		referencePath = referencePath.Mid( strlen( fileSystemPrefix ),
			referencePath.Length() );
	}
	referencePath.StripFileExtension();

	idMapFile referenceFile;
	++referenceDepth;
	const bool parsed = referenceFile.Parse( referencePath, true, false,
		false, true );
	--referenceDepth;
	if ( !parsed ) {
		idLib::common->Warning( "ETQW world reference '%s' could not be loaded",
			referencePath.c_str() );
		return false;
	}

	// References are instances. Prefix their scoped entity names and update
	// exact intra-reference links so repeated uses of one source do not collide.
	idDict renamedEntities;
	for ( int entityIndex = 0; entityIndex < referenceFile.entities.Num();
		++entityIndex ) {
		idMapEntity *entity = referenceFile.entities[ entityIndex ];
		if ( idStr::Icmp( entity->epairs.GetString( "classname" ),
			"worldspawn" ) == 0 ) {
			continue;
		}
		const char *oldName = entity->epairs.GetString( "name" );
		if ( oldName[0] != '\0' ) {
			const idStr newName = va( "%s_%s", instanceName, oldName );
			renamedEntities.Set( oldName, newName );
			entity->epairs.Set( "name", newName );
		}
	}
	for ( int entityIndex = 0; entityIndex < referenceFile.entities.Num();
		++entityIndex ) {
		idMapEntity *entity = referenceFile.entities[ entityIndex ];
		for ( int keyIndex = 0; keyIndex < entity->epairs.GetNumKeyVals();
			++keyIndex ) {
			const idKeyValue *keyValue = entity->epairs.GetKeyVal( keyIndex );
			const idStr key = keyValue->GetKey();
			const idStr value = keyValue->GetValue();
			const char *renamed = renamedEntities.GetString( value, "" );
			if ( renamed[0] != '\0' ) {
				entity->epairs.Set( key, renamed );
			}
		}
	}

	int appendedEntities = 0;
	while ( referenceFile.entities.Num() > 0 ) {
		idMapEntity *entity = referenceFile.entities[ 0 ];
		referenceFile.entities.RemoveIndex( 0 );
		TransformWorldReferenceEntity( entity, translation, rotation );
		if ( idStr::Icmp( entity->epairs.GetString( "classname" ),
			"worldspawn" ) == 0 ) {
			idMapEntity *world = NULL;
			for ( int destinationIndex = 0; destinationIndex < entities.Num();
				++destinationIndex ) {
				if ( idStr::Icmp( entities[ destinationIndex ]->epairs.GetString(
					"classname" ), "worldspawn" ) == 0 ) {
					world = entities[ destinationIndex ];
					break;
				}
			}
			if ( world == NULL ) {
				entities.Append( entity );
			} else {
				world->primitives.Append( entity->primitives );
				entity->primitives.Clear();
				delete entity;
			}
		} else {
			entities.Append( entity );
			++appendedEntities;
		}
	}
	idLib::common->Printf( "ETQW world reference: %s (%d entities)\n",
		referencePath.c_str(), appendedEntities );
	return true;
}

bool idMapFile::ParseWorldFile( idLexer &src, bool osPath ) {
	idToken token;
	if ( !src.ExpectTokenString( "version" ) || !src.ReadToken( &token ) || !src.ExpectTokenString( "{" ) ) {
		return false;
	}
	version = token.GetFloatValue();
	idLib::common->Printf( "ETQW world parse: version %g\n", version );
	fileTime = src.GetFileTime();
	entities.DeleteContents( true );
	idDict worldDictionary;
	idStr terrainModel;
	idStr terrainSource = name;
	terrainSource.SetFileExtension( ".sft" );
	idLexer terrainLexer( LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS |
		LEXFL_ALLOWPATHNAMES );
	if ( terrainLexer.LoadFile( terrainSource, osPath ) ) {
		idToken terrainToken;
		if ( terrainLexer.ReadToken( &terrainToken ) &&
			terrainToken.Icmp( "version" ) == 0 &&
			terrainLexer.ReadToken( &terrainToken ) &&
			terrainLexer.ExpectTokenString( "{" ) ) {
			while ( terrainLexer.ReadToken( &terrainToken ) && terrainToken != "}" ) {
				if ( terrainToken.Icmp( "model" ) == 0 ) {
					idToken modelToken;
					if ( terrainLexer.ReadToken( &modelToken ) ) {
						terrainModel = modelToken.c_str();
					}
				} else if ( !ETQWSkipWorldValue( terrainLexer ) ) {
					break;
				}
			}
		}
	}

	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			break;
		}
		if ( token.Icmp( "sdDictionary" ) == 0 ) {
			if ( !ETQWParseWorldDictionary( src, worldDictionary ) ) return false;
			continue;
		}
		if ( token.Icmp( "meta" ) == 0 ) {
			if ( !src.SkipBracedSection() ) return false;
			continue;
		}
		if ( token.Icmp( "primitiveHolders" ) != 0 ) {
			if ( !ETQWSkipWorldValue( src ) ) return false;
			continue;
		}
		if ( !src.ExpectTokenString( "{" ) ) return false;
		idToken holderType;
		while ( src.ReadToken( &holderType ) ) {
			if ( holderType == "}" ) break;
			idToken holderName;
			if ( !src.ReadToken( &holderName ) ) return false;
			if ( holderType.Icmp( "sdPrimitiveTerrainFile" ) == 0 ) {
				if ( !terrainModel.IsEmpty() ) {
					idMapEntity *terrainEntity = new idMapEntity();
					terrainEntity->epairs.Set( "classname", "model_static" );
					terrainEntity->epairs.Set( "name", holderName.c_str() );
					terrainEntity->epairs.Set( "model", terrainModel.c_str() );
					terrainEntity->epairs.Set( "terrainSource", terrainSource.c_str() );
					terrainEntity->epairs.SetBool( "noclipmodel", true );
					entities.Append( terrainEntity );
					idLib::common->Printf( "ETQW world terrain: %s from %s\n",
						terrainModel.c_str(), terrainSource.c_str() );
				} else {
					idLib::common->Warning( "ETQW world terrain holder '%s' has no model in %s",
						holderName.c_str(), terrainSource.c_str() );
				}
				continue;
			}
			if ( !ETQWReadWorldHolderBody( src ) ) {
				continue;
			}
			if ( holderType.Icmp( "sdPrimitiveEntity" ) == 0 ) {
				idMapEntity *entity = ETQWParseWorldEntity( src, holderName.c_str() );
				if ( entity == NULL ) {
					idLib::common->Printf( "ETQW world parse: failed entity '%s' near line %d\n", holderName.c_str(), src.GetLineNum() );
					return false;
				}
				if ( idStr::Icmp( entity->epairs.GetString( "classname" ), "worldspawn" ) == 0 ) {
					for ( int keyIndex = 0; keyIndex < worldDictionary.GetNumKeyVals(); keyIndex++ ) {
						const idKeyValue *keyValue = worldDictionary.GetKeyVal( keyIndex );
						if ( entity->epairs.FindKey( keyValue->GetKey() ) == NULL ) {
							entity->epairs.Set( keyValue->GetKey(), keyValue->GetValue() );
						}
					}
					entities.Insert( entity, 0 );
				} else {
					entities.Append( entity );
				}
			} else if ( holderType.Icmp( "sdPrimitiveReference" ) == 0 ) {
				etqwWorldTransform_t referenceTransform;
				idStr referenceURL;
				if ( !ETQWParseWorldReference( src, referenceTransform,
					referenceURL ) ) {
					idLib::common->Warning( "ETQW world reference '%s' is invalid",
						holderName.c_str() );
				} else {
					AppendWorldReference( referenceURL,
						referenceTransform.translation,
						referenceTransform.rotation, holderName.c_str() );
				}
			} else {
				// References and terrain source holders are editor metadata rather
				// than idMap primitives. Keep parsing their block so following
				// entities and brushes remain visible.
				src.SkipBracedSection( false );
			}
		}
	}

	// A referenced world may precede this file's own worldspawn. Consolidate
	// every referenced world brush into the primary world entity now that all
	// holders have been expanded.
	idMapEntity *primaryWorld = NULL;
	for ( int entityIndex = 0; entityIndex < entities.Num(); ++entityIndex ) {
		if ( idStr::Icmp( entities[ entityIndex ]->epairs.GetString( "classname" ),
			"worldspawn" ) != 0 ) {
			continue;
		}
		if ( primaryWorld == NULL ) {
			primaryWorld = entities[ entityIndex ];
			if ( entityIndex != 0 ) {
				entities.RemoveIndex( entityIndex );
				entities.Insert( primaryWorld, 0 );
			}
			continue;
		}
		idMapEntity *referencedWorld = entities[ entityIndex ];
		primaryWorld->primitives.Append( referencedWorld->primitives );
		referencedWorld->primitives.Clear();
		delete referencedWorld;
		entities.RemoveIndex( entityIndex-- );
	}
	if ( entities.Num() == 0 || idStr::Icmp( entities[0]->epairs.GetString( "classname" ), "worldspawn" ) != 0 ) {
		return false;
	}
	for ( int entityIndex = 0; entityIndex < entities.Num(); entityIndex++ ) {
		idLib::common->Printf( "ETQW world entity %d: %s (%d primitives)\n", entityIndex,
			entities[entityIndex]->epairs.GetString( "classname" ), entities[entityIndex]->GetNumPrimitives() );
	}
	hasPrimitiveData = true;
	SetGeometryCRC();
	return true;
}

/*
===============
idMapFile::Parse
===============
*/
bool idMapFile::Parse( const char *filename, bool ignoreRegion, bool osPath, bool moveFuncGroups, bool ignoreEntities, const char* onlyEntitiesOfClass ) {
	// no string concatenation for epairs and allow path names for materials
	idLexer src( LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES );
	idToken token;
	idStr fullName;
	idMapEntity *mapEnt;
	int i, j, k;

	name = filename;
	name.StripFileExtension();
	fullName = name;
	hasPrimitiveData = false;

	if ( !ignoreRegion ) {
		// try loading a .reg file first
		fullName.SetFileExtension( "reg" );
		src.LoadFile( fullName, osPath );
	}

	if ( !src.IsLoaded() && !ignoreEntities ) {
		// now try an entity file
		fullName.SetFileExtension( ENTITY_FILE_EXT );
		src.LoadFile( fullName, osPath );
	}

	if ( !src.IsLoaded() ) {
		// ETQW source levels use .world. Region and entity sidecars retain their
		// existing extensions, but the editable brush/patch source is never .map.
		fullName.SetFileExtension( ".world" );
		src.LoadFile( fullName, osPath );
	}

	if ( !src.IsLoaded() ) {
		return false;
	}

	if ( src.CheckTokenString( "sdWorldFile" ) ) {
		return ParseWorldFile( src, osPath );
	}

	version = OLD_MAP_VERSION;
	fileTime = src.GetFileTime();
	entities.DeleteContents( true );

	if ( src.CheckTokenString( "Version" ) ) {
		src.ReadTokenOnLine( &token );
		version = token.GetFloatValue();
	}

	while ( true ) {
		mapEnt = idMapEntity::Parse( src, ( entities.Num() == 0 ), version );
		if ( mapEnt == NULL ) {
			break;
		}

		if ( onlyEntitiesOfClass != NULL ) {
			if ( idStr::Icmp( mapEnt->epairs.GetString( "classname" ), onlyEntitiesOfClass ) != 0 ) {
				delete mapEnt;
				continue;
			}
		}

		entities.Append( mapEnt );
	}

	SetGeometryCRC();
	// Generated ETQW .entities files do not contain the world primitives, so
	// their traditional primitive-only geometry CRC is commonly zero.  Bind
	// derived data such as the Detour navmesh to both the entity placement data
	// and the compiled collision bundle used to build it.
	idStr loadedExtension;
	fullName.ExtractFileExtension( loadedExtension );
	if ( !loadedExtension.Icmp( ENTITY_FILE_EXT ) && !osPath ) {
		unsigned int compiledCRC = 0;
		idFile* checksumFile = fileSystem->OpenFileRead( fullName, true, NULL, false );
		if ( checksumFile != NULL ) {
			compiledCRC ^= static_cast< unsigned int >( fileSystem->FileChecksum( checksumFile ) );
			fileSystem->CloseFile( checksumFile );
		}
		idStr collisionPath = "generated/cm/";
		collisionPath += name;
		collisionPath.SetFileExtension( "cmb" );
		checksumFile = fileSystem->OpenFileRead( collisionPath, true, NULL, false );
		if ( checksumFile == NULL ) {
			collisionPath = name;
			collisionPath.SetFileExtension( "cmb" );
			checksumFile = fileSystem->OpenFileRead( collisionPath, true, NULL, false );
		}
		if ( checksumFile != NULL ) {
			compiledCRC ^= static_cast< unsigned int >( fileSystem->FileChecksum( checksumFile ) );
			fileSystem->CloseFile( checksumFile );
		}
		geometryCRC = compiledCRC != 0 ? compiledCRC : 1;
	}

	// if the map has a worldspawn
	if ( entities.Num() ) {

		// "removeEntities" "classname" can be set in the worldspawn to remove all entities with the given classname
		const idKeyValue *removeEntities = entities[0]->epairs.MatchPrefix( "removeEntities", NULL );
		while ( removeEntities ) {
			RemoveEntities( removeEntities->GetValue() );
			removeEntities = entities[0]->epairs.MatchPrefix( "removeEntities", removeEntities );
		}

		// "overrideMaterial" "material" can be set in the worldspawn to reset all materials
		idStr material;
		if ( entities[0]->epairs.GetString( "overrideMaterial", "", material ) ) {
			for ( i = 0; i < entities.Num(); i++ ) {
				mapEnt = entities[i];
				for ( j = 0; j < mapEnt->GetNumPrimitives(); j++ ) {
					idMapPrimitive *mapPrimitive = mapEnt->GetPrimitive( j );
					switch( mapPrimitive->GetType() ) {
						case idMapPrimitive::TYPE_BRUSH: {
							idMapBrush *mapBrush = static_cast<idMapBrush *>(mapPrimitive);
							for ( k = 0; k < mapBrush->GetNumSides(); k++ ) {
								mapBrush->GetSide( k )->SetMaterial( material );
							}
							break;
						}
						case idMapPrimitive::TYPE_PATCH: {
							static_cast<idMapPatch *>(mapPrimitive)->SetMaterial( material );
							break;
						}
					}
				}
			}
		}

		// force all entities to have a name key/value pair
		if ( entities[0]->epairs.GetBool( "forceEntityNames" ) ) {
			for ( i = 1; i < entities.Num(); i++ ) {
				mapEnt = entities[i];
				if ( !mapEnt->epairs.FindKey( "name" ) ) {
					mapEnt->epairs.Set( "name", va( "%s%d", mapEnt->epairs.GetString( "classname", "forcedName" ), i ) );
				}
			}
		}

		// move the primitives of any func_group entities to the worldspawn
		if ( moveFuncGroups && entities[0]->epairs.GetBool( "moveFuncGroups" ) ) {
			for ( i = 1; i < entities.Num(); i++ ) {
				mapEnt = entities[i];
				if ( idStr::Icmp( mapEnt->epairs.GetString( "classname" ), "func_group" ) == 0 ) {
					// transform primitives into worldspawn space
					idMapPrimitive *mapPrim;
					idVec3 translationVec;
					mapEnt->epairs.GetVector( "origin", "", translationVec );
					for ( j = 0; j < mapEnt->GetNumPrimitives(); j++ ) {
						mapPrim = mapEnt->GetPrimitive( j );
						if ( mapPrim->GetType() == idMapPrimitive::TYPE_PATCH ) {
							static_cast<idMapPatch*>(mapPrim)->TranslateSelf( translationVec );
						}
						if ( mapPrim->GetType() == idMapPrimitive::TYPE_BRUSH ) {
							for ( k = 0; k < static_cast<idMapBrush*>(mapPrim)->GetNumSides(); k++ ) {
								idMapBrushSide *side;
								side = static_cast<idMapBrush*>(mapPrim)->GetSide( k );

								idPlane &plane = side->GetPlane();
								plane.TranslateSelf( translationVec );

								side->TranslateSelf( -translationVec );
							}
						}
					}
					entities[0]->primitives.Append( mapEnt->primitives );
					mapEnt->primitives.Clear();
				}
			}

			// we have no need anymore for the func_group entities
			for ( i = entities.Num() - 1; i > 0; i-- ) {
				mapEnt = entities[i];
				if ( idStr::Icmp( mapEnt->epairs.GetString( "classname" ), "func_group" ) == 0 ) {
					entities.RemoveIndex( i );
				}
			}
		}
	}

	hasPrimitiveData = true;
	return true;
}

/*
===============
idMapFile::ParseBotEntities

Parse the bot_entities file, and setup the info for the bot thread.
===============
*/
bool idMapFile::ParseBotEntities( const char *filename ) {
	idLexer src( LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES );
	idToken token;
	idMapEntity *botEnt;
	idStr fullName;
	name = filename;
	name.StripFileExtension();
	fullName = name;

	version = -1.0f;

	if ( !src.IsLoaded() ) {
		// now try an entity file
		fullName.SetFileExtension( BOT_ENTITY_FILE_EXT );
		src.LoadFile( fullName );
	}

	if ( !src.IsLoaded() ) {
		return false;
	}

	if ( src.CheckTokenString( "Version" ) ) {
		src.ReadTokenOnLine( &token );
		version = token.GetFloatValue();
	}

	if ( version != BOT_MAP_VERSION ) {
		idLib::common->Warning( "%s is an old version, and can't be used. Recompile your map to generate a new one!", fullName.c_str() );
		return false;
	}

	while( 1 ) {		
		botEnt = idMapEntity::ParseActions( src );
		
		if ( !botEnt ) {
			break;
		}		
		entities.Append( botEnt );
	}

	return true;
}

/*
============
idMapFile::Write
============
*/
bool idMapFile::WriteBuffer( idStr& buffer ) {
	buffer += va( "Version %f\n", (float) CURRENT_MAP_VERSION );

	int i;
	for ( i = 0; i < entities.Num(); i++ ) {
		entities[i]->Write( buffer, i );
	}

	return true;
}

/*
============
idMapFile::Write
============
*/
bool idMapFile::Write( const char *fileName, const char *ext, bool fromBasePath ) {
	int i;
	idStr qpath;
	idFile *fp;

	qpath = fileName;
	qpath.SetFileExtension( ext );

	idLib::common->Printf( "writing %s...\n", qpath.c_str() );

	if ( fromBasePath ) {
		fp = idLib::fileSystem->OpenFileWrite( qpath, "fs_devpath" );
	}
	else {
		fp = idLib::fileSystem->OpenExplicitFileWrite( qpath );
	}

	if ( !fp ) {
//		idLib::common->Error( "Couldn't open %s", qpath.c_str() );
		idLib::common->Warning( "Couldn't open %s", qpath.c_str() );
		return false;
	}

	fp->WriteFloatString( "Version %f\n", (float) CURRENT_MAP_VERSION );

	for ( i = 0; i < entities.Num(); i++ ) {
		entities[i]->Write( fp, i );
	}

	idLib::fileSystem->CloseFile( fp );

	return true;
}

/*
===============
idMapFile::SetGeometryCRC
===============
*/
void idMapFile::SetGeometryCRC( void ) {
	int i;

	geometryCRC = 0;
	for ( i = 0; i < entities.Num(); i++ ) {
		geometryCRC ^= entities[i]->GetGeometryCRC();
	}
}

/*
===============
idMapFile::AddEntity
===============
*/
int idMapFile::AddEntity( idMapEntity *mapEnt ) {
	int ret = entities.Append( mapEnt );
	return ret;
}

/*
===============
idMapFile::FindEntity
===============
*/
idMapEntity *idMapFile::FindEntity( const char *name ) const {
	for ( int i = 0; i < entities.Num(); i++ ) {
		idMapEntity *ent = entities[i];
		if ( idStr::Icmp( ent->epairs.GetString( "name" ), name ) == 0 ) {
			return ent;
		}
	}
	return NULL;
}

/*
===============
idMapFile::RemoveEntity
===============
*/
void idMapFile::RemoveEntity( idMapEntity *mapEnt ) {
	entities.Remove( mapEnt );
	delete mapEnt;
}

/*
===============
idMapFile::RemoveEntity
===============
*/
void idMapFile::RemoveEntities( const char *classname ) {
	for ( int i = 0; i < entities.Num(); i++ ) {
		idMapEntity *ent = entities[i];
		if ( idStr::Icmp( ent->epairs.GetString( "classname" ), classname ) == 0 ) {
			delete entities[i];
			entities.RemoveIndex( i );
			i--;
		}
	}
}

/*
===============
idMapFile::RemoveAllEntities
===============
*/
void idMapFile::RemoveAllEntities() {
	entities.DeleteContents( true );
	hasPrimitiveData = false;
}

/*
===============
idMapFile::RemovePrimitiveData
===============
*/
void idMapFile::RemovePrimitiveData() {
	for ( int i = 0; i < entities.Num(); i++ ) {
		idMapEntity *ent = entities[i];
		ent->RemovePrimitiveData();
	}
	hasPrimitiveData = false;
}

/*
===============
idMapFile::NeedsReload
===============
*/
bool idMapFile::NeedsReload() {
	if ( name.Length() ) {
		unsigned int time = (unsigned int)-1;
		if ( idLib::fileSystem->ReadFile( name, NULL, &time ) > 0 ) {
			return ( time > fileTime );
		}
	}
	return true;
}
