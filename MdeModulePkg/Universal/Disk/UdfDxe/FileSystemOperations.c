/** @file
  UDF/ECMA-167 filesystem driver.

Copyright (c) 2014-2015 Paulo Alcantara <pcacjr@zytor.com><BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "Udf.h"

//
// This driver *only* supports UDF revision 2.00 or higher.
//
// Note the "NSR03" identifier.
//
UDF_STANDARD_IDENTIFIER gUdfStandardIdentifiers[NR_STANDARD_IDENTIFIERS] = {
  { { 'B', 'E', 'A', '0', '1' } },
  { { 'N', 'S', 'R', '0', '3' } },
  { { 'T', 'E', 'A', '0', '1' } },
};

EFI_STATUS
FindAnchorVolumeDescriptorPointer (
  IN   EFI_BLOCK_IO_PROTOCOL                 *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL                  *DiskIo,
  OUT  UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER  *AnchorPoint
  )
{
  EFI_STATUS  Status;
  UINT32      BlockSize;
  EFI_LBA     EndLBA;

  BlockSize  = BlockIo->Media->BlockSize;
  EndLBA     = BlockIo->Media->LastBlock;

  //
  // Look for an AVDP at LBA 256.
  //
  Status = DiskIo->ReadDisk (
                          DiskIo,
                          BlockIo->Media->MediaId,
                          MultU64x32 (0x100ULL, BlockSize),
                          sizeof (UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER),
                          (VOID *)AnchorPoint
                          );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (IS_AVDP (AnchorPoint)) {
    return EFI_SUCCESS;
  }

  //
  // Look for an AVDP at last LBA - 256.
  //
  Status = DiskIo->ReadDisk (
                          DiskIo,
                          BlockIo->Media->MediaId,
                          MultU64x32 (EndLBA - 0x100ULL, BlockSize),
                          sizeof (UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER),
                          (VOID *)AnchorPoint
                          );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (IS_AVDP (AnchorPoint)) {
    return EFI_SUCCESS;
  }

  //
  // Look for an AVDP at last LBA.
  //
  Status = DiskIo->ReadDisk (
                          DiskIo,
                          BlockIo->Media->MediaId,
                          MultU64x32 (EndLBA, BlockSize),
                          sizeof (UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER),
                          (VOID *)AnchorPoint
                          );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (IS_AVDP (AnchorPoint)) {
    return EFI_SUCCESS;
  }

  //
  // There is no AVDP on this disk, so it's not an UDF volume and we cannot
  // start Main Volume Descriptor Sequence.
  //
  return EFI_VOLUME_CORRUPTED;
}

EFI_STATUS
StartMainVolumeDescriptorSequence (
  IN   EFI_BLOCK_IO_PROTOCOL                 *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL                  *DiskIo,
  IN   UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER  *AnchorPoint,
  OUT  UDF_VOLUME_INFO                       *Volume
  )
{
  EFI_STATUS                     Status;
  UINT32                         BlockSize;
  UDF_EXTENT_AD                  *ExtentAd;
  UINT64                         StartingLsn;
  UINT64                         EndingLsn;
  VOID                           *Buffer;
  UDF_LOGICAL_VOLUME_DESCRIPTOR  *LogicalVolDesc;
  UDF_PARTITION_DESCRIPTOR       *PartitionDesc;
  UINTN                          Index;
  UINT32                         LogicalBlockSize;

  //
  // We've alreay found an ADVP on the volume. It contains the extent
  // (MainVolumeDescriptorSequenceExtent) where the Main Volume Descriptor
  // Sequence starts. Therefore, we'll look for Logical Volume Descriptors and
  // Partitions Descriptors and save them in memory accordingly.
  //
  // Note also that each descriptor will be aligned on a block size (BlockSize)
  // boundary, so we need to read one block at a time.
  //
  BlockSize    = BlockIo->Media->BlockSize;
  ExtentAd     = &AnchorPoint->MainVolumeDescriptorSequenceExtent;
  StartingLsn  = (UINT64)ExtentAd->ExtentLocation;
  EndingLsn    = StartingLsn + DivU64x32 (
                                     (UINT64)ExtentAd->ExtentLength,
                                     BlockSize
                                     );

  Volume->LogicalVolDescs =
    (UDF_LOGICAL_VOLUME_DESCRIPTOR **)AllocateZeroPool (ExtentAd->ExtentLength);
  if (!Volume->LogicalVolDescs) {
    return EFI_OUT_OF_RESOURCES;
  }

  Volume->PartitionDescs =
    (UDF_PARTITION_DESCRIPTOR **)AllocateZeroPool (ExtentAd->ExtentLength);
  if (!Volume->PartitionDescs) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Error_Alloc_Pds;
  }

  Buffer = AllocateZeroPool (BlockSize);
  if (!Buffer) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Error_Alloc_Buf;
  }

  Volume->LogicalVolDescsNo  = 0;
  Volume->PartitionDescsNo   = 0;

  while (StartingLsn <= EndingLsn) {
    Status = DiskIo->ReadDisk (
                            DiskIo,
                            BlockIo->Media->MediaId,
                            MultU64x32 (StartingLsn, BlockSize),
                            BlockSize,
                            Buffer
                         );
    if (EFI_ERROR (Status)) {
      goto Error_Read_Disk_Blk;
    }

    if (IS_TD (Buffer)) {
      //
      // Found a Terminating Descriptor. Stop the sequence then.
      //
      break;
    }

    if (IS_LVD (Buffer)) {
      //
      // Found a Logical Volume Descriptor.
      //
      LogicalVolDesc =
        (UDF_LOGICAL_VOLUME_DESCRIPTOR *)
        AllocateZeroPool (sizeof (UDF_LOGICAL_VOLUME_DESCRIPTOR));
      if (!LogicalVolDesc) {
        Status = EFI_OUT_OF_RESOURCES;
        goto Error_Alloc_Lvd;
      }

      CopyMem (
        (VOID *)LogicalVolDesc,
        Buffer,
        sizeof (UDF_LOGICAL_VOLUME_DESCRIPTOR)
        );

      Volume->LogicalVolDescs[Volume->LogicalVolDescsNo] = LogicalVolDesc;
      Volume->LogicalVolDescsNo++;
    } else if (IS_PD (Buffer)) {
      //
      // Found a Partition Descriptor.
      //
      PartitionDesc =
        (UDF_PARTITION_DESCRIPTOR *)
        AllocateZeroPool (sizeof (UDF_PARTITION_DESCRIPTOR));
      if (!PartitionDesc) {
        Status = EFI_OUT_OF_RESOURCES;
        goto Error_Alloc_Pd;
      }

      CopyMem (
        (VOID *)PartitionDesc,
        Buffer,
        sizeof (UDF_PARTITION_DESCRIPTOR)
        );

      Volume->PartitionDescs[Volume->PartitionDescsNo] = PartitionDesc;
      Volume->PartitionDescsNo++;
    } else if (IS_USD (Buffer)) {
      Volume->UnallocatedSpaceDescLsn = StartingLsn;
    }

    StartingLsn++;
  }

  //
  // When an UDF volume (revision 2.00 or higher) contains a File Entry rather
  // than an Extended File Entry (which is not recommended as per spec), we need
  // to make sure the size of a FE will be _at least_ 2048
  // (UDF_LOGICAL_SECTOR_SIZE) bytes long to keep backward compatibility.
  //
  LogicalBlockSize = LV_BLOCK_SIZE (Volume, UDF_DEFAULT_LV_NUM);
  if (LogicalBlockSize >= UDF_LOGICAL_SECTOR_SIZE) {
    Volume->FileEntrySize = LogicalBlockSize;
  } else {
    Volume->FileEntrySize = UDF_LOGICAL_SECTOR_SIZE;
  }

  FreePool (Buffer);

  return EFI_SUCCESS;

Error_Alloc_Pd:
Error_Alloc_Lvd:
  for (Index = 0; Index < Volume->PartitionDescsNo; Index++) {
    FreePool ((VOID *)Volume->PartitionDescs[Index]);
  }

  for (Index = 0; Index < Volume->LogicalVolDescsNo; Index++) {
    FreePool ((VOID *)Volume->LogicalVolDescs[Index]);
  }

Error_Read_Disk_Blk:
  FreePool (Buffer);

Error_Alloc_Buf:
  FreePool ((VOID *)Volume->PartitionDescs);
  Volume->PartitionDescs = NULL;

Error_Alloc_Pds:
  FreePool ((VOID *)Volume->LogicalVolDescs);
  Volume->LogicalVolDescs = NULL;

  return Status;
}

//
// Return a Partition Descriptor given a Long Allocation Descriptor. This is
// necessary to calculate the right extent (LongAd) offset which is added up
// with partition's starting location.
//
UDF_PARTITION_DESCRIPTOR *
GetPdFromLongAd (
  IN UDF_VOLUME_INFO                 *Volume,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR  *LongAd
  )
{
  UINTN                     Index;
  UDF_PARTITION_DESCRIPTOR  *PartitionDesc;
  UINT16                    PartitionNum;

  for (Index = 0; Index < Volume->PartitionDescsNo; Index++) {
    PartitionDesc = Volume->PartitionDescs[Index];

    PartitionNum = PartitionDesc->PartitionNumber;
    if (PartitionNum == LongAd->ExtentLocation.PartitionReferenceNumber) {
      return PartitionDesc;
    }
  }

  return NULL;
}

//
// Return logical sector number of the given Long Allocation Descriptor.
//
UINT64
GetLongAdLsn (
  IN UDF_VOLUME_INFO                 *Volume,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR  *LongAd
  )
{
  UDF_PARTITION_DESCRIPTOR *PartitionDesc;

  PartitionDesc = GetPdFromLongAd (Volume, LongAd);

  return (UINT64)PartitionDesc->PartitionStartingLocation +
                 LongAd->ExtentLocation.LogicalBlockNumber;
}

//
// Return logical sector number of the given Short Allocation Descriptor.
//
UINT64
GetShortAdLsn (
  IN UDF_PARTITION_DESCRIPTOR         *PartitionDesc,
  IN UDF_SHORT_ALLOCATION_DESCRIPTOR  *ShortAd
  )
{
  return (UINT64)PartitionDesc->PartitionStartingLocation +
                 ShortAd->ExtentPosition;
}

//
// Find File Set Descriptor of the given Logical Volume Descriptor.
//
// The found FSD will contain the extent (LogicalVolumeContentsUse) where our
// root directory is.
//
EFI_STATUS
FindFileSetDescriptor (
  IN   EFI_BLOCK_IO_PROTOCOL    *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL     *DiskIo,
  IN   UDF_VOLUME_INFO          *Volume,
  IN   UINTN                    LogicalVolDescNo,
  OUT  UDF_FILE_SET_DESCRIPTOR  *FileSetDesc
  )
{
  EFI_STATUS                     Status;
  UINT64                         Lsn;
  UDF_LOGICAL_VOLUME_DESCRIPTOR  *LogicalVolDesc;

  LogicalVolDesc  = Volume->LogicalVolDescs[LogicalVolDescNo];
  Lsn             = GetLongAdLsn (
                              Volume,
                              &LogicalVolDesc->LogicalVolumeContentsUse
                              );

  //
  // Read extent (Long Ad).
  //
  Status = DiskIo->ReadDisk (
                          DiskIo,
                          BlockIo->Media->MediaId,
                          MultU64x32 (Lsn, LogicalVolDesc->LogicalBlockSize),
                          sizeof (UDF_FILE_SET_DESCRIPTOR),
                          (VOID *)FileSetDesc
                       );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Check if the read extent contains a valid FSD's tag identifier.
  //
  if (!IS_FSD (FileSetDesc)) {
    return EFI_VOLUME_CORRUPTED;
  }

  return EFI_SUCCESS;
}

//
// Get all File Set Descriptors for each Logical Volume Descriptor.
//
EFI_STATUS
GetFileSetDescriptors (
  IN      EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN      EFI_DISK_IO_PROTOCOL   *DiskIo,
  IN OUT  UDF_VOLUME_INFO        *Volume
  )
{
  EFI_STATUS               Status;
  UINTN                    Index;
  UDF_FILE_SET_DESCRIPTOR  *FileSetDesc;
  UINTN                    Count;

  Volume->FileSetDescs =
    (UDF_FILE_SET_DESCRIPTOR **)AllocateZeroPool (
                                            Volume->LogicalVolDescsNo *
                                            sizeof (UDF_FILE_SET_DESCRIPTOR)
                                            );
  if (!Volume->FileSetDescs) {
    return EFI_OUT_OF_RESOURCES;
  }

  for (Index = 0; Index < Volume->LogicalVolDescsNo; Index++) {
    FileSetDesc = AllocateZeroPool (sizeof (UDF_FILE_SET_DESCRIPTOR));
    if (!FileSetDesc) {
      Status = EFI_OUT_OF_RESOURCES;
      goto Error_Alloc_Fsd;
    }

    //
    // Find a FSD for this LVD.
    //
    Status = FindFileSetDescriptor (
                                BlockIo,
                                DiskIo,
                                Volume,
                                Index,
                                FileSetDesc
                                );
    if (EFI_ERROR (Status)) {
      goto Error_Find_Fsd;
    }

    //
    // Got one. Save it.
    //
    Volume->FileSetDescs[Index] = FileSetDesc;
  }

  Volume->FileSetDescsNo = Volume->LogicalVolDescsNo;

  return EFI_SUCCESS;

Error_Find_Fsd:
  Count = Index + 1;
  for (Index = 0; Index < Count; Index++) {
    FreePool ((VOID *)Volume->FileSetDescs[Index]);
  }

  FreePool ((VOID *)Volume->FileSetDescs);
  Volume->FileSetDescs = NULL;

Error_Alloc_Fsd:
  return Status;
}

//
// Read Volume and File Structure of an UDF file system.
//
EFI_STATUS
ReadVolumeFileStructure (
  IN   EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL   *DiskIo,
  OUT  UDF_VOLUME_INFO        *Volume
  )
{
  EFI_STATUS                            Status;
  UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER  AnchorPoint;

  //
  // Find an AVDP.
  //
  Status = FindAnchorVolumeDescriptorPointer (
                                          BlockIo,
                                          DiskIo,
                                          &AnchorPoint
                                          );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // AVDP has been found. Start MVDS.
  //
  Status = StartMainVolumeDescriptorSequence (
                                          BlockIo,
                                          DiskIo,
                                          &AnchorPoint,
                                          Volume
                                          );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return Status;
}

//
// Calculate length of the given File Identifier Descriptor.
//
UINT64
GetFidDescriptorLength (
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR  *FileIdentifierDesc
  )
{
  return (UINT64)(
             (INTN)((OFFSET_OF (UDF_FILE_IDENTIFIER_DESCRIPTOR, Data[0]) + 3 +
                     FileIdentifierDesc->LengthOfFileIdentifier +
                     FileIdentifierDesc->LengthOfImplementationUse) >> 2) << 2
             );
}

//
// Duplicate a given File Identifier Descriptor.
//
VOID
DuplicateFid (
  IN   UDF_FILE_IDENTIFIER_DESCRIPTOR  *FileIdentifierDesc,
  OUT  UDF_FILE_IDENTIFIER_DESCRIPTOR  **NewFileIdentifierDesc
  )
{
  *NewFileIdentifierDesc =
    (UDF_FILE_IDENTIFIER_DESCRIPTOR *)AllocateCopyPool (
                                    GetFidDescriptorLength (FileIdentifierDesc),
                                    FileIdentifierDesc
                                    );
}

//
// Duplicate either a given File Entry or a given Extended File Entry.
//
VOID
DuplicateFe (
  IN   EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN   UDF_VOLUME_INFO        *Volume,
  IN   VOID                   *FileEntry,
  OUT  VOID                   **NewFileEntry
  )
{
  *NewFileEntry = AllocateCopyPool (Volume->FileEntrySize, FileEntry);
}

//
// Get raw data + length of the given File Entry or Extended File Entry.
//
// The file's recorded data can contain either real file content (inline) or
// a sequence of extents (or Allocation Descriptors) which tells where file's
// content is stored in.
//
VOID
GetFileEntryData (
  IN   VOID    *FileEntryData,
  OUT  VOID    **Data,
  OUT  UINT64  *Length
  )
{
  UDF_EXTENDED_FILE_ENTRY  *ExtendedFileEntry;
  UDF_FILE_ENTRY           *FileEntry;

  if (IS_EFE (FileEntryData)) {
    ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)FileEntryData;

    *Length  = ExtendedFileEntry->InformationLength;
    *Data    = (VOID *)((UINT8 *)&ExtendedFileEntry->Data[0] +
                        ExtendedFileEntry->LengthOfExtendedAttributes);
  } else if (IS_FE (FileEntryData)) {
    FileEntry = (UDF_FILE_ENTRY *)FileEntryData;

    *Length  = FileEntry->InformationLength;
    *Data    = (VOID *)((UINT8 *)&FileEntry->Data[0] +
                        FileEntry->LengthOfExtendedAttributes);
  }
}

VOID
GetFileEntryData2 (
  IN   VOID    *FileEntryData,
  OUT  VOID    **Data,
  OUT  UINT64  **Length
  )
{
  UDF_EXTENDED_FILE_ENTRY  *ExtendedFileEntry;
  UDF_FILE_ENTRY           *FileEntry;

  if (IS_EFE (FileEntryData)) {
    ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)FileEntryData;

    *Length  = &ExtendedFileEntry->InformationLength;
    *Data    = (VOID *)((UINT8 *)&ExtendedFileEntry->Data[0] +
                        ExtendedFileEntry->LengthOfExtendedAttributes);
  } else if (IS_FE (FileEntryData)) {
    FileEntry = (UDF_FILE_ENTRY *)FileEntryData;

    *Length  = &FileEntry->InformationLength;
    *Data    = (VOID *)((UINT8 *)&FileEntry->Data[0] +
                        FileEntry->LengthOfExtendedAttributes);
  }
}

EFI_STATUS
WriteFileEntry (
  IN  EFI_BLOCK_IO_PROTOCOL           *BlockIo,
  IN  EFI_DISK_IO_PROTOCOL            *DiskIo,
  IN  UDF_VOLUME_INFO                 *Volume,
  IN  UDF_FILE_IDENTIFIER_DESCRIPTOR  *FileIdentifierDesc,
  IN  VOID                            *FileEntry
  )
{
  UINT32 LogicalBlockSize;
  UINT64 Lsn;

  LogicalBlockSize = LV_BLOCK_SIZE (Volume, UDF_DEFAULT_LV_NUM);
  Lsn = GetLongAdLsn (Volume, &FileIdentifierDesc->Icb);

  return DiskIo->WriteDisk (
                         DiskIo,
                         BlockIo->Media->MediaId,
                         MultU64x32 (Lsn, LogicalBlockSize),
                         Volume->FileEntrySize,
                         FileEntry
                         );
}

//
// Get Allocation Descriptors' data information from the given FE/EFE.
//
VOID
GetAdsInformation (
  IN   VOID    *FileEntryData,
  OUT  VOID    **AdsData,
  OUT  UINT64  *Length
  )
{
  UDF_EXTENDED_FILE_ENTRY  *ExtendedFileEntry;
  UDF_FILE_ENTRY           *FileEntry;

  if (IS_EFE (FileEntryData)) {
    ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)FileEntryData;

    *Length = ExtendedFileEntry->LengthOfAllocationDescriptors;
    *AdsData = (VOID *)((UINT8 *)&ExtendedFileEntry->Data[0] +
                        ExtendedFileEntry->LengthOfExtendedAttributes);
  } else if (IS_FE (FileEntryData)) {
    FileEntry = (UDF_FILE_ENTRY *)FileEntryData;

    *Length = FileEntry->LengthOfAllocationDescriptors;
    *AdsData = (VOID *)((UINT8 *)&FileEntry->Data[0] +
                        FileEntry->LengthOfExtendedAttributes);
  }
}

//
// Read next Short Allocation Descriptor from the given file's data.
//
EFI_STATUS
GetLongAdFromAds (
  IN      VOID                            *Data,
  IN OUT  UINT64                          *Offset,
  IN      UINT64                          Length,
  OUT     UDF_LONG_ALLOCATION_DESCRIPTOR  **FoundLongAd
  )
{
  UDF_LONG_ALLOCATION_DESCRIPTOR  *LongAd;
  UDF_EXTENT_FLAGS                ExtentFlags;

  for (;;) {
    if (*Offset >= Length) {
      //
      // No more Long Allocation Descriptors.
      //
      return EFI_DEVICE_ERROR;
    }

    LongAd =
      (UDF_LONG_ALLOCATION_DESCRIPTOR *)((UINT8 *)Data + *Offset);

    //
    // If it's either an indirect AD (Extended Alllocation Descriptor) or an
    // allocated AD, then return it.
    //
    ExtentFlags = GET_EXTENT_FLAGS (LONG_ADS_SEQUENCE, LongAd);
    if (ExtentFlags == EXTENT_IS_NEXT_EXTENT ||
        ExtentFlags == EXTENT_RECORDED_AND_ALLOCATED) {
      break;
    }

    //
    // This AD is either not recorded but allocated, or not recorded and not
    // allocated. Skip it.
    //
    *Offset += AD_LENGTH (LONG_ADS_SEQUENCE);
  }

  *FoundLongAd = LongAd;

  return EFI_SUCCESS;
}

//
// Read next Short Allocation Descriptor from the given file's data.
//
EFI_STATUS
GetShortAdFromAds (
  IN      VOID                             *Data,
  IN OUT  UINT64                           *Offset,
  IN      UINT64                           Length,
  OUT     UDF_SHORT_ALLOCATION_DESCRIPTOR  **FoundShortAd
  )
{
  UDF_SHORT_ALLOCATION_DESCRIPTOR *ShortAd;
  UDF_EXTENT_FLAGS                ExtentFlags;

  for (;;) {
    if (*Offset >= Length) {
      //
      // No more Short Allocation Descriptors.
      //
      return EFI_DEVICE_ERROR;
    }

    ShortAd =
      (UDF_SHORT_ALLOCATION_DESCRIPTOR *)((UINT8 *)Data + *Offset);

    //
    // If it's either an indirect AD (Extended Alllocation Descriptor) or an
    // allocated AD, then return it.
    //
    ExtentFlags = GET_EXTENT_FLAGS (SHORT_ADS_SEQUENCE, ShortAd);
    if (ExtentFlags == EXTENT_IS_NEXT_EXTENT ||
        ExtentFlags == EXTENT_RECORDED_AND_ALLOCATED) {
      break;
    }

    //
    // This AD is either not recorded but allocated, or not recorded and not
    // allocated. Skip it.
    //
    *Offset += AD_LENGTH (SHORT_ADS_SEQUENCE);
  }

  *FoundShortAd = ShortAd;

  return EFI_SUCCESS;
}

//
// Get either a Short Allocation Descriptor or a Long Allocation Descriptor from
// the given file's data.
//
EFI_STATUS
GetAllocationDescriptor (
  IN      UDF_FE_RECORDING_FLAGS  RecordingFlags,
  IN      VOID                    *Data,
  IN OUT  UINT64                  *Offset,
  IN      UINT64                  Length,
  OUT     VOID                    **FoundAd
  )
{
  if (RecordingFlags == LONG_ADS_SEQUENCE) {
    return GetLongAdFromAds (
                         Data,
                         Offset,
                         Length,
                         (UDF_LONG_ALLOCATION_DESCRIPTOR **)FoundAd
                         );
  } else if (RecordingFlags == SHORT_ADS_SEQUENCE) {
    return GetShortAdFromAds (
                         Data,
                         Offset,
                         Length,
                         (UDF_SHORT_ALLOCATION_DESCRIPTOR **)FoundAd
                         );
  }

  return EFI_DEVICE_ERROR;
}

//
// Return logical sector number of either Short or Long Allocation Descriptor.
//
UINT64
GetAllocationDescriptorLsn (
  IN UDF_FE_RECORDING_FLAGS          RecordingFlags,
  IN UDF_VOLUME_INFO                 *Volume,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR  *ParentIcb,
  IN VOID                            *Ad
  )
{
  if (RecordingFlags == LONG_ADS_SEQUENCE) {
    return GetLongAdLsn (Volume, (UDF_LONG_ALLOCATION_DESCRIPTOR *)Ad);
  } else if (RecordingFlags == SHORT_ADS_SEQUENCE) {
    return GetShortAdLsn (
                   GetPdFromLongAd (Volume, ParentIcb),
                   (UDF_SHORT_ALLOCATION_DESCRIPTOR *)Ad
                   );
  }

  return 0;
}

//
// Return offset + length of a given indirect Allocation Descriptor (AED).
//
EFI_STATUS
GetAedAdsOffset (
  IN   EFI_BLOCK_IO_PROTOCOL           *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL            *DiskIo,
  IN   UDF_VOLUME_INFO                 *Volume,
  IN   UDF_LONG_ALLOCATION_DESCRIPTOR  *ParentIcb,
  IN   UDF_FE_RECORDING_FLAGS          RecordingFlags,
  IN   VOID                            *Ad,
  OUT  UINT64                          *Offset,
  OUT  UINT64                          *Length
  )
{
  EFI_STATUS                        Status;
  UINT32                            ExtentLength;
  UINT64                            Lsn;
  VOID                              *Data;
  UINT32                            LogicalBlockSize;
  UDF_ALLOCATION_EXTENT_DESCRIPTOR  *AllocExtDesc;

  ExtentLength  = GET_EXTENT_LENGTH (RecordingFlags, Ad);
  Lsn           = GetAllocationDescriptorLsn (
                                    RecordingFlags,
                                    Volume,
                                    ParentIcb,
                                    Ad
                                    );

  Data = AllocatePool (ExtentLength);
  if (!Data) {
    return EFI_OUT_OF_RESOURCES;
  }

  LogicalBlockSize = LV_BLOCK_SIZE (Volume, UDF_DEFAULT_LV_NUM);

  //
  // Read extent.
  //
  Status = DiskIo->ReadDisk (
                          DiskIo,
                          BlockIo->Media->MediaId,
                          MultU64x32 (Lsn, LogicalBlockSize),
                          ExtentLength,
                          Data
                          );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  //
  // Check if read extent contains a valid tag identifier for AED.
  //
  AllocExtDesc = (UDF_ALLOCATION_EXTENT_DESCRIPTOR *)Data;
  if (!IS_AED (AllocExtDesc)) {
    Status = EFI_VOLUME_CORRUPTED;
    goto Exit;
  }

  //
  // Get AED's block offset and its length.
  //
  *Offset = MultU64x32 (Lsn, LogicalBlockSize) +
            sizeof (UDF_ALLOCATION_EXTENT_DESCRIPTOR);
  *Length = AllocExtDesc->LengthOfAllocationDescriptors;

Exit:
  if (Data) {
    FreePool (Data);
  }

  return Status;
}

//
// Read Allocation Extent Descriptor into memory.
//
EFI_STATUS
GetAedAdsData (
  IN   EFI_BLOCK_IO_PROTOCOL           *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL            *DiskIo,
  IN   UDF_VOLUME_INFO                 *Volume,
  IN   UDF_LONG_ALLOCATION_DESCRIPTOR  *ParentIcb,
  IN   UDF_FE_RECORDING_FLAGS          RecordingFlags,
  IN   VOID                            *Ad,
  OUT  VOID                            **Data,
  OUT  UINT64                          *Length
  )
{
  EFI_STATUS  Status;
  UINT64      Offset;

  //
  // Get AED's offset + length.
  //
  Status = GetAedAdsOffset (
                        BlockIo,
                        DiskIo,
                        Volume,
                        ParentIcb,
                        RecordingFlags,
                        Ad,
                        &Offset,
                        Length
                        );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Allocate buffer to read in AED's data.
  //
  *Data = AllocatePool (*Length);
  if (!Data) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Read it.
  //
  return DiskIo->ReadDisk (
                        DiskIo,
                        BlockIo->Media->MediaId,
                        Offset,
                        *Length,
                        *Data
                        );
}

//
// Function used to serialise reads of Allocation Descriptors.
//
EFI_STATUS
GrowUpBufferToNextAd (
  IN      UDF_FE_RECORDING_FLAGS  RecordingFlags,
  IN      VOID                    *Ad,
  IN OUT  VOID                    **Buffer,
  IN      UINT64                  Length
  )
{
  UINT32 ExtentLength;

  ExtentLength = GET_EXTENT_LENGTH (RecordingFlags, Ad);

  if (!*Buffer) {
    *Buffer = AllocatePool (ExtentLength);
    if (!*Buffer) {
      return EFI_OUT_OF_RESOURCES;
    }
  } else {
    *Buffer = ReallocatePool (Length, Length + ExtentLength, *Buffer);
    if (!*Buffer) {
      return EFI_OUT_OF_RESOURCES;
    }
  }

  return EFI_SUCCESS;
}

//
// Read data or size of either a File Entry or an Extended File Entry.
//
EFI_STATUS
ReadFile (
  IN      EFI_BLOCK_IO_PROTOCOL           *BlockIo,
  IN      EFI_DISK_IO_PROTOCOL            *DiskIo,
  IN      UDF_VOLUME_INFO                 *Volume,
  IN      UDF_LONG_ALLOCATION_DESCRIPTOR  *ParentIcb,
  IN      VOID                            *FileEntryData,
  IN OUT  UDF_READ_FILE_INFO              *ReadFileInfo
  )
{
  EFI_STATUS              Status;
  UINT32                  LogicalBlockSize;
  VOID                    *Data;
  UINT64                  Length;
  VOID                    *Ad;
  UINT64                  AdOffset;
  UINT64                  Lsn;
  BOOLEAN                 DoFreeAed;
  UINT64                  FilePosition;
  UINT64                  Offset;
  UINT64                  DataOffset;
  UINT64                  BytesLeft;
  UINT64                  DataLength;
  BOOLEAN                 FinishedSeeking;
  UINT32                  ExtentLength;
  UDF_FE_RECORDING_FLAGS  RecordingFlags;

  LogicalBlockSize  = LV_BLOCK_SIZE (Volume, UDF_DEFAULT_LV_NUM);
  DoFreeAed         = FALSE;

  switch (ReadFileInfo->Flags) {
    case READ_FILE_GET_FILESIZE:
    case READ_FILE_ALLOCATE_AND_READ:
      //
      // Initialise ReadFileInfo structure for starting get file's size or read
      // file's recorded data.
      //
      ReadFileInfo->ReadLength = 0;
      ReadFileInfo->FileData = NULL;
      break;
    case READ_FILE_SEEK_AND_READ:
      //
      // About to seek a file and/or read its data.
      //
      Length = ReadFileInfo->FileSize - ReadFileInfo->FilePosition;
      if (ReadFileInfo->FileDataSize > Length) {
        //
        // About to read beyond the EOF -- truncate it.
        //
        ReadFileInfo->FileDataSize = Length;
      }

      //
      // Initialise data to start seeking and/or reading a file.
      //
      BytesLeft = ReadFileInfo->FileDataSize;
      DataOffset = 0;
      FilePosition = 0;
      FinishedSeeking = FALSE;

      break;
  }

  RecordingFlags = GET_FE_RECORDING_FLAGS (FileEntryData);
  switch (RecordingFlags) {
    case INLINE_DATA:
      //
      // There is no extents for this FE/EFE. Data is inline.
      //
      GetFileEntryData (FileEntryData, &Data, &Length);

      if (ReadFileInfo->Flags == READ_FILE_GET_FILESIZE) {
        ReadFileInfo->ReadLength = Length;
      } else if (ReadFileInfo->Flags == READ_FILE_ALLOCATE_AND_READ) {
        //
        // Allocate buffer for starting read data.
        //
        ReadFileInfo->FileData = AllocatePool (Length);
        if (!ReadFileInfo->FileData) {
          return EFI_OUT_OF_RESOURCES;
        }

        //
        // Read all inline data into ReadFileInfo->FileData
        //
        CopyMem (ReadFileInfo->FileData, Data, Length);
        ReadFileInfo->ReadLength = Length;
      } else if (ReadFileInfo->Flags == READ_FILE_SEEK_AND_READ) {
        //
        // If FilePosition is non-zero, seek file to FilePosition, read
        // FileDataSize bytes and then updates FilePosition.
        //
        CopyMem (
          ReadFileInfo->FileData,
          (VOID *)((UINT8 *)Data + ReadFileInfo->FilePosition),
          ReadFileInfo->FileDataSize
          );

        ReadFileInfo->FilePosition += ReadFileInfo->FileDataSize;
      }

      break;
    case LONG_ADS_SEQUENCE:
    case SHORT_ADS_SEQUENCE:
      //
      // This FE/EFE contains a run of Allocation Descriptors. Get data + size
      // for start reading them.
      //
      GetAdsInformation (FileEntryData, &Data, &Length);
      AdOffset = 0;

      for (;;) {
        //
        // Read AD.
        //
        Status = GetAllocationDescriptor (
                                  RecordingFlags,
                                  Data,
                                  &AdOffset,
                                  Length,
                                  &Ad
                                  );
        if (Status == EFI_DEVICE_ERROR) {
          Status = EFI_SUCCESS;
          goto Done;
        }

        //
        // Check if AD is an indirect one. If so, read Allocation Extent
        // Descriptor and read their extents (ADs).
        //
        if (GET_EXTENT_FLAGS (RecordingFlags, Ad) == EXTENT_IS_NEXT_EXTENT) {
          if (!DoFreeAed) {
            DoFreeAed = TRUE;
          } else {
            FreePool (Data);
          }

          Status = GetAedAdsData (
                              BlockIo,
                              DiskIo,
                              Volume,
                              ParentIcb,
                              RecordingFlags,
                              Ad,
                              &Data,
                              &Length
                              );
          if (EFI_ERROR (Status)) {
            goto Error_Get_Aed;
          }

          AdOffset = 0;
          continue;
        }

        ExtentLength = GET_EXTENT_LENGTH (RecordingFlags, Ad);

        Lsn = GetAllocationDescriptorLsn (
                                RecordingFlags,
                                Volume,
                                ParentIcb,
                                Ad
                                );

        switch (ReadFileInfo->Flags) {
          case READ_FILE_GET_FILESIZE:
            ReadFileInfo->ReadLength += ExtentLength;
            break;
          case READ_FILE_ALLOCATE_AND_READ:
            //
            // Increase FileData (if necessary) to read next extent.
            //
            Status = GrowUpBufferToNextAd (
                                     RecordingFlags,
                                     Ad,
                                     &ReadFileInfo->FileData,
                                     ReadFileInfo->ReadLength
                                     );
            if (EFI_ERROR (Status)) {
              goto Error_Alloc_Buffer_To_Next_Ad;
            }

            //
            // Read extent's data into FileData.
            //
            Status = DiskIo->ReadDisk (
                                    DiskIo,
                                    BlockIo->Media->MediaId,
                                    MultU64x32 (Lsn, LogicalBlockSize),
                                    ExtentLength,
                                    (VOID *)((UINT8 *)ReadFileInfo->FileData +
                                             ReadFileInfo->ReadLength)
                                  );
            if (EFI_ERROR (Status)) {
              goto Error_Read_Disk_Blk;
            }

            ReadFileInfo->ReadLength += ExtentLength;
            break;
          case READ_FILE_SEEK_AND_READ:
            //
            // Seek file first before reading in its data.
            //
            if (FinishedSeeking) {
              Offset = 0;
              goto Skip_File_Seek;
            }

            if (FilePosition + ExtentLength < ReadFileInfo->FilePosition) {
              FilePosition += ExtentLength;
              goto Skip_Ad;
            }

            if (FilePosition + ExtentLength > ReadFileInfo->FilePosition) {
              Offset = ReadFileInfo->FilePosition - FilePosition;
              if (Offset < 0) {
                Offset = -(Offset);
              }
            } else {
              Offset = 0;
            }

            //
            // Done with seeking file. Start reading its data.
            //
            FinishedSeeking = TRUE;

Skip_File_Seek:
            //
            // Make sure we don't read more data than really wanted.
            //
            if (ExtentLength - Offset > BytesLeft) {
              DataLength = BytesLeft;
            } else {
              DataLength = ExtentLength - Offset;
            }

            //
            // Read extent's data into FileData.
            //
            Status = DiskIo->ReadDisk (
                                    DiskIo,
                                    BlockIo->Media->MediaId,
                                    Offset + MultU64x32 (Lsn, LogicalBlockSize),
                                    DataLength,
                                    (VOID *)((UINT8 *)ReadFileInfo->FileData +
                                             DataOffset)
                                    );
            if (EFI_ERROR (Status)) {
              goto Error_Read_Disk_Blk;
            }

            //
            // Update current file's position.
            //
            DataOffset += DataLength;
            ReadFileInfo->FilePosition += DataLength;

            BytesLeft -= DataLength;
            if (!BytesLeft) {
              //
              // There is no more file data to read.
              //
              Status = EFI_SUCCESS;
              goto Done;
            }

            break;
        }

Skip_Ad:
        //
        // Point to the next AD (extent).
        //
        AdOffset += AD_LENGTH (RecordingFlags);
      }

      break;
    case EXTENDED_ADS_SEQUENCE:
      // Not supported. Haven't got a volume with that yet.
      Status = EFI_UNSUPPORTED;
      break;
  }

Done:
  if (DoFreeAed) {
    FreePool (Data);
  }

  return Status;

Error_Read_Disk_Blk:
Error_Alloc_Buffer_To_Next_Ad:
  if (ReadFileInfo->Flags != READ_FILE_SEEK_AND_READ) {
    FreePool (ReadFileInfo->FileData);
  }

  if (DoFreeAed) {
    FreePool (Data);
  }

Error_Get_Aed:
  return Status;
}

//
// Find a file by its filename from the given Parent file.
//
EFI_STATUS
InternalFindFile (
  IN   EFI_BLOCK_IO_PROTOCOL           *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL            *DiskIo,
  IN   UDF_VOLUME_INFO                 *Volume,
  IN   CHAR16                          *FileName,
  IN   UDF_FILE_INFO                   *Parent,
  IN   UDF_LONG_ALLOCATION_DESCRIPTOR  *Icb,
  OUT  UDF_FILE_INFO                   *File
  )
{
  EFI_STATUS                      Status;
  UDF_FILE_IDENTIFIER_DESCRIPTOR  *FileIdentifierDesc;
  UDF_READ_DIRECTORY_INFO         ReadDirInfo;
  BOOLEAN                         Found;
  UINTN                           FileNameLength;
  CHAR16                          FoundFileName[UDF_FILENAME_LENGTH];
  VOID                            *CompareFileEntry;

  //
  // Check if parent file is really directory.
  //
  if (!IS_FE_DIRECTORY (Parent->FileEntry)) {
    return EFI_NOT_FOUND;
  }

  //
  // If FileName is current file or working directory, just duplicate Parent's
  // FE/EFE and FID descriptors.
  //
  if (!StrCmp (FileName, L".")) {
    DuplicateFe (BlockIo, Volume, Parent->FileEntry, &File->FileEntry);
    DuplicateFid (Parent->FileIdentifierDesc, &File->FileIdentifierDesc);

    return EFI_SUCCESS;
  }

  //
  // Start directory listing.
  //
  ZeroMem ((VOID *)&ReadDirInfo, sizeof (UDF_READ_DIRECTORY_INFO));
  Found = FALSE;
  FileNameLength = StrLen (FileName);

  for (;;) {
    Status = ReadDirectoryEntry (
                             BlockIo,
                             DiskIo,
                             Volume,
                             Parent->FileIdentifierDesc ?
                             &Parent->FileIdentifierDesc->Icb :
                             Icb,
                             Parent->FileEntry,
                             &ReadDirInfo,
                             &FileIdentifierDesc
                             );
    if (EFI_ERROR (Status)) {
      if (Status == EFI_DEVICE_ERROR) {
        Status = EFI_NOT_FOUND;
      }

      break;
    }

    if (IS_FID_PARENT_FILE (FileIdentifierDesc)) {
      //
      // This FID contains the location (FE/EFE) of the parent directory of this
      // directory (Parent), and if FileName is either ".." or "\\", then it's
      // the expected FID.
      //
      if (!StrCmp (FileName, L"..") || !StrCmp (FileName, L"\\")) {
        Found = TRUE;
        break;
      }
    } else {
      //
      // Check if both filename lengths match each other. Otherwise, we don't
      // need to read in FID's filename.
      //
      if (FileNameLength != FileIdentifierDesc->LengthOfFileIdentifier - 1) {
        goto Skip_Fid;
      }

      //
      // OK - their filename lengths match. Now, compare if their filenames.
      //
      Status = GetFileNameFromFid (FileIdentifierDesc, FoundFileName);
      if (EFI_ERROR (Status)) {
        break;
      }

      if (!StrCmp (FileName, FoundFileName)) {
        //
        // FID has been found. Prepare to find its respective FE/EFE.
        //
        Found = TRUE;
        break;
      }
    }

Skip_Fid:
    FreePool ((VOID *)FileIdentifierDesc);
  }

  if (ReadDirInfo.DirectoryData) {
    //
    // Free all allocated resources for the directory listing.
    //
    FreePool (ReadDirInfo.DirectoryData);
  }

  if (Found) {
    Status = EFI_SUCCESS;

    File->FileIdentifierDesc = FileIdentifierDesc;

    //
    // If the requested file is root directory, then the FE/EFE was already
    // retrieved in UdfOpenVolume() function, thus no need to find it again.
    //
    // Otherwise, find FE/EFE from the respective FID.
    //
    if (StrCmp (FileName, L"\\")) {
      Status = FindFileEntry (
                          BlockIo,
                          DiskIo,
                          Volume,
                          &FileIdentifierDesc->Icb,
                          &CompareFileEntry
                          );
      if (EFI_ERROR (Status)) {
        goto Error_Find_Fe;
      }

      //
      // Make sure that both Parent's FE/EFE and found FE/EFE are not equal.
      //
      if (CompareMem (
            (VOID *)Parent->FileEntry,
            (VOID *)CompareFileEntry,
            Volume->FileEntrySize
            )
         ) {
        File->FileEntry = CompareFileEntry;
      } else {
        FreePool ((VOID *)FileIdentifierDesc);
        FreePool ((VOID *)CompareFileEntry);
        Status = EFI_NOT_FOUND;
      }
    }
  }

  return Status;

Error_Find_Fe:
  FreePool ((VOID *)FileIdentifierDesc);

  return Status;
}

/**
  Check if medium contains an UDF file system.

  @param[in]   BlockIo  BlockIo interface.
  @param[in]   DiskIo   DiskIo interface.

  @retval EFI_SUCCESS          UDF file system found.
  @retval EFI_UNSUPPORTED      UDF file system not found.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The scan was not successful due to lack of
                               resources.

**/
EFI_STATUS
SupportUdfFileSystem (
  IN EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN EFI_DISK_IO_PROTOCOL   *DiskIo
  )
{
  EFI_STATUS                            Status;
  UINT64                                Offset;
  UINT64                                EndDiskOffset;
  UDF_VOLUME_DESCRIPTOR                 VolDescriptor;
  UDF_VOLUME_DESCRIPTOR                 TerminatingVolDescriptor;
  UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER  AnchorPoint;

  ZeroMem ((VOID *)&TerminatingVolDescriptor, sizeof (UDF_VOLUME_DESCRIPTOR));

  //
  // Start Volume Recognition Sequence
  //
  EndDiskOffset = BlockIo->Media->LastBlock * BlockIo->Media->BlockSize;

  for (Offset = UDF_VRS_START_OFFSET; Offset < EndDiskOffset;
       Offset += UDF_LOGICAL_SECTOR_SIZE) {
    Status = DiskIo->ReadDisk (
                           DiskIo,
                           BlockIo->Media->MediaId,
                           Offset,
                           sizeof (UDF_VOLUME_DESCRIPTOR),
                           (VOID *)&VolDescriptor
                           );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (!CompareMem (
          (VOID *)&VolDescriptor.StandardIdentifier,
          (VOID *)&gUdfStandardIdentifiers[BEA_IDENTIFIER],
          UDF_STANDARD_IDENTIFIER_LENGTH
          )
      ) {
      break;
    }

    if (CompareMem (
          (VOID *)&VolDescriptor.StandardIdentifier,
          (VOID *)UDF_CDROM_VOLUME_IDENTIFIER,
          UDF_STANDARD_IDENTIFIER_LENGTH
          ) ||
        !CompareMem (
          (VOID *)&VolDescriptor,
          (VOID *)&TerminatingVolDescriptor,
          sizeof (UDF_VOLUME_DESCRIPTOR)
          )
      ) {
      return EFI_UNSUPPORTED;
    }
  }

  //
  // Look for "NSR03" identifier in the Extended Area
  //
  Offset += UDF_LOGICAL_SECTOR_SIZE;
  if (Offset >= EndDiskOffset) {
    return EFI_UNSUPPORTED;
  }

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       Offset,
                       sizeof (UDF_VOLUME_DESCRIPTOR),
                       (VOID *)&VolDescriptor
                       );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (CompareMem (
        (VOID *)&VolDescriptor.StandardIdentifier,
        (VOID *)&gUdfStandardIdentifiers[VSD_IDENTIFIER],
        UDF_STANDARD_IDENTIFIER_LENGTH
        )
    ) {
    return EFI_UNSUPPORTED;
  }

  //
  // Look for "TEA01" identifier in the Extended Area
  //
  Offset += UDF_LOGICAL_SECTOR_SIZE;
  if (Offset >= EndDiskOffset) {
    return EFI_UNSUPPORTED;
  }

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       Offset,
                       sizeof (UDF_VOLUME_DESCRIPTOR),
                       (VOID *)&VolDescriptor
                       );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (CompareMem (
        (VOID *)&VolDescriptor.StandardIdentifier,
        (VOID *)&gUdfStandardIdentifiers[TEA_IDENTIFIER],
        UDF_STANDARD_IDENTIFIER_LENGTH
        )
    ) {
    return EFI_UNSUPPORTED;
  }

  Status = FindAnchorVolumeDescriptorPointer (BlockIo, DiskIo, &AnchorPoint);
  if (EFI_ERROR (Status)) {
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}

