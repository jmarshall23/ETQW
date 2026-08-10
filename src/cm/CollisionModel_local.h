/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

/*
===============================================================================

	Trace model vs. polygonal model collision detection.

===============================================================================
*/

#include "CollisionModel.h"
#include "../framework/CVarSystem.h"
#include "../framework/Session.h"
#include "../renderer/Model.h"
#include "../renderer/ModelManager.h"
#include "../renderer/Material.h"
#include "../renderer/RenderWorld.h"
#include "../decllib/declTypeHolder.h"

// Temporary compatibility type for the inherited Doom 3 implementation.
// ETQW's public interface exposes idCollisionModel pointers; the integer
// handles remain private until each entry point is converted to the
// symbol-backed object interface.
typedef int cmHandle_t;

#ifndef CONTENTS_REMOVE_UTIL
#define CONTENTS_REMOVE_UTIL ( ~( CONTENTS_AREAPORTAL | CONTENTS_NOCSG | CONTENTS_OCCLUDER ) )
#endif

#define MIN_NODE_SIZE						64.0f
#define MAX_NODE_POLYGONS					128
#define CM_MAX_POLYGON_EDGES				64
#define CIRCLE_APPROXIMATION_LENGTH			64.0f

#define	MAX_SUBMODELS						2048
#define	TRACE_MODEL_HANDLE					MAX_SUBMODELS

#define VERTEX_HASH_BOXSIZE					(1<<6)	// must be power of 2
#define VERTEX_HASH_SIZE					(VERTEX_HASH_BOXSIZE*VERTEX_HASH_BOXSIZE)
#define EDGE_HASH_SIZE						(1<<14)

#define NODE_BLOCK_SIZE_SMALL				8
#define NODE_BLOCK_SIZE_LARGE				256
#define REFERENCE_BLOCK_SIZE_SMALL			8
#define REFERENCE_BLOCK_SIZE_LARGE			256

#define MAX_WINDING_LIST					128		// quite a few are generated at times
#define INTEGRAL_EPSILON					0.01f
#define VERTEX_EPSILON						0.1f
#define CHOP_EPSILON						0.1f


typedef struct cm_windingList_s {
	int					numWindings;			// number of windings
	idFixedWinding		w[MAX_WINDING_LIST];	// windings
	idVec3				normal;					// normal for all windings
	idBounds			bounds;					// bounds of all windings in list
	idVec3				origin;					// origin for radius
	float				radius;					// radius relative to origin for all windings
	int					contents;				// winding surface contents
	int					primitiveNum;			// number of primitive the windings came from
} cm_windingList_t;

/*
===============================================================================

Collision model

===============================================================================
*/

// ETQW keeps mutable collision-test state in per-thread caches.  The model
// geometry itself is compact and immutable after loading.
typedef struct cm_vertex_s {
	idVec3					p;
} cm_vertex_t;

typedef struct cm_edge_s {
	unsigned short			vertexNum[2];
	unsigned short			internal;
	unsigned short			numUsers;
	idVec3					normal;
} cm_edge_t;

typedef struct cm_texAxis_s {
	idMat2					axis;
	unsigned short			offset[2];
} cm_texAxis_t;

typedef struct cm_polygon_s {
	idBoundsShort			bounds;
	const idMaterial *		material;
	idPlane					plane;
	signed short *			edges;
	int						contents;
	unsigned short			numEdges;
	unsigned short			primitiveNum;
	cm_texAxis_t			texAxis;
} cm_polygon_t;

typedef struct cm_polygonRef_s {
	int						polygonNum;
	struct cm_polygonRef_s *next;
} cm_polygonRef_t;

typedef struct cm_polygonRefBlock_s {
	int						size;
	cm_polygonRef_t *		nextRef;
	struct cm_polygonRefBlock_s *next;
} cm_polygonRefBlock_t;

typedef struct cm_brush_s {
	idBoundsShort			bounds;
	idPlane *				planes;
	int						numPlanes;
	int						contents;
	const idMaterial *		material;
	int						primitiveNum;
} cm_brush_t;

typedef struct cm_brushRef_s {
	int						brushNum;
	struct cm_brushRef_s *	next;
} cm_brushRef_t;

typedef struct cm_brushRefBlock_s {
	int						size;
	cm_brushRef_t *			nextRef;
	struct cm_brushRefBlock_s *next;
} cm_brushRefBlock_t;

typedef struct cm_node_s {
	int						planeType;			// node axial plane type
	float					planeDist;			// node plane distance
	cm_polygonRef_t *		polygons;			// polygons in node
	cm_brushRef_t *			brushes;			// brushes in node
	struct cm_node_s *		parent;				// parent of this node
	struct cm_node_s *		children[2];		// node children
} cm_node_t;

