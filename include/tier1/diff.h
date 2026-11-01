//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
// Serialization/unserialization buffer
//=============================================================================//

#pragma once
#pragma once

int FindDiffs(uint8_t const *NewBlock, uint8_t const *OldBlock,
			  int NewSize, int OldSize, int &DiffListSize,uint8_t *Output,uint32_t OutSize);

int FindDiffsForLargeFiles(uint8_t const *NewBlock, uint8_t const *OldBlock,
						   int NewSize, int OldSize, int &DiffListSize,uint8_t *Output,
						   uint32_t OutSize,
						   int hashsize=65536);

void ApplyDiffs(uint8_t const *OldBlock, uint8_t const *DiffList,
                int OldSize, int DiffListSize, int &ResultListSize,uint8_t *Output,uint32_t OutSize);

int FindDiffsLowMemory(uint8_t const *NewBlock, uint8_t const *OldBlock,
					   int NewSize, int OldSize, int &DiffListSize,uint8_t *Output,uint32_t OutSize);


