// Copyright (C) 2007 Id Software, Inc.
//
// Reconstructed from renderer/tr_matrixmath.obj in the original ETQW PDB and
// the address-matched Hex-Rays bodies.

#include "../framework/precompiled.h"
#pragma hdrstop

#include "tr_local.h"

void R_TransformEyeZToWin( float srcZ, const float* projectionMatrix, float& dstZ ) {
	const float clipZ = projectionMatrix[ 10 ] * srcZ + projectionMatrix[ 14 ];
	const float clipW = projectionMatrix[ 11 ] * srcZ + projectionMatrix[ 15 ];
	if ( clipW <= 0.0f ) {
		dstZ = 0.0f;
	} else {
		dstZ = clipZ / clipW * 0.5f + 0.5f;
	}
}

static ID_INLINE void R_MultMatrixInternal( const float* a, const float* b, float* output ) {
	for ( int row = 0; row < 4; row++ ) {
		const int r = row * 4;
		for ( int column = 0; column < 4; column++ ) {
			output[ r + column ] =
				a[ r + 0 ] * b[ column + 0 ] +
				a[ r + 1 ] * b[ column + 4 ] +
				a[ r + 2 ] * b[ column + 8 ] +
				a[ r + 3 ] * b[ column + 12 ];
		}
	}
}

void R_MultMatrix( const float* a, const float* b, float* output ) {
	R_MultMatrixInternal( a, b, output );
}

void R_MultMatrixAligned( const float* a, const float* b, float* output ) {
	assert_16_byte_aligned( a );
	assert_16_byte_aligned( b );
	assert_16_byte_aligned( output );
	R_MultMatrixInternal( a, b, output );
}

void R_AxisToModelMatrix( const idMat3& axis, const idVec3& origin, float* const modelMatrix ) {
	modelMatrix[ 0 ] = axis[ 0 ][ 0 ];
	modelMatrix[ 4 ] = axis[ 1 ][ 0 ];
	modelMatrix[ 8 ] = axis[ 2 ][ 0 ];
	modelMatrix[ 12 ] = origin[ 0 ];

	modelMatrix[ 1 ] = axis[ 0 ][ 1 ];
	modelMatrix[ 5 ] = axis[ 1 ][ 1 ];
	modelMatrix[ 9 ] = axis[ 2 ][ 1 ];
	modelMatrix[ 13 ] = origin[ 1 ];

	modelMatrix[ 2 ] = axis[ 0 ][ 2 ];
	modelMatrix[ 6 ] = axis[ 1 ][ 2 ];
	modelMatrix[ 10 ] = axis[ 2 ][ 2 ];
	modelMatrix[ 14 ] = origin[ 2 ];

	modelMatrix[ 3 ] = 0.0f;
	modelMatrix[ 7 ] = 0.0f;
	modelMatrix[ 11 ] = 0.0f;
	modelMatrix[ 15 ] = 1.0f;
}

void R_LocalPointToGlobal( const float* const modelMatrix, const idVec3& in, idVec3& out ) {
	out[ 0 ] = modelMatrix[ 0 ] * in[ 0 ] + modelMatrix[ 4 ] * in[ 1 ] + modelMatrix[ 8 ] * in[ 2 ] + modelMatrix[ 12 ];
	out[ 1 ] = modelMatrix[ 1 ] * in[ 0 ] + modelMatrix[ 5 ] * in[ 1 ] + modelMatrix[ 9 ] * in[ 2 ] + modelMatrix[ 13 ];
	out[ 2 ] = modelMatrix[ 2 ] * in[ 0 ] + modelMatrix[ 6 ] * in[ 1 ] + modelMatrix[ 10 ] * in[ 2 ] + modelMatrix[ 14 ];
}

void R_LocalPointToGlobal( const idMat3& axis, const idVec3& origin, const idVec3& in, idVec3& out ) {
	out[ 0 ] = axis[ 0 ][ 0 ] * in[ 0 ] + axis[ 1 ][ 0 ] * in[ 1 ] + axis[ 2 ][ 0 ] * in[ 2 ] + origin[ 0 ];
	out[ 1 ] = axis[ 0 ][ 1 ] * in[ 0 ] + axis[ 1 ][ 1 ] * in[ 1 ] + axis[ 2 ][ 1 ] * in[ 2 ] + origin[ 1 ];
	out[ 2 ] = axis[ 0 ][ 2 ] * in[ 0 ] + axis[ 1 ][ 2 ] * in[ 1 ] + axis[ 2 ][ 2 ] * in[ 2 ] + origin[ 2 ];
}

void R_GlobalPointToLocal( const idMat4& modelMatrix, const idVec3& in, idVec3& out ) {
	const idVec3 temp = in - modelMatrix[ 3 ].ToVec3();
	out[ 0 ] = temp * modelMatrix[ 0 ].ToVec3();
	out[ 1 ] = temp * modelMatrix[ 1 ].ToVec3();
	out[ 2 ] = temp * modelMatrix[ 2 ].ToVec3();
}

void R_LocalVectorToGlobal( const idMat4& modelMatrix, const idVec3& in, idVec3& out ) {
	out = modelMatrix[ 0 ].ToVec3() * in[ 0 ] +
		modelMatrix[ 1 ].ToVec3() * in[ 1 ] +
		modelMatrix[ 2 ].ToVec3() * in[ 2 ];
}

void R_GlobalVectorToLocal( const idMat4& modelMatrix, const idVec3& in, idVec3& out ) {
	out[ 0 ] = in * modelMatrix[ 0 ].ToVec3();
	out[ 1 ] = in * modelMatrix[ 1 ].ToVec3();
	out[ 2 ] = in * modelMatrix[ 2 ].ToVec3();
}

