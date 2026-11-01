//
// Created by RED on 16.05.2024.
//

#pragma once

#include "tier0/platform.h"
#include "filesystem/basefilesystem.h"

#define GAMEINFO_FILENAME "GAMEINFO.TXT"

// CStdFile::Seek takes the C seek origins
ASSERT_INVARIANT( SEEK_CUR == FILESYSTEM_SEEK_CURRENT );
ASSERT_INVARIANT( SEEK_SET == FILESYSTEM_SEEK_HEAD );
ASSERT_INVARIANT( SEEK_END == FILESYSTEM_SEEK_TAIL );

// The local disk filesystem (all low-level access goes through filesystem/stdfile.h).
class CFileSystem_Stdio : public CBaseFileSystem
{
public:
    CFileSystem_Stdio();
    ~CFileSystem_Stdio();

    // Used to get at older versions
    void *QueryInterface( const char *pInterfaceName );

    // Higher level filesystem methods requiring specific behavior
    virtual void GetLocalCopy( const char *pFileName );
    virtual int	HintResourceNeed( const char *hintlist, int forgetEverything );
    virtual bool IsFileImmediatelyAvailable(const char *pFileName);
    virtual WaitForResourcesHandle_t WaitForResources( const char *resourcelist );
    virtual bool GetWaitForResourcesProgress( WaitForResourcesHandle_t handle, float *progress /* out */ , bool *complete /* out */ );
    virtual void CancelWaitForResources( WaitForResourcesHandle_t handle );
    virtual bool IsSteam() const { return false; }
    virtual	FilesystemMountRetval_t MountSteamContent( int nExtraAppId = -1 ) { return FILESYSTEM_MOUNT_OK; }

    bool GetOptimalIOConstraints( FileHandle_t hFile, unsigned *pOffsetAlign, unsigned *pSizeAlign, unsigned *pBufferAlign );
};


