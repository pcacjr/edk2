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

UDF_STANDARD_IDENTIFIER gUdfStandardIdentifiers[STANDARD_IDENTIFIERS_NO] = {
  { { 'B', 'E', 'A', '0', '1' } },
  { { 'N', 'S', 'R', '0', '2' } },
  { { 'N', 'S', 'R', '0', '3' } },
  { { 'T', 'E', 'A', '0', '1' } },
};

EFI_STATUS
FindAnchorVolumeDescriptorPointer (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  OUT UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER   *AnchorPoint
  )
{
  EFI_STATUS                                 Status;
  UINT32                                     BlockSize;

  BlockSize = BlockIo->Media->BlockSize;

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
		       MultU64x32 (FIRST_ANCHOR_POINT_LSN, BlockSize),
                       sizeof (UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER),
                       (VOID *)AnchorPoint
                       );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  //
  // TODO: In case of failure, look for the other AVDPs at N - 256 or N
  //
  if (!IS_AVDP (AnchorPoint)) {
    Status = EFI_VOLUME_CORRUPTED;
  }

Exit:
  return Status;
}

EFI_STATUS
StartMainVolumeDescriptorSequence (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER    *AnchorPoint,
  IN UDF_VOLUME_INFO                         *Volume
  )
{
  EFI_STATUS                                 Status;
  UINT32                                     BlockSize;
  UDF_EXTENT_AD                              *ExtentAd;
  UINT32                                     StartingLsn;
  UINT32                                     EndingLsn;
  VOID                                       *Buffer;
  UDF_LOGICAL_VOLUME_DESCRIPTOR              *LogicalVolDesc;
  UDF_PARTITION_DESCRIPTOR                   *PartitionDesc;
  UINTN                                      Index;

  BlockSize     = BlockIo->Media->BlockSize;
  ExtentAd      = &AnchorPoint->MainVolumeDescriptorSequenceExtent;
  StartingLsn   = ExtentAd->ExtentLocation;
  EndingLsn     = StartingLsn + DivU64x32 (
                                       ExtentAd->ExtentLength,
				       BlockSize
                                       );

  Volume->LogicalVolDescs =
    (UDF_LOGICAL_VOLUME_DESCRIPTOR **)AllocateZeroPool (ExtentAd->ExtentLength);
  if (!Volume->LogicalVolDescs) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ErrorAllocLvds;
  }

  Volume->PartitionDescs =
    (UDF_PARTITION_DESCRIPTOR **)AllocateZeroPool (ExtentAd->ExtentLength);
  if (!Volume->PartitionDescs) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ErrorAllocPds;
  }

  Buffer = AllocateZeroPool (BlockSize);
  if (!Buffer) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ErrorAllocBuf;
  }

  Volume->LogicalVolDescsNo   = 0;
  Volume->PartitionDescsNo    = 0;
  while (StartingLsn <= EndingLsn) {
    Status = DiskIo->ReadDisk (
                         DiskIo,
			 BlockIo->Media->MediaId,
			 MultU64x32 (StartingLsn, BlockSize),
			 BlockSize,
			 Buffer
                         );
    if (EFI_ERROR (Status)) {
      goto ErrorReadDiskBlk;
    }

    if (IS_TD (Buffer)) {
      break;
    }

    if (IS_LVD (Buffer)) {
      LogicalVolDesc =
	(UDF_LOGICAL_VOLUME_DESCRIPTOR *)
	AllocateZeroPool (sizeof (UDF_LOGICAL_VOLUME_DESCRIPTOR));
      if (!LogicalVolDesc) {
	Status = EFI_OUT_OF_RESOURCES;
	goto ErrorAllocLvd;
      }

      CopyMem (
	(VOID *)LogicalVolDesc,
	Buffer,
	sizeof (UDF_LOGICAL_VOLUME_DESCRIPTOR)
	);
      Volume->LogicalVolDescs[Volume->LogicalVolDescsNo] = LogicalVolDesc;
      Volume->LogicalVolDescsNo++;
    } else if (IS_PD (Buffer)) {
      PartitionDesc =
	(UDF_PARTITION_DESCRIPTOR *)
	AllocateZeroPool (sizeof (UDF_PARTITION_DESCRIPTOR));
      if (!PartitionDesc) {
	Status = EFI_OUT_OF_RESOURCES;
	goto ErrorAllocPd;
      }

      CopyMem (
	(VOID *)PartitionDesc,
	Buffer,
	sizeof (UDF_PARTITION_DESCRIPTOR)
	);
      Volume->PartitionDescs[Volume->PartitionDescsNo] = PartitionDesc;
      Volume->PartitionDescsNo++;
    }

    StartingLsn++;
  }

  FreePool (Buffer);
  return Status;

ErrorAllocPd:
ErrorAllocLvd:
  for (Index = 0; Index < Volume->PartitionDescsNo; Index++) {
    FreePool ((VOID *)Volume->PartitionDescs[Index]);
  }

  for (Index = 0; Index < Volume->LogicalVolDescsNo; Index++) {
    FreePool ((VOID *)Volume->LogicalVolDescs[Index]);
  }

ErrorReadDiskBlk:
  FreePool (Buffer);

ErrorAllocBuf:
  FreePool ((VOID *)Volume->PartitionDescs);

ErrorAllocPds:
  FreePool ((VOID *)Volume->LogicalVolDescs);

ErrorAllocLvds:
  return Status;
}

UDF_PARTITION_DESCRIPTOR *
GetPdFromLongAd (
  IN UDF_VOLUME_INFO                  *Volume,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR   *LongAd
  )
{
  UINTN                               Index;
  UDF_PARTITION_DESCRIPTOR            *PartitionDesc;

  for (Index = 0; Index < Volume->PartitionDescsNo; Index++) {
    PartitionDesc = Volume->PartitionDescs[Index];
    if (
      PartitionDesc->PartitionNumber ==
      LongAd->ExtentLocation.PartitionReferenceNumber
      )
    {
      return PartitionDesc;
    }
  }

  return NULL;
}

UINT64
GetLsnFromLongAd (
  IN UDF_VOLUME_INFO                  *Volume,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR   *LongAd
  )
{
  UDF_PARTITION_DESCRIPTOR            *PartitionDesc;

  if (!GET_EXTENT_LENGTH (LongAd)) {
    return 0;
  }

  PartitionDesc = GetPdFromLongAd (Volume, LongAd);
  if (!PartitionDesc) {
    return 0;
  }

  return (UINT64)(
	     PartitionDesc->PartitionStartingLocation +
	     LongAd->ExtentLocation.LogicalBlockNumber
             );
}

UINT64
GetLsnFromShortAd (
  IN UDF_PARTITION_DESCRIPTOR         *PartitionDesc,
  IN UDF_SHORT_ALLOCATION_DESCRIPTOR  *ShortAd
  )
{
  if (!ShortAd->ExtentLength) {
    return 0;
  }

  return (UINT64)(
              PartitionDesc->PartitionStartingLocation +
	      ShortAd->ExtentPosition
              );
}

EFI_STATUS
FindFileSetDescriptor (
  IN EFI_BLOCK_IO_PROTOCOL        *BlockIo,
  IN EFI_DISK_IO_PROTOCOL         *DiskIo,
  IN UDF_VOLUME_INFO              *Volume,
  IN UINTN                        LogicalVolDescNo,
  OUT UDF_FILE_SET_DESCRIPTOR     *FileSetDesc
  )
{
  EFI_STATUS                      Status;
  UINT64                          Lsn;
  UINT32                          BlockSize;
  UDF_LOGICAL_VOLUME_DESCRIPTOR   *LogicalVolDesc;

  LogicalVolDesc = Volume->LogicalVolDescs[LogicalVolDescNo];

  Lsn = GetLsnFromLongAd (
                   Volume,
		   &LogicalVolDesc->LogicalVolumeContentsUse
                   );
  if (!Lsn) {
    Status = EFI_VOLUME_CORRUPTED;
    goto ErrorGetLsn;
  }

  BlockSize = BlockIo->Media->BlockSize;

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       MultU64x32 (Lsn, BlockSize),
                       sizeof (UDF_FILE_SET_DESCRIPTOR),
                       (VOID *)FileSetDesc
                       );
  if (EFI_ERROR (Status)) {
    goto ErrorReadDiskBlk;
  }

  if (!IS_FSD (FileSetDesc)) {
    Status = EFI_VOLUME_CORRUPTED;
  }

ErrorReadDiskBlk:
ErrorGetLsn:
  return Status;
}

