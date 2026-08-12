// Copyright (C) 2007 Id Software, Inc.
//
// Reconstructed from the ETQW retail executable.  Collision geometry is
// immutable; all visit and sidedness state lives in idTraceWork::modelCache.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "CollisionModel_local.h"

namespace {

ID_INLINE void CM_SetSurfaceType( contactInfo_t &contact, cm_polygon_t *polygon ) {
	contact.surfaceType = idCollisionModelManagerLocal::GetSurfaceType( &contact, polygon, &contact.surfaceColor );
}

ID_INLINE void CM_EnsureVertexSideCache( idTraceWork *tw, int vertexNum ) {
	cm_vertexCache_t &visit = tw->modelCache.vertexCache[ vertexNum ];
	if ( visit.checkcount != tw->modelCache.checkCount ) {
		tw->modelCache.vertexSideCache[ vertexNum ].sideSet = 0;
		visit.checkcount = tw->modelCache.checkCount;
	}
}

}

/*
================
idCollisionModelManagerLocal::TestTrmVertsInBrush
================
*/
bool idCollisionModelManagerLocal::TestTrmVertsInBrush( idTraceWork *tw, int brushNum ) {
	cm_brushCache_t &visit = tw->modelCache.brushCache[ brushNum ];
	if ( visit.checkcount == tw->modelCache.checkCount ) {
		return false;
	}
	visit.checkcount = tw->modelCache.checkCount;

	cm_brush_t *brush = &tw->model->brushes[ brushNum ];
	if ( !( brush->contents & tw->contents ) || !brush->bounds.IntersectsBounds( tw->traceBounds ) ) {
		return false;
	}

	const int numVertices = tw->pointTrace ? 1 : tw->numVerts;
	for ( int vertexNum = 0; vertexNum < numVertices; ++vertexNum ) {
		const idVec3 &point = tw->vertices[ vertexNum ].p;
		int bestPlane = 0;
		float bestDistance = -idMath::INFINITY;
		int planeNum;
		for ( planeNum = 0; planeNum < brush->numPlanes; ++planeNum ) {
			const float distance = brush->planes[ planeNum ].Distance( point );
			if ( distance >= 0.0f ) {
				break;
			}
			if ( distance > bestDistance ) {
				bestDistance = distance;
				bestPlane = planeNum;
			}
		}

		if ( planeNum == brush->numPlanes ) {
			tw->trace.fraction = 0.0f;
			tw->trace.c.type = CONTACT_TRMVERTEX;
			tw->trace.c.normal = brush->planes[ bestPlane ].Normal();
			tw->trace.c.dist = -brush->planes[ bestPlane ].Dist();
			tw->trace.c.contents = brush->contents;
			tw->trace.c.material = brush->material;
			tw->trace.c.point = point;
			tw->trace.c.trmFeature = vertexNum;
			tw->trace.c.modelFeature = 0;
			CM_SetSurfaceType( tw->trace.c, NULL );
			return true;
		}
	}

	return false;
}

