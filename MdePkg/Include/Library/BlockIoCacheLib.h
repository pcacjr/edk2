/** @file
  Library used Block I/O cache.

  <Copyright notice>

**/
#ifndef __BLOCK_IO_CACHE_LIB_H__
#define __BLOCK_IO_CACHE_LIB_H__

#include <Uefi.h>

#include <Protocol/BlockIo.h>

#define BLOCK_IO_CACHE_SIZE EFI_PAGE_SIZE

EFI_STATUS
EFIAPI
BlockIoCacheInitialize (
  IN    EFI_BLOCK_IO_MEDIA  *Media,
  OUT   VOID                **Cache
  );

EFI_STATUS
EFIAPI
BlockIoCacheGetCacheParameters (
  IN   VOID                       *Cache,
  IN   EFI_LBA                    Lba,
  OUT  EFI_LBA                    *AlignedLba,
  IN   UINTN                      BufferSize,
  OUT  UINTN                      *BlocksCount
  );

EFI_STATUS
EFIAPI
BlockIoCacheFind (
  IN   VOID         *Cache,
  IN   EFI_LBA      Lba
  );

EFI_STATUS
EFIAPI
BlockIoCacheAdd (
  IN   VOID        *Cache,
  IN   EFI_LBA     Lba,
  IN   UINTN       BufferSize,
  IN   VOID        *Buffer
  );

EFI_STATUS
EFIAPI
BlockIoCacheRead (
  IN   VOID         *Cache,
  IN   EFI_LBA      Lba,
  IN   UINTN        BufferSize,
  OUT  VOID         *Buffer
  );

#endif // __BLOCK_IO_CACHE_LIB_H__