/**
  Read volume information on a medium which contains a valid UDF file system.

  @param[in]   BlockIo  BlockIo interface.
  @param[in]   DiskIo   DiskIo interface.
  @param[out]  Volume   UDF volume information structure.

  @retval EFI_SUCCESS          Volume information read.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The volume was not read due to lack of resources.

**/
EFI_STATUS
ReadUdfVolumeInformation (
  IN   EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL   *DiskIo,
  OUT  UDF_VOLUME_INFO        *Volume
  )
{
  EFI_STATUS Status;

  Status = ReadVolumeFileStructure (
                                BlockIo,
                                DiskIo,
                                Volume
                                );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = GetFileSetDescriptors (
                              BlockIo,
                              DiskIo,
                              Volume
                              );
  if (EFI_ERROR (Status)) {
    CleanupVolumeInformation (Volume);
  }

  return Status;
}

/**
  Find the root directory on an UDF volume.

  @param[in]   BlockIo  BlockIo interface.
  @param[in]   DiskIo   DiskIo interface.
  @param[in]   Volume   UDF volume information structure.
  @param[out]  File     Root directory file.

  @retval EFI_SUCCESS          Root directory found.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The root directory was not found due to lack of
                               resources.

**/
EFI_STATUS
FindRootDirectory (
  IN   EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL   *DiskIo,
  IN   UDF_VOLUME_INFO        *Volume,
  OUT  UDF_FILE_INFO          *File
  )
{
  EFI_STATUS     Status;
  UDF_FILE_INFO  Parent;

  Status = FindFileEntry (
                      BlockIo,
                      DiskIo,
                      Volume,
                      &Volume->FileSetDescs[0]->RootDirectoryIcb,
                      &File->FileEntry
                      );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Parent.FileEntry = File->FileEntry;
  Parent.FileIdentifierDesc = NULL;

  Status = FindFile (
                 BlockIo,
                 DiskIo,
                 Volume,
                 L"\\",
                 NULL,
                 &Parent,
                 &Volume->FileSetDescs[0]->RootDirectoryIcb,
                 File
                 );
  if (EFI_ERROR (Status)) {
    FreePool (File->FileEntry);
  }

  return Status;
}

