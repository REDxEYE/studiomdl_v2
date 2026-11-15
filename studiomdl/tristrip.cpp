//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

// tristrip - convert triangle list into tristrips and fans

#pragma warning( disable : 4244 )
#pragma warning( disable : 4237 )
#pragma warning( disable : 4305 )


#include <stdio.h>
#include <stdlib.h>

#include "common/cmdlib.h"
//#include "lbmlib.h"
#include "common/scriplib.h"
#include "studio.h"
#include "studiomdl/studiomdl.h"

int		used[MAXSTUDIOTRIANGLES];

// the command list holds counts and s/t values that are valid for
// every frame
short	commands[MAXSTUDIOTRIANGLES * 13];
int		numcommands;

// all frames will have their vertexes rearranged and expanded
// so they are in the order expected by the command list

int		allverts, alltris;

int		stripverts[MAXSTUDIOTRIANGLES+2];
int		striptris[MAXSTUDIOTRIANGLES+2];
int		stripcount;

int		neighbortri[MAXSTUDIOTRIANGLES][3];
int		neighboredge[MAXSTUDIOTRIANGLES][3];


s_trianglevert_t (*triangles)[3];
s_mesh_t *pmesh;



/*
================
StripLength
================
*/

/*
===========
FanLength
===========
*/


/*
================
BuildTris

Generate a list of trifans or strips
for the model, which holds for all frames
================
*/
int	numcommandnodes;

