// Copyright (C) 2007 Id Software, Inc.

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "RendererMetrics.h"
#include "Image.h"
#include "VulkanBackend.h"

#include "imgui.h"

#include <atomic>
#include <mutex>

idCVar r_showMetrics( "r_showmetrics", "0", CVAR_RENDERER | CVAR_BOOL,
	"show the previous Vulkan frame as an ImGui CPU timing timeline" );

namespace {

static const int MAX_RENDER_METRICS = 256;

struct renderMetricEvent_t {
	const char*		label;
	double			startTicks;
	double			endTicks;
	unsigned int	threadId;
	int			depth;
};

struct renderMetricFrame_t {
	renderMetricEvent_t events[ MAX_RENDER_METRICS ];
	int				numEvents;
	int				droppedEvents;
	double			startTicks;
	double			endTicks;
	unsigned int	serial;
	unsigned int	mainThreadId;

	renderMetricFrame_t() : numEvents( 0 ), droppedEvents( 0 ), startTicks( 0.0 ),
		endTicks( 0.0 ), serial( 0 ), mainThreadId( 0 ) {
	}
};

std::mutex metricsMutex;
std::atomic< bool > collecting( false );
std::atomic< unsigned int > collectingSerial( 0 );
renderMetricFrame_t currentFrame;
renderMetricFrame_t displayedFrame;
bool hasDisplayedFrame = false;
unsigned int nextFrameSerial = 0;
thread_local int metricDepth = 0;

ImGuiContext* metricsImGuiContext = NULL;
idImage* metricsFontImage = NULL;
double lastOverlayTicks = 0.0;

double MetricsNow() {
	return Sys_GetClockTicksNoFlush();
}

double TicksToMilliseconds( double ticks ) {
	const double frequency = Sys_ClockTicksPerSecond();
	return frequency > 0.0 ? ticks * 1000.0 / frequency : 0.0;
}

unsigned int MetricColor( const char* label, int depth ) {
	unsigned int hash = 2166136261u;
	for ( const unsigned char* c = reinterpret_cast< const unsigned char* >( label );
		c != NULL && *c != 0; ++c ) {
		hash = ( hash ^ *c ) * 16777619u;
	}
	const int red = 80 + ( hash & 0x7f );
	const int green = 80 + ( ( hash >> 8 ) & 0x7f );
	const int blue = 80 + ( ( hash >> 16 ) & 0x7f );
	const int alpha = depth == 0 ? 235 : 205;
	return IM_COL32( red, green, blue, alpha );
}

void SortMetricsByStart( renderMetricFrame_t& frame ) {
	for ( int index = 1; index < frame.numEvents; ++index ) {
		const renderMetricEvent_t event = frame.events[ index ];
		int insertion = index;
		while ( insertion > 0 &&
			frame.events[ insertion - 1 ].startTicks > event.startTicks ) {
			frame.events[ insertion ] = frame.events[ insertion - 1 ];
			--insertion;
		}
		frame.events[ insertion ] = event;
	}
}

void RecordMetric( const char* label, double startTicks, double endTicks,
	unsigned int serial, unsigned int threadId, int depth ) {
	if ( !collecting.load( std::memory_order_acquire ) ||
		collectingSerial.load( std::memory_order_relaxed ) != serial ) {
		return;
	}
	std::lock_guard< std::mutex > lock( metricsMutex );
	if ( !collecting.load( std::memory_order_relaxed ) ||
		currentFrame.serial != serial ) {
		return;
	}
	if ( currentFrame.numEvents >= MAX_RENDER_METRICS ) {
		++currentFrame.droppedEvents;
		return;
	}
	renderMetricEvent_t& event = currentFrame.events[ currentFrame.numEvents++ ];
	event.label = label;
	event.startTicks = startTicks;
	event.endTicks = Max( endTicks, startTicks );
	event.threadId = threadId;
	event.depth = depth;
}

class scopedMetricsImGuiContext_t {
public:
	explicit scopedMetricsImGuiContext_t( ImGuiContext* context ) :
		previous( ImGui::GetCurrentContext() ) {
		ImGui::SetCurrentContext( context );
	}
	~scopedMetricsImGuiContext_t() {
		ImGui::SetCurrentContext( previous );
	}
private:
	ImGuiContext* previous;
};

bool EnsureMetricsImGui() {
	if ( metricsImGuiContext == NULL ) {
		ImGuiContext* previous = ImGui::GetCurrentContext();
		IMGUI_CHECKVERSION();
		metricsImGuiContext = ImGui::CreateContext();
		ImGui::SetCurrentContext( metricsImGuiContext );
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = NULL;
		io.LogFilename = NULL;
		io.BackendRendererName = "ETQW Vulkan metrics";
		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 4.0f;
		style.FrameRounding = 2.0f;
		style.WindowBorderSize = 1.0f;
		ImGui::SetCurrentContext( previous );
	}

	scopedMetricsImGuiContext_t context( metricsImGuiContext );
	ImGuiIO& io = ImGui::GetIO();
	if ( metricsFontImage == NULL ) {
		unsigned char* pixels = NULL;
		int fontWidth = 0;
		int fontHeight = 0;
		io.Fonts->GetTexDataAsRGBA32( &pixels, &fontWidth, &fontHeight );
		if ( pixels == NULL || fontWidth <= 0 || fontHeight <= 0 ||
			globalImages == NULL || !vulkanBackend.IsInitialized() ) {
			return false;
		}
		metricsFontImage = globalImages->GetImage( "_renderMetricsFont" );
		if ( metricsFontImage == NULL ) {
			metricsFontImage = globalImages->AllocImage( "_renderMetricsFont" );
		}
		metricsFontImage->GenerateImageEx( pixels, fontWidth, fontHeight,
			TF_LINEAR, false, TR_CLAMP, TD_HIGH_QUALITY, GL_RGBA8, 1 );
		if ( !metricsFontImage->IsLoaded() ) {
			metricsFontImage = NULL;
			return false;
		}
		io.Fonts->SetTexID( static_cast< ImTextureID >(
			reinterpret_cast< uintptr_t >( metricsFontImage ) ) );
	}
	return true;
}

void DrawTimeline( const renderMetricFrame_t& frame ) {
	const double frameTicks = Max( frame.endTicks - frame.startTicks, 1.0 );
	const float frameMilliseconds = static_cast< float >(
		TicksToMilliseconds( frameTicks ) );
	const float rowHeight = 18.0f;
	const float labelWidth = 245.0f;

	ImGui::Text( "Previous Vulkan frame: %.3f ms  |  %d scopes",
		frameMilliseconds, frame.numEvents );
	ImGui::SameLine();
	ImGui::TextDisabled( "(CPU wall time)" );
	if ( frame.droppedEvents != 0 ) {
		ImGui::SameLine();
		ImGui::TextColored( ImVec4( 1.0f, 0.35f, 0.25f, 1.0f ),
			"%d scopes dropped", frame.droppedEvents );
	}
	ImGui::Separator();

	const ImVec2 axisPosition = ImGui::GetCursorScreenPos();
	const float availableWidth = Max( ImGui::GetContentRegionAvail().x, labelWidth + 100.0f );
	const float graphStart = axisPosition.x + labelWidth;
	const float graphWidth = Max( availableWidth - labelWidth, 100.0f );
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddText( ImVec2( graphStart, axisPosition.y ), IM_COL32( 215, 220, 230, 255 ),
		"Frame start  0.000 ms" );
	char endLabel[ 64 ];
	idStr::snPrintf( endLabel, sizeof( endLabel ), "Frame end  %.3f ms", frameMilliseconds );
	const ImVec2 endSize = ImGui::CalcTextSize( endLabel );
	draw->AddText( ImVec2( graphStart + graphWidth - endSize.x, axisPosition.y ),
		IM_COL32( 215, 220, 230, 255 ), endLabel );
	ImGui::Dummy( ImVec2( availableWidth, rowHeight + 3.0f ) );

	ImGui::BeginChild( "##renderMetricsTimeline", ImVec2( 0.0f, 0.0f ), false,
		ImGuiWindowFlags_NoBackground );
	for ( int index = 0; index < frame.numEvents; ++index ) {
		const renderMetricEvent_t& event = frame.events[ index ];
		const double relativeStart = event.startTicks - frame.startTicks;
		const double relativeEnd = event.endTicks - frame.startTicks;
		const float startFraction = idMath::ClampFloat( 0.0f, 1.0f,
			static_cast< float >( relativeStart / frameTicks ) );
		const float endFraction = idMath::ClampFloat( startFraction, 1.0f,
			static_cast< float >( relativeEnd / frameTicks ) );
		const float duration = static_cast< float >(
			TicksToMilliseconds( event.endTicks - event.startTicks ) );

		ImGui::PushID( index );
		const ImVec2 rowPosition = ImGui::GetCursorScreenPos();
		const float rowWidth = Max( ImGui::GetContentRegionAvail().x,
			labelWidth + 100.0f );
		ImGui::InvisibleButton( "##metric", ImVec2( rowWidth, rowHeight ) );
		draw = ImGui::GetWindowDrawList();

		char label[ 256 ];
		const int indent = idMath::ClampInt( 0, 8, event.depth ) * 2;
		if ( event.threadId == frame.mainThreadId ) {
			idStr::snPrintf( label, sizeof( label ), "%*s%s  %.3f ms",
				indent, "", event.label, duration );
		} else {
			idStr::snPrintf( label, sizeof( label ), "%*s%s [T%u]  %.3f ms",
				indent, "", event.label, event.threadId, duration );
		}
		draw->AddText( ImVec2( rowPosition.x, rowPosition.y + 1.0f ),
			IM_COL32( 225, 228, 235, 255 ), label );

		const float timelineX = rowPosition.x + labelWidth;
		const float timelineWidth = Max( rowWidth - labelWidth, 100.0f );
		draw->AddRectFilled( ImVec2( timelineX, rowPosition.y + 2.0f ),
			ImVec2( timelineX + timelineWidth, rowPosition.y + rowHeight - 2.0f ),
			IM_COL32( 28, 31, 39, 220 ), 2.0f );
		for ( int grid = 0; grid <= 4; ++grid ) {
			const float x = timelineX + timelineWidth * grid * 0.25f;
			draw->AddLine( ImVec2( x, rowPosition.y + 2.0f ),
				ImVec2( x, rowPosition.y + rowHeight - 2.0f ),
				IM_COL32( 75, 79, 91, grid == 0 || grid == 4 ? 180 : 90 ) );
		}
		const float barStart = timelineX + timelineWidth * startFraction;
		const float barEnd = Max( barStart + 1.0f,
			timelineX + timelineWidth * endFraction );
		draw->AddRectFilled( ImVec2( barStart, rowPosition.y + 3.0f ),
			ImVec2( Min( barEnd, timelineX + timelineWidth ),
				rowPosition.y + rowHeight - 3.0f ),
			MetricColor( event.label, event.depth ), 2.0f );

		if ( ImGui::IsItemHovered() ) {
			ImGui::SetTooltip( "%s\nStart: %.3f ms\nEnd: %.3f ms\nDuration: %.3f ms",
				event.label, TicksToMilliseconds( relativeStart ),
				TicksToMilliseconds( relativeEnd ), duration );
		}
		ImGui::PopID();
	}
	ImGui::EndChild();
}

void SubmitMetricsDrawData( ImDrawData* drawData ) {
	if ( drawData == NULL || drawData->DisplaySize.x <= 0.0f ||
		drawData->DisplaySize.y <= 0.0f ) {
		return;
	}
	const ImVec2 displayPosition = drawData->DisplayPos;
	const float displayWidth = Max( drawData->DisplaySize.x, 1.0f );
	const float displayHeight = Max( drawData->DisplaySize.y, 1.0f );
	for ( int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex ) {
		const ImDrawList* commandList = drawData->CmdLists[ listIndex ];
		for ( int commandIndex = 0; commandIndex < commandList->CmdBuffer.Size;
			++commandIndex ) {
			const ImDrawCmd& command = commandList->CmdBuffer[ commandIndex ];
			if ( command.UserCallback != NULL ) {
				if ( command.UserCallback != ImDrawCallback_ResetRenderState ) {
					command.UserCallback( commandList, &command );
				}
				continue;
			}
			if ( command.ElemCount == 0 ) {
				continue;
			}

			const int clipLeft = idMath::Ftoi( command.ClipRect.x - displayPosition.x );
			const int clipTop = idMath::Ftoi( command.ClipRect.y - displayPosition.y );
			const int clipRight = idMath::Ftoi( command.ClipRect.z - displayPosition.x );
			const int clipBottom = idMath::Ftoi( command.ClipRect.w - displayPosition.y );
			if ( clipRight <= clipLeft || clipBottom <= clipTop ) {
				continue;
			}

			idList< sdVulkanToolVertex > triangles;
			triangles.SetNum( command.ElemCount, false );
			for ( unsigned int index = 0; index < command.ElemCount; ++index ) {
				const ImDrawIdx sourceIndex = commandList->IdxBuffer[
					command.IdxOffset + index ];
				const ImDrawVert& source = commandList->VtxBuffer[
					command.VtxOffset + sourceIndex ];
				sdVulkanToolVertex& vertex = triangles[ index ];
				vertex.x = ( ( source.pos.x - displayPosition.x ) / displayWidth ) * 2.0f - 1.0f;
				vertex.y = 1.0f - ( ( source.pos.y - displayPosition.y ) / displayHeight ) * 2.0f;
				vertex.z = 0.0f;
				vertex.w = 1.0f;
				vertex.s = source.uv.x;
				vertex.t = source.uv.y;
				vertex.r = ( ( source.col >> IM_COL32_R_SHIFT ) & 0xff ) / 255.0f;
				vertex.g = ( ( source.col >> IM_COL32_G_SHIFT ) & 0xff ) / 255.0f;
				vertex.b = ( ( source.col >> IM_COL32_B_SHIFT ) & 0xff ) / 255.0f;
				vertex.a = ( ( source.col >> IM_COL32_A_SHIFT ) & 0xff ) / 255.0f;
			}
			const void* textureOwner = reinterpret_cast< const void* >(
				static_cast< uintptr_t >( command.GetTexID() ) );
			vulkanBackend.DrawOverlayTriangles( textureOwner, triangles.Begin(),
				triangles.Num(), clipLeft, clipTop, clipRight - clipLeft,
				clipBottom - clipTop );
		}
	}
}

} // namespace

