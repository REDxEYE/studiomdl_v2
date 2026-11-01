//============ Copyright (c) Valve Corporation, All rights reserved. ============


#include "movieobjects/dmeusersettings.h"
#include "datamodel/dmelementfactoryhelper.h"
#include "datamodel/dmattributevar.h"

#if defined( PLATFORM_WINDOWS_PC )
#define WIN32_LEAN_AND_MEAN
#define OEMRESOURCE
#include <windows.h>
#include <winreg.h>
#elif defined( PLATFORM_POSIX )
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <map>
#include <string>
#endif

 // memdbgon must be the last include file in a .cpp file!!!

//-----------------------------------------------------------------------------

IMPLEMENT_ELEMENT_FACTORY( DmeUserSettings, CDmeUserSettings);

static CUtlMap< const char *, CDmAttribute * >s_RegistryPathToAttribute( DefLessFunc( const char * ) );

CUtlVector< IDmeUserSettingsChangedListener * > CDmeUserSettings::s_UserSettingsChangedListeners;

//-----------------------------------------------------------------------------

CDmeUserSettings *CDmeUserSettings::SharedUserSettings()
{
	static CDmeUserSettings *s_UserSettings;
	if( !s_UserSettings )
	{
		s_UserSettings = CreateElement< CDmeUserSettings >( "userSettings", DMFILEID_INVALID );
	}
	return s_UserSettings;
}

//-----------------------------------------------------------------------------

void CDmeUserSettings::OnConstruction()
{

}

void CDmeUserSettings::OnDestruction()
{
}

//-----------------------------------------------------------------------------

void CDmeUserSettings::AddUserSettingsChangedListener( IDmeUserSettingsChangedListener *pListener )
{
	s_UserSettingsChangedListeners.AddToTail( pListener );
}

//-----------------------------------------------------------------------------

void CDmeUserSettings::OnAttributeChanged( CDmAttribute *pAttribute )
{
	BaseClass::OnAttributeChanged( pAttribute );
	
	const char *pRegistryPath = FindRegistryPathForAttribute( pAttribute );
	if( pRegistryPath )
	{
		SetRegistryFromAttribute( pAttribute, pRegistryPath );

		KeyValues *pMessage = new KeyValues( "OnUserSettingsChanged", NameKey, pAttribute->GetName() );
		pMessage->SetPtr( AttributeKey, pAttribute );
		pMessage->SetString( RegistryPathKey, pRegistryPath );

		int nListenerCount = s_UserSettingsChangedListeners.Count();
		for( int i=nListenerCount - 1; i>=0 ; i-- )
		{
			s_UserSettingsChangedListeners[ i ]->OnUserSettingsChanged( pMessage );
		}
	}

}

//-----------------------------------------------------------------------------

void CDmeUserSettings::SetAttributeForRegistryPathInDatabase( CDmAttribute *pAttribute, const char *pRegistryPath )
{
	s_RegistryPathToAttribute.InsertOrReplace( pRegistryPath, pAttribute );
}

CDmAttribute *CDmeUserSettings::FindAttributeForRegistryPath( const char *pRegistryPath )
{
	int nIndex = s_RegistryPathToAttribute.Find( pRegistryPath );
	if( s_RegistryPathToAttribute.IsValidIndex( nIndex ) )
	{
		return s_RegistryPathToAttribute.Element( nIndex );
	}
	else
	{
		return nullptr;
	}
}

const char *CDmeUserSettings::FindRegistryPathForAttribute( CDmAttribute *pAttribute )
{
	const char *pReturnValue = nullptr;
	for ( int i = s_RegistryPathToAttribute.FirstInorder(); i != s_RegistryPathToAttribute.InvalidIndex(); i = s_RegistryPathToAttribute.NextInorder( i ) )
	{
		CDmAttribute *pCurrentAttribute = s_RegistryPathToAttribute[i];
		if( pCurrentAttribute == pAttribute )
		{
			pReturnValue = s_RegistryPathToAttribute.Key( i );
			break;
		}
	}
	return pReturnValue;
}

//-----------------------------------------------------------------------------

void CDmeUserSettings::GetAttributeNameFromRegistryPath( const char *pRegistryPath, char *pAttributeName, int nAttributeNameLength )
{
	CUtlVector<char*, CUtlMemory<char*, int> > attributeStrings;
	V_SplitString( pRegistryPath, "\\", attributeStrings );
	Q_snprintf( pAttributeName, nAttributeNameLength, "%s", attributeStrings.Tail());
	attributeStrings.PurgeAndDeleteElements();
}

