/** @file
  Library used Block I/O cache.

  <Copyright notice>

**/
#ifndef __BLOCK_IO_CACHE_LIB_H__
#define __BLOCK_IO_CACHE_LIB_H__

#include <Uefi.h>

#include <Protocol/BlockIo.h>

typedef struct _BLOCK_IO_CACHE_ENTRY BLOCK_IO_CACHE_ENTRY;
typedef struct _BLOCK_IO_CACHE_DATA BLOCK_IO_CACHE_DATA;
typedef struct _BLOCK_IO_CACHE BLOCK_IO_CACHE;

#define BLOCK_IO_CACHE_ENTRY_SIGNATURE SIGNATURE_32 ('B', 'I', 'O', 'C')

#define BIO_CACHE_DATA_SIZE_SHIFT 12
#define BIO_CACHE_DATA_SIZE (1 << CACHE_DATA_SIZE_SHIFT)

EFI_STATUS
EFIAPI
BlockIoCacheInitialize (
  IN OUT  BLOCK_IO_CACHE      *BlockIoCache,
  IN      EFI_BLOCK_IO_MEDIA  *Media
  );

EFI_STATUS
EFIAPI
BlockIoCacheGetCacheParameters (
  IN      BLOCK_IO_CACHE          *BlockIoCache,
  IN      EFI_LBA                 Lba,
  OUT     EFI_LBA                 *AlignedLba,
  IN      UINTN                   NumberOfBlocks,
  OUT     UINTN                   *AlignedNumberOfBlocks
  );

EFI_STATUS
EFIAPI
BlockIoCacheGetBlockSize (
  IN      BLOCK_IO_CACHE         *BlockIoCache,
  OUT     UINTN                  *BlockSize
  );

EFI_STATUS
EFIAPI
BlockIoCacheRead2 (
  IN OUT  BLOCK_IO_CACHE  *BlockIoCache,
  IN      EFI_LBA         CacheLba,
  IN      UINTN           CacheNumberOfBlocks,
  IN OUT  EFI_LBA         *Lba,
  IN      UINTN           NumberOfBlocks,
  IN      VOID            *Buffer,
  IN OUT  UINTN           *BufferOffset
  );

EFI_STATUS
EFIAPI
BlockIoCacheRead (
  IN OUT  BLOCK_IO_CACHE  *BlockIoCache,
  IN      EFI_LBA         CacheLba,
  IN      UINTN           CacheNumberOfBlocks,
  IN      EFI_LBA         Lba,
  IN      UINTN           NumberOfBlocks,
  IN      VOID            *Buffer
  );

EFI_STATUS
EFIAPI
BlockIoCacheAdd2 (
  IN OUT  BLOCK_IO_CACHE  *BlockIoCache,
  IN      EFI_LBA         Lba,
  IN      VOID            *Buffer
  );

EFI_STATUS
EFIAPI
BlockIoCacheAdd (
  IN OUT  BLOCK_IO_CACHE  *BlockIoCache,
  IN      EFI_LBA         Lba,
  IN      UINTN           NumberOfBlocks,
  IN      VOID            *Buffer
  );

EFI_STATUS
EFIAPI
BlockIoCacheInvalidate (
  IN OUT  BLOCK_IO_CACHE  *BlockIoCache,
  IN      EFI_LBA         CacheLba,
  IN      UINTN           CacheNumberOfBlocks,
  IN      EFI_LBA         Lba,
  IN      UINTN           NumberOfBlocks,
  IN      VOID            *Buffer
  );

EFI_STATUS
EFIAPI
BlockIoCacheCleanup (
  IN OUT BLOCK_IO_CACHE *BlockIoCache
  );

struct _BLOCK_IO_CACHE_DATA {
  EFI_LBA                      Lba;
  VOID                         *Data;
};

struct _BLOCK_IO_CACHE_ENTRY {
  UINT32                        Signature;
  LIST_ENTRY                    Link;
  BLOCK_IO_CACHE_DATA           *BlockInfo;
};

struct _BLOCK_IO_CACHE {
  UINT32                       BlockSize;
  UINT32                       IoAlign;
  EFI_LBA                      LastLba;
  BLOCK_IO_CACHE_DATA          *CacheData;
  UINT16                       CacheSize;
  UINT16                       CacheCount;
  LIST_ENTRY                   CacheList;
  BOOLEAN                      CacheInitialized;
};

#endif // __BLOCK_IO_CACHE_LIB_H__
