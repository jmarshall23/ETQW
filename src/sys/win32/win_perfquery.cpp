/*
===========================================================================

Win32 performance-counter queries.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../sys_local.h"

#include <pdh.h>
#include <pdhmsg.h>

struct sdQueryInfoWin {
	sdPerformanceQueryType	pqType;
	const char*				internalName;
};

class sdPerformanceQueryWin : public sdPerformanceQueryLocal {
public:
	explicit sdPerformanceQueryWin( const sdQueryInfoWin* info );
	virtual ~sdPerformanceQueryWin();

	virtual bool Sample();

private:
	const sdQueryInfoWin*	info;
	PDH_HCOUNTER			counterHandle;
	PDH_STATUS				status;
	PDH_FMT_COUNTERVALUE	value;
};

static sdQueryInfoWin queryInfos[ 19 ] = {
	{ PQT_CPU0,          "\\Processor(0)\\% Processor Time" },
	{ PQT_CPU1,          "\\Processor(1)\\% Processor Time" },
	{ PQT_CPU_TOTAL,     "\\Processor(_Total)\\% Processor Time" },
	{ PQT_GPU_FPS,       "\\NVIDIA OpenGL Driver(CPU/OGL FPS)\\OGL Counter Value" },
	{ PQT_GPU_IDLE,      "\\NVIDIA GPU Performance(GPU0/% gpu_idle)\\GPU Counter Value" },
	{ PQT_GPU_AGPMEM,    "\\NVIDIA OpenGL Driver(CPU/OGL AGP/PCI-E usage (MB))\\OGL Counter Value" },
	{ PQT_GPU_VIDMEM,    "\\NVIDIA OpenGL Driver(CPU/OGL vidmem usage (MB))\\OGL Counter Value" },
	{ PQT_GPU_DRIVERWAIT,"\\NVIDIA OpenGL Driver(CPU/% OGL % driver waiting)\\OGL Counter Value" },
	{ PQT_GPU_VS,        "\\NVIDIA GPU Performance(GPU0/% vertex_shader_busy)\\GPU Counter Value" },
	{ PQT_GPU_PS,        "\\NVIDIA GPU Performance(GPU0/% pixel_shader_busy)\\GPU Counter Value" },
	{ PQT_GPU_TEX,       "\\NVIDIA GPU Performance(GPU0/% shader_waits_for_texture)\\GPU Counter Value" },
	{ PQT_GPU_ROP,       "\\NVIDIA GPU Performance(GPU0/% shader_waits_for_rop)\\GPU Counter Value" },
	{ PQT_GPU_TEXPS,     "\\NVIDIA GPU Performance(GPU0/% texture_waits_for_shader)\\GPU Counter Value" },
	{ PQT_GPU_TRIS,      "\\NVIDIA GPU Performance(GPU0/% triangle_count)\\GPU Counter Value" },
	{ PQT_GPU_VERTS,     "\\NVIDIA GPU Performance(GPU0/% vertex_count)\\GPU Counter Value" },
	{ PQT_GPU_PIXELS,    "\\NVIDIA GPU Performance(GPU0/% shaded_pixel_count)\\GPU Counter Value" },
	{ PQT_GPU_FASTZ,     "\\NVIDIA GPU Performance(GPU0/% fast_z_count)\\GPU Counter Value" },
	{ PQT_OSDEPENDENT,   "" },
	{ PQT_END,           "" }
};

static PDH_HQUERY queryHandle;
static bool pq_init;
static int pq_openQueries;

static bool PdhIsOk( PDH_STATUS status ) {
	if ( status == ERROR_SUCCESS ) {
		return true;
	}

	const char* message;
	switch ( status ) {
		case PDH_CSTATUS_NO_MACHINE:
			message = "A computer entry could not be created.";
			break;
		case PDH_NO_DATA:
			message = "The query does not currently have any counters.";
			break;
		case PDH_CSTATUS_NO_OBJECT:
			message = "The specified object could not be found.";
			break;
		case PDH_CSTATUS_NO_COUNTER:
			message = "The specified counter was not found.";
			break;
		case PDH_MEMORY_ALLOCATION_FAILURE:
			message = "A memory buffer could not be allocated.";
			break;
		case PDH_INVALID_HANDLE:
			message = "The query handle is not valid.";
			break;
		case PDH_INVALID_ARGUMENT:
			message = "An argument is not correct or is incorrectly formatted.";
			break;
		case PDH_FUNCTION_NOT_FOUND:
			message = "The calculation function for this counter could not be determined.";
			break;
		case PDH_CSTATUS_NO_COUNTERNAME:
			message = "An empty counter name path string was passed in.";
			break;
		case PDH_CSTATUS_BAD_COUNTERNAME:
			message = "The counter name path string could not be parsed or interpreted.";
			break;
		case PDH_INVALID_DATA:
			message = "The specified counter does not contain valid data or a successful status code.";
			break;
		default:
			message = "Unknown PDH Error.";
			break;
	}

	common->Printf( "PDH Error: %s\n", message );
	return false;
}

void PQ_Init() {
	common->Printf( "Initializing performance queries\n" );
	if ( PdhIsOk( PdhOpenQueryA( NULL, 0, &queryHandle ) ) ) {
		pq_init = true;
	}
}

void PQ_ShutDown() {
	common->Printf( "Shutting down performance queries\n" );
	if ( pq_openQueries > 0 ) {
		common->Warning( "Not all open performance queries were closed..." );
	}
	PdhIsOk( PdhCloseQuery( queryHandle ) );
	pq_init = false;
}

void Sys_CollectPerformanceData() {
	if ( pq_openQueries > 0 ) {
		PdhIsOk( PdhCollectQueryData( queryHandle ) );
	}
}

sdPerformanceQueryWin::sdPerformanceQueryWin( const sdQueryInfoWin* info ) :
	status( ERROR_SUCCESS ) {
	const char* counterName = info->internalName;
	char nameBuffer[ 1024 ];

	if ( info->pqType == PQT_OSDEPENDENT ) {
		PDH_BROWSE_DLG_CONFIG_A browseDlgData;
		memset( &browseDlgData, 0, sizeof( browseDlgData ) );
		browseDlgData.bSingleCounterPerAdd = TRUE;
		browseDlgData.bSingleCounterPerDialog = TRUE;
		browseDlgData.bWildCardInstances = TRUE;
		browseDlgData.bHideDetailBox = TRUE;
		browseDlgData.szReturnPathBuffer = nameBuffer;
		browseDlgData.cchReturnPathLength = sizeof( nameBuffer );
		browseDlgData.dwDefaultDetailLevel = PERF_DETAIL_WIZARD;
		browseDlgData.szDialogBoxCaption = "Select a performance query to add";
		status = PdhBrowseCountersA( &browseDlgData );
		PdhIsOk( status );
		counterName = nameBuffer;
	}

	common->Printf( "Opening counter: %s\n", counterName );
	status = PdhAddCounterA( queryHandle, counterName, 0, &counterHandle );
	pq_openQueries++;
	if ( !PdhIsOk( status ) ) {
		common->Printf( "\tMake sure this counter is enabled in the control panel\n" );
	}
	memset( &value, 0, sizeof( value ) );
}

sdPerformanceQueryWin::~sdPerformanceQueryWin() {
	PdhIsOk( PdhRemoveCounter( counterHandle ) );
	pq_openQueries--;
}

bool sdPerformanceQueryWin::Sample() {
	status = PdhGetFormattedCounterValue(
		counterHandle,
		PDH_FMT_DOUBLE | PDH_FMT_NOCAP100 | PDH_FMT_NOSCALE,
		NULL,
		&value
	);
	PdhIsOk( status );
	if ( status != ERROR_SUCCESS ) {
		return false;
	}
	Insert( static_cast< float >( value.doubleValue ) );
	return true;
}

sdPerformanceQuery* Sys_GetPerformanceQuery( sdPerformanceQueryType pqType ) {
	for ( sdQueryInfoWin* info = queryInfos; info->pqType != PQT_END; info++ ) {
		if ( info->pqType == pqType ) {
			return new sdPerformanceQueryWin( info );
		}
	}
	return NULL;
}

#if defined( _M_IX86 )
static_assert( sizeof( sdQueryInfoWin ) == 0x8, "sdQueryInfoWin layout drift" );
static_assert( sizeof( sdPerformanceQueryWin ) == 0x40, "sdPerformanceQueryWin layout drift" );
#endif
