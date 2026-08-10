// Copyright (C) 2007 Id Software, Inc.
//
// Previous-frame hardware occlusion system reconstructed from
// renderer/draw_ocq.obj.  Query results are deliberately consumed one frame
// later, matching ETQW's non-blocking path.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "RenderSystem.h"
#include "draw_local.h"
#include "tr_render.h"
#include "renderbindings.h"
#include "../decllib/DeclRenderProgram_opengl.h"
#include "../libs/qglLib/qgl.h"

extern glconfig_t glConfig;

idCVar r_useOcclusionQueries( "r_useOcclusionQueries", "1", CVAR_RENDERER | CVAR_BOOL, "Use hardware occlusion queries" );
idCVar r_useOcclusionQueriesCulling( "r_useOcclusionQueriesCulling", "1", CVAR_RENDERER | CVAR_BOOL, "Use hardware occlusion queries for culling" );
idCVar r_showOcclusions( "r_showOcclusions", "0", CVAR_RENDERER | CVAR_BOOL, "Show occlusion queries culled by occlusion queries" );
idCVar r_occlusionDebug( "r_occlusionDebug", "0", CVAR_RENDERER | CVAR_INTEGER, "Print debug info on occlusion queries, 2 = skip all oq gl commands, 3 = skip oq readback command" );
idCVar r_occlusionBBDebug( "r_occlusionBBDebug", "0", CVAR_RENDERER | CVAR_INTEGER, "Display occlusion BB test" );
idCVar r_occlusionWaitPredict( "r_occlusionWaitPredict", "1", CVAR_RENDERER | CVAR_BOOL, "Predict as objects as visibe when waiting for result." );
idCVar r_occlusionFlush( "r_occlusionFlush", "0", CVAR_RENDERER | CVAR_BOOL, "Use explicit glFlush after firing off queries" );
idCVar r_occlusionSystem( "r_occlusionSystem", "1", CVAR_RENDERER | CVAR_INTEGER, "0 = Clever, 1 = Frame delay" );
idCVar r_occlusionCutoff( "r_occlusionCutoff", "360000", CVAR_RENDERER | CVAR_INTEGER, "Don't do occlusion queries if screen space in pixels is bigger than this" );
idCVar r_occlusionThreshold( "r_occlusionThreshold", "40", CVAR_RENDERER | CVAR_INTEGER, "Consider object as occluded if less or equal than x pixels are visible." );
idCVar sm_occlusionQueries( "sm_occlusionQueries", "1", CVAR_RENDERER | CVAR_BOOL, "Use occlusion queries in shadow map rendering." );
idCVar r_occlusionsMaxFrames( "r_occlusionsMaxFrames", "10", CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER, "Max num of frames before query is retired", 10.0f, 100.0f );

namespace {
	// The reconstructed renderer executes views immediately instead of through
	// the retail back-end command queue, so this is the local equivalent of
	// backEnd.frameCount used by the original occlusion-query code.
	int occlusionFrame = 0;

	bool QueryPathAvailable() {
		return r_useOcclusionQueries.GetBool() && glConfig.occlusionQueryAvailable &&
			qglGenQueriesARB != NULL && qglBeginQueryARB != NULL && qglEndQueryARB != NULL && qglGetQueryObjectivARB != NULL;
	}
}

class sdOcclusionQueryWrapper {
public:
	sdOcclusionQueryWrapper() : ID( 0 ), queryIdx( -1 ), issueFramenum( -1 ), nextUsed( NULL ), nextFree( NULL ) {
		if ( QueryPathAvailable() ) qglGenQueriesARB( 1, &ID );
	}
	~sdOcclusionQueryWrapper() {
		if ( ID != 0 && qglDeleteQueriesARB != NULL ) qglDeleteQueriesARB( 1, &ID );
	}
	void Begin() {
		if ( ID != 0 && r_occlusionDebug.GetInteger() != 2 ) qglBeginQueryARB( GL_SAMPLES_PASSED_ARB, ID );
	}
	void End();
	int QueryResultInternal();
	bool IsResultReady();
	static sdOcclusionQueryWrapper* GetQueryObject( int index );
	static int Retire( sdOcclusionQueryWrapper* object );
	static void ProcessInflightQueries();