EFI_STATUS
GetFileSetDescriptors (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN OUT UDF_VOLUME_INFO                     *Volume
  )
{
  EFI_STATUS                                 Status;
  UINTN                                      Index;
  UDF_FILE_SET_DESCRIPTOR                    *FileSetDesc;
  UINTN                                      Count;

  Volume->FileSetDescs =
    (UDF_FILE_SET_DESCRIPTOR **)
    AllocateZeroPool (
                  Volume->LogicalVolDescsNo *
		  sizeof (UDF_FILE_SET_DESCRIPTOR)
                  );
  if (!Volume->FileSetDescs) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = EFI_SUCCESS;

  for (Index = 0; Index < Volume->LogicalVolDescsNo; Index++) {
    FileSetDesc = AllocateZeroPool (sizeof (UDF_FILE_SET_DESCRIPTOR));
    if (!FileSetDesc) {
      Status = EFI_OUT_OF_RESOURCES;
      goto ErrorAllocFsd;
    }

    Status = FindFileSetDescriptor (
                                BlockIo,
                                DiskIo,
				Volume,
				Index,
				FileSetDesc
                                );
    if (EFI_ERROR (Status)) {
      goto ErrorFindFsd;
    }

    Volume->FileSetDescs[Index] = FileSetDesc;
  }

  Volume->FileSetDescsNo = Volume->LogicalVolDescsNo;
  return Status;

ErrorFindFsd:
  Count = Index + 1;
  for (Index = 0; Index < Count; Index++) {
    FreePool ((VOID *)Volume->FileSetDescs[Index]);
  }

  FreePool ((VOID *)Volume->FileSetDescs);

ErrorAllocFsd:
  return Status;
}

EFI_STATUS
ReadVolumeFileStructure (
  IN EFI_BLOCK_IO_PROTOCOL               *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                *DiskIo,
  OUT UDF_VOLUME_INFO                    *Volume
  )
{
  EFI_STATUS                             Status;
  UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER   AnchorPoint;

  Status = FindAnchorVolumeDescriptorPointer (
                                          BlockIo,
					  DiskIo,
					  &AnchorPoint
                                          );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = StartMainVolumeDescriptorSequence (
                                          BlockIo,
                                          DiskIo,
					  &AnchorPoint,
					  Volume
                                          );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // TODO: handle FSDs in multiple LVDs
  //
  ASSERT (Volume->LogicalVolDescsNo == 1);
  ASSERT (Volume->PartitionDescsNo);

  return Status;
}

EFI_STATUS
ResolveSymlink (
  IN EFI_BLOCK_IO_PROTOCOL            *BlockIo,
  IN EFI_DISK_IO_PROTOCOL             *DiskIo,
  IN UDF_VOLUME_INFO                  *Volume,
  IN UDF_FILE_INFO                    *Parent,
  IN VOID                             *FileEntryData,
  OUT UDF_FILE_INFO                   *File
  )
{
  EFI_STATUS                          Status;
  UDF_EXTENDED_FILE_ENTRY             *ExtendedFileEntry;
  UDF_FILE_ENTRY                      *FileEntry;
  UINT64                              Length;
  UINT8                               *Data;
  UINT8                               *EndData;
  UDF_PATH_COMPONENT                  *PathComp;
  UINT8                               PathCompLength;
  CHAR16                              FileName[UDF_FILENAME_LENGTH];
  CHAR16                              *C;
  UINTN                               Index;
  UINT8                               CompressionId;
  UDF_FILE_INFO                       PreviousFile;

  Status = EFI_VOLUME_CORRUPTED;

  switch (GET_FE_RECORDING_FLAGS (FileEntryData)) {
    case INLINE_DATA:
      if (IS_EFE (FileEntryData)) {
	ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)FileEntryData;

	Length   = ExtendedFileEntry->InformationLength;
	Data     = (UINT8 *)(
                        &ExtendedFileEntry->Data[0] +
			ExtendedFileEntry->LengthOfExtendedAttributes
                        );
      } else if (IS_FE (FileEntryData)) {
	FileEntry = (UDF_FILE_ENTRY *)FileEntryData;

	Length   = FileEntry->InformationLength;
	Data     = (UINT8 *)(
                        &FileEntry->Data[0] +
			FileEntry->LengthOfExtendedAttributes
                        );
      }

      EndData = (UINT8 *)(Data + Length);
      CopyMem ((VOID *)&PreviousFile, (VOID *)Parent, sizeof (UDF_FILE_INFO));
      for (;;) {
	PathComp = (UDF_PATH_COMPONENT *)Data;
	PathCompLength = PathComp->LengthOfComponentIdentifier;
	switch (PathComp->ComponentType) {
	  case 3:
	    FileName[0] = '\0';
	    break;
	  case 5:
	    CompressionId = PathComp->ComponentIdentifier[0];
	    //
	    // Check for valid compression ID
	    //
	    if (CompressionId != 8 && CompressionId != 16) {
	      return EFI_VOLUME_CORRUPTED;
	    }

	    C = FileName;
	    for (Index = 1; Index < PathCompLength; Index++) {
	      if (CompressionId == 16) {
		*C = *(UINT8 *)(
                           (UINT8 *)PathComp->ComponentIdentifier +
			   Index
                           ) << 8;
		Index++;
	      } else {
		*C = 0;
	      }

	      if (Index < Length) {
		*C |= *(UINT8 *)(
                            (UINT8 *)PathComp->ComponentIdentifier +
			    Index
                            );
	      }

	      C++;
	    }

	    *C = L'\0';
	    break;
	  default:
	    Print (L"WARNING: unhandled Component Type\n");
	}

	if (!FileName[0]) {
	  Print (L"Go to parent dir...\n");
	  Status = InternalFindFile (
                         BlockIo,
			 DiskIo,
			 Volume,
			 L"..",
			 &PreviousFile,
			 NULL,
			 File
                         );
	  if (EFI_ERROR (Status)) {
	    goto ErrorFindFile;
	  }
	  Print (L"SUCCESS!!!\n");
	} else {
	  Print (L"Go to %s\n", FileName);
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
	    goto ErrorFindFile;
	  }
	  Print (L"SUCCESS!!!\n");
	}

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
	  FreePool ((VOID *)PreviousFile.FileIdentifierDesc);
	  FreePool (PreviousFile.FileEntry);
	}

	CopyMem ((VOID *)&PreviousFile, (VOID *)File, sizeof (UDF_FILE_INFO));
      }

      Status = EFI_SUCCESS;
      break;
    case LONG_ADS_SEQUENCE:
      Status = EFI_SUCCESS;
      break;
    case SHORT_ADS_SEQUENCE:
      Status = EFI_SUCCESS;
      break;
  }

ErrorFindFile:
  return Status;
}

EFI_STATUS
FindFileEntry (
  IN EFI_BLOCK_IO_PROTOCOL            *BlockIo,
  IN EFI_DISK_IO_PROTOCOL             *DiskIo,
  IN UDF_VOLUME_INFO                  *Volume,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR   *Icb,
  OUT VOID                            **FileEntry
  )
{
  EFI_STATUS                          Status;
  UINT64                              Lsn;
  UINT32                              BlockSize;

  Lsn = GetLsnFromLongAd (Volume, Icb);
  if (!Lsn) {
    return EFI_VOLUME_CORRUPTED;
  }

  BlockSize = BlockIo->Media->BlockSize;

  *FileEntry = AllocateZeroPool (BlockSize);
  if (!*FileEntry) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = DiskIo->ReadDisk (
                          DiskIo,
			  BlockIo->Media->MediaId,
			  MultU64x32 (Lsn, BlockSize),
			  BlockSize,
			  *FileEntry
                          );
  if (EFI_ERROR (Status)) {
    goto ErrorReadDiskBlk;
  }

  if (!IS_FE (*FileEntry) && !IS_EFE (*FileEntry)) {
    Status = EFI_VOLUME_CORRUPTED;
    goto ErrorInvalidFe;
  }

  return Status;

ErrorInvalidFe:
ErrorReadDiskBlk:
  FreePool (*FileEntry);
  return Status;
}

UINT64
GetFidDescriptorLength (
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR   *FileIdentifierDesc
  )
{
  return (UINT64)(
    38 + // Offset of Implementation Use field
    FileIdentifierDesc->LengthOfFileIdentifier +
    FileIdentifierDesc->LengthOfImplementationUse +
    (4 * ((FileIdentifierDesc->LengthOfFileIdentifier +
	   FileIdentifierDesc->LengthOfImplementationUse + 38 + 3) / 4) -
     (FileIdentifierDesc->LengthOfFileIdentifier +
      FileIdentifierDesc->LengthOfImplementationUse + 38))
    );
}

VOID
DuplicateFid (
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR    *FileIdentifierDesc,
  OUT UDF_FILE_IDENTIFIER_DESCRIPTOR   **NewFileIdentifierDesc
  )
{
  UINT64                               FidLength;

  FidLength = GetFidDescriptorLength (FileIdentifierDesc);

  *NewFileIdentifierDesc =
    (UDF_FILE_IDENTIFIER_DESCRIPTOR *)AllocateZeroPool (FidLength);
  ASSERT (*NewFileIdentifierDesc);
  CopyMem (
    (VOID *)*NewFileIdentifierDesc,
    (VOID *)FileIdentifierDesc,
    FidLength
    );
}