/*
================
idCollisionModelManagerLocal::TestTrmInPolygon
================
*/
bool idCollisionModelManagerLocal::TestTrmInPolygon( idTraceWork *tw, int polygonNum ) {
	cm_polygonCache_t &polygonVisit = tw->modelCache.polygonCache[ polygonNum ];
	if ( polygonVisit.checkcount == tw->modelCache.checkCount ) {
		return false;
	}
	polygonVisit.checkcount = tw->modelCache.checkCount;

	cm_polygon_t *polygon = &tw->model->polygons[ polygonNum ];
	if ( !( polygon->contents & tw->contents ) || !polygon->bounds.IntersectsBounds( tw->traceBounds ) ) {
		return false;
	}

	const int planeSide = tw->traceBounds.PlaneSide( polygon->plane, 0.1f );
	if ( planeSide == PLANESIDE_FRONT ) {
		if ( tw->model->isConvex ) {
			tw->quickExit = true;
			return true;
		}
		return false;
	}
	if ( planeSide != PLANESIDE_CROSS ) {
		return false;
	}

	// A model vertex inside a convex trace model is an immediate position hit.
	if ( tw->isConvex ) {
		for ( int polygonEdgeNum = 0; polygonEdgeNum < polygon->numEdges; ++polygonEdgeNum ) {
			const int signedEdgeNum = polygon->edges[ polygonEdgeNum ];
			const int edgeNum = abs( signedEdgeNum );
			if ( tw->modelCache.edgeCache[ edgeNum ].checkcount == tw->modelCache.checkCount ) {
				continue;
			}

			cm_edge_t *edge = &tw->model->edges[ edgeNum ];
			for ( int endpoint = 0; endpoint < 2; ++endpoint ) {
				const int vertexNum = edge->vertexNum[ endpoint ];
				if ( tw->modelCache.vertexCache[ vertexNum ].checkcount == tw->modelCache.checkCount ) {
					continue;
				}

				cm_vertex_t *vertex = &tw->model->vertices[ vertexNum ];
				float bestDistance = -idMath::INFINITY;
				int bestPolygon = 0;
				int tracePolygonNum;
				for ( tracePolygonNum = 0; tracePolygonNum < tw->numPolys; ++tracePolygonNum ) {
					const float distance = tw->polys[ tracePolygonNum ].plane.Distance( vertex->p );
					if ( distance >= 0.0f ) {
						break;
					}
					if ( distance > bestDistance ) {
						bestDistance = distance;
						bestPolygon = tracePolygonNum;
					}
				}

				if ( tracePolygonNum == tw->numPolys ) {
					tw->trace.fraction = 0.0f;
					tw->trace.c.type = CONTACT_MODELVERTEX;
					tw->trace.c.normal = -tw->polys[ bestPolygon ].plane.Normal();
					tw->trace.c.dist = tw->polys[ bestPolygon ].plane.Dist();
					tw->trace.c.contents = polygon->contents;
					tw->trace.c.material = polygon->material;
					tw->trace.c.point = vertex->p;
					tw->trace.c.modelFeature = vertexNum;
					tw->trace.c.trmFeature = 0;
					CM_SetSurfaceType( tw->trace.c, polygon );
					return true;
				}
			}
		}
	}

	// Cache the model polygon edge lines and reset per-trace side masks.
	for ( int i = 0; i < polygon->numEdges; ++i ) {
		const int signedEdgeNum = polygon->edges[ i ];
		const int edgeNum = abs( signedEdgeNum );
		if ( tw->modelCache.edgeCache[ edgeNum ].checkcount != tw->modelCache.checkCount ) {
			tw->modelCache.edgeSideCache[ edgeNum ].sideSet = 0;
		}
		const cm_edge_t &edge = tw->model->edges[ edgeNum ];
		tw->polygonEdgePlueckerCache[ i ].FromLine(
			tw->model->vertices[ edge.vertexNum[ 0 ] ].p,
			tw->model->vertices[ edge.vertexNum[ 1 ] ].p );
		CM_EnsureVertexSideCache( tw, edge.vertexNum[ INTSIGNBITSET( signedEdgeNum ) ] );
	}

	int vertexPlaneSides[ MAX_TRACEMODEL_VERTS ];
	for ( int i = 0; i < tw->numVerts; ++i ) {
		vertexPlaneSides[ i ] = polygon->plane.Distance( tw->vertices[ i ].p ) < 0.0f ? -1 : 1;
	}

	// Trace-model edge through model polygon.
	for ( int traceEdgeNum = 1; traceEdgeNum <= tw->numEdges; ++traceEdgeNum ) {
		cm_trmEdge_t &traceEdge = tw->edges[ traceEdgeNum ];
		if ( vertexPlaneSides[ traceEdge.vertexNum[ 0 ] ] == vertexPlaneSides[ traceEdge.vertexNum[ 1 ] ] ) {
			continue;
		}

		const int flip = INTSIGNBITSET( vertexPlaneSides[ traceEdge.vertexNum[ 0 ] ] );
		int polygonEdgeNum;
		for ( polygonEdgeNum = 0; polygonEdgeNum < polygon->numEdges; ++polygonEdgeNum ) {
			const int signedEdgeNum = polygon->edges[ polygonEdgeNum ];
			cm_edgeSideCache_t &sideCache = tw->modelCache.edgeSideCache[ abs( signedEdgeNum ) ];
			const unsigned int bit = 1u << traceEdgeNum;
			if ( !( sideCache.sideSet & bit ) ) {
				const float side = traceEdge.pl.PermutedInnerProduct( tw->polygonEdgePlueckerCache[ polygonEdgeNum ] );
				sideCache.side = ( sideCache.side & ~bit ) | ( FLOATSIGNBITSET( side ) << traceEdgeNum );
				sideCache.sideSet |= bit;
			}
			if ( INTSIGNBITSET( signedEdgeNum ) ^ ( ( sideCache.side >> traceEdgeNum ) & 1 ) ^ flip ) {
				break;
			}
		}

		if ( polygonEdgeNum == polygon->numEdges ) {
			tw->trace.fraction = 0.0f;
			tw->trace.c.type = CONTACT_EDGE;
			tw->trace.c.normal = polygon->plane.Normal();
			tw->trace.c.dist = -polygon->plane.Dist();
			tw->trace.c.contents = polygon->contents;
			tw->trace.c.material = polygon->material;
			tw->trace.c.point = tw->vertices[ traceEdge.vertexNum[ flip == 0 ] ].p;
			tw->trace.c.modelFeature = polygonNum;
			tw->trace.c.trmFeature = traceEdgeNum;
			CM_SetSurfaceType( tw->trace.c, polygon );
			return true;
		}
	}

	// Model edge through trace-model polygon.
	for ( int polygonEdgeNum = 0; polygonEdgeNum < polygon->numEdges; ++polygonEdgeNum ) {
		const int signedEdgeNum = polygon->edges[ polygonEdgeNum ];
		const int edgeNum = abs( signedEdgeNum );
		cm_edgeCache_t &edgeVisit = tw->modelCache.edgeCache[ edgeNum ];
		if ( edgeVisit.checkcount == tw->modelCache.checkCount ) {
			continue;
		}
		edgeVisit.checkcount = tw->modelCache.checkCount;

		cm_edge_t &edge = tw->model->edges[ edgeNum ];
		cm_edgeSideCache_t &edgeSides = tw->modelCache.edgeSideCache[ edgeNum ];
		for ( int tracePolygonNum = 0; tracePolygonNum < tw->numPolys; ++tracePolygonNum ) {
			const unsigned int polygonBit = 1u << tracePolygonNum;
			const int vertexNum0 = edge.vertexNum[ 0 ];
			const int vertexNum1 = edge.vertexNum[ 1 ];
			CM_EnsureVertexSideCache( tw, vertexNum0 );
			CM_EnsureVertexSideCache( tw, vertexNum1 );

			cm_vertexSideCache_t &side0 = tw->modelCache.vertexSideCache[ vertexNum0 ];
			cm_vertexSideCache_t &side1 = tw->modelCache.vertexSideCache[ vertexNum1 ];
			if ( !( side0.sideSet & polygonBit ) ) {
				const float side = tw->polys[ tracePolygonNum ].plane.Distance( tw->model->vertices[ vertexNum0 ].p );
				side0.side = side < 0.0f ? side0.side | polygonBit : side0.side & ~polygonBit;
				side0.sideSet |= polygonBit;
			}
			if ( !( side1.sideSet & polygonBit ) ) {
				const float side = tw->polys[ tracePolygonNum ].plane.Distance( tw->model->vertices[ vertexNum1 ].p );
				side1.side = side < 0.0f ? side1.side | polygonBit : side1.side & ~polygonBit;
				side1.sideSet |= polygonBit;
			}
			if ( !( ( side0.side ^ side1.side ) & polygonBit ) ) {
				continue;
			}

			const int flip = ( side0.side >> tracePolygonNum ) & 1;
			cm_trmPolygon_t &tracePolygon = tw->polys[ tracePolygonNum ];
			int tracePolygonEdgeNum;
			for ( tracePolygonEdgeNum = 0; tracePolygonEdgeNum < tracePolygon.numEdges; ++tracePolygonEdgeNum ) {
				const int signedTraceEdgeNum = tracePolygon.edges[ tracePolygonEdgeNum ];
				const int traceEdgeNum = abs( signedTraceEdgeNum );
				const unsigned int traceEdgeBit = 1u << traceEdgeNum;
				if ( !( edgeSides.sideSet & traceEdgeBit ) ) {
					const float side = tw->edges[ traceEdgeNum ].pl.PermutedInnerProduct(
						tw->polygonEdgePlueckerCache[ polygonEdgeNum ] );
					edgeSides.side = ( edgeSides.side & ~traceEdgeBit ) | ( FLOATSIGNBITSET( side ) << traceEdgeNum );
					edgeSides.sideSet |= traceEdgeBit;
				}
				if ( INTSIGNBITSET( signedTraceEdgeNum ) ^ ( ( edgeSides.side >> traceEdgeNum ) & 1 ) ^ flip ) {
					break;
				}
			}

			if ( tracePolygonEdgeNum == tracePolygon.numEdges ) {
				tw->trace.fraction = 0.0f;
				tw->trace.c.type = CONTACT_EDGE;
				tw->trace.c.normal = -tracePolygon.plane.Normal();
				tw->trace.c.dist = tracePolygon.plane.Dist();
				tw->trace.c.contents = polygon->contents;
				tw->trace.c.material = polygon->material;
				tw->trace.c.point = tw->model->vertices[ edge.vertexNum[ flip == 0 ] ].p;
				tw->trace.c.modelFeature = signedEdgeNum;
				tw->trace.c.trmFeature = tracePolygonNum;
				CM_SetSurfaceType( tw->trace.c, polygon );
				return true;
			}
		}
	}

	return false;
}