	GLuint ID;
	int queryIdx;
	int issueFramenum;
	sdOcclusionQueryWrapper* nextUsed;
	sdOcclusionQueryWrapper* nextFree;
	static sdOcclusionQueryWrapper* freeHead;
	static sdOcclusionQueryWrapper* usedHead;
	static sdOcclusionQueryWrapper* usedTail;
	static int numInstances;
};

struct viewOccTest_s {
	viewOccTest_s* next;
	const occlusionTest_t* test;
	int handle;
	int occlusionIndex;
	bool culled;
};

struct sdOcclusionQuery {
	sdOcclusionQuery();
	void RunQuery( const viewLight_s* light );
	void RunQuery( const viewEntity_s* entity, const idVec3& viewOrigin );
	void RunQuery( const viewOccTest_s* test );
	void RunQuery( const occlusionTest_t* test );
	int lastVisibleCount;
	int entID;
	int viewID;
	int runFrame;
	int idx;
	sdOcclusionQueryWrapper* wrapper;
};

class sdOcclusionQueryManager {
public:
	~sdOcclusionQueryManager() { Free(); }
	void Init();
	int GetOcclusionQueryIndex( int viewID, int entityID, bool create );
	void Free();
	idList< sdOcclusionQuery > queries;
};

static sdOcclusionQueryManager occlusionManager;
static idList< drawSurf_s* > queriedDrawSurfaces;
sdOcclusionQueryWrapper* sdOcclusionQueryWrapper::freeHead = NULL;
sdOcclusionQueryWrapper* sdOcclusionQueryWrapper::usedHead = NULL;
sdOcclusionQueryWrapper* sdOcclusionQueryWrapper::usedTail = NULL;
int sdOcclusionQueryWrapper::numInstances = 0;

class sdOcclusionTestLocal : public sdOcclusionTest {
public:
	sdOcclusionTestLocal() { memset( &parms, 0, sizeof( parms ) ); }
	virtual ~sdOcclusionTestLocal() {}
	virtual void FreeOcclusionTest() {}
	virtual void UpdateOcclusionTest( const occlusionTest_t* def );
	occlusionTest_t parms;
};

sdOcclusionQuery::sdOcclusionQuery() :
	lastVisibleCount( 1 ),
	entID( -1 ),
	viewID( -1 ),
	runFrame( -1 ),
	idx( -1 ),
	wrapper( NULL ) {
}

void sdOcclusionQueryManager::Init() {
	queries.Append( sdOcclusionQuery() );
}

void sdOcclusionTestLocal::UpdateOcclusionTest( const occlusionTest_t* def ) {
	parms = *def;
}

void sdOcclusionQueryWrapper::End() {
	if ( ID == 0 || r_occlusionDebug.GetInteger() == 2 ) return;
	qglEndQueryARB( GL_SAMPLES_PASSED_ARB );
	issueFramenum = occlusionFrame;
	nextUsed = NULL;
	if ( usedTail != NULL ) usedTail->nextUsed = this;
	else usedHead = this;
	usedTail = this;
}

int sdOcclusionQueryWrapper::QueryResultInternal() {
	if ( ID == 0 || r_occlusionDebug.GetInteger() == 3 ) return 1;
	GLint result = 1;
	qglGetQueryObjectivARB( ID, GL_QUERY_RESULT_ARB, &result );
	return result;
}

bool sdOcclusionQueryWrapper::IsResultReady() {
	if ( ID == 0 || r_occlusionDebug.GetInteger() != 0 || occlusionFrame - issueFramenum > r_occlusionsMaxFrames.GetInteger() ) return true;
	GLint ready = GL_FALSE;
	qglGetQueryObjectivARB( ID, GL_QUERY_RESULT_AVAILABLE_ARB, &ready );
	return ready != GL_FALSE;
}

