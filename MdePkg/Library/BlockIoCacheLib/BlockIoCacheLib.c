#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>

#include <Protocol/BlockIo.h>

#include <Library/BlockIoCacheLib.h>

#define CACHE_ENTRY_SIGNATURE  SIGNATURE_32 ('B', 'I', 'O', 'C')

typedef struct _CACHE_ENTRY   CACHE_ENTRY;
typedef struct _CACHE_DATA    CACHE_DATA;
typedef struct _PRIVATE_INFO  PRIVATE_INFO;

struct _CACHE_DATA {
  EFI_LBA                      Lba;
  UINT8                        Buffer[BLOCK_IO_CACHE_SIZE];
};

struct _CACHE_ENTRY {
  UINT32                        Signature;
  LIST_ENTRY                    Link;
  CACHE_DATA                    *Data;
};

struct _PRIVATE_INFO {
  UINT32                       BlockSize;
  UINT32                       IoAlign;
  EFI_LBA                      LastLba;
  CACHE_DATA                   *CacheData;
  UINTN                        CacheEntsNo;
  UINTN                        CacheBlocksNo;
  UINTN                        CacheBlockAlign;
  UINTN                        CacheCount;
  LIST_ENTRY                   CacheList;
  BOOLEAN                      CacheInitialized;
};

