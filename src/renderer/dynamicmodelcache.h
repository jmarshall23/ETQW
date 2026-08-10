// Copyright (C) 2007 Id Software, Inc.
//
// Reconstructed from the original ETQW PDB.  The cache uses the address of a
// list item as its 32-bit handle, matching the retail method signatures.

#ifndef __RENDERER_DYNAMICMODELCACHE_H__
#define __RENDERER_DYNAMICMODELCACHE_H__

#include "../framework/CVarSystem.h"

class idRenderEntityLocal;

class sdDynamicModelCache {
public:
	sdDynamicModelCache();
	~sdDynamicModelCache();

	unsigned	AddToCache( idRenderEntityLocal* def, int size );
	void		Touch( unsigned handle, idRenderEntityLocal* def, int size );
	void		NeedsPurging( idRenderEntityLocal** def );
	void		Remove( unsigned handle );

private:
	struct modelCacheItem_t {
		unsigned				size;
		idRenderEntityLocal*	def;
		modelCacheItem_t*		next;
		modelCacheItem_t*		prev;
	};
	static_assert( sizeof( modelCacheItem_t ) == 16, "modelCacheItem_t must match the ETQW PDB layout" );

	void		Add( modelCacheItem_t* item );

	modelCacheItem_t*	first;
	modelCacheItem_t*	last;
	unsigned			used;

	static idCVar r_dynamicModelCacheMegs;
};

static_assert( sizeof( sdDynamicModelCache ) == 12, "sdDynamicModelCache must match the ETQW PDB layout" );

ID_INLINE sdDynamicModelCache::sdDynamicModelCache() :
	first( NULL ),
	last( NULL ),
	used( 0 ) {
}

ID_INLINE sdDynamicModelCache::~sdDynamicModelCache() {
	while ( first != NULL ) {
		modelCacheItem_t* item = first;
		first = item->next;
		delete item;
	}
	last = NULL;
	used = 0;
}

ID_INLINE void sdDynamicModelCache::Add( modelCacheItem_t* item ) {
	item->next = NULL;
	item->prev = last;
	if ( last != NULL ) {
		last->next = item;
	} else {
		first = item;
	}
	last = item;
}

ID_INLINE unsigned sdDynamicModelCache::AddToCache( idRenderEntityLocal* def, int size ) {
	modelCacheItem_t* item = new modelCacheItem_t;
	item->size = size > 0 ? static_cast< unsigned >( size ) : 0;
	item->def = def;
	Add( item );
	used += item->size;
	return static_cast< unsigned >( reinterpret_cast< UINT_PTR >( item ) );
}

ID_INLINE void sdDynamicModelCache::Touch( unsigned handle, idRenderEntityLocal* def, int size ) {
	modelCacheItem_t* item = reinterpret_cast< modelCacheItem_t* >( static_cast< UINT_PTR >( handle ) );
	if ( item == NULL ) {
		return;
	}

	used -= item->size;
	item->size = size > 0 ? static_cast< unsigned >( size ) : 0;
	item->def = def;
	used += item->size;

	if ( item == last ) {
		return;
	}
	if ( item->prev != NULL ) {
		item->prev->next = item->next;
	} else {
		first = item->next;
	}
	if ( item->next != NULL ) {
		item->next->prev = item->prev;
	}
	Add( item );
}

ID_INLINE void sdDynamicModelCache::NeedsPurging( idRenderEntityLocal** def ) {
	if ( def == NULL ) {
		return;
	}
	const unsigned limit = static_cast< unsigned >( r_dynamicModelCacheMegs.GetInteger() ) * 1024U * 1024U;
	*def = used > limit && first != NULL ? first->def : NULL;
}

ID_INLINE void sdDynamicModelCache::Remove( unsigned handle ) {
	modelCacheItem_t* item = reinterpret_cast< modelCacheItem_t* >( static_cast< UINT_PTR >( handle ) );
	if ( item == NULL ) {
		return;
	}
	if ( item->prev != NULL ) {
		item->prev->next = item->next;
	} else {
		first = item->next;
	}
	if ( item->next != NULL ) {
		item->next->prev = item->prev;
	} else {
		last = item->prev;
	}
	used -= item->size;
	delete item;
}

#endif /* !__RENDERER_DYNAMICMODELCACHE_H__ */