sdOcclusionQueryWrapper* sdOcclusionQueryWrapper::GetQueryObject( int index ) {
	sdOcclusionQueryWrapper* result = freeHead;
	if ( result != NULL ) freeHead = result->nextFree;
	else {
		result = new sdOcclusionQueryWrapper;
		++numInstances;
	}
	result->nextFree = NULL;
	result->nextUsed = NULL;
	result->queryIdx = index;
	return result;
}

int sdOcclusionQueryWrapper::Retire( sdOcclusionQueryWrapper* object ) {
	if ( object == NULL ) return 1;
	const bool expired = occlusionFrame - object->issueFramenum > r_occlusionsMaxFrames.GetInteger();
	if ( expired ) {
		// Retail discards an over-age GL query instead of blocking to read it.
		// Keep the previous visibility result and destroy the wrapper; querying
		// it here defeats r_occlusionsMaxFrames and can stall shutdown or a
		// heavily backlogged frame.
		if ( object->queryIdx >= 0 && object->queryIdx < occlusionManager.queries.Num() ) {
			occlusionManager.queries[ object->queryIdx ].wrapper = NULL;
		}
		delete object;
		--numInstances;
		return 1;
	}
	const int result = object->QueryResultInternal();
	if ( object->queryIdx >= 0 && object->queryIdx < occlusionManager.queries.Num() ) {
		sdOcclusionQuery& query = occlusionManager.queries[ object->queryIdx ];
		query.lastVisibleCount = result;
		query.wrapper = NULL;
	}
	object->queryIdx = -1;
	object->nextUsed = NULL;
	object->nextFree = freeHead;
	freeHead = object;
	return result;
}

void sdOcclusionQueryWrapper::ProcessInflightQueries() {
	while ( usedHead != NULL && usedHead->IsResultReady() ) {
		sdOcclusionQueryWrapper* object = usedHead;
		usedHead = object->nextUsed;
		if ( usedHead == NULL ) usedTail = NULL;
		Retire( object );
	}
}

int sdOcclusionQueryManager::GetOcclusionQueryIndex( int viewID, int entityID, bool create ) {
	if ( queries.Num() == 0 ) {
		Init();
	}
	for ( int index = 0; index < queries.Num(); ++index ) {
		if ( queries[ index ].entID == entityID && queries[ index ].viewID == viewID ) return index;
	}
	if ( !create ) return -1;
	sdOcclusionQuery query;
	query.entID = entityID;
	query.viewID = viewID;
	query.idx = queries.Num();
	return queries.Append( query );
}

void sdOcclusionQueryManager::Free() {
	queries.Clear();
}

void RB_DrawBoundsFilled( const idBounds& bounds ) {
	const idVec3& mins = bounds[ 0 ];
	const idVec3& maxs = bounds[ 1 ];
	if ( maxs.x < mins.x ) {
		return;
	}

	const idVec3 size = maxs - mins;
	if ( 2.0f * ( size.x * size.y + size.x * size.z + size.y * size.z ) < 1e-5f ) {
		return;
	}

	glBegin( GL_QUADS );
	glVertex3f( mins.x, mins.y, mins.z );
	glVertex3f( mins.x, maxs.y, mins.z );
	glVertex3f( mins.x, maxs.y, maxs.z );
	glVertex3f( mins.x, mins.y, maxs.z );

	glVertex3f( maxs.x, mins.y, maxs.z );
	glVertex3f( maxs.x, maxs.y, maxs.z );
	glVertex3f( maxs.x, maxs.y, mins.z );
	glVertex3f( maxs.x, mins.y, mins.z );

	glVertex3f( mins.x, mins.y, maxs.z );
	glVertex3f( maxs.x, mins.y, maxs.z );
	glVertex3f( maxs.x, mins.y, mins.z );
	glVertex3f( mins.x, mins.y, mins.z );

	glVertex3f( mins.x, maxs.y, mins.z );
	glVertex3f( maxs.x, maxs.y, mins.z );
	glVertex3f( maxs.x, maxs.y, maxs.z );
	glVertex3f( mins.x, maxs.y, maxs.z );

	glVertex3f( mins.x, mins.y, mins.z );
	glVertex3f( maxs.x, mins.y, mins.z );
	glVertex3f( maxs.x, maxs.y, mins.z );
	glVertex3f( mins.x, maxs.y, mins.z );

	glVertex3f( mins.x, maxs.y, maxs.z );
	glVertex3f( maxs.x, maxs.y, maxs.z );
	glVertex3f( maxs.x, mins.y, maxs.z );
	glVertex3f( mins.x, mins.y, maxs.z );
	glEnd();
}

