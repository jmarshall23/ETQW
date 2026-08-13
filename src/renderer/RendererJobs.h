// Copyright (C) 2007 Id Software, Inc.

#ifndef __RENDERER_JOBS_H__
#define __RENDERER_JOBS_H__

typedef void ( *renderParallelForFunction_t )( void* context, int firstItem,
	int itemCount, int workerIndex );

// Persistent fork/join workers for read-only renderer preparation.  The
// calling thread participates in every dispatch and does not return until all
// ranges have completed; Vulkan ownership therefore stays on the render
// thread.
class sdRendererJobSystem {
public:
					sdRendererJobSystem();
					~sdRendererJobSystem();

	void			Init();
	void			Shutdown();
	void			ParallelFor( int itemCount, int grainSize, const char* metricLabel,
					renderParallelForFunction_t function, void* context );
	bool			IsEnabled() const;
	int			GetWorkerCount() const;

private:
	struct impl_t;
	impl_t*			impl;
};

extern sdRendererJobSystem rendererJobs;

#endif /* !__RENDERER_JOBS_H__ */
