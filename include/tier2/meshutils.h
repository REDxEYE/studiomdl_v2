//===== Copyright © 2005-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: A set of utilities to help with generating meshes
//
//===========================================================================//

#pragma once




//-----------------------------------------------------------------------------
// Helper methods to create various standard index buffer types
//-----------------------------------------------------------------------------
void GenerateSequentialIndexBuffer( unsigned short* pIndexMemory, int nIndexCount, int nFirstVertex );
void GenerateQuadIndexBuffer( unsigned short* pIndexMemory, int nIndexCount, int nFirstVertex );
void GeneratePolygonIndexBuffer( unsigned short* pIndexMemory, int nIndexCount, int nFirstVertex );
void GenerateLineStripIndexBuffer( unsigned short* pIndexMemory, int nIndexCount, int nFirstVertex );
void GenerateLineLoopIndexBuffer( unsigned short* pIndexMemory, int nIndexCount, int nFirstVertex );