namespace {
	void BeginBoundsQuery( sdOcclusionQuery& query ) {
		if ( query.wrapper != NULL || !QueryPathAvailable() ) return;
		query.wrapper = sdOcclusionQueryWrapper::GetQueryObject( query.idx );
		query.runFrame = occlusionFrame;
		query.wrapper->Begin();
	}

	void EndBoundsQuery( sdOcclusionQuery& query ) {
		if ( query.wrapper != NULL ) query.wrapper->End();
	}
}

void sdOcclusionQuery::RunQuery( const viewEntity_s* entity, const idVec3& viewVelocity ) {
	if ( entity == NULL || entity->model == NULL ) return;
	if ( runFrame == occlusionFrame ) return;
	runFrame = occlusionFrame;
	idBounds drawBounds = entity->entityDef != NULL ? entity->entityDef->bounds : entity->model->Bounds( NULL );
	if ( drawBounds.IsCleared() ) drawBounds = entity->model->Bounds( entity->entityDef );
	if ( drawBounds.IsCleared() ) {
		lastVisibleCount = 1000000;
		return;
	}
	drawBounds.DirectedExpandSelf( viewVelocity );
	viewDef_s* view = RB_GetViewDef();
	if ( view != NULL ) {
		const float* matrix = entity->modelMatrix;
		const idVec3 delta = view->renderView.vieworg - idVec3( matrix[ 12 ], matrix[ 13 ], matrix[ 14 ] );
		const idVec3 localViewOrigin(
			delta.x * matrix[ 0 ] + delta.y * matrix[ 1 ] + delta.z * matrix[ 2 ],
			delta.x * matrix[ 4 ] + delta.y * matrix[ 5 ] + delta.z * matrix[ 6 ],
			delta.x * matrix[ 8 ] + delta.y * matrix[ 9 ] + delta.z * matrix[ 10 ]
		);
		idBounds eyeBounds = drawBounds;
		eyeBounds.ExpandSelf( 20.0f );
		if ( eyeBounds.ContainsPoint( localViewOrigin ) ) {
			lastVisibleCount = 1000000;
			return;
		}
	}
	BeginBoundsQuery( *this );
	if ( wrapper == NULL ) return;
	drawBounds.ExpandSelf( 4.0f );
	glPushMatrix();
	glLoadMatrixf( entity->modelViewMatrix );
	RB_DrawBoundsFilled( drawBounds );
	glPopMatrix();
	EndBoundsQuery( *this );
}

void sdOcclusionQuery::RunQuery( const viewLight_s* light ) {
	if ( light == NULL || light->frustumTris == NULL ) {
		lastVisibleCount = 1000000;
		return;
	}
	if ( runFrame == occlusionFrame ) return;
	runFrame = occlusionFrame;

	idBounds expandedBounds = light->frustumTris->bounds;
	expandedBounds.ExpandSelf( 16.0f );
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || expandedBounds.ContainsPoint( view->renderView.vieworg ) ||
			light->scissorRect.Area() > r_occlusionCutoff.GetInteger() ) {
		lastVisibleCount = 1000000;
		return;
	}

	BeginBoundsQuery( *this );
	if ( wrapper == NULL ) return;
	RB_DrawElementsImmediate( light->frustumTris );
	EndBoundsQuery( *this );
}