/**
  Find either a File Entry or a Extended File Entry from a given ICB.

  @param[in]   BlockIo    BlockIo interface.
  @param[in]   DiskIo     DiskIo interface.
  @param[in]   Volume     UDF volume information structure.
  @param[in]   Icb        ICB of the FID.
  @param[out]  FileEntry  File Entry or Extended File Entry.

  @retval EFI_SUCCESS          File Entry or Extended File Entry found.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The FE/EFE entry was not found due to lack of
                               resources.

**/
EFI_STATUS
FindFileEntry (
  IN   EFI_BLOCK_IO_PROTOCOL           *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL            *DiskIo,
  IN   UDF_VOLUME_INFO                 *Volume,
  IN   UDF_LONG_ALLOCATION_DESCRIPTOR  *Icb,
  OUT  VOID                            **FileEntry
  )
{
  EFI_STATUS  Status;
  UINT64      Lsn;
  UINT32      LogicalBlockSize;

  Lsn               = GetLongAdLsn (Volume, Icb);
  LogicalBlockSize  = LV_BLOCK_SIZE (Volume, UDF_DEFAULT_LV_NUM);

  *FileEntry = AllocateZeroPool (Volume->FileEntrySize);
  if (!*FileEntry) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Read extent.
  //
  Status = DiskIo->ReadDisk (
                          DiskIo,
                          BlockIo->Media->MediaId,
                          MultU64x32 (Lsn, LogicalBlockSize),
                          Volume->FileEntrySize,
                          *FileEntry
                          );
  if (EFI_ERROR (Status)) {
    goto Error_Read_Disk_Blk;
  }

  //
  // Check if the read extent contains a valid Tag Identifier for the expected
  // FE/EFE.
  //
  if (!IS_FE (*FileEntry) && !IS_EFE (*FileEntry)) {
    Status = EFI_VOLUME_CORRUPTED;
    goto Error_Invalid_Fe;
  }

  return EFI_SUCCESS;

Error_Invalid_Fe:
Error_Read_Disk_Blk:
  FreePool (*FileEntry);

  return Status;
}