idRenderMetricScope::idRenderMetricScope( const char* scopeLabel ) :
	label( scopeLabel ), startTicks( 0.0 ), frameSerial( 0 ), threadId( 0 ),
	depth( 0 ), enabled( false ) {
	if ( scopeLabel == NULL ||
		!collecting.load( std::memory_order_acquire ) ) {
		return;
	}
	frameSerial = collectingSerial.load( std::memory_order_relaxed );
	threadId = GetCurrentThreadId();
	depth = metricDepth++;
	startTicks = MetricsNow();
	enabled = true;
}

idRenderMetricScope::~idRenderMetricScope() {
	if ( !enabled ) {
		return;
	}
	const double endTicks = MetricsNow();
	metricDepth = Max( metricDepth - 1, 0 );
	RecordMetric( label, startTicks, endTicks, frameSerial, threadId, depth );
}

void R_RenderMetricsBeginFrame() {
	collecting.store( false, std::memory_order_release );
	if ( !r_showMetrics.GetBool() || !R_UseVulkanBackend() ) {
		return;
	}
	std::lock_guard< std::mutex > lock( metricsMutex );
	currentFrame.numEvents = 0;
	currentFrame.droppedEvents = 0;
	currentFrame.startTicks = MetricsNow();
	currentFrame.endTicks = currentFrame.startTicks;
	currentFrame.serial = ++nextFrameSerial;
	currentFrame.mainThreadId = GetCurrentThreadId();
	collectingSerial.store( currentFrame.serial, std::memory_order_relaxed );
	collecting.store( true, std::memory_order_release );
}