typedef struct cm_nodeBlock_s {
	int						size;
	cm_node_t *				nextNode;			// next node in block
	struct cm_nodeBlock_s *next;				// next block with nodes
} cm_nodeBlock_t;

#if defined( _WIN32 ) && !defined( _WIN64 )
static_assert( sizeof( cm_vertex_t ) == 12, "cm_vertex_t ABI drift" );
static_assert( sizeof( cm_edge_t ) == 20, "cm_edge_t ABI drift" );
static_assert( sizeof( cm_polygon_t ) == 64, "cm_polygon_t ABI drift" );
static_assert( sizeof( cm_brush_t ) == 32, "cm_brush_t ABI drift" );
static_assert( sizeof( cm_polygonRef_t ) == 8, "cm_polygonRef_t ABI drift" );
static_assert( sizeof( cm_brushRef_t ) == 8, "cm_brushRef_t ABI drift" );
static_assert( sizeof( cm_node_t ) == 28, "cm_node_t ABI drift" );
#endif

class idCollisionModelLocal : public idCollisionModel {
public:
	idCollisionModelLocal();
	virtual ~idCollisionModelLocal();

	virtual const char *		GetName( void ) const { return name.c_str(); }
	virtual const idBounds &	GetBounds( void ) const { return bounds; }
	virtual void				GetBounds( idBounds &outBounds, int surfaceMask, bool inclusive ) const;
	virtual int					GetContents( void ) const { return contents; }
	virtual const idVec3 &		GetVertex( int vertexNum ) const;
	virtual void				GetEdge( int edgeNum, idVec3 &start, idVec3 &end ) const;
	virtual void				GetPolygon( int polygonNum, idFixedWinding &winding ) const;
	virtual void				Draw( int surfaceMask, bool inclusive ) const;
	virtual int					GetNumBrushPlanes( void ) const;
	virtual const idPlane &		GetBrushPlane( int planeNum ) const;
	virtual const idMaterial *	GetPolygonMaterial( int polygonNum ) const;
	virtual const idPlane &		GetPolygonPlane( int polygonNum ) const;
	virtual int					GetNumPolygons( void ) const;
	virtual bool				IsTraceModel( void ) const;
	virtual bool				IsConvex( void ) const;
	virtual bool				IsWorld( void ) const;
	virtual void				SetWorld( bool tf );

	void						NodeBounds_r( const cm_node_t *node, idBounds &outBounds, int surfaceMask, bool inclusive ) const;
	void						DrawNode_r( const cm_node_t *node, int surfaceMask, bool inclusive ) const;

	idStr					name;				// model name
	int						refCount;
	idBounds				bounds;				// model bounds
	int						contents;			// all contents of the model ored together
	bool					isTraceModel;
	bool					isConvex;			// set if model is convex
	bool					isWorld;
	// model geometry
	int						maxVertices;		// size of vertex array
	int						numVertices;		// number of vertices
	cm_vertex_t *			vertices;			// array with all vertices used by the model
	int						maxEdges;			// size of edge array
	int						numEdges;			// number of edges
	cm_edge_t *				edges;				// array with all edges used by the model
	int						maxPolygonEdges;
	int						numPolygonEdges;
	signed short *			polygonEdges;
	int						maxPolygons;
	int						numPolygons;
	cm_polygon_t *			polygons;
	int						maxBrushPlanes;
	int						numBrushPlanes;
	idPlane *				brushPlanes;
	int						maxBrushes;
	int						numBrushes;
	cm_brush_t *			brushes;
	int						numNodes;
	cm_node_t *				node;				// first node of spatial subdivision
	// blocks with allocated memory
	cm_nodeBlock_t *		nodeBlocks;			// list with blocks of nodes
	cm_polygonRefBlock_t *	polygonRefBlocks;	// list with blocks of polygon references
	cm_brushRefBlock_t *	brushRefBlocks;		// list with blocks of brush references
	idVec2 *					texCoords;
	// statistics
	int						numMergedPolys;
	int						numRemovedPolys;
	int						numSharpEdges;
	int						numInternalEdges;
	int						numPolygonRefs;
	int						numBrushRefs;
	int						numPrimitives;
	int						usedMemory;
	idList< cm_contents_override_t > contentsOverrides;
};

typedef idCollisionModelLocal cm_model_t;

/*
===============================================================================

Data used during collision detection calculations

===============================================================================
*/