VOID
DuplicateFe (
  IN EFI_BLOCK_IO_PROTOCOL   *BlockIo,
  IN VOID                    *FileEntry,
  OUT VOID                   **NewFileEntry
  )
{
  UINT32                     BlockSize;

  BlockSize = BlockIo->Media->BlockSize;

  *NewFileEntry = AllocateZeroPool (BlockSize);
  ASSERT (*NewFileEntry);
  CopyMem (*NewFileEntry, FileEntry, BlockSize);
}

EFI_STATUS
GetFileNameFromFid (
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR   *FileIdentifierDesc,
  OUT CHAR16                          *FileName
  )
{
  UINT8                               *OstaCompressed;
  UINT8                               CompressionId;
  UINT8                               Length;
  UINTN                               Index;

  OstaCompressed =
    (UINT8 *)(
           (UINT8 *)&FileIdentifierDesc->Data[0] +
	   FileIdentifierDesc->LengthOfImplementationUse
           );
  CompressionId = OstaCompressed[0];
  //
  // Check for valid compression ID
  //
  if (CompressionId != 8 && CompressionId != 16) {
    return EFI_VOLUME_CORRUPTED;
  }

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

EFI_STATUS
InternalFindFile (
  IN EFI_BLOCK_IO_PROTOCOL            *BlockIo,
  IN EFI_DISK_IO_PROTOCOL             *DiskIo,
  IN UDF_VOLUME_INFO                  *Volume,
  IN CHAR16                           *FileName,
  IN UDF_FILE_INFO                    *Parent,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR   *Icb       OPTIONAL,
  OUT UDF_FILE_INFO                   *File
  )
{
  EFI_STATUS                          Status;
  UDF_FILE_IDENTIFIER_DESCRIPTOR      *FileIdentifierDesc;
  UDF_READ_DIRECTORY_INFO             ReadDirInfo;
  BOOLEAN                             Found;
  UINTN                               FileNameLength;
  CHAR16                              FoundFileName[UDF_FILENAME_LENGTH];
  VOID                                *CompareFileEntry;

  if (!IS_FE_DIRECTORY (Parent->FileEntry)) {
    Print (L"herereherhere\n");
    return EFI_NOT_FOUND;
  }

  if (!StrCmp (FileName, L".")) {
    DuplicateFe (BlockIo, Parent->FileEntry, &File->FileEntry);
    DuplicateFid (Parent->FileIdentifierDesc, &File->FileIdentifierDesc);
    return EFI_SUCCESS;
  }

  ZeroMem ((VOID *)&ReadDirInfo, sizeof (UDF_READ_DIRECTORY_INFO));

  Found            = FALSE;
  FileNameLength   = StrLen (FileName);
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
      if ((!StrCmp (FileName, L"..")) || (!StrCmp (FileName, L"\\"))) {
	Found = TRUE;
	break;
      }
    } else {
      if (FileNameLength != FileIdentifierDesc->LengthOfFileIdentifier - 1) {
	goto ReadNextEntry;
      }

      Status = GetFileNameFromFid (FileIdentifierDesc, FoundFileName);
      if (EFI_ERROR (Status)) {
	break;
      }

      if (!StrCmp (FileName, FoundFileName)) {
	Found = TRUE;
	break;
      }
    }

ReadNextEntry:
    FreePool ((VOID *)FileIdentifierDesc);
  }

  if (Found) {
    Status                     = EFI_SUCCESS;
    File->FileIdentifierDesc   = FileIdentifierDesc;

    //
    // If the requested file is root directory, so no FE needed
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
	goto ErrorFindFe;
      }

      if (CompareMem (
	    (VOID *)Parent->FileEntry,
	    (VOID *)CompareFileEntry,
	    BlockIo->Media->BlockSize
	    )
	 ) {
	File->FileIdentifierDesc   = FileIdentifierDesc;
	File->FileEntry            = CompareFileEntry;
	Status                     = EFI_SUCCESS;
      } else {
	FreePool ((VOID *)FileIdentifierDesc);
	FreePool ((VOID *)CompareFileEntry);
	Status = EFI_NOT_FOUND;
      }
    }
  }

  return Status;

ErrorFindFe:
  FreePool ((VOID *)FileIdentifierDesc);
  return Status;
}

EFI_STATUS
FindFile (
  IN EFI_BLOCK_IO_PROTOCOL            *BlockIo,
  IN EFI_DISK_IO_PROTOCOL             *DiskIo,
  IN UDF_VOLUME_INFO                  *Volume,
  IN CHAR16                           *FilePath,
  IN UDF_FILE_INFO                    *Root,
  IN UDF_FILE_INFO                    *Parent,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR   *Icb       OPTIONAL,
  OUT UDF_FILE_INFO                   *File
  )
{
  EFI_STATUS                          Status;
  CHAR16                              FileName[UDF_FILENAME_LENGTH];
  CHAR16                              *FileNamePointer;
  UDF_FILE_INFO                       PreviousFile;
  VOID                                *FileEntry;

  Status = EFI_NOT_FOUND;

  CopyMem ((VOID *)&PreviousFile, (VOID *)Parent, sizeof (UDF_FILE_INFO));
  ZeroMem ((VOID *)File, sizeof (UDF_FILE_INFO));
  while (*FilePath) {
    FileNamePointer = FileName;
    while (*FilePath && *FilePath != L'\\') {
      *FileNamePointer++ = *FilePath++;
    }

    Print (L"FindFile: FileName: %s\n", FileName);

    *FileNamePointer = L'\0';
    if (!*FileNamePointer) {
      Print (L"FileEntry: 0x%016lx\n", Parent->FileEntry);
      Print (L"FileEntry: 0x%016lx\n", Root ? Root->FileEntry : 0);
      Status = InternalFindFile (
                             BlockIo,
			     DiskIo,
			     Volume,
			     L"\\",
			     &PreviousFile,
			     Icb,
			     File
                             );
      ASSERT (!EFI_ERROR (Status));
    } else {
      Status = InternalFindFile (
                             BlockIo,
			     DiskIo,
			     Volume,
			     FileName,
			     &PreviousFile,
			     Icb,
			     File);
      ASSERT (!EFI_ERROR (Status));
    }

    if (EFI_ERROR (Status)) {
      return Status;
    }

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
      FreePool ((VOID *)PreviousFile.FileIdentifierDesc);
      FreePool (PreviousFile.FileEntry);
    }

    CopyMem ((VOID *)&PreviousFile, (VOID *)File, sizeof (UDF_FILE_INFO));

    if (*FilePath && *FilePath == L'\\') {
      FilePath++;
    }
  }

  return Status;
}

EFI_STATUS
GetShortAdFromAds (
  IN VOID                               *Data,
  IN OUT UINT64                         *Offset,
  IN UINT64                             Length,
  OUT UDF_SHORT_ALLOCATION_DESCRIPTOR   **FoundShortAd
  )
{
  UINT64                                AdLength;
  UDF_SHORT_ALLOCATION_DESCRIPTOR       *ShortAd;

  AdLength = sizeof (UDF_SHORT_ALLOCATION_DESCRIPTOR);

  for (;;) {
    if (*Offset >= Length) {
      return EFI_DEVICE_ERROR;
    }

    ShortAd = (UDF_SHORT_ALLOCATION_DESCRIPTOR *)(
                                             (UINT8 *)Data +
					     *Offset
                                             );
    if (ShortAd->ExtentLength) {
      break;
    }

    *Offset += AdLength;
  }

  *FoundShortAd = ShortAd;
  return EFI_SUCCESS;
}

EFI_STATUS
GetLongAdFromAds (
  IN VOID                              *Data,
  IN OUT UINT64                        *Offset,
  IN UINT64                            Length,
  OUT UDF_LONG_ALLOCATION_DESCRIPTOR   **FoundLongAd
  )
{
  UINT64                               AdLength;
  UDF_LONG_ALLOCATION_DESCRIPTOR       *LongAd;

  AdLength = sizeof (UDF_LONG_ALLOCATION_DESCRIPTOR);

  for (;;) {
    if (*Offset >= Length) {
      return EFI_DEVICE_ERROR;
    }

    LongAd = (UDF_LONG_ALLOCATION_DESCRIPTOR *)(
                                           (UINT8 *)Data +
					   *Offset
                                           );
    switch (GET_EXTENT_FLAGS (LongAd)) {
      case EXTENT_NOT_RECORDED_BUT_ALLOCATED:
      case EXTENT_NOT_RECORDED_NOT_ALLOCATED:
	*Offset += AdLength;
	continue;
      case EXTENT_IS_NEXT_EXTENT:
      case EXTENT_RECORDED_AND_ALLOCATED:
	goto Done;
    }
  }

Done:
  *FoundLongAd = LongAd;
  return EFI_SUCCESS;
}

