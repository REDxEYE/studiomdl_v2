//
// Created by RED on 16.05.2024.
//

#ifndef STUDIOMDL_V2_FILESYSTEM_STDIO_H
#define STUDIOMDL_V2_FILESYSTEM_STDIO_H

#include "tier0/platform.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "filesystem/basefilesystem.h"





#define GAMEINFO_FILENAME "GAMEINFO.TXT"

bool ShouldFailIo();



ASSERT_INVARIANT( SEEK_CUR == FILESYSTEM_SEEK_CURRENT );
ASSERT_INVARIANT( SEEK_SET == FILESYSTEM_SEEK_HEAD );
ASSERT_INVARIANT( SEEK_END == FILESYSTEM_SEEK_TAIL );


#if __DARWIN_64_BIT_INO_T
#error badness
#endif

#if _DARWIN_FEATURE_64_BIT_INODE
#error additional badness
#endif

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
    void *AllocOptimalReadBuffer( FileHandle_t hFile, unsigned nSize, unsigned nOffset );
    void FreeOptimalReadBuffer( void *p );

protected:
    // implementation of CBaseFileSystem virtual functions
    virtual FILE *FS_fopen( const char *filename, const char *options, unsigned flags, int64 *size, CFileLoadInfo *pInfo );
    virtual void FS_setbufsize( FILE *fp, unsigned nBytes );
    virtual void FS_fclose( FILE *fp );
    virtual void FS_fseek( FILE *fp, int64 pos, int seekType );
    virtual long FS_ftell( FILE *fp );
    virtual int FS_feof( FILE *fp );
    virtual size_t FS_fread( void *dest, size_t destSize, size_t size, FILE *fp );
    virtual size_t FS_fwrite( const void *src, size_t size, FILE *fp );
    virtual bool FS_setmode( FILE *fp, FileMode_t mode );
    virtual size_t FS_vfprintf( FILE *fp, const char *fmt, va_list list );
    virtual int FS_ferror( FILE *fp );
    virtual int FS_fflush( FILE *fp );
    virtual char *FS_fgets( char *dest, int destSize, FILE *fp );
    virtual int FS_stat( const char *path, struct _stat *buf );
    virtual int FS_chmod( const char *path, int pmode );
    virtual HANDLE FS_FindFirstFile(const char *findname, WIN32_FIND_DATA *dat);
    virtual bool FS_FindNextFile(HANDLE handle, WIN32_FIND_DATA *dat);
    virtual bool FS_FindClose(HANDLE handle);
    virtual int FS_GetSectorSize( FILE * );

private:
    bool CanAsync() const
    {
        return m_bCanAsync;
    }

    bool m_bMounted;
    bool m_bCanAsync;
};


#endif //STUDIOMDL_V2_FILESYSTEM_STDIO_H
