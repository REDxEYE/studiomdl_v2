//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#pragma once


typedef struct phyheader_s
{
	int		size;
	int		id;
	int		solidCount;
	int		checkSum;	// checksum of source .mdl file
} phyheader_t;