VOID
GetAdsInformation (
  IN VOID                   *FileEntryData,
  OUT VOID                  **AdsData,
  OUT UINT64                *Length
  )
{
  UDF_EXTENDED_FILE_ENTRY   *ExtendedFileEntry;
  UDF_FILE_ENTRY            *FileEntry;

  if (IS_EFE (FileEntryData)) {
    ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)FileEntryData;

    *Length      = ExtendedFileEntry->LengthOfAllocationDescriptors;
    *AdsData     = (VOID *)((UINT8 *)(
                                 &ExtendedFileEntry->Data[0] +
				 ExtendedFileEntry->LengthOfExtendedAttributes
                                 )
                           );
  } else if (IS_FE (FileEntryData)) {
    FileEntry = (UDF_FILE_ENTRY *)FileEntryData;

    *Length      = FileEntry->LengthOfAllocationDescriptors;
    *AdsData     = (VOID *)((UINT8 *)(
                                 &FileEntry->Data[0] +
				 FileEntry->LengthOfExtendedAttributes
                                 )
                           );
  }
}

EFI_STATUS
GetFileSize (
  IN EFI_BLOCK_IO_PROTOCOL          *BlockIo,
  IN EFI_DISK_IO_PROTOCOL           *DiskIo,
  IN UDF_VOLUME_INFO                *Volume,
  IN UDF_FILE_INFO                  *File,
  OUT UINT64                        *Size
  )
{
  EFI_STATUS                        Status;
  VOID                              *FileEntryData;
  UINT64                            Length;
  UINT8                             *AdsData;
  UINT64                            Offset;
  UINT64                            AdLength;
  UDF_LONG_ALLOCATION_DESCRIPTOR    *LongAd;
  UINT64                            DataOffset;
  BOOLEAN                           DoFreeAed;
  UDF_SHORT_ALLOCATION_DESCRIPTOR   *ShortAd;

  Status          = EFI_VOLUME_CORRUPTED;
  FileEntryData   = File->FileEntry;
  *Size           = 0;
  DoFreeAed       = FALSE;

  switch (GET_FE_RECORDING_FLAGS (FileEntryData)) {
    case INLINE_DATA:
      if (IS_EFE (FileEntryData)) {
	*Size = ((UDF_EXTENDED_FILE_ENTRY *)FileEntryData)->InformationLength;
      } else if (IS_FE (FileEntryData)) {
	*Size = ((UDF_FILE_ENTRY *)FileEntryData)->InformationLength;
      }

      Status = EFI_SUCCESS;
      break;
    case LONG_ADS_SEQUENCE:
      GetAdsInformation (FileEntryData, (VOID **)&AdsData, &Length);

      Offset     = 0;
      AdLength   = sizeof (UDF_LONG_ALLOCATION_DESCRIPTOR);
      for (;;) {
	Status = GetLongAdFromAds (AdsData, &Offset, Length, &LongAd);
	if (Status == EFI_DEVICE_ERROR) {
	  Status = EFI_SUCCESS;
	  break;
	}

	if (GET_EXTENT_FLAGS (LongAd) == EXTENT_IS_NEXT_EXTENT) {
	  Status = GetAedAdsData (
                              BlockIo,
			      DiskIo,
			      Volume,
			      LongAd,
			      &DataOffset,
			      &Length
                              );
	  if (EFI_ERROR (Status)) {
	    goto ErrorGetAed;
	  }

	  if (!DoFreeAed) {
	    DoFreeAed = TRUE;
	  } else {
	    FreePool ((VOID *)AdsData);
	  }

	  AdsData = (UINT8 *)AllocatePool (Length);
	  if (!AdsData) {
	    DoFreeAed = FALSE;
	    Status = EFI_OUT_OF_RESOURCES;
	    goto ErrorAllocAdsData;
	  }

          Status = DiskIo->ReadDisk (
                                  DiskIo,
				  BlockIo->Media->MediaId,
				  DataOffset,
				  Length,
				  (VOID *)AdsData
	                          );
	  if (EFI_ERROR (Status)) {
	    goto ErrorReadDiskBlk;
	  }

	  Offset = 0;
	} else {
	  *Size    += GET_EXTENT_LENGTH (LongAd);
	  Offset   += AdLength;
	}
      }

      Status = EFI_SUCCESS;
      break;
    case SHORT_ADS_SEQUENCE:
      GetAdsInformation (FileEntryData, (VOID **)&AdsData, &Length);

      AdLength = sizeof (UDF_SHORT_ALLOCATION_DESCRIPTOR);
      for (Offset = 0; Offset < Length; Offset += AdLength) {
	ShortAd = (UDF_SHORT_ALLOCATION_DESCRIPTOR *)(AdsData + Offset);
	*Size += ShortAd->ExtentLength;
      }

      Status = EFI_SUCCESS;
      break;
  }

ErrorReadDiskBlk:
ErrorAllocAdsData:
ErrorGetAed:
  if (DoFreeAed) {
    FreePool ((VOID *)AdsData);
  }

  return Status;
}

