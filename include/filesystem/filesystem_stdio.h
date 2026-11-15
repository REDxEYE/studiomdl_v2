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

    // Higher level filesystem methods requiring specific behavior
    void GetLocalCopy( const char *pFileName ) override;

    int	HintResourceNeed( const char *hintlist, int forgetEverything ) override;

    bool IsFileImmediatelyAvailable(const char *pFileName) override;

    WaitForResourcesHandle_t WaitForResources( const char *resourcelist ) override;

    bool GetWaitForResourcesProgress( WaitForResourcesHandle_t handle, float *progress /* out */ , bool *complete /* out */ ) override;

    void CancelWaitForResources( WaitForResourcesHandle_t handle ) override;

    bool IsSteam() const override { return false; }
    FilesystemMountRetval_t MountSteamContent( int nExtraAppId = -1 ) override { return FILESYSTEM_MOUNT_OK; }

    bool GetOptimalIOConstraints( FileHandle_t hFile, unsigned *pOffsetAlign, unsigned *pSizeAlign, unsigned *pBufferAlign ) override;
};

// The filesystem instance (g_pFullFileSystem points at it)
extern CFileSystem_Stdio g_FileSystem_Stdio;