CDmeUserSettings *CDmeUserSettings::GetUserSettingsForRegistryPath( const char *pRegistryPath )
{
	CDmeUserSettings *pReturnSettings = this;
	CDmeUserSettings *pCurrentSettings = this;
	CUtlVector<char*, CUtlMemory<char*, int> > attributeStrings;
	V_SplitString( pRegistryPath, "\\", attributeStrings );

	if( attributeStrings.Count() > 1 )
	{
		for( int i = 0; i < attributeStrings.Count() - 1; i++ )
		{
			const char *currentName = attributeStrings[i];
			pCurrentSettings = pReturnSettings->GetValueElement< CDmeUserSettings >(currentName );						
			if( !pCurrentSettings )
			{
				pCurrentSettings = CreateElement< CDmeUserSettings >(  currentName, pReturnSettings->GetFileId() );
				pReturnSettings->SetValue( currentName, pCurrentSettings, true );
			}
			pReturnSettings = pCurrentSettings;
		}
	}
	
	attributeStrings.PurgeAndDeleteElements();

	return pReturnSettings;
}


//-----------------------------------------------------------------------------

bool CDmeUserSettings::RegistryPathHasValue( const char *pRegistryPath )
{
	char pValueString[1024] = "";
	bool bKeyIsAvailable = GetRegistryString( pRegistryPath, pValueString, sizeof( pValueString ) );
	return bKeyIsAvailable;
}

void CDmeUserSettings::SetAttributeFromRegistry( CDmAttribute *pAttribute, const char *pRegistryPath )
{
	char pValueString[1024];
	GetRegistryString( pRegistryPath, pValueString, sizeof( pValueString ) );
	pAttribute->SetValueFromString( pValueString );
}

void CDmeUserSettings::SetRegistryFromAttribute( CDmAttribute *pAttribute, const char *pRegistryPath )
{
	char pValueString[1024];
	pAttribute->GetValueAsString( pValueString, sizeof( pValueString ) );
	SetRegistryString( pRegistryPath, pValueString );
}

//-----------------------------------------------------------------------------
// [10/23/2009 Stefan] Replace the following methods by something in tierX when it is available

void CDmeUserSettings::CreateRegistryEntryAndValueKey( const char *pRegistryPath, char *pEntryKey, int nEntryKeyLength, char *pValueKey, int nValueKeyLength )
{
	char pFullPath[1024];

	V_snprintf( 
		pFullPath, 
		sizeof(pFullPath), 
		"Software\\Valve\\SourceFilmMaker\\%s", 
		pRegistryPath );

	int nFullPathLength = V_strlen( pFullPath );
	int nSplitPosition = -1;
	for( int i = nFullPathLength - 1; i >= 0; i-- )
	{
		if( pFullPath[i]=='\\')
		{
			nSplitPosition = i + 1;
			break;
		}
	}
	if( nSplitPosition >= 0 && nSplitPosition < nFullPathLength )
	{
		V_StrRight( pFullPath, nFullPathLength - nSplitPosition, pValueKey, nValueKeyLength );
		V_StrLeft( pFullPath, nSplitPosition, pEntryKey, nEntryKeyLength );
	}
	else
	{
		V_snprintf( pEntryKey, nEntryKeyLength, "%s", pFullPath );
		V_snprintf( pValueKey, nValueKeyLength, "" );
	}

}

#if defined( PLATFORM_POSIX ) && !defined( PLATFORM_WINDOWS_PC )

// There is no registry on POSIX systems: the settings are stored in a small "path=value" text file,
// $XDG_CONFIG_HOME/Valve/SourceFilmMaker/registry.cfg (falls back to ~/.config when XDG_CONFIG_HOME is not set).
namespace
{
	typedef std::map< std::string, std::string > RegistryMap_t;

	bool GetRegistryFilePath( std::string &directory, std::string &file )
	{
		std::string base;
		const char *pConfigHome = getenv( "XDG_CONFIG_HOME" );
		if ( pConfigHome && pConfigHome[0] == '/' )
		{
			base = pConfigHome;
		}
		else
		{
			const char *pHome = getenv( "HOME" );
			if ( !pHome || !pHome[0] )
				return false;
			base = std::string( pHome ) + "/.config";
		}

		directory = base + "/Valve/SourceFilmMaker";
		file = directory + "/registry.cfg";
		return true;
	}

	// mkdir -p
	bool MakeDirectories( const std::string &path )
	{
		for ( size_t i = 1; i <= path.size(); i++ )
		{
			if ( i != path.size() && path[i] != '/' )
				continue;

			std::string partial = path.substr( 0, i );
			if ( mkdir( partial.c_str(), 0700 ) != 0 && errno != EEXIST )
				return false;
		}
		return true;
	}

	std::string EscapeValue( const std::string &value )
	{
		std::string out;
		for ( size_t i = 0; i < value.size(); i++ )
		{
			switch ( value[i] )
			{
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			default: out += value[i]; break;
			}
		}
		return out;
	}

	std::string UnescapeValue( const std::string &value )
	{
		std::string out;
		for ( size_t i = 0; i < value.size(); i++ )
		{
			if ( value[i] == '\\' && i + 1 < value.size() )
			{
				i++;
				out += ( value[i] == 'n' ) ? '\n' : ( value[i] == 'r' ) ? '\r' : value[i];
			}
			else
			{
				out += value[i];
			}
		}
		return out;
	}

