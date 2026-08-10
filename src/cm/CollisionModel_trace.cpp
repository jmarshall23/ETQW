// Copyright (C) 2007 Id Software, Inc.
//
// Reconstructed from the ETQW retail executable.  Unlike the inherited
// Doom 3 implementation, ETQW dispatches node tests with an explicit trace
// type and stores polygon and brush references as compact array indices.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "CollisionModel_local.h"

enum cmTraceType_t {
	CM_TRACE_TRANSLATION_POINT = 0,
	CM_TRACE_TRANSLATION = 1,
	CM_TRACE_CONTACTS = 2,
	CM_TRACE_ROTATION = 3,
	CM_TRACE_CONTENTS = 4
};

/*
================
idCollisionModelManagerLocal::TraceTrmThroughNode
================
*/
void idCollisionModelManagerLocal::TraceTrmThroughNode( cm_traceWork_t *tw, cm_node_t *node ) {
	switch ( tw->traceType ) {
		case CM_TRACE_TRANSLATION_POINT:
		case CM_TRACE_TRANSLATION:
			if ( tw->pointTrace ) {
				for ( cm_polygonRef_t *ref = node->polygons; ref != NULL; ref = ref->next ) {
					if ( TranslatePointTrmThroughPolygon( tw, ref->polygonNum ) ) {
						break;
					}
				}
			} else {
				for ( cm_polygonRef_t *ref = node->polygons; ref != NULL; ref = ref->next ) {
					if ( TranslateTrmThroughPolygon( tw, ref->polygonNum ) ) {
						break;
					}
				}
			}
			break;

		case CM_TRACE_CONTACTS:
			for ( cm_polygonRef_t *ref = node->polygons; ref != NULL; ref = ref->next ) {
				if ( TestTrmInContactWithPolygon( tw, ref->polygonNum ) ) {
					break;
				}
			}
			break;

		case CM_TRACE_ROTATION:
			for ( cm_polygonRef_t *ref = node->polygons; ref != NULL; ref = ref->next ) {
				if ( RotateTrmThroughPolygon( tw, ref->polygonNum ) ) {
					break;
				}
			}
			break;

		case CM_TRACE_CONTENTS:
			if ( tw->trace.fraction == 0.0f ) {
				break;
			}

			{
				cm_brushRef_t *brushRef = node->brushes;
				while ( brushRef != NULL ) {
					if ( TestTrmVertsInBrush( tw, brushRef->brushNum ) ) {
						break;
					}
					brushRef = brushRef->next;
				}

				if ( brushRef == NULL && !tw->pointTrace ) {
					for ( cm_polygonRef_t *ref = node->polygons; ref != NULL; ref = ref->next ) {
						if ( TestTrmInPolygon( tw, ref->polygonNum ) ) {
							break;
						}
					}
				}
			}
			break;
	}
}

/*
================
idCollisionModelManagerLocal::TraceThroughAxialBSPTree_r
================
*/
void idCollisionModelManagerLocal::TraceThroughAxialBSPTree_r( cm_traceWork_t *tw,
		cm_node_t *node, float p1f, float p2f, idVec3 &p1, idVec3 &p2 ) {
	if ( node == NULL || tw->quickExit ) {
		return;
	}

	if ( node->polygons != NULL || ( tw->traceType == CM_TRACE_CONTENTS && node->brushes != NULL ) ) {
		TraceTrmThroughNode( tw, node );
	}

	if ( tw->traceType == CM_TRACE_CONTENTS && tw->trace.fraction == 0.0f ) {
		return;
	}

	if ( node->planeType == -1 ) {
		return;
	}

	const int planeType = node->planeType;
	const float t1 = p1[ planeType ] - node->planeDist;
	const float t2 = p2[ planeType ] - node->planeDist;
	const float offset = tw->trmExtents[ planeType ];

	if ( t1 >= offset && t2 >= offset ) {
		if ( p1f < tw->trace.fraction ) {
			TraceThroughAxialBSPTree_r( tw, node->children[ 0 ], p1f, p2f, p1, p2 );
		}
		return;
	}

	if ( t1 <= -offset && t2 <= -offset ) {
		if ( p1f < tw->trace.fraction ) {
			TraceThroughAxialBSPTree_r( tw, node->children[ 1 ], p1f, p2f, p1, p2 );
		}
		return;
	}

	int side;
	float frac;
	float frac2;
	if ( t1 < t2 ) {
		const float inverseDistance = 1.0f / ( t1 - t2 );
		side = 1;
		frac = ( t1 - offset ) * inverseDistance;
		frac2 = ( t1 + offset ) * inverseDistance;
	} else if ( t1 > t2 ) {
		const float inverseDistance = 1.0f / ( t1 - t2 );
		side = 0;
		frac = ( t1 + offset ) * inverseDistance;
		frac2 = ( t1 - offset ) * inverseDistance;
	} else {
		side = 0;
		frac = 1.0f;
		frac2 = 0.0f;
	}

	frac = idMath::ClampFloat( 0.0f, 1.0f, frac );
	float midFraction = p1f + ( p2f - p1f ) * frac;
	if ( p1f < tw->trace.fraction ) {
		idVec3 mid = p1 + frac * ( p2 - p1 );
		TraceThroughAxialBSPTree_r( tw, node->children[ side ], p1f, midFraction, p1, mid );
	}

	frac2 = idMath::ClampFloat( 0.0f, 1.0f, frac2 );
	midFraction = p1f + ( p2f - p1f ) * frac2;
	if ( midFraction < tw->trace.fraction ) {
		idVec3 mid = p1 + frac2 * ( p2 - p1 );
		TraceThroughAxialBSPTree_r( tw, node->children[ side ^ 1 ], midFraction, p2f, mid, p2 );
	}
}

/*
================
idCollisionModelManagerLocal::TraceThroughModel
================
*/
void idCollisionModelManagerLocal::TraceThroughModel( cm_traceWork_t *tw ) {
	TraceThroughAxialBSPTree_r( tw, tw->model->node, 0.0f, 1.0f, tw->start, tw->end );
}