/**
  Find a file given its absolute path on an UDF volume.

  @param[in]   BlockIo   BlockIo interface.
  @param[in]   DiskIo    DiskIo interface.
  @param[in]   Volume    UDF volume information structure.
  @param[in]   FilePath  File's absolute path.
  @param[in]   Root      Root directory file.
  @param[in]   Parent    Parent directory file.
  @param[out]  File      Found file.

  @retval EFI_SUCCESS          @p FilePath was found.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The @p FilePath file was not found due to lack of
                               resources.

**/
EFI_STATUS
FindFile (
  IN   EFI_BLOCK_IO_PROTOCOL           *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL            *DiskIo,
  IN   UDF_VOLUME_INFO                 *Volume,
  IN   CHAR16                          *FilePath,
  IN   UDF_FILE_INFO                   *Root,
  IN   UDF_FILE_INFO                   *Parent,
  IN   UDF_LONG_ALLOCATION_DESCRIPTOR  *Icb,
  OUT  UDF_FILE_INFO                   *File
  )
{
  EFI_STATUS     Status;
  CHAR16         FileName[UDF_FILENAME_LENGTH];
  CHAR16         *FileNamePointer;
  UDF_FILE_INFO  PreviousFile;
  VOID           *FileEntry;

  Status = EFI_NOT_FOUND;

  CopyMem ((VOID *)&PreviousFile, (VOID *)Parent, sizeof (UDF_FILE_INFO));
  while (*FilePath) {
    FileNamePointer = FileName;
    while (*FilePath && *FilePath != L'\\') {
      *FileNamePointer++ = *FilePath++;
    }

    *FileNamePointer = L'\0';
    if (!FileName[0]) {
      //
      // Open root directory.
      //
      if (!Root) {
        //
        // There is no file found for the root directory yet. So, find only its
        // FID by now.
        //
        // See UdfOpenVolume() function.
        //
        Status = InternalFindFile (
                               BlockIo,
                               DiskIo,
                               Volume,
                               L"\\",
                               &PreviousFile,
                               Icb,
                               File
                               );
      } else {
        //
        // We've already a file pointer (Root) for the root directory. Duplicate
        // its FE/EFE and FID descriptors.
        //
        DuplicateFe (BlockIo, Volume, Root->FileEntry, &File->FileEntry);
        DuplicateFid (Root->FileIdentifierDesc, &File->FileIdentifierDesc);
        Status = EFI_SUCCESS;
      }
    } else {
      //
      // No root directory. Find filename from the current directory.
      //
      Status = InternalFindFile (
                             BlockIo,
                             DiskIo,
                             Volume,
                             FileName,
                             &PreviousFile,
                             Icb,
                             File
                             );
    }

    if (EFI_ERROR (Status)) {
      return Status;
    }

    //
    // Find the found file is a sysmlink, then finds its FE/EFE and FID descriptors.
    //
    if (IS_FE_SYMLINK (File->FileEntry)) {
      FreePool ((VOID *)File->FileIdentifierDesc);

      FileEntry = File->FileEntry;

      Status = ResolveSymlink (
                           BlockIo,
                           DiskIo,
                           Volume,
                           &PreviousFile,
                           FileEntry,
                           File
                           );

      FreePool (FileEntry);

      if (EFI_ERROR (Status)) {
        return Status;
      }
    }

    if (CompareMem (
          (VOID *)&PreviousFile,
          (VOID *)Parent,
          sizeof (UDF_FILE_INFO)
          )
      ) {
      CleanupFileInformation (&PreviousFile);
    }

    CopyMem ((VOID *)&PreviousFile, (VOID *)File, sizeof (UDF_FILE_INFO));
    if (*FilePath && *FilePath == L'\\') {
      FilePath++;
    }
  }

  return Status;
}

/**
  Read a directory entry at a time on an UDF volume.

  @param[in]      BlockIo        BlockIo interface.
  @param[in]      DiskIo         DiskIo interface.
  @param[in]      Volume         UDF volume information structure.
  @param[in]      ParentIcb      ICB of the parent file.
  @param[in]      FileEntryData  FE/EFE of the parent file.
  @param[in out]  ReadDirInfo    Next read directory listing structure
                                 information.
  @param[out]     FoundFid       File Identifier Descriptor pointer.

  @retval EFI_SUCCESS          Directory entry read.
  @retval EFI_UNSUPPORTED      Extended Allocation Descriptors not supported.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The directory entry was not read due to lack of
                               resources.

**/
EFI_STATUS
ReadDirectoryEntry (
  IN      EFI_BLOCK_IO_PROTOCOL           *BlockIo,
  IN      EFI_DISK_IO_PROTOCOL            *DiskIo,
  IN      UDF_VOLUME_INFO                 *Volume,
  IN      UDF_LONG_ALLOCATION_DESCRIPTOR  *ParentIcb,
  IN      VOID                            *FileEntryData,
  IN OUT  UDF_READ_DIRECTORY_INFO         *ReadDirInfo,
  OUT     UDF_FILE_IDENTIFIER_DESCRIPTOR  **FoundFid
  )
{
  EFI_STATUS                      Status;
  UDF_READ_FILE_INFO              ReadFileInfo;
  UDF_FILE_IDENTIFIER_DESCRIPTOR  *FileIdentifierDesc;

  if (!ReadDirInfo->DirectoryData) {
    //
    // The directory's recorded data has not been read yet. So, let's read it
    // into memory and the next calls won't need to read it again.
    //
    ReadFileInfo.Flags = READ_FILE_ALLOCATE_AND_READ;

    Status = ReadFile (
                   BlockIo,
                   DiskIo,
                   Volume,
                   ParentIcb,
                   FileEntryData,
                   &ReadFileInfo
                   );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    //
    // Fill in ReadDirInfo structure with the read directory's data information.
    //
    ReadDirInfo->DirectoryData = ReadFileInfo.FileData;
    ReadDirInfo->DirectoryLength = ReadFileInfo.ReadLength;
  }

  do {
    if (ReadDirInfo->FidOffset >= ReadDirInfo->DirectoryLength) {
      //
      // There aren't FIDS for this directory anymore. The EFI_DEVICE_ERROR will
      // indicate end of directory listing.
      //
      return EFI_DEVICE_ERROR;
    }

    //
    // Get FID for this entry.
    //
    FileIdentifierDesc = GET_FID_FROM_ADS (
                                     ReadDirInfo->DirectoryData,
                                     ReadDirInfo->FidOffset
                                     );
    //
    // Update FidOffset to point to the next FID.
    //
    ReadDirInfo->FidOffset += GetFidDescriptorLength (FileIdentifierDesc);
  } while (IS_FID_DELETED_FILE (FileIdentifierDesc));

  DuplicateFid (FileIdentifierDesc, FoundFid);

  return EFI_SUCCESS;
}