typedef struct cm_trmVertex_s {
	int used;										// true if this vertex is used for collision detection
	idVec3 p;										// vertex position
	idVec3 endp;									// end point of vertex after movement
	int polygonSide;								// side of polygon this vertex is on (rotational collision)
	idPluecker pl;									// pluecker coordinate for vertex movement
	idVec3 rotationOrigin;							// rotation origin for this vertex
	idBounds rotationBounds;						// rotation bounds for this vertex
} cm_trmVertex_t;

typedef struct cm_trmEdge_s {
	int used;										// true when vertex is used for collision detection
	idVec3 start;									// start of edge
	idVec3 end;										// end of edge
	int vertexNum[2];								// indexes into cm_traceWork_t->vertices
	idPluecker pl;									// pluecker coordinate for edge
	idVec3 cross;									// (z,-y,x) of cross product between edge dir and movement dir
	idBounds rotationBounds;						// rotation bounds for this edge
	idPluecker plzaxis;								// pluecker coordinate for rotation about the z-axis
	unsigned short bitNum;							// vertex bit number
} cm_trmEdge_t;

typedef struct cm_trmPolygon_s {
	int used;
	idPlane plane;									// polygon plane
	int numEdges;									// number of edges
	int edges[MAX_TRACEMODEL_POLYEDGES];			// index into cm_traceWork_t->edges
	idBounds rotationBounds;						// rotation bounds for this polygon
} cm_trmPolygon_t;

// ETQW keeps mutable trace state outside the compact model geometry.
typedef struct cm_vertexCache_s {
	unsigned short checkcount;
} cm_vertexCache_t;

typedef cm_vertexCache_t cm_edgeCache_t;
typedef cm_vertexCache_t cm_polygonCache_t;
typedef cm_vertexCache_t cm_brushCache_t;

typedef struct cm_vertexSideCache_s {
	unsigned int side;
	unsigned int sideSet;
} cm_vertexSideCache_t;

typedef cm_vertexSideCache_t cm_edgeSideCache_t;

typedef struct idModelCache_s {
	cm_vertexCache_t *		vertexCache;
	cm_vertexSideCache_t *	vertexSideCache;
	int						maxVertices;
	cm_edgeCache_t *		edgeCache;
	cm_edgeSideCache_t *	edgeSideCache;
	int						maxEdges;
	cm_polygonCache_t *		polygonCache;
	int						maxPolygons;
	cm_brushCache_t *		brushCache;
	int						maxBrushes;
	contactInfo_t *			contacts;
	int						maxContacts;
	int						numContacts;
	unsigned short			checkCount;

	void UpdateForModel( idCollisionModelLocal *model );
	void IncCheckCount( void );
	void Shutdown( void );
	void Copy( const idModelCache_s &base );
} idModelCache;

#if defined( _WIN32 ) && !defined( _WIN64 )
static_assert( offsetof( idModelCache, checkCount ) == 52, "idModelCache::checkCount ABI drift" );
static_assert( sizeof( idModelCache ) == 56, "idModelCache ABI drift" );
#endif

typedef struct cm_traceWork_s {
	int numVerts;
	cm_trmVertex_t vertices[MAX_TRACEMODEL_VERTS];	// trm vertices
	int numEdges;
	cm_trmEdge_t edges[MAX_TRACEMODEL_EDGES+1];		// trm edges
	int numPolys;
	cm_trmPolygon_t polys[MAX_TRACEMODEL_POLYS];	// trm polygons
	cm_model_t *model;								// model colliding with
	idVec3 start;									// start of trace
	idVec3 end;										// end of trace
	idVec3 dir;										// trace direction
	idBounds bounds;								// bounds of full trace
	idBounds size;									// bounds of transformed trm relative to start
	idVec3 extents;									// largest of abs(size[0]) and abs(size[1]) for BSP trace
	idBoundsShort trmBounds;
	idBoundsShort traceBounds;
	int contents;									// ignore polygons that do not have any of these contents flags
	trace_t trace;									// collision detection result
	idModelCache modelCache;

	bool rotation;									// true if calculating rotational collision
	bool pointTrace;								// true if only tracing a point
	bool positionTest;								// true if not tracing but doing a position test
	bool isConvex;									// true if the trace model is convex
	bool axisIntersectsTrm;							// true if the rotation axis intersects the trace model
	bool getContacts;								// true if retrieving contacts
	bool quickExit;									// set to quickly stop the collision detection calculations

	idVec3 origin;									// origin of rotation in model space
	idVec3 axis;									// rotation axis in model space
	idMat3 matrix;									// rotates axis of rotation to the z-axis
	float angle;									// angle for rotational collision
	float maxTan;									// max tangent of half the positive angle used instead of fraction
	float radius;									// rotation radius of trm start
	idRotation modelVertexRotation;					// inverse rotation for model vertices

	contactInfo_t *contacts;						// array with contacts
	int maxContacts;								// max size of contact array
	int numContacts;								// number of contacts found
	float contactDepth;

	idPlane heartPlane1;							// polygons should be near anough the trace heart planes
	float maxDistFromHeartPlane1;
	idPlane heartPlane2;
	float maxDistFromHeartPlane2;
	idPluecker polygonEdgePlueckerCache[CM_MAX_POLYGON_EDGES];
	idPluecker polygonVertexPlueckerCache[CM_MAX_POLYGON_EDGES];
	idVec3 polygonRotationOriginCache[CM_MAX_POLYGON_EDGES];

	// ETQW selects the collision operation explicitly instead of deriving it
	// from the legacy Doom 3 boolean flags.  These fields are retained here
	// while the rest of idTraceWork is migrated to its retail layout.
	int traceType;
	idVec3 trmExtents;
} cm_traceWork_t;