/*
================
idCollisionModelManagerLocal::PointNode
================
*/
cm_node_t *idCollisionModelManagerLocal::PointNode( const idVec3 &point, idCollisionModelLocal *model ) {
	cm_node_t *node = model->node;
	while ( node != NULL && node->planeType != -1 ) {
		node = point[ node->planeType ] > node->planeDist ? node->children[ 0 ] : node->children[ 1 ];
	}
	return node;
}

/*
================
idCollisionModelManagerLocal::PointContents
================
*/
int idCollisionModelManagerLocal::PointContents( const idVec3 point, idCollisionModelLocal *model ) {
	for ( cm_node_t *node = PointNode( point, model ); node != NULL; node = node->parent ) {
		for ( cm_brushRef_t *ref = node->brushes; ref != NULL; ref = ref->next ) {
			cm_brush_t &brush = model->brushes[ ref->brushNum ];
			if ( !brush.bounds.ContainsPoint( point ) ) {
				continue;
			}

			int planeNum;
			for ( planeNum = 0; planeNum < brush.numPlanes; ++planeNum ) {
				if ( brush.planes[ planeNum ].Distance( point ) >= 0.0f ) {
					break;
				}
			}
			if ( planeNum == brush.numPlanes ) {
				return brush.contents;
			}
		}
	}
	return 0;
}