/**
  Get a filename (encoded in OSTA-compressed format) from a File Identifier
  Descriptor on an UDF volume.

  @param[in]   FileIdentifierDesc  File Identifier Descriptor pointer.
  @param[out]  FileName            Decoded filename.

  @retval EFI_SUCCESS           Filename decoded and read.
  @retval EFI_VOLUME_CORRUPTED  The file system structures are corrupted.
**/
EFI_STATUS
GetFileNameFromFid (
  IN   UDF_FILE_IDENTIFIER_DESCRIPTOR  *FileIdentifierDesc,
  OUT  CHAR16                          *FileName
  )
{
  UINT8 *OstaCompressed;
  UINT8 CompressionId;
  UINT8 Length;
  UINTN Index;

  OstaCompressed =
    (UINT8 *)(
           (UINT8 *)&FileIdentifierDesc->Data[0] +
           FileIdentifierDesc->LengthOfImplementationUse
           );

  CompressionId = OstaCompressed[0];
  if (!IS_VALID_COMPRESSION_ID (CompressionId)) {
    return EFI_VOLUME_CORRUPTED;
  }

  //
  // Decode filename.
  //
  Length = FileIdentifierDesc->LengthOfFileIdentifier;
  for (Index = 1; Index < Length; Index++) {
    if (CompressionId == 16) {
      *FileName = OstaCompressed[Index++] << 8;
    } else {
      *FileName = 0;
    }

    if (Index < Length) {
      *FileName |= OstaCompressed[Index];
    }

    FileName++;
  }

  *FileName = L'\0';

  return EFI_SUCCESS;
}

/**
  Resolve a symlink file on an UDF volume.

  @param[in]   BlockIo        BlockIo interface.
  @param[in]   DiskIo         DiskIo interface.
  @param[in]   Volume         UDF volume information structure.
  @param[in]   Parent         Parent file.
  @param[in]   FileEntryData  FE/EFE structure pointer.
  @param[out]  File           Resolved file.

  @retval EFI_SUCCESS          Symlink file resolved.
  @retval EFI_UNSUPPORTED      Extended Allocation Descriptors not supported.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The symlink file was not resolved due to lack of
                               resources.

**/
EFI_STATUS
ResolveSymlink (
  IN   EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL   *DiskIo,
  IN   UDF_VOLUME_INFO        *Volume,
  IN   UDF_FILE_INFO          *Parent,
  IN   VOID                   *FileEntryData,
  OUT  UDF_FILE_INFO          *File
  )
{
  EFI_STATUS          Status;
  UDF_READ_FILE_INFO  ReadFileInfo;
  UINT8               *Data;
  UINT64              Length;
  UINT8               *EndData;
  UDF_PATH_COMPONENT  *PathComp;
  UINT8               PathCompLength;
  CHAR16              FileName[UDF_FILENAME_LENGTH];
  CHAR16              *C;
  UINTN               Index;
  UINT8               CompressionId;
  UDF_FILE_INFO       PreviousFile;

  //
  // Symlink files on UDF volumes do not contain so much data other than
  // Path Components which resolves to real filenames, so it's OK to read in
  // all its data here -- usually the data will be inline with the FE/EFE for
  // lower filenames.
  //
  ReadFileInfo.Flags = READ_FILE_ALLOCATE_AND_READ;

  Status = ReadFile (
                 BlockIo,
                 DiskIo,
                 Volume,
                 &Parent->FileIdentifierDesc->Icb,
                 FileEntryData,
                 &ReadFileInfo
                 );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Length = ReadFileInfo.ReadLength;

  Data = (UINT8 *)ReadFileInfo.FileData;
  EndData = Data + Length;

  CopyMem ((VOID *)&PreviousFile, (VOID *)Parent, sizeof (UDF_FILE_INFO));

  for (;;) {
    PathComp = (UDF_PATH_COMPONENT *)Data;

    PathCompLength = PathComp->LengthOfComponentIdentifier;

    switch (PathComp->ComponentType) {
      case 1:
        //
        // This Path Component specifies the root directory hierarchy subject to
        // agreement between the originator and recipient of the medium. Skip it.
        //
        // Fall through.
        //
      case 2:
        //
        // "\\." of the current directory. Read next Path Component.
        //
        goto Next_Path_Component;
      case 3:
        //
        // ".." (parent directory). Go to it.
        //
        CopyMem ((VOID *)FileName, L"..", 6);
        break;
      case 4:
        //
        // "." (current file). Duplicate both FE/EFE and FID of this file.
        //
        DuplicateFe (BlockIo, Volume, PreviousFile.FileEntry, &File->FileEntry);
        DuplicateFid (PreviousFile.FileIdentifierDesc, &File->FileIdentifierDesc);
        goto Next_Path_Component;
      case 5:
        //
        // This Path Component identifies an object, either a file or a
        // directory or an alias.
        //
        // Decode it from the compressed data in ComponentIdentifier and find
        // respective path.
        //
        CompressionId = PathComp->ComponentIdentifier[0];
        if (!IS_VALID_COMPRESSION_ID (CompressionId)) {
          return EFI_VOLUME_CORRUPTED;
        }

        C = FileName;
        for (Index = 1; Index < PathCompLength; Index++) {
          if (CompressionId == 16) {
            *C = *(UINT8 *)((UINT8 *)PathComp->ComponentIdentifier +
                            Index) << 8;
            Index++;
          } else {
            *C = 0;
          }

          if (Index < Length) {
            *C |= *(UINT8 *)((UINT8 *)PathComp->ComponentIdentifier + Index);
          }

          C++;
        }

        *C = L'\0';
        break;
    }

    //
    // Find file from the read filename in symlink file's data.
    //
    Status = InternalFindFile (
                           BlockIo,
                           DiskIo,
                           Volume,
                           FileName,
                           &PreviousFile,
                           NULL,
                           File
                           );
    if (EFI_ERROR (Status)) {
      goto Error_Find_File;
    }

Next_Path_Component:
    Data += sizeof (UDF_PATH_COMPONENT) + PathCompLength;
    if (Data >= EndData) {
      break;
    }

    if (CompareMem (
          (VOID *)&PreviousFile,
          (VOID *)Parent,
          sizeof (UDF_FILE_INFO)
          )
      ) {
      CleanupFileInformation (&PreviousFile);
    }

    CopyMem ((VOID *)&PreviousFile, (VOID *)File, sizeof (UDF_FILE_INFO));
  }

  //
  // Unmap the symlink file.
  //
  FreePool (ReadFileInfo.FileData);

  return EFI_SUCCESS;

Error_Find_File:
  if (CompareMem (
        (VOID *)&PreviousFile,
        (VOID *)Parent,
        sizeof (UDF_FILE_INFO)
        )
    ) {
    CleanupFileInformation (&PreviousFile);
  }

  FreePool (ReadFileInfo.FileData);

  return Status;
}

/**
  Clean up in-memory UDF volume information.

  @param[in] Volume Volume information pointer.

**/
VOID
CleanupVolumeInformation (
  IN UDF_VOLUME_INFO *Volume
  )
{
  UINTN Index;

  if (Volume->LogicalVolDescs) {
    for (Index = 0; Index < Volume->LogicalVolDescsNo; Index++) {
      FreePool ((VOID *)Volume->LogicalVolDescs[Index]);
    }

    FreePool ((VOID *)Volume->LogicalVolDescs);
  }

  if (Volume->PartitionDescs) {
    for (Index = 0; Index < Volume->PartitionDescsNo; Index++) {
      FreePool ((VOID *)Volume->PartitionDescs[Index]);
    }

    FreePool ((VOID *)Volume->PartitionDescs);
  }

  if (Volume->FileSetDescs) {
    for (Index = 0; Index < Volume->FileSetDescsNo; Index++) {
      FreePool ((VOID *)Volume->FileSetDescs[Index]);
    }

    FreePool ((VOID *)Volume->FileSetDescs);
  }

  ZeroMem ((VOID *)Volume, sizeof (UDF_VOLUME_INFO));
}

/**
  Clean up in-memory UDF file information.

  @param[in] File File information pointer.

**/
VOID
CleanupFileInformation (
  IN UDF_FILE_INFO *File
  )
{
  if (File->FileEntry) {
    FreePool (File->FileEntry);
  }

  if (File->FileIdentifierDesc) {
    FreePool ((VOID *)File->FileIdentifierDesc);
  }

  ZeroMem ((VOID *)File, sizeof (UDF_FILE_INFO));
}

/**
  Find a file from its absolute path on an UDF volume.

  @param[in]   BlockIo  BlockIo interface.
  @param[in]   DiskIo   DiskIo interface.
  @param[in]   Volume   UDF volume information structure.
  @param[in]   File     File information structure.
  @param[out]  Size     Size of the file.

  @retval EFI_SUCCESS          File size calculated and set in @p Size.
  @retval EFI_UNSUPPORTED      Extended Allocation Descriptors not supported.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The file size was not calculated due to lack of
                               resources.

**/
EFI_STATUS
GetFileSize (
  IN   EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL   *DiskIo,
  IN   UDF_VOLUME_INFO        *Volume,
  IN   UDF_FILE_INFO          *File,
  OUT  UINT64                 *Size
  )
{
  EFI_STATUS          Status;
  UDF_READ_FILE_INFO  ReadFileInfo;

  ReadFileInfo.Flags = READ_FILE_GET_FILESIZE;

  Status = ReadFile (
                 BlockIo,
                 DiskIo,
                 Volume,
                 &File->FileIdentifierDesc->Icb,
                 File->FileEntry,
                 &ReadFileInfo
                 );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *Size = ReadFileInfo.ReadLength;

  return EFI_SUCCESS;
}

/**
  Set information about a file on an UDF volume.

  @param[in]      File        File pointer.
  @param[in]      FileSize    Size of the file.
  @param[in]      FileName    Filename of the file.
  @param[in out]  BufferSize  Size of the returned file infomation.
  @param[out]     Buffer      Data of the returned file information.

  @retval EFI_SUCCESS          File information set.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The file information was not set due to lack of
                               resources.

**/
EFI_STATUS
SetFileInfo (
  IN      UDF_FILE_INFO  *File,
  IN      UINT64         FileSize,
  IN      CHAR16         *FileName,
  IN OUT  UINTN          *BufferSize,
  OUT     VOID           *Buffer
  )
{
  UINTN                    FileInfoLength;
  EFI_FILE_INFO            *FileInfo;
  UDF_FILE_ENTRY           *FileEntry;
  UDF_EXTENDED_FILE_ENTRY  *ExtendedFileEntry;

  //
  // Calculate the needed size for the EFI_FILE_INFO structure.
  //
  FileInfoLength = sizeof (EFI_FILE_INFO) + (FileName ?
                                             StrSize (FileName) :
                                             sizeof (CHAR16)
                                            );
  if (*BufferSize < FileInfoLength) {
    //
    // The given Buffer has no size enough for EFI_FILE_INFO structure.
    //
    *BufferSize = FileInfoLength;
    return EFI_BUFFER_TOO_SMALL;
  }

  //
  // Buffer now contains room enough to store EFI_FILE_INFO structure.
  // Now, fill it in with all necessary information about the file.
  //
  FileInfo = (EFI_FILE_INFO *)Buffer;
  FileInfo->Size         = FileInfoLength;
  FileInfo->Attribute    &= ~EFI_FILE_VALID_ATTR;

  //FileInfo->Attribute    |= EFI_FILE_READ_ONLY;

  if (IS_FID_DIRECTORY_FILE (File->FileIdentifierDesc)) {
    FileInfo->Attribute |= EFI_FILE_DIRECTORY;
  } else if (IS_FID_NORMAL_FILE (File->FileIdentifierDesc)) {
    FileInfo->Attribute |= EFI_FILE_ARCHIVE;
  }

  if (IS_FID_HIDDEN_FILE (File->FileIdentifierDesc)) {
    FileInfo->Attribute |= EFI_FILE_HIDDEN;
  }

  if (IS_FE (File->FileEntry)) {
    FileEntry = (UDF_FILE_ENTRY *)File->FileEntry;

    //
    // Check if FE has the system attribute set.
    //
    if (FileEntry->IcbTag.Flags & (1 << 10)) {
      FileInfo->Attribute |= EFI_FILE_SYSTEM;
    }

    FileInfo->FileSize      = FileSize;
    FileInfo->PhysicalSize  = FileSize;

    FileInfo->CreateTime.Year        = FileEntry->AccessTime.Year;
    FileInfo->CreateTime.Month       = FileEntry->AccessTime.Month;
    FileInfo->CreateTime.Day         = FileEntry->AccessTime.Day;
    FileInfo->CreateTime.Hour        = FileEntry->AccessTime.Hour;
    FileInfo->CreateTime.Minute      = FileEntry->AccessTime.Second;
    FileInfo->CreateTime.Second      = FileEntry->AccessTime.Second;
    FileInfo->CreateTime.Nanosecond  =
                                   FileEntry->AccessTime.HundredsOfMicroseconds;

    FileInfo->LastAccessTime.Year        =
                                   FileEntry->AccessTime.Year;
    FileInfo->LastAccessTime.Month       =
                                   FileEntry->AccessTime.Month;
    FileInfo->LastAccessTime.Day         =
                                   FileEntry->AccessTime.Day;
    FileInfo->LastAccessTime.Hour        =
                                   FileEntry->AccessTime.Hour;
    FileInfo->LastAccessTime.Minute      =
                                   FileEntry->AccessTime.Minute;
    FileInfo->LastAccessTime.Second      =
                                   FileEntry->AccessTime.Second;
    FileInfo->LastAccessTime.Nanosecond  =
                                   FileEntry->AccessTime.HundredsOfMicroseconds;
  } else if (IS_EFE (File->FileEntry)) {
    ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)File->FileEntry;

    //
    // Check if EFE has the system attribute set.
    //
    if (ExtendedFileEntry->IcbTag.Flags & (1 << 10)) {
      FileInfo->Attribute |= EFI_FILE_SYSTEM;
    }

    FileInfo->FileSize      = FileSize;
    FileInfo->PhysicalSize  = FileSize;

    FileInfo->CreateTime.Year        = ExtendedFileEntry->CreationTime.Year;
    FileInfo->CreateTime.Month       = ExtendedFileEntry->CreationTime.Month;
    FileInfo->CreateTime.Day         = ExtendedFileEntry->CreationTime.Day;
    FileInfo->CreateTime.Hour        = ExtendedFileEntry->CreationTime.Hour;
    FileInfo->CreateTime.Minute      = ExtendedFileEntry->CreationTime.Second;
    FileInfo->CreateTime.Second      = ExtendedFileEntry->CreationTime.Second;
    FileInfo->CreateTime.Nanosecond  =
                           ExtendedFileEntry->AccessTime.HundredsOfMicroseconds;

    FileInfo->LastAccessTime.Year        =
                           ExtendedFileEntry->AccessTime.Year;
    FileInfo->LastAccessTime.Month       =
                           ExtendedFileEntry->AccessTime.Month;
    FileInfo->LastAccessTime.Day         =
                           ExtendedFileEntry->AccessTime.Day;
    FileInfo->LastAccessTime.Hour        =
                           ExtendedFileEntry->AccessTime.Hour;
    FileInfo->LastAccessTime.Minute      =
                           ExtendedFileEntry->AccessTime.Minute;
    FileInfo->LastAccessTime.Second      =
                           ExtendedFileEntry->AccessTime.Second;
    FileInfo->LastAccessTime.Nanosecond  =
                           ExtendedFileEntry->AccessTime.HundredsOfMicroseconds;
  }

  FileInfo->CreateTime.TimeZone  = EFI_UNSPECIFIED_TIMEZONE;
  FileInfo->CreateTime.Daylight  = EFI_TIME_ADJUST_DAYLIGHT;

  CopyMem (
    (VOID *)&FileInfo->ModificationTime,
    (VOID *)&FileInfo->LastAccessTime,
    sizeof (EFI_TIME)
    );

  if (FileName) {
    StrCpy (FileInfo->FileName, FileName);
  } else {
    FileInfo->FileName[0] = '\0';
  }

  *BufferSize = FileInfoLength;

  return EFI_SUCCESS;
}

