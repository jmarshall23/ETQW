// Copyright (C) 2007 Id Software, Inc.

#ifndef __RENDERER_METRICS_H__
#define __RENDERER_METRICS_H__

// A scope is inert unless r_showmetrics is enabled at the start of a Vulkan
// frame. Labels must remain valid for the lifetime of the program; string
// literals are the intended use.
class idRenderMetricScope {
public:
	explicit idRenderMetricScope( const char* label );
	~idRenderMetricScope();

private:
	const char*	label;
	double		startTicks;
	unsigned int frameSerial;
	unsigned int threadId;
	int			depth;
	bool		enabled;
};

void R_RenderMetricsBeginFrame();
void R_RenderMetricsEndFrame();
void R_RenderMetricsDrawOverlay( int width, int height );
void R_RenderMetricsShutdown();

#define RENDER_METRIC_JOIN_IMPL( a, b ) a##b
#define RENDER_METRIC_JOIN( a, b ) RENDER_METRIC_JOIN_IMPL( a, b )
#define RENDER_METRIC_SCOPE( label ) \
	idRenderMetricScope RENDER_METRIC_JOIN( renderMetricScope_, __LINE__ )( label )

#endif /* !__RENDERER_METRICS_H__ */