typedef cm_traceWork_t idTraceWork;

/*
===============================================================================

Collision Map

===============================================================================
*/

class idCollisionModelManagerLocal : public idCollisionModelManager {
public:
					idCollisionModelManagerLocal( void );
	virtual			~idCollisionModelManagerLocal( void );

	virtual void	Init( void );
	virtual void	Shutdown( void );
	virtual void	AllocThread( void );
	virtual void	FreeThread( void );
	virtual int		GetThreadId( void );
	virtual int		GetThreadCount( void );

	virtual void	LoadMap( const char *fileName, bool forceReload );
	virtual idCollisionModel *LoadModel( const char *mapName, const char *modelName );
	virtual void	FreeModel( idCollisionModel *model );
	virtual void	PurgeModels( void );
	virtual idCollisionModel *ModelFromTrm( const char *mapName, const char *modelName, const idTraceModel &trm, bool includeBrushes );
	virtual bool	TrmFromModel( const char *mapName, const char *modelName, idTraceModel &trm );
	virtual int		CompoundTrmFromModel( const char *mapName, const char *modelName, idTraceModel *trms, int maxTrms );
	virtual void	CreateCollisionFromWorld( const collisionWorldFile_t &world );

	virtual void	Translation( trace_t *results, const idVec3 &start, const idVec3 &end,
								const idTraceModel *trm, const idMat3 &trmAxis, int contentMask,
								idCollisionModel *model, const idVec3 &modelOrigin, const idMat3 &modelAxis );
	virtual void	Rotation( trace_t *results, const idVec3 &start, const idRotation &rotation,
								const idTraceModel *trm, const idMat3 &trmAxis, int contentMask,
								idCollisionModel *model, const idVec3 &modelOrigin, const idMat3 &modelAxis );
	virtual int		Contents( const idVec3 &start, const idTraceModel *trm, const idMat3 &trmAxis,
								int contentMask, idCollisionModel *model,
								const idVec3 &modelOrigin, const idMat3 &modelAxis );
	virtual int		Contacts( contactInfo_t *contacts, const int maxContacts, const idVec3 &start,
								const idVec3 *dir, const float depth, const idTraceModel *trm,
								const idMat3 &trmAxis, int contentMask, idCollisionModel *model,
								const idVec3 &modelOrigin, const idMat3 &modelAxis );
	virtual void	DebugOutput( const idVec3 &viewOrigin, const idMat3 &viewAxis );
	virtual void	DrawModel( idCollisionModel *model, const idVec3 &modelOrigin, const idMat3 &modelAxis,
								const idVec3 &viewOrigin, const idMat3 &viewAxis,
								const float radius, int lifetime );
	virtual void	GetFullModelName( idStr &out, const char *mapName, const char *modelName ) const;
	virtual void	DumpCollisionModelStats( void );
	static const sdDeclSurfaceType *GetSurfaceType( contactInfo_t *contact, cm_polygon_t *polygon, idVec3 *color );

	// load collision models from a map file
	void			LoadMap( const idMapFile *mapFile );
	// frees all the collision models
	void			FreeMap( void );

	// get clip handle for model
	cmHandle_t		LoadModel( const char *modelName, const bool precache );
	// sets up a trace model for collision with other trace models
	cmHandle_t		SetupTrmModel( const idTraceModel &trm, const idMaterial *material );
	// create trace model from a collision model, returns true if succesfull
	bool			TrmFromModel( const char *modelName, idTraceModel &trm );

