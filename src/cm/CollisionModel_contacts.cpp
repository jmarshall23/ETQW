// Copyright (C) 2007 Id Software, Inc.
//
// Reconstructed from the ETQW retail executable.  ETQW supports both swept
// contacts and a stationary proximity pass; the latter enumerates model and
// trace-model vertex/edge feature pairs within the requested depth.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "CollisionModel_local.h"

/*
==================
idCollisionModelManagerLocal::TestTrmEdgeInContactWithPolygon
==================
*/
void idCollisionModelManagerLocal::TestTrmEdgeInContactWithPolygon( idTraceWork *tw,
		cm_polygon_t *polygon, cm_trmEdge_t *traceEdge ) {
	for ( int polygonEdgeIndex = 0; polygonEdgeIndex < polygon->numEdges; ++polygonEdgeIndex ) {
		const int signedModelEdgeNum = polygon->edges[ polygonEdgeIndex ];
		const int modelEdgeNum = abs( signedModelEdgeNum );
		if ( tw->modelCache.edgeCache[ modelEdgeNum ].checkcount == tw->modelCache.checkCount ) {
			continue;
		}

		cm_edge_t &modelEdge = tw->model->edges[ modelEdgeNum ];
		if ( modelEdge.internal ) {
			continue;
		}

		const idVec3 &modelStart = tw->model->vertices[
			modelEdge.vertexNum[ INTSIGNBITSET( signedModelEdgeNum ) ] ].p;
		const idVec3 &modelEnd = tw->model->vertices[
			modelEdge.vertexNum[ signedModelEdgeNum >= 0 ] ].p;
		const idVec3 &traceStart = tw->vertices[ traceEdge->vertexNum[ 0 ] ].p;
		const idVec3 &traceEnd = tw->vertices[ traceEdge->vertexNum[ 1 ] ].p;

		const idVec3 modelDirection = modelEnd - modelStart;
		const idVec3 traceDirection = traceEnd - traceStart;
		idVec3 contactNormal = modelDirection.Cross( traceDirection );
		const idVec3 planeNormal = modelDirection.Cross( contactNormal );
		const float planeDist = -planeNormal * modelStart;
		const float startDistance = planeNormal * traceStart + planeDist;
		const float endDistance = planeNormal * traceEnd + planeDist;
		if ( startDistance == endDistance ) {
			continue;
		}

		const float fraction = startDistance / ( startDistance - endDistance );
		if ( fraction < 0.0f || fraction > 1.0f ) {
			continue;
		}

		const idVec3 contactPoint = traceStart + fraction * traceDirection;
		const float modelLengthSqr = modelDirection.LengthSqr();
		if ( modelLengthSqr <= 0.0f ) {
			continue;
		}
		const float modelFraction = ( contactPoint - modelStart ) * modelDirection / modelLengthSqr;
		if ( modelFraction < 0.0f || modelFraction > 1.0f ) {
			continue;
		}

		const idVec3 separationVector = contactPoint - ( modelStart + modelFraction * modelDirection );
		if ( separationVector.LengthSqr() > tw->contactDepth * tw->contactDepth ) {
			continue;
		}
		if ( tw->numContacts >= tw->maxContacts ) {
			return;
		}

		if ( traceEdge->cross * contactNormal > 0.0f ) {
			contactNormal = -contactNormal;
		}
		contactNormal.Normalize();

		contactInfo_t &contact = tw->contacts[ tw->numContacts++ ];
		contact.normal = contactNormal;
		contact.dist = contactNormal * modelStart;
		contact.separation = contactNormal * traceStart - contact.dist;
		contact.contents = polygon->contents;
		contact.material = polygon->material;
		contact.type = CONTACT_EDGE;
		contact.modelFeature = signedModelEdgeNum;
		contact.trmFeature = static_cast< int >( traceEdge - &tw->edges[ 0 ] );
		contact.point = contactPoint;
		contact.surfaceType = GetSurfaceType( &contact, polygon, &contact.surfaceColor );
	}
}