void sdOcclusionQuery::RunQuery( const occlusionTest_t* test ) {
	if ( test == NULL ) return;
	BeginBoundsQuery( *this );
	if ( wrapper == NULL ) return;
	float matrix[ 16 ] = {
		test->axis[ 0 ].x, test->axis[ 0 ].y, test->axis[ 0 ].z, 0.0f,
		test->axis[ 1 ].x, test->axis[ 1 ].y, test->axis[ 1 ].z, 0.0f,
		test->axis[ 2 ].x, test->axis[ 2 ].y, test->axis[ 2 ].z, 0.0f,
		test->origin.x, test->origin.y, test->origin.z, 1.0f
	};
	glPushMatrix();
	viewDef_s* view = RB_GetViewDef();
	if ( view != NULL ) glLoadMatrixf( view->worldSpace.modelViewMatrix );
	glMultMatrixf( matrix );
	RB_DrawBoundsFilled( test->bb );
	glPopMatrix();
	EndBoundsQuery( *this );
}

void sdOcclusionQuery::RunQuery( const viewOccTest_s* test ) {
	if ( test != NULL ) RunQuery( test->test );
}

void R_FreeOcclussionQueries() {
	// Retail destroys both pools directly.  Retiring in-flight objects here
	// would read results during GL shutdown and would also leave those objects
	// linked from the used list after placing them on the free list.
	while ( sdOcclusionQueryWrapper::freeHead != NULL ) {
		sdOcclusionQueryWrapper* object = sdOcclusionQueryWrapper::freeHead;
		sdOcclusionQueryWrapper::freeHead = object->nextFree;
		delete object;
		--sdOcclusionQueryWrapper::numInstances;
	}
	while ( sdOcclusionQueryWrapper::usedHead != NULL ) {
		sdOcclusionQueryWrapper* object = sdOcclusionQueryWrapper::usedHead;
		sdOcclusionQueryWrapper::usedHead = object->nextUsed;
		delete object;
		--sdOcclusionQueryWrapper::numInstances;
	}
	sdOcclusionQueryWrapper::usedTail = NULL;
	sdOcclusionQueryWrapper::numInstances = 0;
	occlusionManager.Free();
}

int R_GatherQueryResults( drawSurf_s** drawSurfs, int numDrawSurfs, bool phase ) {
	sdOcclusionQueryWrapper::ProcessInflightQueries();
	int occluded = 0;
	for ( int index = 0; index < numDrawSurfs; ++index ) {
		viewEntity_s* space = drawSurfs[ index ] != NULL ? const_cast< viewEntity_s* >( drawSurfs[ index ]->space ) : NULL;
		if ( space == NULL || space->occlusionIndex <= 0 || space->occlusionIndex >= occlusionManager.queries.Num() ) continue;
		space->culled = occlusionManager.queries[ space->occlusionIndex ].lastVisibleCount <= r_occlusionThreshold.GetInteger();
		if ( space->culled ) ++occluded;
	}
	if ( r_occlusionDebug.GetInteger() != 0 && occluded != 0 ) {
		common->Printf( phase ? "*** PrevVis: %i ents occluded\n" : "*** PrevOcc: %i ents occluded\n", occluded );
	}
	return 0;
}

void R_ExecOccQueries( drawSurf_s** drawSurfs, int numDrawSurfs ) {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || !QueryPathAvailable() || !r_useOcclusionQueriesCulling.GetBool() ) return;
	idVec3 viewVelocity = ( view->renderView.vieworg - view->renderView.lastViewOrg ) * 3.0f;
	const float velocityLength = viewVelocity.Normalize();
	viewVelocity *= Min( velocityLength, 50.0f );
	idList< int > issuedEntities;
	for ( int index = 0; index < numDrawSurfs; ++index ) {
		drawSurf_s* surface = drawSurfs[ index ];
		viewEntity_s* space = const_cast< viewEntity_s* >( surface->space );
		if ( space == NULL || space->entityIndex < 0 || space->model == NULL || issuedEntities.FindIndex( space->entityIndex ) >= 0 ) continue;
		if ( !surface->material->UseOcclusionQuery() && ( space->entityDef == NULL || !space->entityDef->flags.occlusionTest ) ) continue;
		issuedEntities.Append( space->entityIndex );
		const int queryIndex = space->occlusionIndex > 0 ? space->occlusionIndex :
			occlusionManager.GetOcclusionQueryIndex( view->renderView.viewID, space->entityIndex, true );
		space->occlusionIndex = queryIndex;
		sdOcclusionQuery& query = occlusionManager.queries[ queryIndex ];
		const float* matrix = space->modelMatrix;
		const idVec3 localVelocity(
			viewVelocity.x * matrix[ 0 ] + viewVelocity.y * matrix[ 1 ] + viewVelocity.z * matrix[ 2 ],
			viewVelocity.x * matrix[ 4 ] + viewVelocity.y * matrix[ 5 ] + viewVelocity.z * matrix[ 6 ],
			viewVelocity.x * matrix[ 8 ] + viewVelocity.y * matrix[ 9 ] + viewVelocity.z * matrix[ 10 ]
		);
		if ( query.runFrame != occlusionFrame ) query.RunQuery( space, localVelocity );
	}
}