	// name of the model
	const char *	GetModelName( cmHandle_t model ) const;
	// bounds of the model
	bool			GetModelBounds( cmHandle_t model, idBounds &bounds ) const;
	// all contents flags of brushes and polygons ored together
	bool			GetModelContents( cmHandle_t model, int &contents ) const;
	// get the vertex of a model
	bool			GetModelVertex( cmHandle_t model, int vertexNum, idVec3 &vertex ) const;
	// get the edge of a model
	bool			GetModelEdge( cmHandle_t model, int edgeNum, idVec3 &start, idVec3 &end ) const;
	// get the polygon of a model
	bool			GetModelPolygon( cmHandle_t model, int polygonNum, idFixedWinding &winding ) const;

	// translates a trm and reports the first collision if any
	void			Translation( trace_t *results, const idVec3 &start, const idVec3 &end,
								const idTraceModel *trm, const idMat3 &trmAxis, int contentMask,
								cmHandle_t model, const idVec3 &modelOrigin, const idMat3 &modelAxis );
	// rotates a trm and reports the first collision if any
	void			Rotation( trace_t *results, const idVec3 &start, const idRotation &rotation,
								const idTraceModel *trm, const idMat3 &trmAxis, int contentMask,
								cmHandle_t model, const idVec3 &modelOrigin, const idMat3 &modelAxis );
	// returns the contents the trm is stuck in or 0 if the trm is in free space
	int				Contents( const idVec3 &start,
								const idTraceModel *trm, const idMat3 &trmAxis, int contentMask,
								cmHandle_t model, const idVec3 &modelOrigin, const idMat3 &modelAxis );
	// stores all contact points of the trm with the model, returns the number of contacts
	int				Contacts( contactInfo_t *contacts, const int maxContacts, const idVec3 &start, const idVec6 &dir, const float depth,
								const idTraceModel *trm, const idMat3 &trmAxis, int contentMask,
								cmHandle_t model, const idVec3 &modelOrigin, const idMat3 &modelAxis );
	// test collision detection
	void			DebugOutput( const idVec3 &origin );
	// draw a model
	void			DrawModel( cmHandle_t model, const idVec3 &origin, const idMat3 &axis,
											const idVec3 &viewOrigin, const float radius );
	// print model information, use -1 handle for accumulated model info
	void			ModelInfo( cmHandle_t model );
	// list all loaded models
	void			ListModels( void );
	// write a collision model file for the map entity
	bool			WriteCollisionModelForMapEntity( const idMapEntity *mapEnt, const char *filename, const bool testTraceModel = true );

private:			// CollisionMap_translate.cpp
	cmHandle_t		HandleForModel( const idCollisionModel *model ) const;
	int				TranslateEdgeThroughEdge( idVec3 &cross, idPluecker &l1, idPluecker &l2, float *fraction );
	void			TranslateTrmEdgeThroughPolygon( cm_traceWork_t *tw, cm_polygon_t *poly, cm_trmEdge_t *trmEdge );
	void			TranslateTrmVertexThroughPolygon( cm_traceWork_t *tw, cm_polygon_t *poly, cm_trmVertex_t *v, int bitNum );
	void			TranslatePointThroughPolygon( cm_traceWork_t *tw, cm_polygon_t *poly, cm_trmVertex_t *v );
	void			TranslateVertexThroughTrmPolygon( cm_traceWork_t *tw, cm_trmPolygon_t *trmpoly, cm_polygon_t *poly, cm_vertex_t *v, idVec3 &endp, idPluecker &pl );
	bool			TranslateTrmThroughPolygon( cm_traceWork_t *tw, cm_polygon_t *p );
	bool			TranslatePointTrmThroughPolygon( idTraceWork *tw, int polygonNum );
	bool			TranslateTrmThroughPolygon( idTraceWork *tw, int polygonNum );
	void			SetupTranslationHeartPlanes( cm_traceWork_t *tw );
	void			SetupTrm( cm_traceWork_t *tw, const idTraceModel *trm );

private:			// CollisionMap_rotate.cpp
	int				CollisionBetweenEdgeBounds( cm_traceWork_t *tw, const idVec3 &va, const idVec3 &vb,
											const idVec3 &vc, const idVec3 &vd, float tanHalfAngle,
											idVec3 &collisionPoint, idVec3 &collisionNormal );
	int				RotateEdgeThroughEdge( cm_traceWork_t *tw, const idPluecker &pl1,
											const idVec3 &vc, const idVec3 &vd,
											const float minTan, float &tanHalfAngle );
	int				EdgeFurthestFromEdge( cm_traceWork_t *tw, const idPluecker &pl1,
											const idVec3 &vc, const idVec3 &vd,
											float &tanHalfAngle, float &dir );
	void			RotateTrmEdgeThroughPolygon( cm_traceWork_t *tw, cm_polygon_t *poly, cm_trmEdge_t *trmEdge );
	int				RotatePointThroughPlane( const cm_traceWork_t *tw, const idVec3 &point, const idPlane &plane,
											const float angle, const float minTan, float &tanHalfAngle );
	int				PointFurthestFromPlane( const cm_traceWork_t *tw, const idVec3 &point, const idPlane &plane,
											const float angle, float &tanHalfAngle, float &dir );
	int				RotatePointThroughEpsilonPlane( const cm_traceWork_t *tw, const idVec3 &point, const idVec3 &endPoint,
											const idPlane &plane, const float angle, const idVec3 &origin,
											float &tanHalfAngle, idVec3 &collisionPoint, idVec3 &endDir );
	void			RotateTrmVertexThroughPolygon( cm_traceWork_t *tw, cm_polygon_t *poly, cm_trmVertex_t *v, int vertexNum);
	void			RotateVertexThroughTrmPolygon( cm_traceWork_t *tw, cm_trmPolygon_t *trmpoly, cm_polygon_t *poly,
											cm_vertex_t *v, idVec3 &rotationOrigin );
	bool			RotateTrmThroughPolygon( cm_traceWork_t *tw, cm_polygon_t *p );
	bool			RotateTrmThroughPolygon( idTraceWork *tw, int polygonNum );
	void			BoundsForRotation( const idVec3 &origin, const idVec3 &axis, const idVec3 &start, const idVec3 &end, idBounds &bounds );
	void			Rotation180( trace_t *results, const idVec3 &rorg, const idVec3 &axis,
									const float startAngle, const float endAngle, const idVec3 &start,
									const idTraceModel *trm, const idMat3 &trmAxis, int contentMask,
									idCollisionModel *model, const idVec3 &origin, const idMat3 &modelAxis );

private:			// CollisionMap_contents.cpp
	static void		TestTrmEdgeInContactWithPolygon( idTraceWork *tw, cm_polygon_t *polygon, cm_trmEdge_t *traceEdge );
	static void		TestTrmVertexInContactWithPolygon( idTraceWork *tw, cm_polygon_t *polygon, cm_trmVertex_t *vertex );
	static void		TestVertexInContactWithTrmPolygon( idTraceWork *tw, cm_trmPolygon_t *tracePolygon,
									cm_polygon_t *polygon, cm_vertex_t *vertex );
	bool			TestTrmVertsInBrush( cm_traceWork_t *tw, cm_brush_t *b );
	bool			TestTrmInPolygon( cm_traceWork_t *tw, cm_polygon_t *p );
	bool			TestTrmVertsInBrush( idTraceWork *tw, int brushNum );
	bool			TestTrmInPolygon( idTraceWork *tw, int polygonNum );
	bool			TestTrmInContactWithPolygon( idTraceWork *tw, int polygonNum );
	static cm_node_t *PointNode( const idVec3 &p, cm_model_t *model );
	int				PointContents( const idVec3 p, cmHandle_t model );
	int				TransformedPointContents( const idVec3 &p, cmHandle_t model, const idVec3 &origin, const idMat3 &modelAxis );
	int				ContentsTrm( trace_t *results, const idVec3 &start,
									const idTraceModel *trm, const idMat3 &trmAxis, int contentMask,
									cmHandle_t model, const idVec3 &modelOrigin, const idMat3 &modelAxis );
	static int		PointContents( const idVec3 point, idCollisionModelLocal *model );
	static int		TransformedPointContents( const idVec3 &point, idCollisionModelLocal *model,
									const idVec3 &origin, const idMat3 &modelAxis );
	int				ContentsTrm( trace_t *results, const idVec3 &start,
									const idTraceModel *trm, const idMat3 &trmAxis, int contentMask,
									idCollisionModel *model, const idVec3 &modelOrigin, const idMat3 &modelAxis );

private:			// CollisionMap_trace.cpp
	void			TraceTrmThroughNode( cm_traceWork_t *tw, cm_node_t *node );
	void			TraceThroughAxialBSPTree_r( cm_traceWork_t *tw, cm_node_t *node, float p1f, float p2f, idVec3 &p1, idVec3 &p2);
	void			TraceThroughModel( cm_traceWork_t *tw );
	void			RecurseProcBSP_r( trace_t *results, int parentNodeNum, int nodeNum, float p1f, float p2f, const idVec3 &p1, const idVec3 &p2 );

private:			// CollisionMap_load.cpp
	static void		ClearModel( cm_model_t *model );
	static void		FreeModelMemory( cm_model_t *model );
	void			Clear( void );
	void			FreeTrmModelStructure( void );
					// model deallocation
	void			RemovePolygonReferences_r( cm_node_t *node, cm_polygon_t *p );
	void			RemoveBrushReferences_r( cm_node_t *node, cm_brush_t *b );
	void			FreeNode( cm_node_t *node );
	void			FreePolygonReference( cm_polygonRef_t *pref );
	void			FreeBrushReference( cm_brushRef_t *bref );
	void			FreePolygon( cm_model_t *model, cm_polygon_t *poly );
	void			FreeBrush( cm_model_t *model, cm_brush_t *brush );
	void			FreeTree_r( cm_model_t *model, cm_node_t *headNode, cm_node_t *node );
	void			FreeModel( cm_model_t *model );
					// merging polygons
	void			ReplacePolygons( cm_model_t *model, cm_node_t *node, cm_polygon_t *p1, cm_polygon_t *p2, cm_polygon_t *newp );
	cm_polygon_t *	TryMergePolygons( cm_model_t *model, cm_polygon_t *p1, cm_polygon_t *p2 );
	bool			MergePolygonWithTreePolygons( cm_model_t *model, cm_node_t *node, cm_polygon_t *polygon );
	void			MergeTreePolygons( cm_model_t *model, cm_node_t *node );
					// finding internal edges
	bool			PointInsidePolygon( cm_model_t *model, cm_polygon_t *p, idVec3 &v );
	void			FindInternalEdgesOnPolygon( cm_model_t *model, cm_polygon_t *p1, cm_polygon_t *p2 );
	void			FindInternalPolygonEdges( cm_model_t *model, cm_node_t *node, cm_polygon_t *polygon );
	void			FindInternalEdges( cm_model_t *model, cm_node_t *node );
	void			FindContainedEdges( cm_model_t *model, cm_polygon_t *p );
					// loading of proc BSP tree
	void			ParseProcNodes( idLexer *src );
	void			LoadProcBSP( const char *name );
					// removal of contained polygons
	int				R_ChoppedAwayByProcBSP( int nodeNum, idFixedWinding *w, const idVec3 &normal, const idVec3 &origin, const float radius );
	int				ChoppedAwayByProcBSP( const idFixedWinding &w, const idPlane &plane, int contents );
	void			ChopWindingListWithBrush( cm_windingList_t *list, cm_brush_t *b );
	void			R_ChopWindingListWithTreeBrushes( cm_windingList_t *list, cm_node_t *node );
	idFixedWinding *WindingOutsideBrushes( idFixedWinding *w, const idPlane &plane, int contents, int patch, cm_node_t *headNode );
					// creation of axial BSP tree
	cm_model_t *	AllocModel( void );
	cm_node_t *		AllocNode( cm_model_t *model, int blockSize );
	cm_polygonRef_t*AllocPolygonReference( cm_model_t *model, int blockSize );
	cm_brushRef_t *	AllocBrushReference( cm_model_t *model, int blockSize );
	cm_polygon_t *	AllocPolygon( cm_model_t *model, int numEdges );
	cm_brush_t *	AllocBrush( cm_model_t *model, int numPlanes );
	void			AddPolygonToNode( cm_model_t *model, cm_node_t *node, cm_polygon_t *p );
	void			AddBrushToNode( cm_model_t *model, cm_node_t *node, cm_brush_t *b );
	void			SetupTrmModelStructure( void );
	void			R_FilterPolygonIntoTree( cm_model_t *model, cm_node_t *node, cm_polygonRef_t *pref, cm_polygon_t *p );
	void			R_FilterBrushIntoTree( cm_model_t *model, cm_node_t *node, cm_brushRef_t *pref, cm_brush_t *b );
	cm_node_t *		R_CreateAxialBSPTree( cm_model_t *model, cm_node_t *node, const idBounds &bounds );
	cm_node_t *		CreateAxialBSPTree( cm_model_t *model, cm_node_t *node );
					// creation of raw polygons
	void			SetupHash(void);
	void			ShutdownHash(void);
	void			ClearHash( idBounds &bounds );
	int				HashVec(const idVec3 &vec);
	int				GetVertex( cm_model_t *model, const idVec3 &v, int *vertexNum );
	int				GetEdge( cm_model_t *model, const idVec3 &v1, const idVec3 &v2, int *edgeNum, int v1num );
	void			CreatePolygon( cm_model_t *model, idFixedWinding *w, const idPlane &plane, const idMaterial *material, int primitiveNum );
	void			PolygonFromWinding( cm_model_t *model, idFixedWinding *w, const idPlane &plane, const idMaterial *material, int primitiveNum );
	void			CalculateEdgeNormals( cm_model_t *model, cm_node_t *node );
	void			CreatePatchPolygons( cm_model_t *model, idSurface_Patch &mesh, const idMaterial *material, int primitiveNum );
	void			ConvertPatch( cm_model_t *model, const idMapPatch *patch, int primitiveNum );
	void			ConvertBrushSides( cm_model_t *model, const idMapBrush *mapBrush, int primitiveNum );
	void			ConvertBrush( cm_model_t *model, const idMapBrush *mapBrush, int primitiveNum );
	void			PrintModelInfo( const cm_model_t *model );
	void			AccumulateModelInfo( cm_model_t *model );
	void			RemapEdges( cm_node_t *node, int *edgeRemap );
	void			OptimizeArrays( cm_model_t *model );
	void			FinishModel( cm_model_t *model );
	void			BuildModels( const idMapFile *mapFile );
	cmHandle_t		FindModel( const char *name );
	cm_model_t *	CollisionModelForMapEntity( const idMapEntity *mapEnt );	// brush/patch model from .map
	cm_model_t *	LoadRenderModel( const char *fileName );					// ASE/LWO models
	bool			TrmFromModel_r( idTraceModel &trm, cm_node_t *node );
	bool			TrmFromModel( const cm_model_t *model, idTraceModel &trm );

private:			// CollisionMap_files.cpp
					// writing
	void			WriteNodes( idFile *fp, cm_node_t *node );
	int				CountPolygonMemory( cm_model_t *model, cm_node_t *node );
	void			WritePolygons( idFile *fp, cm_model_t *model, cm_node_t *node );
	int				CountBrushMemory( cm_model_t *model, cm_node_t *node );
	void			WriteBrushes( idFile *fp, cm_model_t *model, cm_node_t *node );
	void			WriteCollisionModel( idFile *fp, cm_model_t *model );
	void			WriteCollisionModelsToFile( const char *filename, int firstModel, int lastModel, unsigned int mapFileCRC );
					// loading
	cm_node_t *		ParseNodes( idLexer *src, cm_model_t *model, cm_node_t *parent );
	void			ParseVertices( idLexer *src, cm_model_t *model );
	void			ParseEdges( idLexer *src, cm_model_t *model );
	void			ParsePolygons( idLexer *src, cm_model_t *model );
	void			ParseBrushes( idLexer *src, cm_model_t *model );
	bool			ParseCollisionModel( idLexer *src );
	cm_node_t *	ParseNodesBinary( idFile *file, cm_model_t *model, cm_node_t *parent, bool &valid );
	bool			ParseVerticesBinary( idFile *file, cm_model_t *model );
	bool			ParseEdgesBinary( idFile *file, cm_model_t *model );
	bool			ParsePolygonsBinary( idFile *file, cm_model_t *model, const idList< const idMaterial * > &materialCache );
	bool			ParseBrushesBinary( idFile *file, cm_model_t *model );
	bool			ParseCollisionModelBinary( idFile *file, const char *sourceName );
	bool			LoadCollisionModelFileBinary( const char *name );
	bool			LoadCollisionModelFile( const char *name, unsigned int mapFileCRC );

private:			// CollisionMap_debug
	int				ContentsFromString( const char *string, int startIndex = 1 ) const;
	const char *	StringFromContents( const int contents ) const;
	void			DrawEdge( cm_model_t *model, int edgeNum, const idVec3 &origin, const idMat3 &axis,
						const idVec3 &viewOrigin, const idMat3 &viewAxis, float radius, int lifetime );
	void			DrawPolygon( cm_model_t *model, cm_polygon_t *p, const idVec3 &origin, const idMat3 &axis,
								const idVec3 &viewOrigin, const idMat3 &viewAxis, float radius, int lifetime );
	void			DrawNodePolygons( cm_model_t *model, cm_node_t *node, const idVec3 &origin, const idMat3 &axis,
								const idVec3 &viewOrigin, const idMat3 &viewAxis, const float radius, int lifetime );

private:			// collision map data
	idStr			mapName;
	unsigned int		mapFileTime;
	int				loaded;
					// for multi-check avoidance
	int				checkCount;
					// models
	int				maxModels;
	int				numModels;
	cm_model_t **	models;
					// polygons and brush for trm model
	cm_polygonRef_t*trmPolygons[MAX_TRACEMODEL_POLYS];
	cm_brushRef_t *	trmBrushes[1];
	const idMaterial *trmMaterial;
					// for data pruning
	int				numProcNodes;
	cm_procNode_t *	procNodes;
					// for retrieving contact points
	bool			getContacts;
	contactInfo_t *	contacts;
	int				maxContacts;
	int				numContacts;
	idTraceWork *	baseTraceWork;
	idTraceWork *	traceWork[ 32 ];
	int				threadCount;
};

// for debugging
extern idCVar cm_debugCollision;
