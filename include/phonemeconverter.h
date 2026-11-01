//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#pragma once
#ifdef _WIN32
#pragma once
#endif

const char *ConvertPhoneme( int code );
int TextToPhoneme( const char *text );
float WeightForPhonemeCode( int code );
float WeightForPhoneme( char *text );

int NumPhonemes();
int TextToPhonemeIndex( const char *text );
const char *NameForPhonemeByIndex( int index );
int CodeForPhonemeByIndex( int index );
const char *DescForPhonemeByIndex( int index );
bool IsStandardPhoneme( int index );
unsigned char CodeToByteCode( int code );
int ByteCodeToCode( unsigned char byteCode );

