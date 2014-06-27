/** @file
  UDF filesystem driver.

Copyright (c) 2014 Paulo Alcantara <pcacjr@zytor.com><BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "Udf.h"

STATIC
EFI_STATUS
FindAnchorVolumeDescriptorPointer (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UINT32                                  BlockSize,
  OUT VOID                                   **Buffer
  )
{
  EFI_STATUS                                 Status;
  UINT32                                     StartingLsn;
  UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER       *AnchorPoint;
  UDF_DESCRIPTOR_TAG                         *DescriptorTag;

  *Buffer = AllocatePool (BlockSize);
  if (!*Buffer) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  StartingLsn = FIRST_ANCHOR_POINT_LSN;

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       StartingLsn * BlockSize,
                       BlockSize,
                       *Buffer
                       );
  if (EFI_ERROR (Status)) {
    goto FreeExit;
  }

  Print (
    L"UdfDriverStart: Get AVDP\n"
    );

  AnchorPoint = (UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER *) *Buffer;

  DescriptorTag = &AnchorPoint->DescriptorTag;

  Print (
    L"UdfDriverStart: [AVDP] Tag Identifier: %d (0x%08x)\n",
    DescriptorTag->TagIdentifier,
    DescriptorTag->TagIdentifier
    );

  //
  // TODO: In case of failure, look for the other AVDPs at N or N - 256
  //
  if (!IS_ANCHOR_VOLUME_DESCRIPTOR_POINTER (AnchorPoint)) {
    Print (
      L"UdfDriverStart: [AVDP] Invalid Tag Identifier number\n"
      );

    Status = EFI_UNSUPPORTED;
    goto FreeExit;
  }

  return Status;

FreeExit:
  FreePool(*Buffer);
  *Buffer = NULL;

Exit:
  return Status;
}

STATIC
EFI_STATUS
StartMainVolumeDescriptorSequence (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UINT32                                  BlockSize,
  IN UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER    *AnchorPoint,
  OUT VOID                                   **Buffer0,
  OUT VOID                                   **Buffer1
  )
{
  EFI_STATUS                                 Status;
  UDF_EXTENT_AD                              *ExtentAd;
  UINT32                                     StartingLsn;
  UINT32                                     EndingLsn;
  VOID                                       *Buffer;
  UDF_PARTITION_DESCRIPTOR                   *PartitionDesc;
  UDF_LOGICAL_VOLUME_DESCRIPTOR              *LogicalVolDesc;
  UDF_DESCRIPTOR_TAG                         *DescriptorTag;
  UDF_LONG_ALLOCATION_DESCRIPTOR             *LongAd;

  ExtentAd = &AnchorPoint->MainVolumeDescriptorSequenceExtent;

  Print (
    L"UdfDriverStart: [MVDS] Extent location:   %d (0x%08x)\n",
    ExtentAd->ExtentLocation,
    ExtentAd->ExtentLocation
    );
  Print (
    L"UdfDriverStart: [MVDS] Extent length:     %d (0x%08x)\n",
    ExtentAd->ExtentLength,
    ExtentAd->ExtentLength
    );

  if (ExtentAd->ExtentLength / BlockSize < 16) {
    Print (
      L"UdfDriverStart: [MVDS] Invalid length of extents\n"
      );

    Status = EFI_UNSUPPORTED;
    goto Exit;
  }

  //
  // Start Main Volume Descriptor Sequence
  //
  // ==> Primary Volume Descriptor
  // ==> Implementation Use Volume Descriptor
  // ==> Partition Descriptor
  // ==> Logical Volume Descriptor
  // ==> Unallocated Space Descriptor
  // ==> Terminating Descriptor
  // ==> if any, Trailing Logical Sectors
  //
  Print (
    L"UdfDriverStart: Start Main Volume Descriptor Sequence\n"
    );

  *Buffer0 = AllocatePool (BlockSize);
  if (!*Buffer0) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  *Buffer1 = AllocatePool (BlockSize);
  if (!*Buffer1) {
    Status = EFI_OUT_OF_RESOURCES;
    goto FreeExit0;
  }

  Buffer = AllocatePool (BlockSize);
  if (!Buffer) {
    Status = EFI_OUT_OF_RESOURCES;
    goto FreeExit1;
  }

  StartingLsn = ExtentAd->ExtentLocation;
  EndingLsn   = StartingLsn + (ExtentAd->ExtentLength / BlockSize);

  while (StartingLsn <= EndingLsn) {
    Status = DiskIo->ReadDisk (
                         DiskIo,
			 BlockIo->Media->MediaId,
			 StartingLsn * BlockSize,
			 BlockSize,
			 Buffer
                         );
    if (EFI_ERROR (Status)) {
      goto FreeExit2;
    }

    if (IS_PARTITION_DESCRIPTOR (Buffer)) {
      Print (
	L"UdfDriverStart: Get PD\n"
	);

      CopyMem (*Buffer0, Buffer, BlockSize);
    } else if (IS_LOGICAL_VOLUME_DESCRIPTOR (Buffer)) {
      Print (
	L"UdfDriverStart: Get LVD\n"
	);

      CopyMem (*Buffer1, Buffer, BlockSize);
    }

    StartingLsn++;
  }

  PartitionDesc = (UDF_PARTITION_DESCRIPTOR *) *Buffer0;

  Print (
    L"UdfDriverStart: [PD] Partition Starting Location:   %d (0x%08x)\n",
    PartitionDesc->PartitionStartingLocation,
    PartitionDesc->PartitionStartingLocation
    );

  Print (
    L"UdfDriverStart: [PD] Partition Length:              %d (0x%08x)\n",
    PartitionDesc->PartitionLength,
    PartitionDesc->PartitionLength
    );

  LogicalVolDesc = (UDF_LOGICAL_VOLUME_DESCRIPTOR *) *Buffer1;

  DescriptorTag = &LogicalVolDesc->DescriptorTag;

  Print (
    L"UdfDriverStart: [LVD] Tag Identifier:               %d (0x%08x)\n",
    DescriptorTag->TagIdentifier,
    DescriptorTag->TagIdentifier
    );

  Print (
    L"UdfDriverStart: [LVD] Logical Block Size:           %d (0x%08x)\n",
    LogicalVolDesc->LogicalBlockSize,
    LogicalVolDesc->LogicalBlockSize
    );

  LongAd = &LogicalVolDesc->LogicalVolumeContentsUse;

  Print (
    L"UdfDriverStart: [LVD] LBN of FSD:                   %d (0x%08x)\n",
    LongAd->ExtentLocation.LogicalBlockNumber,
    LongAd->ExtentLocation.LogicalBlockNumber
    );

  Print (
    L"UdfDriverStart: [LVD] Extent length of FSD:         %d (0x%08x)\n",
    LongAd->ExtentLength,
    LongAd->ExtentLength
    );

  Print (
    L"UdfDriverStart: [LVD] Partition Ref # of FSD:       %d (0x%08x)\n",
    LongAd->ExtentLocation.PartitionReferenceNumber,
    LongAd->ExtentLocation.PartitionReferenceNumber
    );

  FreePool(Buffer);

  return Status;

FreeExit2:
  FreePool(Buffer);

FreeExit1:
  FreePool(*Buffer1);
  *Buffer1 = NULL;

FreeExit0:
  FreePool(*Buffer0);
  *Buffer0 = NULL;

Exit:
  return Status;
}

STATIC
EFI_STATUS
FindFileSetDescriptor (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UINT32                                  BlockSize,
  IN UDF_PARTITION_DESCRIPTOR                *PartitionDesc,
  IN UDF_LOGICAL_VOLUME_DESCRIPTOR           *LogicalVolDesc,
  OUT VOID                                   **Buffer
  )
{
  EFI_STATUS                                 Status;
  UDF_LONG_ALLOCATION_DESCRIPTOR             *LongAd;
  UINT32                                     StartingLsn;
  UDF_FILE_SET_DESCRIPTOR                    *FileSetDesc;
  UDF_DESCRIPTOR_TAG                         *DescriptorTag;

  *Buffer = AllocatePool (BlockSize);
  if (!*Buffer) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  LongAd = &LogicalVolDesc->LogicalVolumeContentsUse;

  StartingLsn = PartitionDesc->PartitionStartingLocation +
                   LongAd->ExtentLocation.LogicalBlockNumber;

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       StartingLsn * BlockSize,
                       BlockSize,
                       *Buffer
                       );
  if (EFI_ERROR (Status)) {
    goto FreeExit;
  }

  Print (L"UdfDriverStart: Get FSD\n");

  FileSetDesc = (UDF_FILE_SET_DESCRIPTOR *) *Buffer;

  DescriptorTag = &FileSetDesc->DescriptorTag;

  Print (
    L"UdfDriverStart: [FSD] Tag Identifier:                      %d (0x%08x)\n",
    DescriptorTag->TagIdentifier,
    DescriptorTag->TagIdentifier
    );

  if (!IS_FILE_SET_DESCRIPTOR (FileSetDesc)) {
      Print (
	L"UdfDriverStart: [FSD] Invalid Tag Identifier number\n"
	);
      goto FreeExit;
  }

  return Status;

FreeExit:
  FreePool(*Buffer);
  *Buffer = NULL;

Exit:
  return Status;
}

STATIC
EFI_STATUS
FindFileEntryRootDir (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UINT32                                  BlockSize,
  IN UDF_PARTITION_DESCRIPTOR                *PartitionDesc,
  IN UDF_FILE_SET_DESCRIPTOR                 *FileSetDesc,
  OUT VOID                                   **Buffer
  )
{
  EFI_STATUS                                 Status;
  UDF_LONG_ALLOCATION_DESCRIPTOR             *LongAd;
  UINT32                                     StartingLsn;
  UDF_FILE_ENTRY                             *FileEntry;
  UDF_DESCRIPTOR_TAG                         *DescriptorTag;

  *Buffer = AllocatePool (BlockSize);
  if (!*Buffer) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  LongAd = &FileSetDesc->RootDirectoryIcb;

  Print (
    L"UdfDriverStart: [FSD] LBN of Root directory:               %d (0x%08x)\n",
    LongAd->ExtentLocation.LogicalBlockNumber,
    LongAd->ExtentLocation.LogicalBlockNumber
    );

  Print (
    L"UdfDriverStart: [FSD] Partition Ref # of Root directory:   %d (0x%08x)\n",
    LongAd->ExtentLocation.PartitionReferenceNumber,
    LongAd->ExtentLocation.PartitionReferenceNumber
    );

  StartingLsn = PartitionDesc->PartitionStartingLocation +
                   LongAd->ExtentLocation.LogicalBlockNumber;

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       StartingLsn * BlockSize,
                       BlockSize,
                       *Buffer
                       );
  if (EFI_ERROR (Status)) {
    goto FreeExit;
  }

  Print (L"UdfDriverStart: Get File Entry (Root Directory)\n");

  FileEntry = (UDF_FILE_ENTRY *) *Buffer;

  DescriptorTag = &FileEntry->DescriptorTag;

  Print (
    L"UdfDriverStart: [ROOT] Tag Identifier:               %d (0x%08x)\n",
    DescriptorTag->TagIdentifier,
    DescriptorTag->TagIdentifier
    );

  if (!IS_FILE_ENTRY (FileEntry)) {
      Print (
	L"UdfDriverStart: [ROOT] Invalid Tag Identifier number\n"
	);

      Status = EFI_UNSUPPORTED;
      goto FreeExit;
  }

  //
  // Root Directory cannot be file, obivously. Check its file type.
  //
  if (!IS_FILE_ENTRY_DIRECTORY (FileEntry)) {
    Print (L"UdfDriverStart: [ROOT] Root Directory is NOT a directory!\n");

    Status = EFI_UNSUPPORTED;
    goto FreeExit;
  }

  return Status;

FreeExit:
  FreePool(*Buffer);
  *Buffer = NULL;

Exit:
  return Status;
}

STATIC
EFI_STATUS
FindFileIdentifierDescriptorRootDir (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UINT32                                  BlockSize,
  IN UDF_PARTITION_DESCRIPTOR                *PartitionDesc,
  IN UDF_FILE_SET_DESCRIPTOR                 *FileSetDesc,
  IN UDF_FILE_ENTRY                          *FileEntry,
  OUT VOID                                   **Buffer
  )
{
  EFI_STATUS                                 Status;
  UDF_LONG_ALLOCATION_DESCRIPTOR             *LongAd;
  UDF_ICB_TAG                                *IcbTag;
  UINT32                                     StartingLsn;
  UDF_FILE_IDENTIFIER_DESCRIPTOR             *FileIdentifierDesc;
  UDF_DESCRIPTOR_TAG                         *DescriptorTag;

  LongAd = &FileSetDesc->RootDirectoryIcb;

  IcbTag = &FileEntry->IcbTag;

  Print (
    L"UdfDriverStart: [ROOT] Prior Rec # of Direct ents:   %d (0x%08x)\n",
    IcbTag->PriorRecordNumberOfDirectEntries,
    IcbTag->PriorRecordNumberOfDirectEntries
    );

  Print (
    L"UdfDriverStart: [ROOT] Strategy Type:                %d (0x%08x)\n",
    IcbTag->StrategyType,
    IcbTag->StrategyType
    );

  Print (
    L"UdfDriverStart: [ROOT] Max # of ents:                %d (0x%08x)\n",
    IcbTag->MaximumNumberOfEntries,
    IcbTag->MaximumNumberOfEntries
    );

  Print (
    L"UdfDriverStart: [ROOT] File type:                    %d (0x%08x)\n",
    IcbTag->FileType,
    IcbTag->FileType
    );

  Print (
    L"UdfDriverStart: [ROOT] Flags:                        %d (0x%08x)\n",
    IcbTag->Flags,
    IcbTag->Flags
    );

  //
  // TODO: Handle strategy type of 4096 as well
  //
  if (IcbTag->StrategyType != 4) {
    Print (
      L"UdfDriverStart: [ROOT] Unhandled strategy type\n"
      );

    Status = EFI_UNSUPPORTED;
    goto Exit;
  }

  *Buffer = AllocatePool (BlockSize);
  if (!*Buffer) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  //
  // For ICB strategy type of 4, the File Identifier Descriptor of the Root
  // Directory immediately follows the File Entry (Root Directory).
  //
  StartingLsn = PartitionDesc->PartitionStartingLocation +
                   LongAd->ExtentLocation.LogicalBlockNumber + 1;

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       StartingLsn * BlockSize,
                       BlockSize,
                       *Buffer
                       );
  if (EFI_ERROR (Status)) {
    goto FreeExit;
  }

  Print (L"UdfDriverStart: [ROOT] Get FID\n");

  FileIdentifierDesc = (UDF_FILE_IDENTIFIER_DESCRIPTOR *) *Buffer;

  DescriptorTag = &FileIdentifierDesc->DescriptorTag;

  Print (
    L"UdfDriverStart: [ROOT-FID] Tag Identifier:           %d (0x%08x)\n",
    DescriptorTag->TagIdentifier,
    DescriptorTag->TagIdentifier
    );

  if (!IS_FILE_IDENTIFIER_DESCRIPTOR (FileIdentifierDesc)) {
      Print (
	L"UdfDriverStart: [ROOT-FID] Invalid tag identifier number\n"
	);

      Status = EFI_UNSUPPORTED;
      goto FreeExit;
  }

  return Status;

FreeExit:
  FreePool(*Buffer);
  *Buffer = NULL;

Exit:
  return Status;
}

EFI_STATUS
EFIAPI
FindRootDirectory (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UINT32                                  BlockSize,
  OUT UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER   **AnchorPoint,
  OUT UDF_PARTITION_DESCRIPTOR               **PartitionDesc,
  OUT UDF_LOGICAL_VOLUME_DESCRIPTOR          **LogicalVolDesc,
  OUT UDF_FILE_SET_DESCRIPTOR                **FileSetDesc,
  OUT UDF_FILE_ENTRY                         **FileEntry,
  OUT UDF_FILE_IDENTIFIER_DESCRIPTOR         **FileIdentifierDesc
  )
{
  EFI_STATUS                                 Status;

  Print (
    L"UdfDriverStart: Start reading UDF Volume and File Structure\n"
    );

  Status = FindAnchorVolumeDescriptorPointer (
                                          BlockIo,
					  DiskIo,
					  BlockSize,
					  (VOID **)AnchorPoint
                                          );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = StartMainVolumeDescriptorSequence (
                                          BlockIo,
					  DiskIo,
					  BlockSize,
					  *AnchorPoint,
					  (VOID **)PartitionDesc,
					  (VOID **)LogicalVolDesc
                                          );
  if (EFI_ERROR (Status)) {
    goto FreeExit0;
  }

  Status = FindFileSetDescriptor (
                              BlockIo,
			      DiskIo,
			      BlockSize,
			      *PartitionDesc,
			      *LogicalVolDesc,
			      (VOID **)FileSetDesc
                              );
  if (EFI_ERROR (Status)) {
    goto FreeExit1;
  }

  Status = FindFileEntryRootDir (
                             BlockIo,
			     DiskIo,
			     BlockSize,
			     *PartitionDesc,
			     *FileSetDesc,
			     (VOID **)FileEntry
                             );
  if (EFI_ERROR (Status)) {
    goto FreeExit2;
  }

  Status = FindFileIdentifierDescriptorRootDir (
                                            BlockIo,
					    DiskIo,
					    BlockSize,
					    *PartitionDesc,
					    *FileSetDesc,
					    *FileEntry,
					    (VOID **)FileIdentifierDesc
                                            );
  if (EFI_ERROR (Status)) {
    goto FreeExit3;
  }

  return Status;

FreeExit3:
  FreePool((VOID *)*FileEntry);
  *FileEntry = NULL;

FreeExit2:
  FreePool((VOID *)*FileSetDesc);
  *FileSetDesc = NULL;

FreeExit1:
  FreePool((VOID *)*PartitionDesc);
  FreePool((VOID *)*LogicalVolDesc);
  *PartitionDesc   = NULL;
  *LogicalVolDesc  = NULL;

FreeExit0:
  FreePool((VOID *)*AnchorPoint);
  *AnchorPoint = NULL;

Exit:
  return Status;
}