EFI_STATUS
ReadFileData (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_VOLUME_INFO                         *Volume,
  IN UDF_FILE_INFO                           *File,
  IN UINT64                                  FileSize,
  IN OUT UINT64                              *CurrentFilePosition,
  IN OUT VOID                                *Buffer,
  IN OUT UINT64                              *BufferSize
  )
{
  EFI_STATUS                                 Status;
  VOID                                       *FileEntryData;
  UINT32                                     BlockSize;
  UDF_EXTENDED_FILE_ENTRY                    *ExtendedFileEntry;
  UDF_FILE_ENTRY                             *FileEntry;
  UINT64                                     Offset;
  UINT64                                     Length;
  UINT8                                      *AdsData;
  UINT64                                     AdLength;
  UDF_LONG_ALLOCATION_DESCRIPTOR             *LongAd;
  UDF_SHORT_ALLOCATION_DESCRIPTOR            *ShortAd;
  UDF_LONG_ALLOCATION_DESCRIPTOR             *ParentIcb;
  UINT64                                     FilePosition;
  INT64                                      ExtStartOffset;
  UINT64                                     ExtLen;
  UINT64                                     BytesLeft;
  UINT64                                     BufferOffset;
  UINT64                                     Lsn;
  UINT64                                     DataOffset;
  BOOLEAN                                    DoFreeAed;

  Status          = EFI_VOLUME_CORRUPTED;
  FileEntryData   = File->FileEntry;
  BlockSize       = BlockIo->Media->BlockSize;
  DoFreeAed       = FALSE;

  switch (GET_FE_RECORDING_FLAGS (FileEntryData)) {
    case INLINE_DATA:
      if (*BufferSize > FileSize - *CurrentFilePosition) {
	*BufferSize = FileSize - *CurrentFilePosition;
      }

      if (IS_EFE (FileEntryData)) {
	ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)FileEntryData;
	CopyMem (
	  Buffer,
	  (VOID *)((UINT8 *)(
                        &ExtendedFileEntry->Data[0] +
			ExtendedFileEntry->LengthOfExtendedAttributes +
			*CurrentFilePosition
		     )),
	  *BufferSize
          );
      } else if (IS_FE (FileEntryData)) {
	FileEntry = (UDF_FILE_ENTRY *)FileEntryData;
	CopyMem (
	  Buffer,
	  (VOID *)((UINT8 *)(
                        &FileEntry->Data[0] +
			FileEntry->LengthOfExtendedAttributes +
			*CurrentFilePosition
		     )),
	  *BufferSize
          );
      }

      *CurrentFilePosition   += *BufferSize;
      Status                 = EFI_SUCCESS;
      break;
    case LONG_ADS_SEQUENCE:
      GetAdsInformation (FileEntryData, (VOID **)&AdsData, &Length);

      FilePosition     = 0;
      AdLength         = sizeof (UDF_LONG_ALLOCATION_DESCRIPTOR);
      ExtStartOffset   = 0;
      ExtLen           = 0;
      Offset           = 0;
      for (;;) {
	Status = GetLongAdFromAds (AdsData, &Offset, Length, &LongAd);
	if (EFI_ERROR (Status)) {
	  goto ErrorGetLongAd;
	}

	if (GET_EXTENT_FLAGS (LongAd) == EXTENT_IS_NEXT_EXTENT) {
	  Status = GetAedAdsData (
                              BlockIo,
			      DiskIo,
			      Volume,
			      LongAd,
			      &DataOffset,
			      &Length
                              );
	  if (EFI_ERROR (Status)) {
	    goto ErrorGetAed;
	  }

	  if (!DoFreeAed) {
	    DoFreeAed = TRUE;
	  } else {
	    FreePool ((VOID *)AdsData);
	  }

	  AdsData = (UINT8 *)AllocatePool (Length);
	  if (!AdsData) {
	    DoFreeAed = FALSE;
	    Status = EFI_OUT_OF_RESOURCES;
	    goto ErrorAllocAdsData;
	  }

          Status = DiskIo->ReadDisk (
                                  DiskIo,
				  BlockIo->Media->MediaId,
				  DataOffset,
				  Length,
				  (VOID *)AdsData
	                          );
	  if (EFI_ERROR (Status)) {
	    goto ErrorReadDiskBlk;
	  }

	  Offset = 0;
	} else if (GET_EXTENT_FLAGS (LongAd) == EXTENT_RECORDED_AND_ALLOCATED) {
	  if (FilePosition == *CurrentFilePosition) {
	    break;
	  }

	  if (FilePosition + GET_EXTENT_LENGTH (LongAd) == *CurrentFilePosition) {
	    Offset += AdLength;
	    break;
	  }

	  if (FilePosition + GET_EXTENT_LENGTH (LongAd) >
	      *CurrentFilePosition) {
	    ExtStartOffset =
	      GET_EXTENT_LENGTH (LongAd) - ((FilePosition +
					     GET_EXTENT_LENGTH (LongAd)) -
					    *CurrentFilePosition);
	    if (ExtStartOffset < 0) {
	      ExtStartOffset = -(ExtStartOffset);
	    }

	    ExtLen = ExtStartOffset;
	    break;
	  }

	  FilePosition   += GET_EXTENT_LENGTH (LongAd);
	  Offset         += AdLength;
	}
      }

      BufferOffset   = 0;
      BytesLeft      = *BufferSize;
      while (BytesLeft) {
	Status = GetLongAdFromAds (AdsData, &Offset, Length, &LongAd);
	if (EFI_ERROR (Status)) {
	  goto ErrorGetLongAd;
	}

	if (GET_EXTENT_FLAGS (LongAd) == EXTENT_IS_NEXT_EXTENT) {
	  Status = GetAedAdsData (
                              BlockIo,
			      DiskIo,
			      Volume,
			      LongAd,
			      &DataOffset,
			      &Length
                              );
	  if (EFI_ERROR (Status)) {
	    goto ErrorGetAed;
	  }

	  if (!DoFreeAed) {
	    DoFreeAed = TRUE;
	  } else {
	    FreePool ((VOID *)AdsData);
	  }

	  AdsData = (UINT8 *)AllocatePool (Length);
	  if (!AdsData) {
	    DoFreeAed = FALSE;
	    Status = EFI_OUT_OF_RESOURCES;
	    goto ErrorAllocAdsData;
	  }

          Status = DiskIo->ReadDisk (
                                  DiskIo,
				  BlockIo->Media->MediaId,
				  DataOffset,
				  Length,
				  (VOID *)AdsData
	                          );
	  if (EFI_ERROR (Status)) {
	    goto ErrorReadDiskBlk;
	  }

	  Offset = 0;
	} else if (GET_EXTENT_FLAGS (LongAd) == EXTENT_RECORDED_AND_ALLOCATED) {
	  while (BytesLeft) {
	    if (GET_EXTENT_LENGTH (LongAd) - ExtLen > BytesLeft) {
	      ExtLen = BytesLeft;
	    } else {
	      ExtLen = GET_EXTENT_LENGTH (LongAd) - ExtLen;
	    }

	    Lsn = GetLsnFromLongAd (Volume, LongAd);
	    if (!Lsn) {
	      Status = EFI_VOLUME_CORRUPTED;
	      goto ErrorGetLsnFromLongAd;
	    }

	    DataOffset = (UINT64)(MultU64x32 (Lsn, BlockSize) + ExtStartOffset);

            Status = DiskIo->ReadDisk (
                                    DiskIo,
				    BlockIo->Media->MediaId,
				    DataOffset,
				    ExtLen,
				    (VOID *)((UINT8 *)Buffer + BufferOffset)
			            );
	    if (EFI_ERROR (Status)) {
	      goto ErrorReadDiskBlk;
	    }

	    BytesLeft      -= ExtLen;
	    BufferOffset   += ExtLen;

	    *CurrentFilePosition += ExtLen;
	    if (*CurrentFilePosition >= FileSize) {
	      //
	      // Avoid reading past the EOF
	      //
	      *CurrentFilePosition = FileSize;
	      goto ReadDone;
	    }

	    ExtStartOffset   = 0;
	    ExtLen           = 0;
	  }

	  Offset += AdLength;
	}
      }

ReadDone:
      *BufferSize   = BufferOffset;
      Status        = EFI_SUCCESS;
      break;
    case SHORT_ADS_SEQUENCE:
      GetAdsInformation (FileEntryData, (VOID **)&AdsData, &Length);

      ParentIcb        = &File->FileIdentifierDesc->Icb;
      FilePosition     = 0;
      AdLength         = sizeof (UDF_SHORT_ALLOCATION_DESCRIPTOR);
      ExtStartOffset   = 0;
      ExtLen           = 0;
      for (;;) {
	Status = GetShortAdFromAds (AdsData, &Offset, Length, &ShortAd);
	if (EFI_ERROR (Status)) {
	  goto ErrorGetShortAd;
	}

	if (FilePosition == *CurrentFilePosition) {
	  break;
	}

	if (FilePosition + ShortAd->ExtentLength == *CurrentFilePosition) {
	  Offset += AdLength;
	  break;
	}

	if (FilePosition + ShortAd->ExtentLength > *CurrentFilePosition) {
	  ExtStartOffset =
	    ShortAd->ExtentLength - ((FilePosition +
				      ShortAd->ExtentLength) -
				     *CurrentFilePosition);
	  if (ExtStartOffset < 0) {
	    ExtStartOffset = -(ExtStartOffset);
	  }

	  ExtLen = ExtStartOffset;
	  break;
	}

	FilePosition += ShortAd->ExtentLength;
	Offset       += AdLength;
      }

      BufferOffset   = 0;
      BytesLeft      = *BufferSize;
      while (BytesLeft) {
	Status = GetShortAdFromAds (AdsData, &Offset, Length, &ShortAd);
	if (EFI_ERROR (Status)) {
	  goto ErrorGetShortAd;
	}

	while (BytesLeft) {
	  if (ShortAd->ExtentLength - ExtLen > BytesLeft) {
	    ExtLen = BytesLeft;
	  } else {
	    ExtLen = ShortAd->ExtentLength - ExtLen;
	  }

          Lsn = GetLsnFromShortAd (
                              GetPdFromLongAd (Volume, ParentIcb),
			      ShortAd
                              );
	  if (!Lsn) {
	    Status = EFI_VOLUME_CORRUPTED;
	    goto ErrorGetLsnFromShortAd;
	  }

	  DataOffset = (UINT64)(MultU64x32 (Lsn, BlockSize) + ExtStartOffset);

          Status = DiskIo->ReadDisk (
                                  DiskIo,
				  BlockIo->Media->MediaId,
				  DataOffset,
				  ExtLen,
				  (VOID *)((UINT8 *)Buffer + BufferOffset)
			          );
	  if (EFI_ERROR (Status)) {
	    Status = EFI_DEVICE_ERROR;
	    goto ErrorReadDiskBlk;
	  }

	  BytesLeft      -= ExtLen;
	  BufferOffset   += ExtLen;

	  *CurrentFilePosition += ExtLen;
	  if (*CurrentFilePosition >= FileSize) {
	    //
	    // Avoid reading past the EOF
	    //
	    *CurrentFilePosition = FileSize;
	    goto ReadDone2;
	  }

	  ExtStartOffset   = 0;
	  ExtLen           = 0;
	}
      }

ReadDone2:
      *BufferSize   = BufferOffset;
      Status        = EFI_SUCCESS;
      break;
  }

ErrorGetLsnFromShortAd:
ErrorGetShortAd:
ErrorGetLsnFromLongAd:
ErrorReadDiskBlk:
ErrorAllocAdsData:
ErrorGetAed:
ErrorGetLongAd:
  if (DoFreeAed) {
    FreePool ((VOID *)AdsData);
  }

  return Status;
}

