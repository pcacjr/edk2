#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>

#include <Protocol/BlockIo.h>

#include <Library/BlockIoCacheLib.h>

#define BIO_CACHE_DEBUG

#define NR_CACHE_BLOCKS(_BlockIoCache) (UINTN)(EFI_PAGE_SIZE / _BlockIoCache->BlockSize)

EFI_STATUS
EFIAPI
BlockIoCacheInitialize (
  IN OUT  BLOCK_IO_CACHE      *BlockIoCache,
  IN      EFI_BLOCK_IO_MEDIA  *Media
  )
{
  UINT16 CacheSize;

  if (Media == NULL || BlockIoCache == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (BlockIoCache->CacheInitialized) {
    return EFI_SUCCESS;
  }

  CacheSize = PcdGet16 (PcdBlockIoCacheSize);
  if ((CacheSize & (CacheSize - 1)) != 0) {
    return EFI_INVALID_PARAMETER;
  }

  BlockIoCache->CacheData = AllocateZeroPool (CacheSize * sizeof (BLOCK_IO_CACHE_DATA));
  if (BlockIoCache->CacheData == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  BlockIoCache->BlockSize = Media->BlockSize;
  BlockIoCache->IoAlign = Media->IoAlign;
  BlockIoCache->LastLba = Media->LastBlock;
  BlockIoCache->CacheSize = CacheSize;
  BlockIoCache->CacheCount = 0;
  InitializeListHead (&BlockIoCache->CacheList);
  BlockIoCache->CacheInitialized = TRUE;
  DEBUG ((DEBUG_ERROR, "BlockIoCache: cache initialized - BlockSize: %ld\n", Media->BlockSize));
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BlockIoCacheGetCacheParameters (
  IN      BLOCK_IO_CACHE          *BlockIoCache,
  IN      EFI_LBA                 Lba,
  OUT     EFI_LBA                 *AlignedLba,
  IN      UINTN                   NumberOfBlocks,
  OUT     UINTN                   *AlignedNumberOfBlocks
  )
{
  UINTN CacheBlocksNo;

  if (BlockIoCache == NULL || AlignedLba == NULL || AlignedNumberOfBlocks == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (!BlockIoCache->CacheInitialized) {
    return EFI_INVALID_PARAMETER;
  }

  CacheBlocksNo = NR_CACHE_BLOCKS (BlockIoCache);

  if ((Lba % CacheBlocksNo) != 0) {
    *AlignedLba = Lba & ~(CacheBlocksNo - 1);
  } else {
    *AlignedLba = Lba;
  }

  *AlignedNumberOfBlocks = (NumberOfBlocks + CacheBlocksNo - 1) / CacheBlocksNo;
  if ((Lba - *AlignedLba) + NumberOfBlocks > CacheBlocksNo) {
    (*AlignedNumberOfBlocks)++;
  }
  DEBUG ((DEBUG_ERROR, "BlockIoCache: cache parameters:\n"
	  "            Lba:                    %lld\n"
	  "            AlignedLba:             %lld\n"
	  "            NumberOfBlocks:         %d\n"
	  "            AlignedNumberOfBlocks:  %d\n",
	  Lba, *AlignedLba, NumberOfBlocks, *AlignedNumberOfBlocks));
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BlockIoCacheGetBlockSize (
  IN      BLOCK_IO_CACHE         *BlockIoCache,
  OUT     UINTN                  *BlockSize
  )
{
  if (BlockIoCache == NULL || !BlockIoCache->CacheInitialized || BlockSize == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  *BlockSize = EFI_PAGE_SIZE;
  return EFI_SUCCESS;
}

EFI_STATUS
GetNewBlockInfo (
  IN      BLOCK_IO_CACHE       *BlockIoCache,
  IN      EFI_LBA              Lba,
  IN      VOID                 *Data,
  IN OUT  BLOCK_IO_CACHE_DATA  *BlockInfo
  )
{
  BlockInfo->Data = Data;
  BlockInfo->Lba = Lba;
  return EFI_SUCCESS;
}

EFI_STATUS
GetNewCacheEntry (
  IN   BLOCK_IO_CACHE_DATA   *BlockInfo,
  IN   BLOCK_IO_CACHE_ENTRY  **CacheEntry
  )
{
  BLOCK_IO_CACHE_ENTRY *NewCacheEntry;

  NewCacheEntry = AllocatePool (sizeof (BLOCK_IO_CACHE_ENTRY));
  if (NewCacheEntry == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  NewCacheEntry->Signature = BLOCK_IO_CACHE_ENTRY_SIGNATURE;
  NewCacheEntry->BlockInfo = BlockInfo;
  *CacheEntry = NewCacheEntry;
  return EFI_SUCCESS;
}

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
  )
{
  EFI_STATUS              Status;
  LIST_ENTRY              *CacheList;
  LIST_ENTRY              *Link;
  BLOCK_IO_CACHE_ENTRY    *CacheEntry;
  BLOCK_IO_CACHE_DATA     *BlockInfo;
  UINTN                   DataOffset;
  UINTN                   CacheBlocksNo;
  UINTN                   BlocksToCopy;

  if (BlockIoCache == NULL || Lba == NULL || Buffer == NULL || BufferOffset == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!BlockIoCache->CacheInitialized) {
    return EFI_INVALID_PARAMETER;
  }

  DEBUG ((DEBUG_ERROR,
	  "BlockIoCache: CacheLba %lld CacheNumberOfBlocks %d Lba %lld NumberOfBlocks %d\n",
	  CacheLba, CacheNumberOfBlocks, *Lba, NumberOfBlocks));

  CacheBlocksNo = NR_CACHE_BLOCKS (BlockIoCache);

  if ((*Lba - CacheLba) + NumberOfBlocks > CacheBlocksNo) {
    BlocksToCopy = CacheBlocksNo - (*Lba - CacheLba);
  } else {
    BlocksToCopy = NumberOfBlocks;
  }

  Status = EFI_NOT_FOUND;

  if (BlockIoCache->CacheCount == 0) {
    DEBUG ((DEBUG_ERROR, "BlockIoCache: LBA %lld - cache miss\n", CacheLba));
    goto Done;
  }

  CacheList = &BlockIoCache->CacheList;
  CacheEntry = NULL;
  BlockInfo = NULL;
  for (Link = GetFirstNode (CacheList); !IsNull (CacheList, Link);
       Link = GetNextNode (CacheList, Link)) {
    CacheEntry = CR (Link, BLOCK_IO_CACHE_ENTRY, Link, BLOCK_IO_CACHE_ENTRY_SIGNATURE);
    BlockInfo = CacheEntry->BlockInfo;
    if (BlockInfo->Lba == CacheLba) {
      break;
    }
  }

  if (IsNull (CacheList, Link)) {
    DEBUG ((DEBUG_ERROR, "BlockIoCache: LBA %lld - cache miss\n", CacheLba));
    goto Done;
  }

  DEBUG ((DEBUG_ERROR, "BlockIoCache: LBA %lld - cache hit\n", CacheLba));

  Status = EFI_SUCCESS;

  if ((*Lba & (CacheBlocksNo - 1)) != 0) {
    DataOffset = (*Lba - CacheLba) * BlockIoCache->BlockSize;
  } else {
    DataOffset = 0;
  }

  DEBUG ((DEBUG_ERROR, "BlockIoCache: DataOffset %d BufferOffset %d\n", DataOffset, *BufferOffset));

  CopyMem (
    (VOID *)((UINTN)Buffer + *BufferOffset),
    (VOID *)((UINTN)BlockInfo->Data + DataOffset),
    BlocksToCopy * BlockIoCache->BlockSize
    );

  DEBUG ((DEBUG_ERROR, "BlockIoCache: copied %d blocks from cache\n", BlocksToCopy));

  RemoveEntryList (Link);
  InsertHeadList (CacheList, &CacheEntry->Link);

  *BufferOffset += BlocksToCopy * BlockIoCache->BlockSize;

Done:
  *Lba += BlocksToCopy;

  DEBUG ((DEBUG_ERROR, "BlockIoCache: new values: BlocksToCopy %d Lba %lld\n", BlocksToCopy, *Lba));

  return Status;
}

EFI_STATUS
EFIAPI
BlockIoCacheRead (
  IN OUT  BLOCK_IO_CACHE  *BlockIoCache,
  IN      EFI_LBA         CacheLba,
  IN      UINTN           CacheNumberOfBlocks,
  IN      EFI_LBA         Lba,
  IN      UINTN           NumberOfBlocks,
  IN      VOID            *Buffer
  )
{
  LIST_ENTRY              *CacheList;
  LIST_ENTRY              *Link;
  BLOCK_IO_CACHE_ENTRY    *CacheEntry;
  BLOCK_IO_CACHE_DATA     *BlockInfo;
  UINTN                   DataOffset;
  UINTN                   BufferOffset;
  UINTN                   CacheBlocksNo;
  UINTN                   BlocksToCopy;

  if (BlockIoCache == NULL || Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!BlockIoCache->CacheInitialized) {
    return EFI_INVALID_PARAMETER;
  }

  if (BlockIoCache->CacheCount == 0) {
    DEBUG ((DEBUG_ERROR, "BlockIoCache: LBA %lld - cache miss\n", Lba));
    return EFI_NOT_FOUND;
  }

  DEBUG ((DEBUG_ERROR,
	  "BlockIoCache: CacheLba %lld CacheNumberOfBlocks %d Lba %lld NumberOfBlocks %d\n",
	  CacheLba, CacheNumberOfBlocks, Lba, NumberOfBlocks));

  CacheList = &BlockIoCache->CacheList;
  CacheBlocksNo = NR_CACHE_BLOCKS (BlockIoCache);
  BufferOffset = 0;
  while (NumberOfBlocks > 0) {
    CacheEntry = NULL;
    BlockInfo = NULL;
    for (Link = GetFirstNode (CacheList); !IsNull (CacheList, Link);
	 Link = GetNextNode (CacheList, Link)) {
      CacheEntry = CR (Link, BLOCK_IO_CACHE_ENTRY, Link, BLOCK_IO_CACHE_ENTRY_SIGNATURE);
      BlockInfo = CacheEntry->BlockInfo;
      if (BlockInfo->Lba == CacheLba) {
	break;
      }
    }

    if (IsNull (CacheList, Link)) {
      DEBUG ((DEBUG_ERROR, "BlockIoCache: LBA %lld - cache miss\n", CacheLba));
      return EFI_NOT_FOUND;
    }

    DEBUG ((DEBUG_ERROR, "BlockIoCache: LBA %lld - cache hit\n", CacheLba));

    if ((Lba - CacheLba) + NumberOfBlocks > CacheBlocksNo) {
      BlocksToCopy = CacheBlocksNo - (Lba - CacheLba);
    } else {
      BlocksToCopy = NumberOfBlocks;
    }

    if ((Lba & (CacheBlocksNo - 1)) != 0) {
      DataOffset = (Lba - CacheLba) * BlockIoCache->BlockSize;
    } else {
      DataOffset = 0;
    }

    CopyMem (
      (VOID *)((UINTN)Buffer + BufferOffset),
      (VOID *)((UINTN)BlockInfo->Data + DataOffset),
      BlocksToCopy * BlockIoCache->BlockSize
      );
    RemoveEntryList (Link);
    InsertHeadList (CacheList, &CacheEntry->Link);

    BufferOffset += BlocksToCopy * BlockIoCache->BlockSize;
    NumberOfBlocks -= BlocksToCopy;
    Lba += BlocksToCopy;
    CacheLba += CacheBlocksNo;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BlockIoCacheAdd2 (
  IN OUT  BLOCK_IO_CACHE  *BlockIoCache,
  IN      EFI_LBA         Lba,
  IN      VOID            *Buffer
  )
{
  LIST_ENTRY              *CacheList;
  UINTN                   CacheBlocksNo;
  EFI_STATUS              Status;
  BLOCK_IO_CACHE_DATA     *BlockInfo;
  BLOCK_IO_CACHE_ENTRY    *CacheEntry;
  BLOCK_IO_CACHE_ENTRY    *NewCacheEntry;

  if (BlockIoCache == NULL || Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (!BlockIoCache->CacheInitialized) {
    return EFI_INVALID_PARAMETER;
  }

  DEBUG ((DEBUG_ERROR, "BlockIoCache: Lba %lld\n", Lba));

  CacheList = &BlockIoCache->CacheList;
  CacheBlocksNo = NR_CACHE_BLOCKS (BlockIoCache);
  if (BlockIoCache->CacheCount < BlockIoCache->CacheSize) {
    BlockInfo = &BlockIoCache->CacheData[BlockIoCache->CacheCount];
    BlockInfo->Data = Buffer;
    BlockInfo->Lba = Lba;

    BlockIoCache->CacheCount++;

    Status = GetNewCacheEntry (BlockInfo, &NewCacheEntry);
    if (EFI_ERROR (Status)) {
      goto ERROR;
    }
  } else {
    CacheEntry = CR (CacheList->BackLink, BLOCK_IO_CACHE_ENTRY, Link, BLOCK_IO_CACHE_ENTRY_SIGNATURE);
    RemoveEntryList (CacheList->BackLink);

    BlockInfo = CacheEntry->BlockInfo;

    BlockInfo->Lba = Lba;
    FreePool (BlockInfo->Data);
    BlockInfo->Data = Buffer;

    NewCacheEntry = CacheEntry;
  }
#ifdef BIO_CACHE_DEBUG
  DEBUG ((DEBUG_ERROR, "BlockIoCache: insert new cache entry (LBA %lld)\n", BlockInfo->Lba));
#endif
  InsertHeadList (CacheList, &NewCacheEntry->Link);

  return EFI_SUCCESS;

ERROR:
  if (NewCacheEntry != NULL) {
    FreePool (NewCacheEntry);
  }
  return Status;
}

EFI_STATUS
EFIAPI
BlockIoCacheAdd (
  IN OUT  BLOCK_IO_CACHE  *BlockIoCache,
  IN      EFI_LBA         Lba,
  IN      UINTN           NumberOfBlocks,
  IN      VOID            *Buffer
  )
{
  LIST_ENTRY              *CacheList;
  UINTN                   CacheBlocksNo;
  LIST_ENTRY              *Link;
  EFI_STATUS              Status;
  BLOCK_IO_CACHE_DATA     *BlockInfo;
  BLOCK_IO_CACHE_ENTRY    *CacheEntry;
  BLOCK_IO_CACHE_ENTRY    *NewCacheEntry;

  if (BlockIoCache == NULL || Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (!BlockIoCache->CacheInitialized) {
    return EFI_INVALID_PARAMETER;
  }

  DEBUG ((DEBUG_ERROR, "BlockIoCache: Lba %lld NumberOfBlocks %d\n", Lba, NumberOfBlocks));

  CacheList = &BlockIoCache->CacheList;
  CacheBlocksNo = NR_CACHE_BLOCKS (BlockIoCache);
  for (;;) {
    for (Link = GetFirstNode (CacheList); !IsNull (CacheList, Link);
	 Link = GetNextNode (CacheList, Link)) {
      CacheEntry = CR (Link, BLOCK_IO_CACHE_ENTRY, Link, BLOCK_IO_CACHE_ENTRY_SIGNATURE);
      if (CacheEntry->BlockInfo->Lba == Lba) {
	DEBUG ((DEBUG_ERROR, "BlockIoCache: LBA %lld is already cached\n", CacheEntry->BlockInfo->Lba));
	RemoveEntryList (Link);
	InsertHeadList (CacheList, &CacheEntry->Link);
	goto NEXT_LBA;
      }
    }

    NewCacheEntry = NULL;
    if (BlockIoCache->CacheCount < BlockIoCache->CacheSize) {
      BlockInfo = &BlockIoCache->CacheData[BlockIoCache->CacheCount];
      BlockInfo->Data = Buffer;
      BlockInfo->Lba = Lba;

      BlockIoCache->CacheCount++;

      Status = GetNewCacheEntry (BlockInfo, &NewCacheEntry);
      if (EFI_ERROR (Status)) {
	goto ERROR;
      }
    } else {
      CacheEntry = CR (CacheList->BackLink, BLOCK_IO_CACHE_ENTRY, Link, BLOCK_IO_CACHE_ENTRY_SIGNATURE);
      RemoveEntryList (CacheList->BackLink);

      BlockInfo = CacheEntry->BlockInfo;
      BlockInfo->Lba = Lba;
      CopyMem (BlockInfo->Data, Buffer, EFI_PAGE_SIZE);

      NewCacheEntry = CacheEntry;
    }
#ifdef BIO_CACHE_DEBUG
    DEBUG ((DEBUG_ERROR, "BlockIoCache: insert new cache entry (LBA %lld)\n", BlockInfo->Lba));
#endif
    InsertHeadList (CacheList, &NewCacheEntry->Link);

  NEXT_LBA:
    if (--NumberOfBlocks == 0) {
      break;
    }
    Buffer = (VOID *)((UINTN)Buffer + EFI_PAGE_SIZE);
    Lba += CacheBlocksNo;
  }

  return EFI_SUCCESS;

ERROR:
  if (NewCacheEntry != NULL) {
    FreePool (NewCacheEntry);
  }
  return Status;
}

EFI_STATUS
EFIAPI
BlockIoCacheInvalidate (
  IN OUT  BLOCK_IO_CACHE  *BlockIoCache,
  IN      EFI_LBA         CacheLba,
  IN      UINTN           CacheNumberOfBlocks,
  IN      EFI_LBA         Lba,
  IN      UINTN           NumberOfBlocks,
  IN      VOID            *Buffer
  )
{
  LIST_ENTRY              *CacheList;
  LIST_ENTRY              *Link;
  BLOCK_IO_CACHE_ENTRY    *CacheEntry;
  BLOCK_IO_CACHE_DATA     *BlockInfo;
  UINTN                   DataOffset;
  UINTN                   BufferOffset;
  UINTN                   CacheBlocksNo;
  UINTN                   BlocksToCopy;

  if (BlockIoCache == NULL || Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!BlockIoCache->CacheInitialized || BlockIoCache->CacheCount == 0) {
    return EFI_SUCCESS;
  }

  DEBUG ((DEBUG_ERROR,
	  "BlockIoCache: CacheLba %lld CacheNumberOfBlocks %d Lba %lld NumberOfBlocks %d\n",
	  CacheLba, CacheNumberOfBlocks, Lba, NumberOfBlocks));

  CacheList = &BlockIoCache->CacheList;
  CacheBlocksNo = NR_CACHE_BLOCKS (BlockIoCache);
  BufferOffset = 0;
  while (NumberOfBlocks > 0) {
    CacheEntry = NULL;
    BlockInfo = NULL;
    for (Link = GetFirstNode (CacheList); !IsNull (CacheList, Link);
	 Link = GetNextNode (CacheList, Link)) {
      CacheEntry = CR (Link, BLOCK_IO_CACHE_ENTRY, Link, BLOCK_IO_CACHE_ENTRY_SIGNATURE);
      BlockInfo = CacheEntry->BlockInfo;
      if (BlockInfo->Lba == CacheLba) {
	break;
      }
    }

    if ((Lba - CacheLba) + NumberOfBlocks > CacheBlocksNo) {
      BlocksToCopy = CacheBlocksNo - (Lba - CacheLba);
    } else {
      BlocksToCopy = NumberOfBlocks;
    }

    if (IsNull (CacheList, Link)) {
      goto NEXT_LBA;
    }

    DEBUG ((DEBUG_ERROR, "BlockIoCache: invalidate LBA %lld\n", Lba));

    if ((Lba & (CacheBlocksNo - 1)) != 0) {
      DataOffset = (Lba - CacheLba) * BlockIoCache->BlockSize;
    } else {
      DataOffset = 0;
    }

    CopyMem (
      (VOID *)((UINTN)BlockInfo->Data + DataOffset),
      (VOID *)((UINTN)Buffer + BufferOffset),
      BlocksToCopy * BlockIoCache->BlockSize
      );
    RemoveEntryList (Link);
    InsertHeadList (CacheList, &CacheEntry->Link);

  NEXT_LBA:
    BufferOffset += BlocksToCopy * BlockIoCache->BlockSize;
    NumberOfBlocks -= BlocksToCopy;
    Lba += BlocksToCopy;
    CacheLba += CacheBlocksNo;
  }
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BlockIoCacheCleanup (
  IN OUT BLOCK_IO_CACHE *BlockIoCache
  )
{
  //
  // TODO: implement me
  //
  return EFI_SUCCESS;
}