void R_ExecOccQueriesForLights() {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || !QueryPathAvailable() || !r_useOcclusionQueriesCulling.GetBool() ) return;
	int lightIndex = 0;
	for ( viewLight_s* light = view->viewLights; light != NULL; light = light->next, ++lightIndex ) {
		const int id = static_cast< int >( 0x80000000u | static_cast< unsigned int >( light->lightIndex ) );
		const int queryIndex = light->occlusionIndex > 0 ? light->occlusionIndex :
			occlusionManager.GetOcclusionQueryIndex( view->renderView.viewID, id, true );
		light->occlusionIndex = queryIndex;
		sdOcclusionQuery& query = occlusionManager.queries[ queryIndex ];
		if ( query.runFrame != occlusionFrame ) query.RunQuery( light );
	}
}

void R_ExecOccQueriesForGameTests() {
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || view->renderWorld == NULL || !QueryPathAvailable() ) return;
	for ( int index = 0; index < view->renderWorld->BackendNumOcclusionTests(); ++index ) {
		const occlusionTest_t* test = view->renderWorld->BackendOcclusionTest( index );
		if ( test == NULL || ( test->view != 0 && test->view != view->renderView.viewID ) ) continue;
		const int queryIndex = occlusionManager.GetOcclusionQueryIndex( view->renderView.viewID, 0x40000000 | index, true );
		sdOcclusionQuery& query = occlusionManager.queries[ queryIndex ];
		if ( query.runFrame != occlusionFrame ) query.RunQuery( test );
	}
}

void R_GatherOccQueriesForLights() {
	if ( !r_useOcclusionQueries.GetBool() || !r_useOcclusionQueriesCulling.GetBool() ) return;
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL ) return;
	for ( viewLight_s* light = view->viewLights; light != NULL; light = light->next ) {
		if ( light->occlusionIndex <= 0 || light->occlusionIndex >= occlusionManager.queries.Num() ) continue;
		light->culled = occlusionManager.queries[ light->occlusionIndex ].lastVisibleCount <= r_occlusionThreshold.GetInteger();
	}
}
void R_GatherOccQueriesForGameTests() { sdOcclusionQueryWrapper::ProcessInflightQueries(); }
void R_RetirePrevFrameOcclusionSystem() {
	if ( !sm_occlusionQueries.GetBool() || !QueryPathAvailable() ) return;
	sdOcclusionQueryWrapper::ProcessInflightQueries();
	R_GatherQueryResults( queriedDrawSurfaces.Begin(), queriedDrawSurfaces.Num(), false );
	R_GatherOccQueriesForLights();
	R_GatherOccQueriesForGameTests();
	queriedDrawSurfaces.Clear();
}

int R_IsVisibleOcclusionSystem( int viewID, int occid ) {
	const int index = occlusionManager.GetOcclusionQueryIndex( viewID, occid | 0x40000000, false );
	if ( index <= 0 ) return 1;
	return occlusionManager.queries[ index ].lastVisibleCount;
}

int R_CountVisibleOcclusionSystem( int viewID, int occid ) {
	const int index = occlusionManager.GetOcclusionQueryIndex( viewID, occid | 0x40000000, false );
	return index > 0 ? occlusionManager.queries[ index ].lastVisibleCount : 1;
}