EFI_STATUS
GetAedAdsData (
  IN EFI_BLOCK_IO_PROTOCOL            *BlockIo,
  IN EFI_DISK_IO_PROTOCOL             *DiskIo,
  IN UDF_VOLUME_INFO                  *Volume,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR   *LongAd,
  OUT UINT64                          *Offset,
  OUT UINT64                          *Length
  )
{
  EFI_STATUS                          Status;
  UINT32                              ExtentLength;
  VOID                                *Data;
  UINT64                              Lsn;
  UINT32                              BlockSize;
  UDF_ALLOCATION_EXTENT_DESCRIPTOR    *AllocExtDesc;

  Data           = NULL;
  ExtentLength   = GET_EXTENT_LENGTH (LongAd);

  Data = AllocatePool (ExtentLength);
  if (!Data) {
    return EFI_OUT_OF_RESOURCES;
  }

  Lsn = GetLsnFromLongAd (Volume, LongAd);
  if (!Lsn) {
    Status = EFI_VOLUME_CORRUPTED;
    goto Exit;
  }

  BlockSize = BlockIo->Media->BlockSize;

  Status = DiskIo->ReadDisk (
                          DiskIo,
			  BlockIo->Media->MediaId,
			  MultU64x32 (Lsn, BlockSize),
			  ExtentLength,
                          Data
                          );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  AllocExtDesc = (UDF_ALLOCATION_EXTENT_DESCRIPTOR *)Data;
  if (!IS_AED (AllocExtDesc)) {
    Status = EFI_VOLUME_CORRUPTED;
    goto Exit;
  }

  *Offset   = (UINT64)(
                   MultU64x32 (Lsn, BlockSize) +
                   sizeof (UDF_ALLOCATION_EXTENT_DESCRIPTOR)
                   );
  *Length   = AllocExtDesc->LengthOfAllocationDescriptors;

Exit:
  if (Data) {
    FreePool (Data);
  }

  return Status;
}

EFI_STATUS
ReadDirectoryEntry (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_VOLUME_INFO                         *Volume,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR          *ParentIcb,
  IN VOID                                    *FileEntryData,
  IN OUT UDF_READ_DIRECTORY_INFO             *ReadDirInfo,
  OUT UDF_FILE_IDENTIFIER_DESCRIPTOR         **FoundFileIdentifierDesc
  )
{
  EFI_STATUS                                 Status;
  UINT32                                     BlockSize;
  UDF_FILE_IDENTIFIER_DESCRIPTOR             *FileIdentifierDesc;
  UDF_EXTENDED_FILE_ENTRY                    *ExtendedFileEntry;
  UDF_FILE_ENTRY                             *FileEntry;
  UINT64                                     Length;
  UINT8                                      *Data;
  UINT8                                      *AdsData;
  UINT64                                     AdLength;
  UDF_LONG_ALLOCATION_DESCRIPTOR             *LongAd;
  UDF_SHORT_ALLOCATION_DESCRIPTOR            *ShortAd;
  VOID                                       *ExtentData;
  UINT64                                     ExtentLength;
  UINT64                                     RemBytes;
  UINT64                                     Lsn;
  UINT8                                      *Buffer;
  BOOLEAN                                    DoFreeAed;
  UINT64                                     FidLength;
  UINT64                                     RemainingFidBytes;
  UINT64                                     LastFidOffset;

  Status                     = EFI_VOLUME_CORRUPTED;
  BlockSize                  = BlockIo->Media->BlockSize;
  ExtentData                 = NULL;
  DoFreeAed                  = FALSE;
  Buffer                     = NULL;
  *FoundFileIdentifierDesc   = NULL;
  RemainingFidBytes          = 0;

  switch (GET_FE_RECORDING_FLAGS (FileEntryData)) {
    case INLINE_DATA:
      if (IS_EFE (FileEntryData)) {
	ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)FileEntryData;

	Length   = ExtendedFileEntry->InformationLength;
	Data     = (UINT8 *)(
                        &ExtendedFileEntry->Data[0] +
			ExtendedFileEntry->LengthOfExtendedAttributes
                        );
      } else if (IS_FE (FileEntryData)) {
	FileEntry = (UDF_FILE_ENTRY *)FileEntryData;

	Length   = FileEntry->InformationLength;
	Data     = (UINT8 *)(
                        &FileEntry->Data[0] +
			FileEntry->LengthOfExtendedAttributes
                        );
      }

      do {
	if (ReadDirInfo->FidOffset >= Length) {
	  return EFI_DEVICE_ERROR;
	}

	FileIdentifierDesc = GET_FID_FROM_ADS (
                                           Data,
					   ReadDirInfo->FidOffset
                                           );
	ReadDirInfo->FidOffset += GetFidDescriptorLength (FileIdentifierDesc);
      } while (IS_FID_DELETED_FILE (FileIdentifierDesc));

      Status = EFI_SUCCESS;
      break;
    case LONG_ADS_SEQUENCE:
      if (!ReadDirInfo->AedAdsLength) {
	GetAdsInformation (FileEntryData, (VOID **)&AdsData, &Length);
      } else {
HandleIndirectExt:
	Length = ReadDirInfo->AedAdsLength;

	if (!DoFreeAed) {
	  DoFreeAed = TRUE;
	} else {
	  FreePool ((VOID *)AdsData);
	}

	AdsData = (UINT8 *)AllocatePool (Length);
	if (!AdsData) {
	  DoFreeAed = FALSE;
	  Status = EFI_OUT_OF_RESOURCES;
	  goto ErrorAllocAdsData;
	}

        Status = DiskIo->ReadDisk (
                                DiskIo,
			        BlockIo->Media->MediaId,
			        ReadDirInfo->AedAdsOffset,
			        Length,
			        (VOID *)AdsData
	                        );
	if (EFI_ERROR (Status)) {
	  goto ErrorReadDiskBlk;
	}
      }

      AdLength = sizeof (UDF_LONG_ALLOCATION_DESCRIPTOR);

ExcludeDeletedFile:
ReadRemainingFidBytes:
      for (;;) {
	Status = GetLongAdFromAds (
                              AdsData,
			      &ReadDirInfo->AdOffset,
			      Length,
			      &LongAd
                              );
	if (EFI_ERROR (Status)) {
	  goto ErrorGetLongAd;
	}

	if (GET_EXTENT_FLAGS (LongAd) == EXTENT_IS_NEXT_EXTENT) {
	  Status = GetAedAdsData (
                              BlockIo,
			      DiskIo,
			      Volume,
			      LongAd,
			      &ReadDirInfo->AedAdsOffset,
			      &ReadDirInfo->AedAdsLength
                              );
	  if (EFI_ERROR (Status)) {
	    goto ErrorGetAed;
	  }

	  ReadDirInfo->AdOffset = 0;
	  goto HandleIndirectExt;
	}

	ExtentLength = GET_EXTENT_LENGTH (LongAd);
	if (ReadDirInfo->FidOffset < ExtentLength) {
	  break;
	}

	ReadDirInfo->AdOffset    += AdLength;
	ReadDirInfo->FidOffset   -= ExtentLength;
      }

      Lsn = GetLsnFromLongAd (Volume, LongAd);
      if (!Lsn) {
	Status = EFI_VOLUME_CORRUPTED;
	goto ErrorGetLsnFromLongAd;
      }

      ExtentData = AllocateZeroPool (ExtentLength);
      if (!ExtentData) {
	Status = EFI_OUT_OF_RESOURCES;
	goto ErrorAllocExtData;
      }

      Status = DiskIo->ReadDisk (
                              DiskIo,
			      BlockIo->Media->MediaId,
			      MultU64x32 (Lsn, BlockSize),
			      ExtentLength,
			      ExtentData
	                      );
      if (EFI_ERROR (Status)) {
	goto ErrorReadDiskBlk;
      }

      RemBytes = ExtentLength - ReadDirInfo->FidOffset;
      if (RemBytes < sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)) {
	ReadDirInfo->AdOffset += AdLength;
	Status = GetLongAdFromAds (
                              AdsData,
			      &ReadDirInfo->AdOffset,
			      Length,
			      &LongAd
                              );
	if (EFI_ERROR (Status)) {
	  goto ErrorGetLongAd;
	}

	Buffer = AllocatePool (RemBytes);
	if (!Buffer) {
	  Status = EFI_OUT_OF_RESOURCES;
	  goto ErrorAllocBuf;
	}

	CopyMem (
	  Buffer,
	  (VOID *)((UINT8 *)(ExtentData + (ExtentLength - RemBytes))),
	  RemBytes
	  );

	FreePool (ExtentData);
	ExtentData = NULL;

	if (GET_EXTENT_FLAGS (LongAd) == EXTENT_IS_NEXT_EXTENT) {
	  Status = GetAedAdsData (
                              BlockIo,
			      DiskIo,
			      Volume,
			      LongAd,
			      &ReadDirInfo->AedAdsOffset,
			      &ReadDirInfo->AedAdsLength
                              );
	  if (EFI_ERROR (Status)) {
	    goto ErrorGetAed;
	  }

	  Length = ReadDirInfo->AedAdsLength;

	  if (!DoFreeAed) {
	    DoFreeAed = TRUE;
	  } else {
	    FreePool ((VOID *)AdsData);
	  }

	  AdsData = (UINT8 *)AllocatePool (Length);
	  if (!AdsData) {
	    DoFreeAed = FALSE;
	    Status = EFI_OUT_OF_RESOURCES;
	    goto ErrorAllocAdsData;
	  }

	  Status = DiskIo->ReadDisk (
                                  DiskIo,
				  BlockIo->Media->MediaId,
				  ReadDirInfo->AedAdsOffset,
				  Length,
				  (VOID *)AdsData
	                          );
	  if (EFI_ERROR (Status)) {
	    goto ErrorReadDiskBlk;
	  }

	  ReadDirInfo->AdOffset = 0;
	  Status = GetLongAdFromAds (
                                AdsData,
				&ReadDirInfo->AdOffset,
				Length,
				&LongAd
                                );
	  if (EFI_ERROR (Status)) {
	    goto ErrorGetLongAd;
	  }
	}

	ExtentLength = GET_EXTENT_LENGTH (LongAd);

        Lsn = GetLsnFromLongAd (Volume, LongAd);
	if (!Lsn) {
	  Status = EFI_VOLUME_CORRUPTED;
	  goto ErrorGetLsnFromLongAd;
	}

	ExtentData = AllocatePool (ExtentLength + RemBytes);
	if (!ExtentData) {
	  Status = EFI_OUT_OF_RESOURCES;
	  goto ErrorAllocExtData;
	}

	CopyMem (ExtentData, (VOID *)Buffer, RemBytes);

	FreePool (Buffer);
	Buffer = NULL;

	Status = DiskIo->ReadDisk (
                                DiskIo,
				BlockIo->Media->MediaId,
				MultU64x32 (Lsn, BlockSize),
				ExtentLength,
				(VOID *)((UINT8 *)(ExtentData + RemBytes))
                                );
	if (EFI_ERROR (Status)) {
	  goto ErrorReadDiskBlk;
	}

	ReadDirInfo->FidOffset = 0;
      } else {
	RemBytes = 0;
      }

      FileIdentifierDesc = GET_FID_FROM_ADS (
                                         ExtentData,
					 ReadDirInfo->FidOffset
                                         );
      FidLength = GetFidDescriptorLength (FileIdentifierDesc);
      if (RemainingFidBytes ||
	  (ReadDirInfo->FidOffset + FidLength > ExtentLength)) {
	if (!RemainingFidBytes) {
	  *FoundFileIdentifierDesc =
	    (UDF_FILE_IDENTIFIER_DESCRIPTOR *)AllocatePool (FidLength);
	  if (!*FoundFileIdentifierDesc) {
	    goto ErrorAllocFid;
	  }

	  CopyMem (
	    (VOID *)*FoundFileIdentifierDesc,
	    (VOID *)FileIdentifierDesc,
	    ExtentLength - ReadDirInfo->FidOffset
	    );
	  LastFidOffset       = ExtentLength - ReadDirInfo->FidOffset;
	  RemainingFidBytes   = FidLength - (ExtentLength -
					     ReadDirInfo->FidOffset);
	  ReadDirInfo->AdOffset += AdLength;
	  ReadDirInfo->FidOffset = 0;
	  FreePool (ExtentData);
	  ExtentData = NULL;
	  goto ReadRemainingFidBytes;
	}

	CopyMem (
	  (VOID *)((UINT8 *)*FoundFileIdentifierDesc + LastFidOffset),
	  (VOID *)FileIdentifierDesc,
	  RemainingFidBytes
	  );
	ReadDirInfo->FidOffset   += RemainingFidBytes;
	RemainingFidBytes        = 0;
	LastFidOffset            = 0;
      } else {
	ReadDirInfo->FidOffset += FidLength - RemBytes;
      }

      if (IS_FID_DELETED_FILE (FileIdentifierDesc)) {
	if (*FoundFileIdentifierDesc) {
	  FreePool ((VOID *)*FoundFileIdentifierDesc);
	  *FoundFileIdentifierDesc = NULL;
	}

	FreePool (ExtentData);
	ExtentData = NULL;
	goto ExcludeDeletedFile;
      }

      Status = EFI_SUCCESS;
      break;
    case SHORT_ADS_SEQUENCE:
      GetAdsInformation (FileEntryData, (VOID **)&AdsData, &Length);

      AdLength = sizeof (UDF_SHORT_ALLOCATION_DESCRIPTOR);