/*
================
idCollisionModelManagerLocal::TransformedPointContents
================
*/
int idCollisionModelManagerLocal::TransformedPointContents( const idVec3 &point,
		idCollisionModelLocal *model, const idVec3 &origin, const idMat3 &modelAxis ) {
	idVec3 localPoint = point - origin;
	if ( modelAxis.IsRotated() ) {
		localPoint *= modelAxis;
	}
	return PointContents( localPoint, model );
}

/*
================
idCollisionModelManagerLocal::ContentsTrm
================
*/
int idCollisionModelManagerLocal::ContentsTrm( trace_t *results, const idVec3 &start,
		const idTraceModel *trm, const idMat3 &trmAxis, int contentMask,
		idCollisionModel *collisionModel, const idVec3 &modelOrigin, const idMat3 &modelAxis ) {
	idCollisionModelLocal *model = static_cast< idCollisionModelLocal * >( collisionModel );
	if ( trm == NULL ) {
		results->c.contents = TransformedPointContents( start, model, modelOrigin, modelAxis );
		results->fraction = results->c.contents == 0 ? 1.0f : 0.0f;
		results->endpos = start;
		results->endAxis = trmAxis;
		return results->c.contents;
	}

	idTraceWork *tw = traceWork[ GetThreadId() ];
	if ( tw == NULL ) {
		tw = baseTraceWork;
	}
	assert( tw != NULL );
	tw->modelCache.UpdateForModel( model );
	tw->modelCache.IncCheckCount();

	tw->trace.fraction = 1.0f;
	tw->trace.c.contents = 0;
	tw->trace.c.type = CONTACT_NONE;
	tw->trace.c.material = NULL;
	tw->trace.c.surfaceType = NULL;
	tw->contents = contentMask;
	tw->model = model;
	tw->isConvex = trm->isConvex;
	tw->traceType = 4;
	tw->rotation = false;
	tw->positionTest = true;
	tw->pointTrace = false;
	tw->quickExit = false;
	tw->contacts = NULL;
	tw->maxContacts = 0;
	tw->numContacts = 0;

	const idVec3 untranslatedStart = start - modelOrigin;
	const bool modelRotated = modelAxis.IsRotated();
	const idMat3 inverseModelAxis = modelRotated ? modelAxis.Transpose() : mat3_identity;

	SetupTrm( tw, trm );
	for ( int i = 0; i < tw->numVerts; ++i ) {
		tw->vertices[ i ].used = true;
		idVec3 point = trm->verts[ i ];
		if ( trmAxis.IsRotated() ) {
			point *= trmAxis;
		}
		point += untranslatedStart;
		if ( modelRotated ) {
			point *= inverseModelAxis;
		}
		tw->vertices[ i ].p = point;
	}

	idVec3 transformedOffset = trm->offset;
	if ( trmAxis.IsRotated() ) {
		transformedOffset *= trmAxis;
	}
	tw->start = untranslatedStart + transformedOffset;
	if ( modelRotated ) {
		tw->start *= inverseModelAxis;
	}
	tw->end = tw->start;

	idBounds floatTrmBounds;
	floatTrmBounds.Clear();
	for ( int i = 0; i < tw->numVerts; ++i ) {
		floatTrmBounds.AddPoint( tw->vertices[ i ].p - tw->start );
	}
	tw->trmBounds.SetBounds( floatTrmBounds );

	for ( int i = 1; i <= tw->numEdges; ++i ) {
		tw->edges[ i ].used = true;
		tw->edges[ i ].pl.FromLine(
			tw->vertices[ tw->edges[ i ].vertexNum[ 0 ] ].p,
			tw->vertices[ tw->edges[ i ].vertexNum[ 1 ] ].p );
	}

	idMat3 polygonAxis = trmAxis;
	if ( modelRotated ) {
		polygonAxis *= inverseModelAxis;
	}
	for ( int i = 0; i < tw->numPolys; ++i ) {
		tw->polys[ i ].used = true;
		idVec3 normal = trm->polys[ i ].normal;
		if ( polygonAxis.IsRotated() ) {
			normal *= polygonAxis;
		}
		tw->polys[ i ].plane.SetNormal( normal );
		const cm_trmEdge_t &edge = tw->edges[ abs( tw->polys[ i ].edges[ 0 ] ) ];
		tw->polys[ i ].plane.FitThroughPoint( tw->vertices[ edge.vertexNum[ 0 ] ].p );
	}

	idBounds floatTraceBounds;
	const idBounds relativeBounds = tw->trmBounds.ToBounds();
	floatTraceBounds[ 0 ] = tw->start + relativeBounds[ 0 ] - idVec3( CM_BOX_EPSILON, CM_BOX_EPSILON, CM_BOX_EPSILON );
	floatTraceBounds[ 1 ] = tw->start + relativeBounds[ 1 ] + idVec3( CM_BOX_EPSILON, CM_BOX_EPSILON, CM_BOX_EPSILON );
	tw->traceBounds.SetBounds( floatTraceBounds );
	for ( int i = 0; i < 3; ++i ) {
		tw->trmExtents[ i ] = Max( idMath::Fabs( relativeBounds[ 0 ][ i ] ),
			idMath::Fabs( relativeBounds[ 1 ][ i ] ) ) + CM_BOX_EPSILON;
	}

	TraceThroughModel( tw );
	*results = tw->trace;
	results->fraction = results->c.contents == 0 ? 1.0f : 0.0f;
	results->endpos = start;
	results->endAxis = trmAxis;
	return results->c.contents;
}

/*
================
idCollisionModelManagerLocal::Contents
================
*/
int idCollisionModelManagerLocal::Contents( const idVec3 &start, const idTraceModel *trm,
		const idMat3 &trmAxis, int contentMask, idCollisionModel *model,
		const idVec3 &modelOrigin, const idMat3 &modelAxis ) {
	if ( model == NULL ) {
		return 0;
	}
	trace_t results;
	return ContentsTrm( &results, start, trm, trmAxis, contentMask, model, modelOrigin, modelAxis );
}