EFI_STATUS
EFIAPI
BlockIoCacheInitialize (
  IN    EFI_BLOCK_IO_MEDIA  *Media,
  OUT   VOID                **Cache
  )
{
  EFI_STATUS                Status;
  PRIVATE_INFO              *Private;
  UINT16                    CacheEntsNo;

  ASSERT (Media != NULL);
  ASSERT (Cache != NULL);

  Private = AllocatePool (sizeof (PRIVATE_INFO));
  if (Private == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CacheEntsNo = PcdGet16 (PcdBlockIoCacheSize);
  if ((CacheEntsNo & (CacheEntsNo - 1)) != 0) {
    Status = EFI_INVALID_PARAMETER;
    goto ERROR;
  }

  DEBUG ((DEBUG_ERROR, "BlockIoCache: number of cache entries: %d\n", CacheEntsNo));

  Private->CacheData = AllocatePool ((UINTN)CacheEntsNo * sizeof (CACHE_DATA));
  if (Private->CacheData == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ERROR;
  }

  Private->BlockSize = Media->BlockSize;
  Private->IoAlign = Media->IoAlign;
  Private->LastLba = Media->LastBlock;
  Private->CacheEntsNo = (UINTN)CacheEntsNo;
  Private->CacheBlocksNo = BLOCK_IO_CACHE_SIZE / Private->BlockSize;
  Private->CacheBlockAlign = ~(Private->CacheBlocksNo - 1);
  Private->CacheCount = 0;
  InitializeListHead (&Private->CacheList);
  Private->CacheInitialized = TRUE;

  *Cache = Private;

  DEBUG ((DEBUG_ERROR, "BlockIoCache: cache initialized successfully\n"));

  return EFI_SUCCESS;

ERROR:
  FreePool (Private);
  return Status;
}

LIST_ENTRY *
FindCacheEntry (
  IN   PRIVATE_INFO  *Private,
  IN   EFI_LBA       Lba
  )
{
  LIST_ENTRY         *List;
  LIST_ENTRY         *Link;
  CACHE_ENTRY        *Entry;

  if (Private->CacheCount == 0) {
    return NULL;
  }

  DEBUG ((DEBUG_ERROR, "BlockIoCache: FindCacheEntry(%lld)\n", Lba));

  List = &Private->CacheList;
  for (Link = GetFirstNode (List); !IsNull (List, Link); Link = GetNextNode (List, Link)) {
    Entry = CR (Link, CACHE_ENTRY, Link, CACHE_ENTRY_SIGNATURE);
    if (Entry->Data->Lba == Lba) {
      return Link;
    }
  }
  return NULL;
}

VOID
PrintLru (
  IN   PRIVATE_INFO  *Private
  )
{
  LIST_ENTRY         *List;
  LIST_ENTRY         *Link;
  CACHE_ENTRY        *Entry;

  if (Private->CacheCount == 0) {
    return;
  }

  DEBUG ((DEBUG_ERROR, "BlockIoCache: LRU list:\n"));

  List = &Private->CacheList;
  for (Link = GetFirstNode (List); !IsNull (List, Link); Link = GetNextNode (List, Link)) {
    Entry = CR (Link, CACHE_ENTRY, Link, CACHE_ENTRY_SIGNATURE);
    DEBUG ((DEBUG_ERROR, "%lld ", Entry->Data->Lba));
  }
  DEBUG ((DEBUG_ERROR, "\n"));
}

EFI_STATUS
EFIAPI
BlockIoCacheGetCacheParameters (
  IN   VOID                       *Cache,
  IN   EFI_LBA                    Lba,
  OUT  EFI_LBA                    *AlignedLba,
  IN   UINTN                      BufferSize,
  OUT  UINTN                      *BlocksCount
  )
{
  PRIVATE_INFO *Private;

  ASSERT (Cache != NULL);
  ASSERT (AlignedLba != NULL);
  ASSERT (BlocksCount != NULL);
  // TODO: check for other invalid params

  Private = Cache;

  *AlignedLba = Lba & Private->CacheBlockAlign;
  *BlocksCount = ((Lba - *AlignedLba) + (BufferSize / Private->BlockSize) +
		  Private->CacheBlocksNo - 1) / Private->CacheBlocksNo;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BlockIoCacheFind (
  IN   VOID         *Cache,
  IN   EFI_LBA      Lba
  )
{
  PRIVATE_INFO      *Private;
  LIST_ENTRY        *Link;
  CACHE_ENTRY       *Entry;

  ASSERT (Cache != NULL);
  // TODO: check for other invalid params

  Private = Cache;

  Link = FindCacheEntry (Private, Lba);
  if (Link == NULL) {
    DEBUG ((DEBUG_ERROR, "BlockIoCache: cache miss on LBA %lld\n", Lba));
    return EFI_NOT_FOUND;
  }

  DEBUG ((DEBUG_ERROR, "BlockIoCache: cache hit on LBA %lld\n", Lba));

  Entry = CR (Link, CACHE_ENTRY, Link, CACHE_ENTRY_SIGNATURE);
  RemoveEntryList (Link);
  InsertHeadList (&Private->CacheList, &Entry->Link);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BlockIoCacheAdd (
  IN   VOID        *Cache,
  IN   EFI_LBA     Lba,
  IN   UINTN       BufferSize,
  IN   VOID        *Buffer
  )
{
  PRIVATE_INFO     *Private;
  CACHE_DATA       *Data;
  CACHE_ENTRY      *NewEntry;

  ASSERT (Cache != NULL);
  ASSERT (Buffer != NULL);

  Private = Cache;

  if (BufferSize != BLOCK_IO_CACHE_SIZE) {
    return EFI_INVALID_PARAMETER;
  }

  if (Private->CacheCount < Private->CacheEntsNo) {
    Data = &Private->CacheData[Private->CacheCount];
    Data->Lba = Lba;

    NewEntry = AllocatePool (sizeof (CACHE_ENTRY));
    if (NewEntry == NULL) {
      return EFI_INVALID_PARAMETER;
    }
    NewEntry->Signature = CACHE_ENTRY_SIGNATURE;
    NewEntry->Data = Data;

    Private->CacheCount++;
  } else {
    NewEntry = CR (Private->CacheList.BackLink, CACHE_ENTRY, Link, CACHE_ENTRY_SIGNATURE);
    RemoveEntryList (Private->CacheList.BackLink);
    NewEntry->Data->Lba = Lba;
  }

  DEBUG ((DEBUG_ERROR, "BlockIoCache: add new cache entry for LBA %lld\n", Lba));
  CopyMem (NewEntry->Data->Buffer, Buffer, BufferSize);
  InsertHeadList (&Private->CacheList, &NewEntry->Link);

  DEBUG ((DEBUG_ERROR, "BlockIoCache: cache count %d\n", Private->CacheCount));

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BlockIoCacheRead (
  IN   VOID         *Cache,
  IN   EFI_LBA      Lba,
  IN   UINTN        BufferSize,
  OUT  VOID         *Buffer
  )
{
  PRIVATE_INFO      *Private;
  EFI_LBA           CacheLba;
  UINTN             BlocksNo;
  UINTN             BlocksCount;
  UINTN             CacheBlocksCount;
  LIST_ENTRY        *Link;
  UINTN             EntryOffset;
  UINTN             EntrySize;
  CACHE_ENTRY       *Entry;

  ASSERT (Cache != NULL);

  // TODO: check for other invalid params

  Private = Cache;

  PrintLru (Private);

  CacheLba = Lba & Private->CacheBlockAlign;
  BlocksNo = BufferSize / Private->BlockSize;
  BlocksCount = 0;
  CacheBlocksCount = ((Lba - CacheLba) + BlocksNo + Private->CacheBlocksNo - 1) / Private->CacheBlocksNo;
  DEBUG ((DEBUG_ERROR, "BlockIoCache: start cache LBA %d - BlocksNo %d - CacheBlocksCount %d\n",
          CacheLba, BlocksNo, CacheBlocksCount));
  while (CacheBlocksCount--) {
    Link = FindCacheEntry (Private, CacheLba);
    ASSERT (Link != NULL);

    Entry = CR (Link, CACHE_ENTRY, Link, CACHE_ENTRY_SIGNATURE);

    if ((Lba & (Private->CacheBlocksNo - 1)) == 0) {
      EntryOffset = 0;
    } else {
      EntryOffset = (Lba - CacheLba) * Private->BlockSize;
    }

    if (BlocksCount + Private->CacheBlocksNo > BlocksNo) {
      EntrySize = (BlocksNo - BlocksCount) * Private->BlockSize;
    } else {
      EntrySize = BLOCK_IO_CACHE_SIZE;
    }
    if (EntrySize + EntryOffset > BLOCK_IO_CACHE_SIZE) {
      EntrySize -= EntryOffset;
    }

    DEBUG ((DEBUG_ERROR, "BlockIoCache: LBA %lld - cache LBA %lld - EntryOffset %d - EntrySize %d\n",
            Lba, CacheLba, EntryOffset, EntrySize));
    DEBUG ((DEBUG_ERROR, "BlockIoCache: copy %d bytes from cached LBA %lld\n", EntrySize, CacheLba));

    CopyMem (
      (VOID *)((UINTN)Buffer + (BlocksCount * Private->BlockSize)),
      &Entry->Data->Buffer[EntryOffset],
      EntrySize
      );

    BlocksCount += EntrySize / Private->BlockSize;
    Lba += EntrySize / Private->BlockSize;
    CacheLba += Private->CacheBlocksNo;
  }

  return EFI_SUCCESS;
}