ExcludeDeletedFile2:
ReadRemainingFidBytes2:
      for (;;) {
	Status = GetShortAdFromAds (
                               AdsData,
			       &ReadDirInfo->AdOffset,
			       Length,
			       &ShortAd
                               );
	if (EFI_ERROR (Status)) {
	  goto ErrorGetShortAd;
	}

	ExtentLength = ShortAd->ExtentLength;
	if (ReadDirInfo->FidOffset < ExtentLength) {
	  break;
	}

	ReadDirInfo->AdOffset    += AdLength;
	ReadDirInfo->FidOffset   -= ExtentLength;
      }

      Lsn = GetLsnFromShortAd (
                          GetPdFromLongAd (Volume, ParentIcb),
			  ShortAd
                          );
      if (!Lsn) {
	Status = EFI_VOLUME_CORRUPTED;
	goto ErrorGetLsnFromShortAd;
      }

      ExtentLength = ShortAd->ExtentLength;

      ExtentData = AllocateZeroPool (ExtentLength);
      if (!ExtentData) {
	Status = EFI_OUT_OF_RESOURCES;
	goto ErrorAllocExtData;
      }

      Status = DiskIo->ReadDisk (
                              DiskIo,
			      BlockIo->Media->MediaId,
			      MultU64x32 (Lsn, BlockSize),
			      ExtentLength,
			      ExtentData
	                      );
      if (EFI_ERROR (Status)) {
	goto ErrorReadDiskBlk;
      }

      RemBytes = ExtentLength - ReadDirInfo->FidOffset;
      if (RemBytes < sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)) {
	ReadDirInfo->AdOffset += AdLength;
	Status = GetShortAdFromAds (
                               AdsData,
			       &ReadDirInfo->AdOffset,
                               Length,
			       &ShortAd
                               );
	if (EFI_ERROR (Status)) {
	  goto ErrorGetShortAd;
	}

        Lsn = GetLsnFromShortAd (
                            GetPdFromLongAd (Volume, ParentIcb),
			    ShortAd
                            );
	if (!Lsn) {
	  Status = EFI_VOLUME_CORRUPTED;
	  goto ErrorGetLsnFromShortAd;
	}

	Buffer = AllocatePool (RemBytes);
	if (!Buffer) {
	  Status = EFI_OUT_OF_RESOURCES;
	  goto ErrorAllocBuf;
	}

	CopyMem (
	  Buffer,
	  (VOID *)((UINT8 *)(ExtentData + (ExtentLength - RemBytes))),
	  RemBytes
	  );

	FreePool (ExtentData);

	ExtentData = AllocatePool (ShortAd->ExtentLength + RemBytes);
	if (!ExtentData) {
	  FreePool (Buffer);
	  Status = EFI_OUT_OF_RESOURCES;
	  goto ErrorAllocExtData;
	}

	CopyMem (ExtentData, (VOID *)Buffer, RemBytes);

	FreePool (Buffer);
	Buffer = NULL;

	Status = DiskIo->ReadDisk (
                                DiskIo,
				BlockIo->Media->MediaId,
				MultU64x32 (Lsn, BlockSize),
				ShortAd->ExtentLength,
				(VOID *)((UINT8 *)(ExtentData + RemBytes))
                                );
	if (EFI_ERROR (Status)) {
	  goto ErrorReadDiskBlk;
	}

	ReadDirInfo->FidOffset = 0;
      } else {
	RemBytes = 0;
      }

      FileIdentifierDesc = GET_FID_FROM_ADS (
                                         ExtentData,
					 ReadDirInfo->FidOffset
                                         );
      FidLength = GetFidDescriptorLength (FileIdentifierDesc);
      if (RemainingFidBytes ||
	  (ReadDirInfo->FidOffset + FidLength > ExtentLength)) {
	if (!RemainingFidBytes) {
	  *FoundFileIdentifierDesc =
	    (UDF_FILE_IDENTIFIER_DESCRIPTOR *)AllocatePool (FidLength);
	  if (!*FoundFileIdentifierDesc) {
	    goto ErrorAllocFid;
	  }

	  CopyMem (
	    (VOID *)*FoundFileIdentifierDesc,
	    (VOID *)FileIdentifierDesc,
	    ExtentLength - ReadDirInfo->FidOffset
	    );
	  LastFidOffset = ExtentLength - ReadDirInfo->FidOffset;
	  RemainingFidBytes = FidLength - (ExtentLength -
					   ReadDirInfo->FidOffset);
	  ReadDirInfo->AdOffset += AdLength;
	  ReadDirInfo->FidOffset = 0;
	  FreePool (ExtentData);
	  ExtentData = NULL;
	  goto ReadRemainingFidBytes2;
	}

	CopyMem (
	  (VOID *)((UINT8 *)*FoundFileIdentifierDesc + LastFidOffset),
	  (VOID *)FileIdentifierDesc,
	  RemainingFidBytes
	  );
	ReadDirInfo->FidOffset   += RemainingFidBytes;
	RemainingFidBytes        = 0;
	LastFidOffset            = 0;
      } else {
	ReadDirInfo->FidOffset += FidLength - RemBytes;
      }

      if (IS_FID_DELETED_FILE (FileIdentifierDesc)) {
	if (*FoundFileIdentifierDesc) {
	  FreePool ((VOID *)*FoundFileIdentifierDesc);
	  *FoundFileIdentifierDesc = NULL;
	}

	FreePool (ExtentData);
	ExtentData = NULL;
	goto ExcludeDeletedFile2;
      }

      Status = EFI_SUCCESS;
      break;
  }

  if (!EFI_ERROR (Status)) {
    if (!*FoundFileIdentifierDesc) {
      DuplicateFid (FileIdentifierDesc, FoundFileIdentifierDesc);
    }
  }