/**
  Get volume and free space size information of an UDF volume.

  @param[in]   BlockIo        BlockIo interface.
  @param[in]   DiskIo         DiskIo interface.
  @param[in]   Volume         UDF volume information structure.
  @param[out]  VolumeSize     Volume size.
  @param[out]  FreeSpaceSize  Free space size.

  @retval EFI_SUCCESS          Volume and free space size calculated.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The volume and free space size were not
                               calculated due to lack of resources.

**/
EFI_STATUS
GetVolumeSize (
  IN   EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL   *DiskIo,
  IN   UDF_VOLUME_INFO        *Volume,
  OUT  UINT64                 *VolumeSize,
  OUT  UINT64                 *FreeSpaceSize
  )
{
  UDF_EXTENT_AD                 ExtentAd;
  UINT32                        LogicalBlockSize;
  UINT64                        Lsn;
  EFI_STATUS                    Status;
  UDF_LOGICAL_VOLUME_INTEGRITY  *LogicalVolInt;
  UINTN                         Index;
  UINTN                         Length;
  UINT32                        LsnsNo;

  *VolumeSize     = 0;
  *FreeSpaceSize  = 0;

  for (Index = 0; Index < Volume->LogicalVolDescsNo; Index++) {
    CopyMem (
      (VOID *)&ExtentAd,
      (VOID *)&Volume->LogicalVolDescs[Index]->IntegritySequenceExtent,
      sizeof (UDF_EXTENT_AD)
      );
    if (!ExtentAd.ExtentLength) {
      continue;
    }

    LogicalBlockSize = LV_BLOCK_SIZE (Volume, Index);

Read_Next_Sequence:
    LogicalVolInt = (UDF_LOGICAL_VOLUME_INTEGRITY *)AllocatePool (
                                                          ExtentAd.ExtentLength
                                                          );
    if (!LogicalVolInt) {
      return EFI_OUT_OF_RESOURCES;
    }

    Lsn = (UINT64)ExtentAd.ExtentLocation;

    Status = DiskIo->ReadDisk (
                            DiskIo,
                            BlockIo->Media->MediaId,
                            MultU64x32 (Lsn, LogicalBlockSize),
                            ExtentAd.ExtentLength,
                            (VOID *)LogicalVolInt
                            );
    if (EFI_ERROR (Status)) {
      FreePool ((VOID *)LogicalVolInt);
      return Status;
    }

    if (!IS_LVID (LogicalVolInt)) {
      FreePool ((VOID *)LogicalVolInt);
      return EFI_VOLUME_CORRUPTED;
    }

    Length = LogicalVolInt->NumberOfPartitions;
    for (Index = 0; Index < Length; Index += sizeof (UINT32)) {
      LsnsNo = *(UINT32 *)((UINT8 *)&LogicalVolInt->Data[0] + Index);
      if (LsnsNo == 0xFFFFFFFFUL) {
        //
        // Size not specified.
        //
        continue;
      }

      *FreeSpaceSize += MultU64x32 ((UINT64)LsnsNo, LogicalBlockSize);
    }

    Length = (LogicalVolInt->NumberOfPartitions * sizeof (UINT32)) << 1;
    for (; Index < Length; Index += sizeof (UINT32)) {
      LsnsNo = *(UINT32 *)((UINT8 *)&LogicalVolInt->Data[0] + Index);
      if (LsnsNo == 0xFFFFFFFFUL) {
        //
        // Size not specified.
        //
        continue;
      }

      *VolumeSize += MultU64x32 ((UINT64)LsnsNo, LogicalBlockSize);
    }

    CopyMem (
      (VOID *)&ExtentAd,
      (VOID *)&LogicalVolInt->NextIntegrityExtent,
      sizeof (UDF_EXTENT_AD)
      );
    if (ExtentAd.ExtentLength) {
      FreePool ((VOID *)LogicalVolInt);
      goto Read_Next_Sequence;
    }

    FreePool ((VOID *)LogicalVolInt);
  }

  return EFI_SUCCESS;
}

/**
  Seek a file and write data to it on an UDF volume.

  @param[in]      BlockIo       BlockIo interface.
  @param[in]      DiskIo        DiskIo interface.
  @param[in]      Volume        UDF volume information structure.
  @param[in]      File          File information structure.
  @param[in]      FileSize      Size of the file.
  @param[in out]  FilePosition  File position.
  @param[in out]  Buffer        File data.
  @param[in out]  BufferSize    Write size.

  @retval EFI_SUCCESS          File seeked and data written.
  @retval EFI_UNSUPPORTED      Extended Allocation Descriptors not supported.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The file's recorded data was not read due to lack
                               of resources.

**/
EFI_STATUS
WriteFileData (
  IN      EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN      EFI_DISK_IO_PROTOCOL   *DiskIo,
  IN      UDF_VOLUME_INFO        *Volume,
  IN      UDF_FILE_INFO          *File,
  IN      UINT64                 FileSize,
  IN OUT  UINT64                 *FilePosition,
  OUT     VOID                   *Buffer,
  IN OUT  UINT64                 *BufferSize
  )
{
  EFI_STATUS              Status;
  VOID                    *Data;
  UINT64                  *Length;
  UDF_FE_RECORDING_FLAGS  RecordingFlags;

  RecordingFlags = GET_FE_RECORDING_FLAGS (File->FileEntry);
  switch (RecordingFlags) {
    case INLINE_DATA:
      //
      // There is no extent for this FE/EFE. Data is inline.
      //
      GetFileEntryData2 (File->FileEntry, &Data, &Length);
      if (*FilePosition + *BufferSize <= Volume->FileEntrySize) {
	Print (L"Writing data in-line to file\n");
	CopyMem (
	  (VOID *)((UINTN)Data + *FilePosition),
	  Buffer,
	  *BufferSize
	  );
	*FilePosition += *BufferSize;
	*Length += *BufferSize;
        Status = WriteFileEntry (
                             BlockIo,
                             DiskIo,
                             Volume,
                             File->FileIdentifierDesc,
                             File->FileEntry
                             );
      }
      break;
    case LONG_ADS_SEQUENCE:
      Print (L"long ads sequence\n");
      break;
    case SHORT_ADS_SEQUENCE:
      Print (L"short ads sequence\n");
      break;
    case EXTENDED_ADS_SEQUENCE:
      break;
  }

  return Status;
}

/**
  Seek a file and read its data into memory on an UDF volume.

  @param[in]      BlockIo       BlockIo interface.
  @param[in]      DiskIo        DiskIo interface.
  @param[in]      Volume        UDF volume information structure.
  @param[in]      File          File information structure.
  @param[in]      FileSize      Size of the file.
  @param[in out]  FilePosition  File position.
  @param[in out]  Buffer        File data.
  @param[in out]  BufferSize    Read size.

  @retval EFI_SUCCESS          File seeked and read.
  @retval EFI_UNSUPPORTED      Extended Allocation Descriptors not supported.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The file's recorded data was not read due to lack
                               of resources.

**/
EFI_STATUS
ReadFileData (
  IN      EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN      EFI_DISK_IO_PROTOCOL   *DiskIo,
  IN      UDF_VOLUME_INFO        *Volume,
  IN      UDF_FILE_INFO          *File,
  IN      UINT64                 FileSize,
  IN OUT  UINT64                 *FilePosition,
  IN OUT  VOID                   *Buffer,
  IN OUT  UINT64                 *BufferSize
  )
{
  EFI_STATUS          Status;
  UDF_READ_FILE_INFO  ReadFileInfo;

  ReadFileInfo.Flags         = READ_FILE_SEEK_AND_READ;
  ReadFileInfo.FilePosition  = *FilePosition;
  ReadFileInfo.FileData      = Buffer;
  ReadFileInfo.FileDataSize  = *BufferSize;
  ReadFileInfo.FileSize      = FileSize;

  Status = ReadFile (
                 BlockIo,
                 DiskIo,
                 Volume,
                 &File->FileIdentifierDesc->Icb,
                 File->FileEntry,
                 &ReadFileInfo
                 );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *BufferSize    = ReadFileInfo.FileDataSize;
  *FilePosition  = ReadFileInfo.FilePosition;

  return EFI_SUCCESS;
}

UINT8
CalculateTagChecksum (
  IN VOID *Descriptor
  )
{
  UINT8 *Data = (UINT8 *)Descriptor;
  UINTN Index;
  UINTN Sum = 0;

  //
  // Sum up 0-3 bytes.
  //
  for (Index = 0; Index < 4; Index++) {
    Sum = (Sum + Data[Index]) % 256;
  }

  //
  // Sum up 5-15 bytes.
  //
  for (Index = 5; Index < 16; Index++) {
    Sum = (Sum + Data[Index]) % 256;
  }

  return Sum & 0xFF;
}