	void ParseRegistryLine( const std::string &line, RegistryMap_t &values )
	{
		size_t nSeparator = line.find( '=' );
		if ( line.empty() || line[0] == '#' || nSeparator == std::string::npos || nSeparator == 0 )
			return;

		values[ line.substr( 0, nSeparator ) ] = UnescapeValue( line.substr( nSeparator + 1 ) );
	}

	void LoadRegistry( const std::string &file, RegistryMap_t &values )
	{
		FILE *fp = fopen( file.c_str(), "rb" );
		if ( !fp )
			return;

		std::string line;
		int c;
		while ( ( c = fgetc( fp ) ) != EOF )
		{
			if ( c == '\n' )
			{
				ParseRegistryLine( line, values );
				line.clear();
			}
			else
			{
				line += (char)c;
			}
		}
		ParseRegistryLine( line, values );
		fclose( fp );
	}

	// Writes the file next to the destination and renames it, so a crash never leaves a half written settings file
	bool SaveRegistry( const std::string &directory, const std::string &file, const RegistryMap_t &values )
	{
		if ( !MakeDirectories( directory ) )
			return false;

		std::string tempFile = file + ".tmp";
		FILE *fp = fopen( tempFile.c_str(), "wb" );
		if ( !fp )
			return false;

		bool bOk = true;
		for ( RegistryMap_t::const_iterator it = values.begin(); it != values.end(); ++it )
		{
			std::string line = it->first + "=" + EscapeValue( it->second ) + "\n";
			if ( fwrite( line.data(), 1, line.size(), fp ) != line.size() )
			{
				bOk = false;
				break;
			}
		}
		if ( fclose( fp ) != 0 )
		{
			bOk = false;
		}

		if ( !bOk || rename( tempFile.c_str(), file.c_str() ) != 0 )
		{
			unlink( tempFile.c_str() );
			return false;
		}
		return true;
	}
}

#endif // POSIX

bool CDmeUserSettings::SetRegistryString(const char *pRegistryPath, const char *pStringValue)
{
#if defined( PLATFORM_WINDOWS_PC )
	HKEY hKey;
	HKEY hSlot = HKEY_CURRENT_USER;

	char pEntryKey[1024];
	char pValueKey[1024];
	CreateRegistryEntryAndValueKey( pRegistryPath, pEntryKey, sizeof( pEntryKey), pValueKey, sizeof( pValueKey ) );

	if ( RegCreateKeyEx( hSlot, pEntryKey ,NULL, NULL, REG_OPTION_NON_VOLATILE, pStringValue ? KEY_WRITE : KEY_ALL_ACCESS, NULL, &hKey , NULL) != ERROR_SUCCESS )
	{
		return false;
	}

	if ( RegSetValueEx( hKey, pValueKey, NULL, REG_SZ, (unsigned char *)pStringValue, strlen(pStringValue) + 1 ) == ERROR_SUCCESS )
	{
		RegCloseKey(hKey);
		return true;
	}

	RegCloseKey(hKey);
	return false;
#elif defined( PLATFORM_POSIX )
	if ( !pRegistryPath || !pStringValue )
		return false;

	std::string directory, file;
	if ( !GetRegistryFilePath( directory, file ) )
		return false;

	RegistryMap_t values;
	LoadRegistry( file, values );
	values[ pRegistryPath ] = pStringValue;
	return SaveRegistry( directory, file, values );
#else
	return false;
#endif
}

bool CDmeUserSettings::GetRegistryString(const char *pRegistryPath, char *pStringValue, int nStringValueLen)
{
#if defined( PLATFORM_WINDOWS_PC )
	pStringValue[0] = 0;

	HKEY hKey;
	HKEY hSlot = HKEY_CURRENT_USER;
	
	char pEntryKey[1024];
	char pValueKey[1024];
	CreateRegistryEntryAndValueKey( pRegistryPath, pEntryKey, sizeof( pEntryKey), pValueKey, sizeof( pValueKey ) );

	if ( RegOpenKeyEx( hSlot, pEntryKey, NULL, KEY_READ, &hKey ) != ERROR_SUCCESS )
	{
		return false;
	}

	DWORD len = nStringValueLen;
	if ( RegQueryValueEx( hKey, pValueKey, NULL, NULL, (unsigned char *)pStringValue, &len ) == ERROR_SUCCESS )
	{		
		RegCloseKey(hKey);
		return true;
	}

	RegCloseKey(hKey);
	return false;
#elif defined( PLATFORM_POSIX )
	pStringValue[0] = 0;
	if ( !pRegistryPath || nStringValueLen <= 0 )
		return false;

	std::string directory, file;
	if ( !GetRegistryFilePath( directory, file ) )
		return false;

	RegistryMap_t values;
	LoadRegistry( file, values );

	RegistryMap_t::const_iterator it = values.find( pRegistryPath );
	if ( it == values.end() )
		return false;

	V_strncpy( pStringValue, it->second.c_str(), nStringValueLen );
	return true;
#else
	pStringValue[0] = 0;
	return false;
#endif
}