void R_GlobalPlaneToLocal( const idMat4& modelMatrix, const idPlane& in, idPlane& out ) {
	out[ 0 ] = in.Normal() * modelMatrix[ 0 ].ToVec3();
	out[ 1 ] = in.Normal() * modelMatrix[ 1 ].ToVec3();
	out[ 2 ] = in.Normal() * modelMatrix[ 2 ].ToVec3();
	out[ 3 ] = in[ 3 ] + in.Normal() * modelMatrix[ 3 ].ToVec3();
}

void R_LocalPlaneToGlobal( const idMat4& modelMatrix, const idPlane& in, idPlane& out ) {
	out.Normal() = modelMatrix[ 0 ].ToVec3() * in[ 0 ] +
		modelMatrix[ 1 ].ToVec3() * in[ 1 ] +
		modelMatrix[ 2 ].ToVec3() * in[ 2 ];
	out[ 3 ] = in[ 3 ] - modelMatrix[ 3 ].ToVec3() * out.Normal();
}

void R_TransformModelToClip( const idVec3& src, const float* modelMatrix, const float* projectionMatrix, idPlane& eye, idPlane& dst ) {
	for ( int i = 0; i < 4; i++ ) {
		eye[ i ] = src[ 0 ] * modelMatrix[ i + 0 ] +
			src[ 1 ] * modelMatrix[ i + 4 ] +
			src[ 2 ] * modelMatrix[ i + 8 ] +
			modelMatrix[ i + 12 ];
	}
	for ( int i = 0; i < 4; i++ ) {
		dst[ i ] = eye[ 0 ] * projectionMatrix[ i + 0 ] +
			eye[ 1 ] * projectionMatrix[ i + 4 ] +
			eye[ 2 ] * projectionMatrix[ i + 8 ] +
			eye[ 3 ] * projectionMatrix[ i + 12 ];
	}
}

void R_GlobalToNormalizedDeviceCoordinates( const idVec3& global, idVec3& ndc ) {
	const viewDef_t* currentView = tr.viewDef != NULL ? tr.viewDef : tr.primaryView;
	if ( currentView == NULL ) {
		ndc.Zero();
		return;
	}

	idPlane viewPoint;
	idPlane clip;
	for ( int i = 0; i < 4; i++ ) {
		viewPoint[ i ] = global[ 0 ] * currentView->worldSpace.modelViewMatrix[ i + 0 ] +
			global[ 1 ] * currentView->worldSpace.modelViewMatrix[ i + 4 ] +
			global[ 2 ] * currentView->worldSpace.modelViewMatrix[ i + 8 ] +
			currentView->worldSpace.modelViewMatrix[ i + 12 ];
	}
	for ( int i = 0; i < 4; i++ ) {
		clip[ i ] = viewPoint[ 0 ] * currentView->projectionMatrix[ i + 0 ] +
			viewPoint[ 1 ] * currentView->projectionMatrix[ i + 4 ] +
			viewPoint[ 2 ] * currentView->projectionMatrix[ i + 8 ] +
			viewPoint[ 3 ] * currentView->projectionMatrix[ i + 12 ];
	}

	ndc[ 0 ] = clip[ 0 ] / clip[ 3 ];
	ndc[ 1 ] = clip[ 1 ] / clip[ 3 ];
	ndc[ 2 ] = ( clip[ 2 ] + clip[ 3 ] ) / ( clip[ 3 ] + clip[ 3 ] );
}

void R_TransformClipToDevice( const idPlane& clip, const viewDef_t*, idVec3& normalized ) {
	normalized[ 0 ] = clip[ 0 ] / clip[ 3 ];
	normalized[ 1 ] = clip[ 1 ] / clip[ 3 ];
	normalized[ 2 ] = clip[ 2 ] / clip[ 3 ];
}

void R_GenerateViewMatrix( const idMat3& axis, const idVec3& origin, float* const out ) {
	static const float s_flipMatrix[ 16 ] = {
		0.0f, 0.0f, -1.0f, 0.0f,
		-1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	ALIGN16( float viewerMatrix[ 16 ] );
	viewerMatrix[ 0 ] = axis[ 0 ][ 0 ];
	viewerMatrix[ 4 ] = axis[ 0 ][ 1 ];
	viewerMatrix[ 8 ] = axis[ 0 ][ 2 ];
	viewerMatrix[ 12 ] = -( origin[ 0 ] * viewerMatrix[ 0 ] + origin[ 1 ] * viewerMatrix[ 4 ] + origin[ 2 ] * viewerMatrix[ 8 ] );
	viewerMatrix[ 1 ] = axis[ 1 ][ 0 ];
	viewerMatrix[ 5 ] = axis[ 1 ][ 1 ];
	viewerMatrix[ 9 ] = axis[ 1 ][ 2 ];
	viewerMatrix[ 13 ] = -( origin[ 0 ] * viewerMatrix[ 1 ] + origin[ 1 ] * viewerMatrix[ 5 ] + origin[ 2 ] * viewerMatrix[ 9 ] );
	viewerMatrix[ 2 ] = axis[ 2 ][ 0 ];
	viewerMatrix[ 6 ] = axis[ 2 ][ 1 ];
	viewerMatrix[ 10 ] = axis[ 2 ][ 2 ];
	viewerMatrix[ 14 ] = -( origin[ 0 ] * viewerMatrix[ 2 ] + origin[ 1 ] * viewerMatrix[ 6 ] + origin[ 2 ] * viewerMatrix[ 10 ] );
	viewerMatrix[ 3 ] = viewerMatrix[ 7 ] = viewerMatrix[ 11 ] = 0.0f;
	viewerMatrix[ 15 ] = 1.0f;

	R_MultMatrix( viewerMatrix, s_flipMatrix, out );
}