bool R_IsVisibleEntity( int viewID, int occid ) {
	const int index = occlusionManager.GetOcclusionQueryIndex( viewID, occid, true );
	return index <= 0 || occlusionManager.queries[ index ].lastVisibleCount > 0;
}

void R_PrevFrameOcclusionSystemUpdateViewEnts( drawSurf_s** drawSurfs, int numDrawSurfs ) {
	sdOcclusionQueryWrapper::ProcessInflightQueries();
	viewDef_s* view = RB_GetViewDef();
	if ( view == NULL || !r_useOcclusionQueries.GetBool() || !r_useOcclusionQueriesCulling.GetBool() ) return;
	for ( int index = 0; index < numDrawSurfs; ++index ) {
		viewEntity_s* space = const_cast< viewEntity_s* >( drawSurfs[ index ]->space );
		if ( space == NULL || space->entityIndex < 0 ) continue;
		if ( !drawSurfs[ index ]->material->UseOcclusionQuery() && ( space->entityDef == NULL || !space->entityDef->flags.occlusionTest ) ) continue;
		if ( space->occlusionIndex < 1 ) {
			space->occlusionIndex = occlusionManager.GetOcclusionQueryIndex( view->renderView.viewID, space->entityIndex, true );
			space->culled = space->occlusionIndex > 0 && occlusionManager.queries[ space->occlusionIndex ].lastVisibleCount <= 0;
		}
	}
	int lightIndex = 0;
	for ( viewLight_s* light = view->viewLights; light != NULL; light = light->next, ++lightIndex ) {
		const int id = static_cast< int >( 0x80000000u | static_cast< unsigned int >( light->lightIndex ) );
		if ( light->occlusionIndex < 1 ) {
			light->occlusionIndex = occlusionManager.GetOcclusionQueryIndex( view->renderView.viewID, id, true );
			light->culled = light->occlusionIndex > 0 && occlusionManager.queries[ light->occlusionIndex ].lastVisibleCount <= 0;
		}
	}
}

void R_PrevFrameOcclusionSystem( drawSurf_s** drawSurfs, int numDrawSurfs ) {
	if ( !sm_occlusionQueries.GetBool() || !QueryPathAvailable() ) return;
	++occlusionFrame;
	queriedDrawSurfaces.Clear();
	if ( r_useOcclusionQueriesCulling.GetBool() ) {
		for ( int index = 0; index < numDrawSurfs; ++index ) {
			drawSurf_s* surface = drawSurfs[ index ];
			if ( surface == NULL || surface->space == NULL || surface->space->entityIndex < 0 ) continue;
			if ( !surface->material->UseOcclusionQuery() && !surface->space->occtest ) continue;
			queriedDrawSurfaces.Append( surface );
		}
	}
	RB_ARB2_ClearSpace();
	if ( rbinds != NULL && rbinds->occlusionProgram != NULL ) {
		RB_ARB2_SetupProgram( rbinds->occlusionProgram, r_occlusionBBDebug.GetInteger() != 0 ? 0 : 0x1F00,
			CT_FRONT_SIDED, NULL );
	} else {
		SD_UnbindRenderProgram();
		GL_State( r_occlusionBBDebug.GetInteger() != 0 ? 0 : 0x1F00 );
		GL_Cull( CT_FRONT_SIDED );
	}
	R_ExecOccQueries( queriedDrawSurfaces.Begin(), queriedDrawSurfaces.Num() );
	R_ExecOccQueriesForLights();
	R_ExecOccQueriesForGameTests();
	if ( r_occlusionFlush.GetBool() ) glFlush();
}

void R_PrevFrameOcclusionSystemUpdateViewEnts() {
	viewDef_s* view = RB_GetViewDef();
	if ( view != NULL ) R_PrevFrameOcclusionSystemUpdateViewEnts( view->drawSurfs, view->numDrawSurfs );
}

void R_PrevFrameOcclusionSystem() {
	viewDef_s* view = RB_GetViewDef();
	if ( view != NULL ) R_PrevFrameOcclusionSystem( view->drawSurfs, view->numDrawSurfs );
}

void R_RetireOcclusionQueries() { R_RetirePrevFrameOcclusionSystem(); }