void R_RenderMetricsEndFrame() {
	if ( !collecting.exchange( false, std::memory_order_acq_rel ) ) {
		return;
	}
	std::lock_guard< std::mutex > lock( metricsMutex );
	currentFrame.endTicks = MetricsNow();
	SortMetricsByStart( currentFrame );
	displayedFrame = currentFrame;
	hasDisplayedFrame = true;
}

void R_RenderMetricsDrawOverlay( int width, int height ) {
	if ( !r_showMetrics.GetBool() || !vulkanBackend.IsFrameActive() ||
		width <= 0 || height <= 0 || !EnsureMetricsImGui() ) {
		return;
	}
	renderMetricFrame_t frame;
	{
		std::lock_guard< std::mutex > lock( metricsMutex );
		if ( !hasDisplayedFrame ) {
			return;
		}
		frame = displayedFrame;
	}

	scopedMetricsImGuiContext_t context( metricsImGuiContext );
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2( static_cast< float >( width ), static_cast< float >( height ) );
	io.DisplayFramebufferScale = ImVec2( 1.0f, 1.0f );
	const double now = MetricsNow();
	io.DeltaTime = lastOverlayTicks > 0.0 ? idMath::ClampFloat( 0.001f, 0.25f,
		static_cast< float >( TicksToMilliseconds( now - lastOverlayTicks ) * 0.001 ) ) :
		( 1.0f / 60.0f );
	lastOverlayTicks = now;

	ImGui::NewFrame();
	ImGui::SetNextWindowPos( ImVec2( 12.0f, 12.0f ), ImGuiCond_Always );
	ImGui::SetNextWindowSize( ImVec2( Max( 640.0f, width * 0.72f ),
		Max( 240.0f, height - 24.0f ) ), ImGuiCond_Always );
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoInputs;
	if ( ImGui::Begin( "Vulkan Frame Metrics  (r_showmetrics 1)", NULL, flags ) ) {
		DrawTimeline( frame );
	}
	ImGui::End();
	ImGui::Render();
	SubmitMetricsDrawData( ImGui::GetDrawData() );
}

void R_RenderMetricsShutdown() {
	collecting.store( false, std::memory_order_release );
	if ( metricsImGuiContext != NULL ) {
		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImGuiContext* destroyedContext = metricsImGuiContext;
		ImGui::SetCurrentContext( destroyedContext );
		ImGui::DestroyContext( destroyedContext );
		metricsImGuiContext = NULL;
		ImGui::SetCurrentContext( previous == destroyedContext ? NULL : previous );
	}
	metricsFontImage = NULL;
	lastOverlayTicks = 0.0;
	hasDisplayedFrame = false;
}
