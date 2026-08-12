/*
===========================================================================

DarklightNG Source Code
Copyright (C) 2026 - Justin Marshall(aka IceColdDuke).

This file is part of the DarklightNG GPL source code.
This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

DarklightNG is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DarklightNG is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

===========================================================================
*/

#include "../../../idlib/precompiled.h"
#pragma hdrstop

#include "dmap.h"

/*
==============================================================================

LEAF FILE GENERATION

Save out name.line for qe3 to read
==============================================================================
*/


/*
=============
LeakFile

Finds the shortest possible chain of portals
that leads from the outside leaf to a specifically
occupied leaf
=============
*/
void LeakFile (tree_t *tree)
{
	idVec3	mid;
	FILE	*linefile;
	idStr	filename;
	idStr	ospath;
	node_t	*node;
	int		count;

	if (!tree->outside_node.occupied)
		return;

	common->Printf ("--- LeakFile ---\n");

	//
	// write the points to the file
	//
	sprintf( filename, "%s.lin", dmapGlobals.mapFileBase );
	ospath = fileSystem->RelativePathToOSPath( filename );
	linefile = fopen( ospath, "w" );
	if ( !linefile ) {
		common->Error( "Couldn't open %s\n", filename.c_str() );
	}

	count = 0;
	node = &tree->outside_node;
	while (node->occupied > 1)
	{
		int			next;
		uPortal_t	*p, *nextportal;
		node_t		*nextnode;
		int			s;

		// find the best portal exit
		next = node->occupied;
		for (p=node->portals ; p ; p = p->next[!s])
		{
			s = (p->nodes[0] == node);
			if (p->nodes[s]->occupied
				&& p->nodes[s]->occupied < next)
			{
				nextportal = p;
				nextnode = p->nodes[s];
				next = nextnode->occupied;
			}
		}
		node = nextnode;
		mid = nextportal->winding->GetCenter();
		fprintf (linefile, "%f %f %f\n", mid[0], mid[1], mid[2]);
		count++;
	}
	// add the occupant center
	node->occupant->mapEntity->epairs.GetVector( "origin", "", mid );

	fprintf (linefile, "%f %f %f\n", mid[0], mid[1], mid[2]);
	common->Printf ("%5i point linefile\n", count+1);

	fclose (linefile);
}