/*
==================
idCollisionModelManagerLocal::TestTrmVertexInContactWithPolygon
==================
*/
void idCollisionModelManagerLocal::TestTrmVertexInContactWithPolygon( idTraceWork *tw,
		cm_polygon_t *polygon, cm_trmVertex_t *vertex ) {
	const float separation = polygon->plane.Distance( vertex->p );
	if ( separation < 0.0f || separation > tw->contactDepth ) {
		return;
	}

	idPluecker vertexRay;
	vertexRay.FromRay( vertex->p, -polygon->plane.Normal() );
	for ( int edgeNum = 0; edgeNum < polygon->numEdges; ++edgeNum ) {
		const float side = vertexRay.PermutedInnerProduct( tw->polygonEdgePlueckerCache[ edgeNum ] );
		if ( FLOATSIGNBITSET( side ) != INTSIGNBITSET( polygon->edges[ edgeNum ] ) ) {
			return;
		}
	}

	if ( tw->numContacts >= tw->maxContacts ) {
		return;
	}
	contactInfo_t &contact = tw->contacts[ tw->numContacts++ ];
	contact.normal = polygon->plane.Normal();
	contact.dist = -polygon->plane.Dist();
	contact.separation = separation;
	contact.contents = polygon->contents;
	contact.material = polygon->material;
	contact.type = CONTACT_TRMVERTEX;
	contact.modelFeature = static_cast< int >( polygon - tw->model->polygons );
	contact.trmFeature = static_cast< int >( vertex - &tw->vertices[ 0 ] );
	contact.point = vertex->p;
	contact.surfaceType = GetSurfaceType( &contact, polygon, &contact.surfaceColor );
}

/*
==================
idCollisionModelManagerLocal::TestVertexInContactWithTrmPolygon
==================
*/
void idCollisionModelManagerLocal::TestVertexInContactWithTrmPolygon( idTraceWork *tw,
		cm_trmPolygon_t *tracePolygon, cm_polygon_t *polygon, cm_vertex_t *vertex ) {
	const float separation = tracePolygon->plane.Distance( vertex->p );
	if ( separation < 0.0f || separation > tw->contactDepth ) {
		return;
	}

	idPluecker vertexRay;
	vertexRay.FromRay( vertex->p, -tracePolygon->plane.Normal() );
	for ( int edgeIndex = 0; edgeIndex < tracePolygon->numEdges; ++edgeIndex ) {
		const int signedEdgeNum = tracePolygon->edges[ edgeIndex ];
		const float side = vertexRay.PermutedInnerProduct( tw->edges[ abs( signedEdgeNum ) ].pl );
		if ( FLOATSIGNBITSET( side ) != INTSIGNBITSET( signedEdgeNum ) ) {
			return;
		}
	}

	if ( tw->numContacts >= tw->maxContacts ) {
		return;
	}
	contactInfo_t &contact = tw->contacts[ tw->numContacts++ ];
	contact.normal = -tracePolygon->plane.Normal();
	contact.dist = contact.normal * vertex->p;
	contact.separation = separation;
	contact.contents = polygon->contents;
	contact.material = polygon->material;
	contact.type = CONTACT_MODELVERTEX;
	contact.modelFeature = static_cast< int >( vertex - tw->model->vertices );
	contact.trmFeature = static_cast< int >( tracePolygon - &tw->polys[ 0 ] );
	contact.point = vertex->p;
	contact.surfaceType = GetSurfaceType( &contact, polygon, &contact.surfaceColor );
}

