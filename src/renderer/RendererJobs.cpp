// Copyright (C) 2007 Id Software, Inc.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "RendererJobs.h"
#include "RendererMetrics.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

idCVar r_renderJobs( "r_renderjobs", "1", CVAR_RENDERER | CVAR_BOOL,
	"parallelize read-only Vulkan renderer preparation" );
idCVar r_renderJobWorkers( "r_renderjobworkers", "0",
	CVAR_RENDERER | CVAR_ARCHIVE | CVAR_INTEGER,
	"renderer preparation worker threads; 0 = automatic, takes effect after renderer restart",
	0, 12 );
idCVar r_renderJobMinItems( "r_renderjobminitems", "64",
	CVAR_RENDERER | CVAR_INTEGER,
	"minimum number of items before renderer preparation uses worker threads", 1, 4096 );

namespace {

struct rendererJobDispatch_t {
	renderParallelForFunction_t	function;
	void*					context;
	const char*				metricLabel;
	int					itemCount;
	int					grainSize;
	std::atomic< int >			nextItem;
	std::atomic< int >			workersRemaining;

	rendererJobDispatch_t() : function( NULL ), context( NULL ), metricLabel( NULL ),
		itemCount( 0 ), grainSize( 1 ), nextItem( 0 ), workersRemaining( 0 ) {
	}
};

void ExecuteRendererDispatch( rendererJobDispatch_t* dispatch, int workerIndex ) {
	if ( dispatch == NULL || dispatch->function == NULL ) return;
	idRenderMetricScope metric( dispatch->metricLabel != NULL ?
		dispatch->metricLabel : "Renderer job" );
	for ( ;; ) {
		const int firstItem = dispatch->nextItem.fetch_add( dispatch->grainSize,
			std::memory_order_relaxed );
		if ( firstItem >= dispatch->itemCount ) break;
		const int itemCount = Min( dispatch->grainSize,
			dispatch->itemCount - firstItem );
		dispatch->function( dispatch->context, firstItem, itemCount, workerIndex );
	}
}

int AutomaticRendererWorkerCount() {
	const unsigned int hardwareThreads = std::thread::hardware_concurrency();
	if ( hardwareThreads <= 4 ) return 1;
	// Keep the render/game thread and both MegaTexture workers from being
	// starved.  More than six preparation workers has little value for the
	// current per-surface workloads.
	return idMath::ClampInt( 1, 6, static_cast< int >( hardwareThreads ) - 3 );
}

}

struct sdRendererJobSystem::impl_t {
	std::mutex				mutex;
	std::condition_variable		workSignal;
	std::condition_variable		completeSignal;
	std::vector< std::thread* >	workers;
	rendererJobDispatch_t*		dispatch;
	unsigned int				generation;
	bool					terminate;

	impl_t() : dispatch( NULL ), generation( 0 ), terminate( false ) {
	}

	void WorkerMain( int workerIndex ) {
		unsigned int observedGeneration = 0;
		for ( ;; ) {
			rendererJobDispatch_t* activeDispatch = NULL;
			{
				std::unique_lock< std::mutex > lock( mutex );
				workSignal.wait( lock, [ this, observedGeneration ]() {
					return terminate || generation != observedGeneration;
				} );
				if ( terminate ) return;
				observedGeneration = generation;
				activeDispatch = dispatch;
			}

			ExecuteRendererDispatch( activeDispatch, workerIndex );
			if ( activeDispatch->workersRemaining.fetch_sub( 1,
					std::memory_order_acq_rel ) == 1 ) {
				std::lock_guard< std::mutex > lock( mutex );
				completeSignal.notify_one();
			}
		}
	}
};

sdRendererJobSystem rendererJobs;

sdRendererJobSystem::sdRendererJobSystem() : impl( NULL ) {
}

sdRendererJobSystem::~sdRendererJobSystem() {
	Shutdown();
}

void sdRendererJobSystem::Init() {
	if ( impl != NULL ) return;
	impl = new impl_t;
	const int requestedWorkers = r_renderJobWorkers.GetInteger();
	const int workerCount = requestedWorkers > 0 ?
		idMath::ClampInt( 1, 12, requestedWorkers ) : AutomaticRendererWorkerCount();
	impl->workers.reserve( workerCount );
	for ( int workerIndex = 0; workerIndex < workerCount; ++workerIndex ) {
		impl->workers.push_back( new std::thread( [ this, workerIndex ]() {
			impl->WorkerMain( workerIndex );
		} ) );
	}
	common->Printf( "Renderer jobs: %d persistent preparation workers\n", workerCount );
}

void sdRendererJobSystem::Shutdown() {
	if ( impl == NULL ) return;
	{
		std::lock_guard< std::mutex > lock( impl->mutex );
		impl->terminate = true;
		++impl->generation;
	}
	impl->workSignal.notify_all();
	for ( size_t index = 0; index < impl->workers.size(); ++index ) {
		impl->workers[ index ]->join();
		delete impl->workers[ index ];
	}
	delete impl;
	impl = NULL;
}

bool sdRendererJobSystem::IsEnabled() const {
	return impl != NULL && !impl->workers.empty() && r_renderJobs.GetBool();
}

int sdRendererJobSystem::GetWorkerCount() const {
	return impl != NULL ? static_cast< int >( impl->workers.size() ) : 0;
}

void sdRendererJobSystem::ParallelFor( int itemCount, int grainSize,
	const char* metricLabel, renderParallelForFunction_t function, void* context ) {
	if ( itemCount <= 0 || function == NULL ) return;
	grainSize = Max( grainSize, 1 );
	const int chunkCount = ( itemCount + grainSize - 1 ) / grainSize;
	if ( !IsEnabled() || itemCount < r_renderJobMinItems.GetInteger() ||
		chunkCount < 2 ) {
		idRenderMetricScope metric( metricLabel != NULL ? metricLabel : "Renderer job" );
		function( context, 0, itemCount, GetWorkerCount() );
		return;
	}

	rendererJobDispatch_t dispatch;
	dispatch.function = function;
	dispatch.context = context;
	dispatch.metricLabel = metricLabel;
	dispatch.itemCount = itemCount;
	dispatch.grainSize = grainSize;
	dispatch.workersRemaining.store( GetWorkerCount(), std::memory_order_relaxed );
	{
		std::lock_guard< std::mutex > lock( impl->mutex );
		assert( impl->dispatch == NULL );
		impl->dispatch = &dispatch;
		++impl->generation;
	}
	impl->workSignal.notify_all();

	// The render thread consumes ranges as well, reducing dispatch latency and
	// ensuring the pool is fork/join rather than a separate renderer.
	ExecuteRendererDispatch( &dispatch, GetWorkerCount() );

	{
		std::unique_lock< std::mutex > lock( impl->mutex );
		impl->completeSignal.wait( lock, [ &dispatch ]() {
			return dispatch.workersRemaining.load( std::memory_order_acquire ) == 0;
		} );
		impl->dispatch = NULL;
	}
}