EFI_STATUS
GetUnallocatedSpaceLsn (
  IN EFI_BLOCK_IO_PROTOCOL *BlockIo,
  IN EFI_DISK_IO_PROTOCOL *DiskIo,
  IN UDF_VOLUME_INFO *Volume,
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR *FileIdentifierDesc,
  OUT UINT32 *LogicalBlockNumber
  )
{
  UINT32 LogicalBlockSize;
  UDF_PARTITION_DESCRIPTOR *PartitionDesc;
  UDF_PARTITION_HEADER_DESCRIPTOR *PartitionHdrDesc;
  UINT32 ExtentLength;
  EFI_STATUS Status;
  UINT64 Lsn;
  UDF_SPACE_BITMAP_DESCRIPTOR SpaceBitmap;
  UINTN Count;
  BOOLEAN Done;
  UINT64 Offset;
  UINTN BytesToRead;
  UINTN Bits;
  UINTN Index;
  BOOLEAN InvalidateBitmap;
  UINT8 *Data;

  LogicalBlockSize = LV_BLOCK_SIZE (Volume, UDF_DEFAULT_LV_NUM);

  PartitionDesc = GetPdFromLongAd (Volume, &FileIdentifierDesc->Icb);

  PartitionHdrDesc =
    (UDF_PARTITION_HEADER_DESCRIPTOR *)&PartitionDesc->PartitionContentsUse[0];

  ExtentLength = GET_EXTENT_LENGTH (
                         SHORT_ADS_SEQUENCE,
                         &PartitionHdrDesc->UnallocatedSpaceBitmap
                         );
  if (ExtentLength) {
    Print (L"Partition has Unallocated Space Bitmap\n");
    Print (L"ExtentLength: %d - ExtentPosition: (%d; %d) - blocks: %d\n",
           ExtentLength,
           PartitionHdrDesc->UnallocatedSpaceBitmap.ExtentPosition,
           PartitionDesc->PartitionStartingLocation +
           PartitionHdrDesc->UnallocatedSpaceBitmap.ExtentPosition,
           ExtentLength / LogicalBlockSize);

    Lsn = GetShortAdLsn (
                  PartitionDesc,
                  &PartitionHdrDesc->UnallocatedSpaceBitmap
                  );

    Status = DiskIo->ReadDisk (
                            DiskIo,
                            BlockIo->Media->MediaId,
                            MultU64x32 (Lsn, LogicalBlockSize),
                            sizeof (UDF_SPACE_BITMAP_DESCRIPTOR),
                            (VOID *)&SpaceBitmap
                         );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (!IS_SBD (&SpaceBitmap)) {
      return EFI_VOLUME_CORRUPTED;
    }

    Print (L"space bitmap desc: bits no: %d - bytes no: %d\n",
           SpaceBitmap.NumberOfBits,
           SpaceBitmap.NumberOfBytes);

    Data = (UINT8 *)AllocatePool (LogicalBlockSize);
    if (!Data) {
      return EFI_OUT_OF_RESOURCES;
    }

    Count = 0;
    Done = FALSE;
    do {
      if (Count == SpaceBitmap.NumberOfBytes) {
        break;
      }

      if (Count > SpaceBitmap.NumberOfBytes) {
        BytesToRead = Count - SpaceBitmap.NumberOfBytes;
        Count = SpaceBitmap.NumberOfBytes - BytesToRead;
        Done = TRUE;
      } else {
        BytesToRead = LogicalBlockSize;
      }

      Offset = (UINT64)MultU64x32 (Lsn, LogicalBlockSize) +
               OFFSET_OF (UDF_SPACE_BITMAP_DESCRIPTOR, Bitmap[0]) +
               Count;

      Status = DiskIo->ReadDisk (
                              DiskIo,
                              BlockIo->Media->MediaId,
                              Offset,
                              BytesToRead,
                              (VOID *)Data
                              );
      if (EFI_ERROR (Status)) {
        goto Error_Read_Disk_Blk;
      }

      InvalidateBitmap = FALSE;
      for (Index = 0; Index < BytesToRead; Index++) {
        Bits = 0;
        while (Bits <= sizeof (UINT8) * 8) {
          if (Data[Index] & (1 << Bits)) {
            *LogicalBlockNumber = (Count + Index) * 8 + Bits;
            Data[Index] &= ~(1 << Bits);
            InvalidateBitmap = TRUE;
            Done = TRUE;
            goto Found_Unallocated_Space;
          }

          Bits++;
        }
      }

Found_Unallocated_Space:
      if (InvalidateBitmap) {
        Status = DiskIo->WriteDisk (
                                 DiskIo,
                                 BlockIo->Media->MediaId,
                                 Offset,
                                 BytesToRead,
                                 (VOID *)Data
                                 );
        if (EFI_ERROR (Status)) {
          goto Error_Write_Disk_Blk;
        }

        Status = BlockIo->FlushBlocks (BlockIo);
        if (EFI_ERROR (Status)) {
          goto Error_Flush_Disk_Blks;
        }
      }

      Count += LogicalBlockSize;
    } while (!Done);
  }

  FreePool ((VOID *)Data);

  return EFI_SUCCESS;

Error_Flush_Disk_Blks:
Error_Write_Disk_Blk:
Error_Read_Disk_Blk:
    FreePool ((VOID *)Data);

    return Status;
}

EFI_STATUS
ReadLogicalVolumeIntegrity (
  IN   EFI_BLOCK_IO_PROTOCOL          *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL           *DiskIo,
  IN   UDF_LOGICAL_VOLUME_DESCRIPTOR  *LogicalVolDesc,
  OUT  UDF_LOGICAL_VOLUME_INTEGRITY   **LogicalVolInt
  )
{
  UDF_EXTENT_AD  *ExtentAd;
  EFI_STATUS     Status;
  UINT64         Lsn;

  ExtentAd = &LogicalVolDesc->IntegritySequenceExtent;

  *LogicalVolInt = AllocatePool (ExtentAd->ExtentLength);
  if (!*LogicalVolInt) {
    return EFI_OUT_OF_RESOURCES;
  }

  Lsn = (UINT64)ExtentAd->ExtentLocation;

  Status = DiskIo->ReadDisk (
                        DiskIo,
                        BlockIo->Media->MediaId,
                        MultU64x32 (Lsn, LogicalVolDesc->LogicalBlockSize),
                        ExtentAd->ExtentLength,
                        (VOID *)*LogicalVolInt
                        );
  if (EFI_ERROR (Status)) {
    FreePool ((VOID *)*LogicalVolInt);
  }

  return Status;
}

VOID
EfiTimeToUdfTime(
  IN   EFI_TIME       *EfiTime,
  OUT  UDF_TIMESTAMP  *UdfTime
  )
{
  //
  // Local Time (15-12) + timezone offset (11-0)
  //
  UdfTime->TypeAndTimezone = (~EfiTime->TimeZone | 0x1) & 0x1FFF;
  UdfTime->Year = EfiTime->Year;
  UdfTime->Month = EfiTime->Month;
  UdfTime->Day = EfiTime->Day;
  UdfTime->Hour = EfiTime->Hour;
  UdfTime->Minute = EfiTime->Minute;
  UdfTime->Second = EfiTime->Second;
  UdfTime->HundredsOfMicroseconds = EfiTime->Nanosecond;
  UdfTime->Microseconds = EfiTime->Nanosecond * 100;
  UdfTime->Centiseconds = EfiTime->Second / 100;
}

EFI_STATUS
UpdateLogicalVolumeIntegrity (
  IN EFI_BLOCK_IO_PROTOCOL          *BlockIo,
  IN EFI_DISK_IO_PROTOCOL           *DiskIo,
  IN UDF_LOGICAL_VOLUME_DESCRIPTOR  *LogicalVolDesc,
  IN UDF_LOGICAL_VOLUME_INTEGRITY   *LogicalVolInt
  )
{
  UDF_EXTENT_AD  *ExtentAd;
  UINT64         Lsn;

  ExtentAd = &LogicalVolDesc->IntegritySequenceExtent;
  Lsn = (UINT64)ExtentAd->ExtentLocation;

  Print (L"extad: length: %d\n", ExtentAd->ExtentLength);
  Print (L"lvid: length: %d\n", sizeof (UDF_LOGICAL_VOLUME_INTEGRITY));
  Print (L"desc tag: length: %d\n", sizeof (UDF_DESCRIPTOR_TAG));

#if 0
  EFI_TIME Time;
  gRT->GetTime (&Time, NULL);
  EfiTimeToUdfTime (&Time, &LogicalVolInt->RecordingDateTime);
#endif

  LogicalVolInt->DescriptorTag.DescriptorCRCLength = (sizeof (UDF_LOGICAL_VOLUME_INTEGRITY) +
                                                      LogicalVolInt->LengthOfImplementationUse) -
                                                     sizeof (UDF_DESCRIPTOR_TAG);
  LogicalVolInt->DescriptorTag.DescriptorCRC = CalculateCrc (
                                                   (VOID *)((UINTN)LogicalVolInt +
                                                            sizeof (UDF_DESCRIPTOR_TAG)),
                                                   LogicalVolInt->DescriptorTag.DescriptorCRCLength
                                                   );

  LogicalVolInt->DescriptorTag.TagChecksum = CalculateTagChecksum (LogicalVolInt);

  return DiskIo->WriteDisk (
                        DiskIo,
                        BlockIo->Media->MediaId,
                        MultU64x32 (Lsn, LogicalVolDesc->LogicalBlockSize),
                        ExtentAd->ExtentLength,
                        (VOID *)LogicalVolInt
                        );
}

EFI_STATUS
SetupNewFileEntry (
  IN EFI_BLOCK_IO_PROTOCOL               *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                *DiskIo,
  IN   UDF_VOLUME_INFO                   *Volume,
  IN OUT UDF_FILE_IDENTIFIER_DESCRIPTOR  *FileIdentifierDesc,
  OUT  UDF_EXTENDED_FILE_ENTRY           **NewFileEntry
  )
{
  UDF_PARTITION_DESCRIPTOR  *PartitionDesc;
  UDF_EXTENDED_FILE_ENTRY *ExtendedFileEntry;
  UINT64 Lsn;
  UDF_LOGICAL_VOLUME_INTEGRITY *LogicalVolInt;
  UDF_LOGICAL_VOLUME_HEADER_DESCRIPTOR *LogicalVolHdrDesc;
  UDF_TIMESTAMP                        FeTime;
  EFI_TIME                             Time;
  UDF_AD_IMPLEMENTATION_USE            *AdImpUse;

  ExtendedFileEntry = AllocateZeroPool (LV_BLOCK_SIZE (Volume, UDF_DEFAULT_LV_NUM));
  if (!ExtendedFileEntry) {
    return EFI_OUT_OF_RESOURCES;
  }

  ExtendedFileEntry->DescriptorTag.TagIdentifier = 266;
  ExtendedFileEntry->DescriptorTag.DescriptorVersion = 3;
  ExtendedFileEntry->DescriptorTag.Reserved = 0;
  ExtendedFileEntry->DescriptorTag.TagSerialNumber = 1;

  PartitionDesc = GetPdFromLongAd (Volume, &FileIdentifierDesc->Icb);

  Lsn = GetLongAdLsn (Volume, &FileIdentifierDesc->Icb) -
        PartitionDesc->PartitionStartingLocation;

  ExtendedFileEntry->DescriptorTag.TagLocation = Lsn;

  if (IS_FID_DIRECTORY_FILE (FileIdentifierDesc)) {
    ExtendedFileEntry->IcbTag.FileType = 4;
  } else if (IS_FID_NORMAL_FILE (FileIdentifierDesc)) {
    ExtendedFileEntry->IcbTag.FileType = 5;
  }

  ExtendedFileEntry->IcbTag.StrategyType = 4;
  ExtendedFileEntry->IcbTag.StrategyParameter = 0;
  ExtendedFileEntry->IcbTag.MaximumNumberOfEntries = 1;
  ExtendedFileEntry->IcbTag.Flags = 0x0003;

  ExtendedFileEntry->ImplementationIdentifier.Flags = 0;
  CopyMem (
    &ExtendedFileEntry->ImplementationIdentifier.Identifier,
    "*Tianocore UEFI UDF",
    19
    );

  ExtendedFileEntry->ImplementationIdentifier.IdentifierSuffix[0] = 0x00; // OS class: undefined
  ExtendedFileEntry->ImplementationIdentifier.IdentifierSuffix[1] = 0x00; // OS identifier: undefined
  CopyMem (
    &ExtendedFileEntry->ImplementationIdentifier.IdentifierSuffix[2],
    "UEFIFS",
    6
    );

  gRT->GetTime (&Time, NULL);
  EfiTimeToUdfTime (&Time, &FeTime);

  CopyMem (&ExtendedFileEntry->AccessTime, &FeTime, sizeof (UDF_TIMESTAMP));
  CopyMem (&ExtendedFileEntry->ModificationTime, &FeTime, sizeof (UDF_TIMESTAMP));
  CopyMem (&ExtendedFileEntry->CreationTime, &FeTime, sizeof (UDF_TIMESTAMP));
  CopyMem (&ExtendedFileEntry->AttributeTime, &FeTime, sizeof (UDF_TIMESTAMP));

  ExtendedFileEntry->Uid = 0xFFFFFFFF;
  ExtendedFileEntry->Gid = 0xFFFFFFFF;

  ExtendedFileEntry->FileLinkCount = 1;
  ExtendedFileEntry->CheckPoint = 1;

  ReadLogicalVolumeIntegrity (
    BlockIo,
    DiskIo,
    Volume->LogicalVolDescs[UDF_DEFAULT_LV_NUM],
    &LogicalVolInt
    );

  LogicalVolHdrDesc = (UDF_LOGICAL_VOLUME_HEADER_DESCRIPTOR *)(UINTN)LogicalVolInt->LogicalVolumeContentsUse;

  ExtendedFileEntry->UniqueId = LogicalVolHdrDesc->UniqueId++;

  Print (L"FE: UniqueId: 0x%016lx\n", ExtendedFileEntry->UniqueId);

  AdImpUse = (UDF_AD_IMPLEMENTATION_USE *)(UINTN)FileIdentifierDesc->Icb.ImplementationUse;

  AdImpUse->Flags &= ~EXTENT_ERASED;
  *((UINT32 *)AdImpUse->ImpUse) = ExtendedFileEntry->UniqueId & 0xFFFFFFFF;

  Print (L"FID: UniqueId: 0x%016lx\n", *(UINT32 *)AdImpUse->ImpUse);

  UpdateLogicalVolumeIntegrity (
    BlockIo,
    DiskIo,
    Volume->LogicalVolDescs[UDF_DEFAULT_LV_NUM],
    LogicalVolInt
    );

  FreePool ((VOID *)LogicalVolInt);

  ExtendedFileEntry->DescriptorTag.DescriptorCRCLength = sizeof (UDF_EXTENDED_FILE_ENTRY) -
                                                         sizeof (UDF_DESCRIPTOR_TAG);
  ExtendedFileEntry->DescriptorTag.DescriptorCRC = CalculateCrc (
                                             (VOID *)((UINTN)ExtendedFileEntry +
                                                      sizeof (UDF_DESCRIPTOR_TAG)),
                                             ExtendedFileEntry->DescriptorTag.DescriptorCRCLength
                                             );

  ExtendedFileEntry->DescriptorTag.TagChecksum = CalculateTagChecksum (ExtendedFileEntry);

  Print (L"fe: tag checksum: 0x%x\n", ExtendedFileEntry->DescriptorTag.TagChecksum);

  *NewFileEntry = ExtendedFileEntry;
  return EFI_SUCCESS;
}

