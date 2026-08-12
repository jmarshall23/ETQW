#ifndef __MEGATEXTURE_SHADOW_BAKER_H__
#define __MEGATEXTURE_SHADOW_BAKER_H__

class idMapFile;
struct megaTextureProject_t;

// Builds a terrain-space visibility grid for sun shadows cast by static model
// entities in the owning map.  bakeLightmap controls whether a model receives
// an atlas; it deliberately does not stop that model from occluding terrain.
bool MegaTextureBuildStaticModelShadows( const idMapFile &mapFile,
	const megaTextureProject_t &project, const std::vector<float> &heights,
	const idVec3 &sunDirection, int shadowResolution,
	std::vector<float> &visibility, int &entityCount, int &triangleCount,
	idStr &error );

#endif