/*
==================
idCollisionModelManagerLocal::TestTrmInContactWithPolygon
==================
*/
bool idCollisionModelManagerLocal::TestTrmInContactWithPolygon( idTraceWork *tw, int polygonNum ) {
	cm_polygonCache_t &polygonVisit = tw->modelCache.polygonCache[ polygonNum ];
	if ( polygonVisit.checkcount == tw->modelCache.checkCount ) {
		return false;
	}
	polygonVisit.checkcount = tw->modelCache.checkCount;

	cm_polygon_t *polygon = &tw->model->polygons[ polygonNum ];
	if ( !( polygon->contents & tw->contents ) || !polygon->bounds.IntersectsBounds( tw->traceBounds ) ) {
		return false;
	}
	if ( tw->dir * polygon->plane.Normal() > 0.0f ) {
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

	for ( int edgeIndex = 0; edgeIndex < polygon->numEdges; ++edgeIndex ) {
		const int edgeNum = abs( polygon->edges[ edgeIndex ] );
		const cm_edge_t &edge = tw->model->edges[ edgeNum ];
		tw->polygonEdgePlueckerCache[ edgeIndex ].FromLine(
			tw->model->vertices[ edge.vertexNum[ 0 ] ].p,
			tw->model->vertices[ edge.vertexNum[ 1 ] ].p );
	}

	for ( int vertexNum = 0; vertexNum < tw->numVerts; ++vertexNum ) {
		if ( tw->vertices[ vertexNum ].used ) {
			TestTrmVertexInContactWithPolygon( tw, polygon, &tw->vertices[ vertexNum ] );
		}
	}
	for ( int edgeNum = 1; edgeNum <= tw->numEdges; ++edgeNum ) {
		if ( tw->edges[ edgeNum ].used ) {
			TestTrmEdgeInContactWithPolygon( tw, polygon, &tw->edges[ edgeNum ] );
		}
	}

	for ( int polygonEdgeIndex = 0; polygonEdgeIndex < polygon->numEdges; ++polygonEdgeIndex ) {
		const int signedEdgeNum = polygon->edges[ polygonEdgeIndex ];
		const int edgeNum = abs( signedEdgeNum );
		cm_edgeCache_t &edgeVisit = tw->modelCache.edgeCache[ edgeNum ];
		if ( edgeVisit.checkcount == tw->modelCache.checkCount ) {
			continue;
		}
		edgeVisit.checkcount = tw->modelCache.checkCount;

		cm_edge_t &edge = tw->model->edges[ edgeNum ];
		if ( edge.internal ) {
			continue;
		}
		for ( int endpoint = 0; endpoint < 2; ++endpoint ) {
			const int vertexNum = edge.vertexNum[ endpoint ^ INTSIGNBITSET( signedEdgeNum ) ];
			cm_vertexCache_t &vertexVisit = tw->modelCache.vertexCache[ vertexNum ];
			if ( vertexVisit.checkcount == tw->modelCache.checkCount ) {
				continue;
			}
			vertexVisit.checkcount = tw->modelCache.checkCount;

			cm_vertex_t *vertex = &tw->model->vertices[ vertexNum ];
			if ( !tw->traceBounds.ContainsPoint( vertex->p ) ) {
				continue;
			}
			for ( int tracePolygonNum = 0; tracePolygonNum < tw->numPolys; ++tracePolygonNum ) {
				if ( tw->polys[ tracePolygonNum ].used ) {
					TestVertexInContactWithTrmPolygon( tw, &tw->polys[ tracePolygonNum ], polygon, vertex );
				}
			}
		}
	}

	return false;
}

/*
==================
idCollisionModelManagerLocal::Contacts
==================
*/
int idCollisionModelManagerLocal::Contacts( contactInfo_t *contacts, const int maxContacts,
		const idVec3 &start, const idVec3 *dir, const float depth,
		const idTraceModel *trm, const idMat3 &trmAxis, int contentMask,
		idCollisionModel *collisionModel, const idVec3 &modelOrigin, const idMat3 &modelAxis ) {
	if ( collisionModel == NULL || maxContacts <= 0 ) {
		return 0;
	}

	idTraceWork *tw = traceWork[ GetThreadId() ];
	if ( tw == NULL ) {
		tw = baseTraceWork;
	}
	assert( tw != NULL );

	if ( dir != NULL ) {
		tw->modelCache.contacts = contacts;
		tw->modelCache.maxContacts = maxContacts;
		tw->modelCache.numContacts = 0;
		trace_t trace;
		const idVec3 end = start + ( *dir * depth );
		Translation( &trace, start, end, trm, trmAxis, contentMask,
			collisionModel, modelOrigin, modelAxis );
		const int count = tw->modelCache.numContacts;
		tw->modelCache.contacts = NULL;
		tw->modelCache.maxContacts = 0;
		return count;
	}

	if ( trm == NULL ) {
		return 0;
	}

	idCollisionModelLocal *model = static_cast< idCollisionModelLocal * >( collisionModel );
	tw->modelCache.UpdateForModel( model );
	tw->modelCache.IncCheckCount();
	tw->trace.fraction = 1.0f;
	tw->trace.c.contents = 0;
	tw->trace.c.type = CONTACT_NONE;
	tw->contactDepth = depth;
	tw->contents = contentMask;
	tw->model = model;
	tw->isConvex = trm->isConvex;
	tw->traceType = 2;
	tw->pointTrace = false;
	tw->quickExit = false;
	tw->contacts = contacts;
	tw->maxContacts = maxContacts;
	tw->numContacts = 0;
	tw->dir.Zero();

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

	idMat3 traceAxis = trmAxis;
	if ( modelRotated ) {
		traceAxis *= inverseModelAxis;
	}
	for ( int i = 1; i <= tw->numEdges; ++i ) {
		tw->edges[ i ].used = true;
		tw->edges[ i ].vertexNum[ 0 ] = trm->edges[ i ].v[ 0 ];
		tw->edges[ i ].vertexNum[ 1 ] = trm->edges[ i ].v[ 1 ];
		tw->edges[ i ].cross = trm->edges[ i ].normal;
		if ( traceAxis.IsRotated() ) {
			tw->edges[ i ].cross *= traceAxis;
		}
		tw->edges[ i ].pl.FromLine(
			tw->vertices[ tw->edges[ i ].vertexNum[ 0 ] ].p,
			tw->vertices[ tw->edges[ i ].vertexNum[ 1 ] ].p );
	}
	for ( int i = 0; i < tw->numPolys; ++i ) {
		tw->polys[ i ].used = true;
		idVec3 normal = trm->polys[ i ].normal;
		if ( traceAxis.IsRotated() ) {
			normal *= traceAxis;
		}
		tw->polys[ i ].plane.SetNormal( normal );
		const cm_trmEdge_t &edge = tw->edges[ abs( tw->polys[ i ].edges[ 0 ] ) ];
		tw->polys[ i ].plane.FitThroughPoint( tw->vertices[ edge.vertexNum[ 0 ] ].p );
	}

	const idBounds relativeBounds = tw->trmBounds.ToBounds();
	idBounds floatTraceBounds;
	const idVec3 contactExpansion( depth + CM_BOX_EPSILON,
		depth + CM_BOX_EPSILON, depth + CM_BOX_EPSILON );
	floatTraceBounds[ 0 ] = tw->start + relativeBounds[ 0 ] - contactExpansion;
	floatTraceBounds[ 1 ] = tw->start + relativeBounds[ 1 ] + contactExpansion;
	tw->traceBounds.SetBounds( floatTraceBounds );
	for ( int i = 0; i < 3; ++i ) {
		tw->trmExtents[ i ] = Max( idMath::Fabs( relativeBounds[ 0 ][ i ] ),
			idMath::Fabs( relativeBounds[ 1 ][ i ] ) ) + CM_BOX_EPSILON;
	}

	TraceThroughModel( tw );

	for ( int i = 0; i < tw->numContacts; ++i ) {
		contactInfo_t &contact = tw->contacts[ i ];
		if ( modelRotated ) {
			contact.normal *= modelAxis;
			contact.point *= modelAxis;
		}
		contact.point += modelOrigin;
		contact.dist += contact.normal * modelOrigin;
	}
	return tw->numContacts;
}