ErrorAllocFid:
ErrorGetLsnFromShortAd:
ErrorGetShortAd:
ErrorAllocBuf:
ErrorAllocExtData:
ErrorGetLsnFromLongAd:
ErrorGetAed:
ErrorGetLongAd:
ErrorReadDiskBlk:
ErrorAllocAdsData:
  if (DoFreeAed) {
    FreePool ((VOID *)AdsData);
  }
  if (Buffer) {
    FreePool ((VOID *)Buffer);
  }
  if (ExtentData) {
    FreePool (ExtentData);
  }

  return Status;
}

EFI_STATUS
SetFileInfo (
  IN UDF_FILE_INFO                    *File,
  IN UINT64                           FileSize,
  IN CHAR16                           *FileName,
  IN OUT UINTN                        *BufferSize,
  OUT VOID                            *Buffer
  )
{
  EFI_STATUS                          Status;
  UINTN                               FileInfoLength;
  EFI_FILE_INFO                       *FileInfo;
  UDF_FILE_ENTRY                      *FileEntry;
  UDF_EXTENDED_FILE_ENTRY             *ExtendedFileEntry;

  Status = EFI_SUCCESS;

  FileInfoLength =
    (sizeof (EFI_FILE_INFO) + (FileName ?
			       ((StrLen (FileName) + 1) * sizeof (CHAR16)) :
			       sizeof (CHAR16)));
  if (*BufferSize < FileInfoLength) {
    *BufferSize = FileInfoLength;
    return EFI_BUFFER_TOO_SMALL;
  }

  FileInfo = (EFI_FILE_INFO *)Buffer;

  FileInfo->Size         = FileInfoLength;
  FileInfo->Attribute    &= ~EFI_FILE_VALID_ATTR;
  FileInfo->Attribute    |= EFI_FILE_READ_ONLY;

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

    if (FileEntry->IcbTag.Flags & (1 << 10)) {
      FileInfo->Attribute |= EFI_FILE_SYSTEM;
    }

    FileInfo->FileSize       = FileSize;
    FileInfo->PhysicalSize   = FileSize;

    FileInfo->CreateTime.Year         = FileEntry->AccessTime.Year;
    FileInfo->CreateTime.Month        = FileEntry->AccessTime.Month;
    FileInfo->CreateTime.Day          = FileEntry->AccessTime.Day;
    FileInfo->CreateTime.Hour         = FileEntry->AccessTime.Hour;
    FileInfo->CreateTime.Minute       = FileEntry->AccessTime.Second;
    FileInfo->CreateTime.Second       = FileEntry->AccessTime.Second;
    FileInfo->CreateTime.Nanosecond   =
      FileEntry->AccessTime.HundredsOfMicroseconds;

    FileInfo->LastAccessTime.Year         =
                         FileEntry->AccessTime.Year;
    FileInfo->LastAccessTime.Month        =
                         FileEntry->AccessTime.Month;
    FileInfo->LastAccessTime.Day          =
                         FileEntry->AccessTime.Day;
    FileInfo->LastAccessTime.Hour         =
                         FileEntry->AccessTime.Hour;
    FileInfo->LastAccessTime.Minute       =
                         FileEntry->AccessTime.Minute;
    FileInfo->LastAccessTime.Second       =
                         FileEntry->AccessTime.Second;
    FileInfo->LastAccessTime.Nanosecond   =
                         FileEntry->AccessTime.HundredsOfMicroseconds;
  } else if (IS_EFE (File->FileEntry)) {
    ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)File->FileEntry;

    if (ExtendedFileEntry->IcbTag.Flags & (1 << 10)) {
      FileInfo->Attribute |= EFI_FILE_SYSTEM;
    }

    FileInfo->FileSize       = FileSize;
    FileInfo->PhysicalSize   = FileSize;

    FileInfo->CreateTime.Year         = ExtendedFileEntry->CreationTime.Year;
    FileInfo->CreateTime.Month        = ExtendedFileEntry->CreationTime.Month;
    FileInfo->CreateTime.Day          = ExtendedFileEntry->CreationTime.Day;
    FileInfo->CreateTime.Hour         = ExtendedFileEntry->CreationTime.Hour;
    FileInfo->CreateTime.Minute       = ExtendedFileEntry->CreationTime.Second;
    FileInfo->CreateTime.Second       = ExtendedFileEntry->CreationTime.Second;
    FileInfo->CreateTime.Nanosecond   =
      ExtendedFileEntry->AccessTime.HundredsOfMicroseconds;

    FileInfo->LastAccessTime.Year         =
                         ExtendedFileEntry->AccessTime.Year;
    FileInfo->LastAccessTime.Month        =
                         ExtendedFileEntry->AccessTime.Month;
    FileInfo->LastAccessTime.Day          =
                         ExtendedFileEntry->AccessTime.Day;
    FileInfo->LastAccessTime.Hour         =
                         ExtendedFileEntry->AccessTime.Hour;
    FileInfo->LastAccessTime.Minute       =
                         ExtendedFileEntry->AccessTime.Minute;
    FileInfo->LastAccessTime.Second       =
                         ExtendedFileEntry->AccessTime.Second;
    FileInfo->LastAccessTime.Nanosecond   =
                         ExtendedFileEntry->AccessTime.HundredsOfMicroseconds;
  }

  //
  // For OSTA UDF compliant media, the time within the UDF_TIMESTAMP
  // structures should be interpreted as Local Time. Use
  // EFI_UNSPECIFIED_TIMEZONE for Local Time.
  //
  FileInfo->CreateTime.TimeZone   = EFI_UNSPECIFIED_TIMEZONE;
  FileInfo->CreateTime.Daylight   = EFI_TIME_ADJUST_DAYLIGHT;

  //
  // As per ECMA-167 specification, the Modification Time should be identical
  // to the content of the Access Time field.
  //
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

EFI_STATUS
IsSupportedUdfVolume (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  OUT BOOLEAN                                *Supported
  )
{
  EFI_STATUS                                 Status;
  UDF_NSR_DESCRIPTOR                         NsrDescriptor;

  *Supported = FALSE;

  //
  // Start Volume Recognition Sequence
  //
  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       BEA_DESCRIPTOR_LSN_OFFSET,
                       sizeof (UDF_NSR_DESCRIPTOR),
                       (VOID *)&NsrDescriptor
                       );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if (CompareMem (
	(VOID *)&NsrDescriptor.StandardIdentifier,
	(VOID *)&gUdfStandardIdentifiers[BEA_IDENTIFIER],
	UDF_STANDARD_IDENTIFIER_LENGTH
	)
    ) {
    goto Exit;
  }

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       BEA_DESCRIPTOR_LSN_OFFSET + LOGICAL_SECTOR_SIZE,
                       sizeof (UDF_NSR_DESCRIPTOR),
                       (VOID *)&NsrDescriptor
                       );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if (CompareMem (
	(VOID *)&NsrDescriptor.StandardIdentifier,
	(VOID *)&gUdfStandardIdentifiers[VSD_IDENTIFIER_0],
	UDF_STANDARD_IDENTIFIER_LENGTH
	)
      &&
      CompareMem (
	(VOID *)&NsrDescriptor.StandardIdentifier,
	(VOID *)&gUdfStandardIdentifiers[VSD_IDENTIFIER_1],
	UDF_STANDARD_IDENTIFIER_LENGTH
        )
    ) {
    goto Exit;
  }

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       BEA_DESCRIPTOR_LSN_OFFSET + (LOGICAL_SECTOR_SIZE << 1),
                       sizeof (UDF_NSR_DESCRIPTOR),
                       (VOID *)&NsrDescriptor
                       );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if (CompareMem (
	(VOID *)&NsrDescriptor.StandardIdentifier,
	(VOID *)&gUdfStandardIdentifiers[TEA_IDENTIFIER],
	UDF_STANDARD_IDENTIFIER_LENGTH
	)
    ) {
    goto Exit;
  }

  *Supported = TRUE;

Exit:
  return Status;
}