EFI_STATUS
SetupNewFileIdentifierDesc (
  IN   EFI_BLOCK_IO_PROTOCOL           *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL            *DiskIo,
  IN   UDF_VOLUME_INFO                 *Volume,
  IN   UDF_FILE_IDENTIFIER_DESCRIPTOR  *ParentFid,
  IN   CHAR16                          *FileName,
  IN   UINT16                          Attributes,
  OUT  UDF_FILE_IDENTIFIER_DESCRIPTOR  **FileIdentifierDesc
  )
{
  UDF_FILE_IDENTIFIER_DESCRIPTOR  *NewFid;
  UDF_PARTITION_DESCRIPTOR  *PartitionDesc;
  UINT64                    Lsn;
  UINTN                     Count;
  EFI_STATUS                Status;
  UINT8                     CompressionId;
  UINT8                     *FileNamePointer;
  UINT8                     *FidFileNamePointer;

  NewFid = AllocateZeroPool (LV_BLOCK_SIZE (Volume, UDF_DEFAULT_LV_NUM));
  if (!NewFid) {
    return EFI_OUT_OF_RESOURCES;
  }

  NewFid->DescriptorTag.TagIdentifier      = 257;
  NewFid->DescriptorTag.DescriptorVersion  = 3;
  NewFid->DescriptorTag.Reserved           = 0;
  NewFid->DescriptorTag.TagSerialNumber    = 1;

  PartitionDesc = GetPdFromLongAd (Volume, &ParentFid->Icb);

  Lsn = GetLongAdLsn (Volume, &ParentFid->Icb) -
        PartitionDesc->PartitionStartingLocation;

  NewFid->DescriptorTag.TagLocation  = Lsn;
  NewFid->FileVersionNumber          = 1;
  NewFid->LengthOfImplementationUse  = 0;

  if (Attributes & EFI_FILE_DIRECTORY) {
    NewFid->FileCharacteristics |= DIRECTORY_FILE;
  } else {
    NewFid->FileCharacteristics &= ~(DIRECTORY_FILE | PARENT_FILE);
  }

  CompressionId = FID_COMPRESSION_ID (ParentFid);

  *((UINT8 *)NewFid->Data) = CompressionId;

  NewFid->LengthOfFileIdentifier = StrSize (FileName);
  if (CompressionId == 8) {
    NewFid->LengthOfFileIdentifier /= 2;
  }

  FileNamePointer = (UINT8 *)FileName;
  FidFileNamePointer = (UINT8 *)((UINTN)NewFid->Data +
                                 NewFid->LengthOfImplementationUse + 1);
  while (*FileNamePointer) {
    if (CompressionId == 16) {
      *FidFileNamePointer++ = *FileNamePointer++;
      *FidFileNamePointer++ = *FileNamePointer++;
    } else {
      *FidFileNamePointer++ = *FileNamePointer++;
      if (*FileNamePointer) {
        *FidFileNamePointer++ = *FileNamePointer;
      }
      FileNamePointer++;
    }
  }

  Status = GetUnallocatedSpaceLsn (
                               BlockIo,
                               DiskIo,
                               Volume,
                               ParentFid,
                               &NewFid->Icb.ExtentLocation.LogicalBlockNumber
                               );
  if (EFI_ERROR (Status)) {
    goto Error_Get_Unallocated_Space;
  }

  Count = Volume->FileEntrySize / LV_BLOCK_SIZE (Volume, UDF_DEFAULT_LV_NUM);
  while (Count--) {
    Status = GetUnallocatedSpaceLsn (
      BlockIo,
      DiskIo,
      Volume,
      ParentFid,
      (UINT32 *)&Lsn
      );
    if (EFI_ERROR (Status)) {
      goto Error_Get_Unallocated_Space;
    }
  }

  NewFid->Icb.ExtentLocation.PartitionReferenceNumber =
                                           PartitionDesc->PartitionNumber;
  NewFid->Icb.ExtentLength = LV_BLOCK_SIZE (Volume, UDF_DEFAULT_LV_NUM);

  *FileIdentifierDesc = NewFid;

  return EFI_SUCCESS;

Error_Get_Unallocated_Space:
  FreePool ((VOID *)NewFid);
  return Status;
}

//
// Create FID and FE descriptors for the new file.
//
EFI_STATUS
SetupNewFileDescriptors (
  IN   EFI_BLOCK_IO_PROTOCOL           *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL            *DiskIo,
  IN   UDF_VOLUME_INFO                 *Volume,
  IN   UDF_FILE_IDENTIFIER_DESCRIPTOR  *ParentFid,
  IN   CHAR16                          *FileName,
  IN   UINT64                          Attributes,
  OUT  UDF_FILE_IDENTIFIER_DESCRIPTOR  **FileIdentifierDesc,
  OUT  UDF_EXTENDED_FILE_ENTRY         **FileEntry
  )
{
  EFI_STATUS                      Status;
  UDF_FILE_IDENTIFIER_DESCRIPTOR  *NewFid;

  Status = SetupNewFileIdentifierDesc (
    BlockIo,
    DiskIo,
    Volume,
    ParentFid,
    FileName,
    Attributes,
    &NewFid
    );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = SetupNewFileEntry (
    BlockIo,
    DiskIo,
    Volume,
    NewFid,
    FileEntry
    );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  NewFid->DescriptorTag.DescriptorCRCLength = GetFidDescriptorLength (NewFid) -
                                              sizeof (UDF_DESCRIPTOR_TAG);
  NewFid->DescriptorTag.DescriptorCRC = CalculateCrc (
    (VOID *)((UINTN)NewFid +
             sizeof (UDF_DESCRIPTOR_TAG)),
    NewFid->DescriptorTag.DescriptorCRCLength
    );

  NewFid->DescriptorTag.TagChecksum = CalculateTagChecksum (NewFid);

  *FileIdentifierDesc = NewFid;
  return EFI_SUCCESS;
}

/**
  Create a file on an UDF volume.

  @param[in]      BlockIo       BlockIo interface.
  @param[in]      DiskIo        DiskIo interface.
  @param[in]      Volume        UDF volume information structure.
  @param[in]      File          File information structure.
  @param[in]      FileSize      Size of the file.
  @param[in out]  FilePosition  File position.
  @param[in out]  Buffer        File data.
  @param[in out]  BufferSize    Read size.

  @retval EFI_SUCCESS          File seeked and read.
  @retval EFI_UNSUPPORTED      Extended Allocation Descriptors not supported.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The file's recorded data was not read due to lack
                               of resources.

**/
EFI_STATUS
CreateFile (
  IN   EFI_BLOCK_IO_PROTOCOL           *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL            *DiskIo,
  IN   UDF_VOLUME_INFO                 *Volume,
  IN   CHAR16                          *FileName,
  IN   UINT64                          Attributes,
  IN   UDF_FILE_INFO                   *Root,
  IN   UDF_FILE_INFO                   *Parent,
  IN   UDF_LONG_ALLOCATION_DESCRIPTOR  *Icb,
  OUT  UDF_FILE_INFO                   *File
  )
{
  EFI_STATUS                       Status;
  UDF_PARTITION_DESCRIPTOR         *PartitionDesc;
  UDF_PARTITION_HEADER_DESCRIPTOR  *PartitionHdrDesc;
  UINT32                           ExtentLength;
  UINT32                           LogicalBlockSize;
  CHAR16                           TmpFileName[UDF_FILENAME_LENGTH] = { 0 };
  CHAR16                           *TmpFileNamePointer;
  UDF_FILE_INFO                    TmpFile;
  UDF_FE_RECORDING_FLAGS           RecordingFlags;
  UINT64                           Lsn;
  VOID                             *Data;
  UINTN                            Length;
  UDF_FILE_IDENTIFIER_DESCRIPTOR   *NewFid;
  UDF_EXTENDED_FILE_ENTRY          *NewFe;
  UDF_EXTENDED_FILE_ENTRY          *ExtendedFileEntry;
  UDF_FILE_ENTRY                   *FileEntry;

  StrCpy (TmpFileName, FileName);
  StrCat (TmpFileName, L"\\..");
  MangleFileName (TmpFileName);

  //
  // Get parent file
  //
  Status = FindFile (
                 BlockIo,
                 DiskIo,
                 Volume,
                 TmpFileName,
                 Root,
                 Parent,
                 Icb,
                 &TmpFile
                 );
  if (EFI_ERROR (Status)) {
    Print (L"CreateFile: %s does not exist\n", TmpFileName);
    return Status;
  }

  //
  // Make sure parent file is really a directory
  //
  if (!IS_FID_DIRECTORY_FILE (TmpFile.FileIdentifierDesc)) {
    return EFI_NOT_FOUND;
  }

  while ((TmpFileNamePointer = StrStr (FileName, L"\\"))) {
    FileName = TmpFileNamePointer + 1;
  }

  LogicalBlockSize = LV_BLOCK_SIZE (Volume, UDF_DEFAULT_LV_NUM);

  RecordingFlags = GET_FE_RECORDING_FLAGS (TmpFile.FileEntry);
  switch (RecordingFlags) {
    case INLINE_DATA:
      Lsn = GetLongAdLsn (Volume, &TmpFile.FileIdentifierDesc->Icb);

      Status = SetupNewFileDescriptors (BlockIo, DiskIo, Volume,
					TmpFile.FileIdentifierDesc,
					FileName, Attributes,
					&NewFid, &NewFe);
      if (EFI_ERROR (Status)) {
        Print (L"Failed to create FID and FE descs for new file\n");
        return Status;
      }

      GetFileEntryData (TmpFile.FileEntry, &Data, &Length);
      if (Length + GetFidDescriptorLength (NewFid) < LogicalBlockSize) {
        CopyMem (
          (VOID *)((UINTN)Data + Length),
          (VOID *)NewFid,
          GetFidDescriptorLength (NewFid)
          );
      }

      if (IS_EFE (TmpFile.FileEntry)) {
        ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)TmpFile.FileEntry;

	ExtendedFileEntry->ObjectSize += GetFidDescriptorLength (NewFid);
        ExtendedFileEntry->InformationLength += GetFidDescriptorLength (NewFid);
        ExtendedFileEntry->LengthOfAllocationDescriptors += GetFidDescriptorLength (NewFid);

        ExtendedFileEntry->DescriptorTag.DescriptorCRCLength = (sizeof (UDF_EXTENDED_FILE_ENTRY) +
                                                                ExtendedFileEntry->InformationLength) -
                                                         sizeof (UDF_DESCRIPTOR_TAG);
        ExtendedFileEntry->DescriptorTag.DescriptorCRC = CalculateCrc (
                                                (VOID *)((UINTN)ExtendedFileEntry +
                                                         sizeof (UDF_DESCRIPTOR_TAG)),
                                                ExtendedFileEntry->DescriptorTag.DescriptorCRCLength
                                                );
        ExtendedFileEntry->DescriptorTag.TagChecksum = CalculateTagChecksum (ExtendedFileEntry);
      } else if (IS_FE (TmpFile.FileEntry)) {
        FileEntry = (UDF_FILE_ENTRY *)TmpFile.FileEntry;

        FileEntry->InformationLength += GetFidDescriptorLength (NewFid);
        FileEntry->LengthOfAllocationDescriptors += GetFidDescriptorLength (NewFid);

        FileEntry->InformationLength += GetFidDescriptorLength (NewFid);
        FileEntry->LengthOfAllocationDescriptors += GetFidDescriptorLength (NewFid);

        FileEntry->DescriptorTag.DescriptorCRCLength = sizeof (UDF_FILE_ENTRY) -
                                                         sizeof (UDF_DESCRIPTOR_TAG);
        FileEntry->DescriptorTag.DescriptorCRC = CalculateCrc (
                                                (VOID *)((UINTN)FileEntry +
                                                         sizeof (UDF_DESCRIPTOR_TAG)),
                                                FileEntry->DescriptorTag.DescriptorCRCLength
                                                );
        FileEntry->DescriptorTag.TagChecksum = CalculateTagChecksum (FileEntry);
      }

      Status = DiskIo->WriteDisk (
        DiskIo,
        BlockIo->Media->MediaId,
        MultU64x32 (Lsn, LogicalBlockSize),
        Volume->FileEntrySize,
        TmpFile.FileEntry
        );
      if (EFI_ERROR (Status)) {
        return Status;
      }

      Lsn = GetLongAdLsn (Volume, &NewFid->Icb);

      Status = DiskIo->WriteDisk (
                                  DiskIo,
                                  BlockIo->Media->MediaId,
                                  MultU64x32 (Lsn, LogicalBlockSize),
                                  LV_BLOCK_SIZE (Volume, UDF_DEFAULT_LV_NUM),
                                  (VOID *)NewFe
                                  );
      if (EFI_ERROR (Status)) {
        return Status;
      }

      Status = BlockIo->FlushBlocks (BlockIo);

      File->FileEntry = NewFe;
      File->FileIdentifierDesc = NewFid;
      Print (L"---> done\n");
      break;
    case LONG_ADS_SEQUENCE:
      Print (L"long ads sequence\n");
      break;
    case SHORT_ADS_SEQUENCE:
      Print (L"short ads sequence\n");
      break;
    case EXTENDED_ADS_SEQUENCE:
      break;
  }

  PartitionDesc = GetPdFromLongAd (Volume, &Parent->FileIdentifierDesc->Icb);
  if (!PartitionDesc) {
    return EFI_VOLUME_CORRUPTED;
  }

  if (CompareMem (
        PartitionDesc->PartitionContents.Identifier,
        "+NSR03",
        5
        )
    ) {
    return EFI_UNSUPPORTED;
  }

  Print (L"Partition recorded as specified by ECMA-167 standard\n");

  PartitionHdrDesc =
    (UDF_PARTITION_HEADER_DESCRIPTOR *)&PartitionDesc->PartitionContentsUse[0];

  ExtentLength = GET_EXTENT_LENGTH (
                           SHORT_ADS_SEQUENCE,
                           &PartitionHdrDesc->UnallocatedSpaceBitmap
                           );
  if (ExtentLength) {
    Print (L"Partition has Unallocated Space Bitmap\n");
    Print (L"ExtentLength: %d - ExtentPosition: (%d; %d) - blocks: %d\n",
           ExtentLength,
           PartitionHdrDesc->UnallocatedSpaceBitmap.ExtentPosition,
           PartitionDesc->PartitionStartingLocation +
           PartitionHdrDesc->UnallocatedSpaceBitmap.ExtentPosition,
           ExtentLength / LogicalBlockSize);
  }

  ExtentLength = GET_EXTENT_LENGTH (
                           SHORT_ADS_SEQUENCE,
                           &PartitionHdrDesc->UnallocatedSpaceTable
                           );
  if (ExtentLength) {
    Print (L"Partition has Unallocated Space Table\n");
  }

  Print (L"CreatFile: out: %r\n", EFI_SUCCESS);
  return EFI_SUCCESS;
}
