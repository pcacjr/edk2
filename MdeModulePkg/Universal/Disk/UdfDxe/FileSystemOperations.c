/** @file
  UDF/ECMA-167 filesystem driver.

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

  BlockSize = BlockIo->Media->BlockSize;
  EndLBA = BlockIo->Media->LastBlock;
  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
		       MultU64x32 (0x100ULL, BlockSize),
                       sizeof (UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER),
                       (VOID *)AnchorPoint
                       );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = EFI_VOLUME_CORRUPTED;
  if (IS_AVDP (AnchorPoint)) {
    Status = EFI_SUCCESS;
    goto Exit;
  }

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
		       MultU64x32 (EndLBA - 0x100ULL, BlockSize),
                       sizeof (UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER),
                       (VOID *)AnchorPoint
                       );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if (IS_AVDP (AnchorPoint)) {
    Status = EFI_SUCCESS;
    goto Exit;
  }

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
		       MultU64x32 (EndLBA, BlockSize),
                       sizeof (UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER),
                       (VOID *)AnchorPoint
                       );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if (IS_AVDP (AnchorPoint)) {
    Status = EFI_SUCCESS;
  }

Exit:
  return Status;
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
  UINT32                         StartingLsn;
  UINT32                         EndingLsn;
  VOID                           *Buffer;
  UDF_LOGICAL_VOLUME_DESCRIPTOR  *LogicalVolDesc;
  UDF_PARTITION_DESCRIPTOR       *PartitionDesc;
  UINTN                          Index;

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
  IN UDF_VOLUME_INFO                 *Volume,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR  *LongAd
  )
{
  UINTN                     Index;
  UDF_PARTITION_DESCRIPTOR  *PartitionDesc;

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
GetLongAdLsn (
  IN UDF_VOLUME_INFO                 *Volume,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR  *LongAd
  )
{
  UDF_PARTITION_DESCRIPTOR *PartitionDesc;

  PartitionDesc = GetPdFromLongAd (Volume, LongAd);
  if (!PartitionDesc) {
    return 0;
  }

  return (UINT64)(PartitionDesc->PartitionStartingLocation +
		  LongAd->ExtentLocation.LogicalBlockNumber);
}

UINT64
GetShortAdLsn (
  IN UDF_PARTITION_DESCRIPTOR         *PartitionDesc,
  IN UDF_SHORT_ALLOCATION_DESCRIPTOR  *ShortAd
  )
{
  return (UINT64)(PartitionDesc->PartitionStartingLocation +
		  ShortAd->ExtentPosition);
}

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
  UINT32                         BlockSize;
  UDF_LOGICAL_VOLUME_DESCRIPTOR  *LogicalVolDesc;

  LogicalVolDesc = Volume->LogicalVolDescs[LogicalVolDescNo];
  Lsn = GetLongAdLsn (
                Volume,
		&LogicalVolDesc->LogicalVolumeContentsUse
                );
  BlockSize = BlockIo->Media->BlockSize;

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       MultU64x32 (Lsn, BlockSize),
                       sizeof (UDF_FILE_SET_DESCRIPTOR),
                       (VOID *)FileSetDesc
                       );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (!IS_FSD (FileSetDesc)) {
    return EFI_VOLUME_CORRUPTED;
  }

  return EFI_SUCCESS;
}

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
  IN   EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL   *DiskIo,
  OUT  UDF_VOLUME_INFO        *Volume
  )
{
  EFI_STATUS                            Status;
  UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER  AnchorPoint;

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

VOID
GetInlineDataInformation (
  IN   VOID    *FileEntryData,
  OUT  VOID    **Data,
  OUT  UINT64  *Length
  )
{
  UDF_EXTENDED_FILE_ENTRY  *ExtendedFileEntry;
  UDF_FILE_ENTRY           *FileEntry;

  if (IS_EFE (FileEntryData)) {
    ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)FileEntryData;
    *Length = ExtendedFileEntry->InformationLength;
    *Data = (VOID *)((UINT8 *)&ExtendedFileEntry->Data[0] +
		     ExtendedFileEntry->LengthOfExtendedAttributes);
  } else if (IS_FE (FileEntryData)) {
    FileEntry = (UDF_FILE_ENTRY *)FileEntryData;
    *Length = FileEntry->InformationLength;
    *Data = (VOID *)((UINT8 *)&FileEntry->Data[0] +
		     FileEntry->LengthOfExtendedAttributes);
  }
}

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
    *Length = ExtendedFileEntry->InformationLength;
    *Data = (VOID *)((UINT8 *)&ExtendedFileEntry->Data[0] +
		     ExtendedFileEntry->LengthOfExtendedAttributes);
  } else if (IS_FE (FileEntryData)) {
    FileEntry = (UDF_FILE_ENTRY *)FileEntryData;
    *Length = FileEntry->InformationLength;
    *Data = (VOID *)((UINT8 *)&FileEntry->Data[0] +
		     FileEntry->LengthOfExtendedAttributes);
  }
}

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

  Data = (UINT8 *)ReadFileInfo.FileData;
  Length = ReadFileInfo.ReadLength;
  EndData = (UINT8 *)(Data + Length);
  CopyMem ((VOID *)&PreviousFile, (VOID *)Parent, sizeof (UDF_FILE_INFO));
  for (;;) {
    PathComp = (UDF_PATH_COMPONENT *)Data;
    PathCompLength = PathComp->LengthOfComponentIdentifier;
    switch (PathComp->ComponentType) {
      case 3:
	FileName[0] = L'.';
	FileName[1] = L'.';
	FileName[2] = L'\0';
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

  FreePool (ReadFileInfo.FileData);
  return EFI_SUCCESS;

ErrorFindFile:
  if (CompareMem (
	(VOID *)&PreviousFile,
	(VOID *)Parent,
	sizeof (UDF_FILE_INFO)
	)
    ) {
    FreePool ((VOID *)PreviousFile.FileIdentifierDesc);
    FreePool (PreviousFile.FileEntry);
  }

  FreePool (ReadFileInfo.FileData);
  return Status;
}

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
  UINT32      BlockSize;

  Lsn = GetLongAdLsn (Volume, Icb);
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
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR  *FileIdentifierDesc
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
  IN   UDF_FILE_IDENTIFIER_DESCRIPTOR  *FileIdentifierDesc,
  OUT  UDF_FILE_IDENTIFIER_DESCRIPTOR  **NewFileIdentifierDesc
  )
{
  UINT64 FidLength;

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
  IN   EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN   VOID                   *FileEntry,
  OUT  VOID                   **NewFileEntry
  )
{
  UINT32 BlockSize;

  BlockSize = BlockIo->Media->BlockSize;
  *NewFileEntry = AllocateZeroPool (BlockSize);
  ASSERT (*NewFileEntry);
  CopyMem (*NewFileEntry, FileEntry, BlockSize);
}

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

  if (!IS_FE_DIRECTORY (Parent->FileEntry)) {
    return EFI_NOT_FOUND;
  }

  if (!StrCmp (FileName, L".")) {
    DuplicateFe (BlockIo, Parent->FileEntry, &File->FileEntry);
    DuplicateFid (Parent->FileIdentifierDesc, &File->FileIdentifierDesc);
    return EFI_SUCCESS;
  }

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
      if ((!StrCmp (FileName, L"..")) || (!StrCmp (FileName, L"\\"))) {
	Found = TRUE;
	break;
      }
    } else {
      if (FileNameLength != FileIdentifierDesc->LengthOfFileIdentifier - 1) {
	goto SkipFid;
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

SkipFid:
    FreePool ((VOID *)FileIdentifierDesc);
  }

  if (ReadDirInfo.DirectoryData) {
    FreePool (ReadDirInfo.DirectoryData);
  }

  if (Found) {
    Status = EFI_SUCCESS;
    File->FileIdentifierDesc = FileIdentifierDesc;
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
      if (!Root) {
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
	DuplicateFe (BlockIo, Root->FileEntry, &File->FileEntry);
	DuplicateFid (Root->FileIdentifierDesc, &File->FileIdentifierDesc);
	Status = EFI_SUCCESS;
      }
    } else {
      Status = InternalFindFile (
                             BlockIo,
			     DiskIo,
			     Volume,
			     FileName,
			     &PreviousFile,
			     Icb,
			     File);
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
  IN      VOID                             *Data,
  IN OUT  UINT64                           *Offset,
  IN      UINT64                           Length,
  OUT     UDF_SHORT_ALLOCATION_DESCRIPTOR  **FoundShortAd
  )
{
  UDF_SHORT_ALLOCATION_DESCRIPTOR *ShortAd;

  for (;;) {
    if (*Offset >= Length) {
      return EFI_DEVICE_ERROR;
    }

    ShortAd = (UDF_SHORT_ALLOCATION_DESCRIPTOR *)((UINT8 *)Data +
						  *Offset);
    if (ShortAd->ExtentLength) {
      break;
    }

    *Offset += sizeof (UDF_SHORT_ALLOCATION_DESCRIPTOR);
  }

  *FoundShortAd = ShortAd;
  return EFI_SUCCESS;
}

EFI_STATUS
GetLongAdFromAds (
  IN      VOID                            *Data,
  IN OUT  UINT64                          *Offset,
  IN      UINT64                          Length,
  OUT     UDF_LONG_ALLOCATION_DESCRIPTOR  **FoundLongAd
  )
{
  UDF_LONG_ALLOCATION_DESCRIPTOR       *LongAd;

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
	*Offset += sizeof (UDF_LONG_ALLOCATION_DESCRIPTOR);
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

  ReadFileInfo.Flags = READ_FILE_SEEK_AND_READ;
  ReadFileInfo.FilePosition = *FilePosition;
  ReadFileInfo.FileData = Buffer;
  ReadFileInfo.FileDataSize = *BufferSize;
  ReadFileInfo.FileSize = FileSize;

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

  *BufferSize = ReadFileInfo.FileDataSize;
  *FilePosition = ReadFileInfo.FilePosition;
  return EFI_SUCCESS;
}

EFI_STATUS
GetAedAdsOffset (
  IN   EFI_BLOCK_IO_PROTOCOL           *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL            *DiskIo,
  IN   UDF_VOLUME_INFO                 *Volume,
  IN   UDF_LONG_ALLOCATION_DESCRIPTOR  *LongAd,
  OUT  UINT64                          *Offset,
  OUT  UINT64                          *Length
  )
{
  EFI_STATUS                        Status;
  UINT32                            ExtentLength;
  VOID                              *Data;
  UINT64                            Lsn;
  UINT32                            BlockSize;
  UDF_ALLOCATION_EXTENT_DESCRIPTOR  *AllocExtDesc;

  ExtentLength = GET_EXTENT_LENGTH (LongAd);
  Data = AllocatePool (ExtentLength);
  if (!Data) {
    return EFI_OUT_OF_RESOURCES;
  }

  Lsn = GetLongAdLsn (Volume, LongAd);
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

  *Offset = (UINT64)(MultU64x32 (Lsn, BlockSize) +
		     sizeof (UDF_ALLOCATION_EXTENT_DESCRIPTOR));
  *Length = AllocExtDesc->LengthOfAllocationDescriptors;

Exit:
  if (Data) {
    FreePool (Data);
  }

  return Status;
}

EFI_STATUS
GetAedAdsData (
  IN   EFI_BLOCK_IO_PROTOCOL           *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL            *DiskIo,
  IN   UDF_VOLUME_INFO                 *Volume,
  IN   UDF_LONG_ALLOCATION_DESCRIPTOR  *LongAd,
  OUT  VOID                            **Data,
  OUT  UINT64                          *Length
  )
{
  EFI_STATUS  Status;
  UINT64      Offset;

  Status = GetAedAdsOffset (BlockIo, DiskIo, Volume, LongAd, &Offset, Length);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *Data = AllocatePool (*Length);
  if (!Data) {
    return EFI_OUT_OF_RESOURCES;
  }

  return DiskIo->ReadDisk (
                        DiskIo,
			BlockIo->Media->MediaId,
			Offset,
			*Length,
			*Data
                        );
}

EFI_STATUS
GrowUpBufferToNextAd (
  IN      UDF_FE_RECORDING_FLAGS  RecordingFlags,
  IN      VOID                    *Ad,
  IN OUT  VOID                    **Buffer,
  IN      UINT64                  Length
  )
{
  UINT32 ExtentLength;

  if (RecordingFlags == LONG_ADS_SEQUENCE) {
    ExtentLength = GET_EXTENT_LENGTH ((UDF_LONG_ALLOCATION_DESCRIPTOR *)Ad);
  } else if (RecordingFlags == SHORT_ADS_SEQUENCE) {
    ExtentLength = ((UDF_SHORT_ALLOCATION_DESCRIPTOR *)Ad)->ExtentLength;
  }

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
  EFI_STATUS                       Status;
  UINT32                           BlockSize;
  VOID                             *Data;
  UINT64                           Length;
  UINT64                           AdOffset;
  UDF_LONG_ALLOCATION_DESCRIPTOR   *LongAd;
  UINT64                           Lsn;
  BOOLEAN                          DoFreeAed;
  UDF_SHORT_ALLOCATION_DESCRIPTOR  *ShortAd;
  UINT64                           FilePosition;
  UINT64                           Offset;
  UINT64                           DataOffset;
  UINT64                           BytesLeft;
  UINT64                           DataLength;
  BOOLEAN                          FinishedSeeking;

  BlockSize = BlockIo->Media->BlockSize;
  DoFreeAed = FALSE;

  switch (ReadFileInfo->Flags) {
    case READ_FILE_GET_FILESIZE:
      ReadFileInfo->ReadLength = 0;
      break;
    case READ_FILE_ALLOCATE_AND_READ:
      ReadFileInfo->FileData = NULL;
      ReadFileInfo->ReadLength = 0;
      break;
    case READ_FILE_SEEK_AND_READ:
      Length = ReadFileInfo->FileSize - ReadFileInfo->FilePosition;
      if (ReadFileInfo->FileDataSize > Length) {
	//
	// About to read beyond the EOF -- truncate it
	//
	ReadFileInfo->FileDataSize = Length;
      }

      BytesLeft = ReadFileInfo->FileDataSize;
      DataOffset = 0;
      FilePosition = 0;
      FinishedSeeking = FALSE;
      break;
  }

  switch (GET_FE_RECORDING_FLAGS (FileEntryData)) {
    case INLINE_DATA:
      GetInlineDataInformation (FileEntryData, &Data, &Length);
      switch (ReadFileInfo->Flags) {
	case READ_FILE_GET_FILESIZE:
	  ReadFileInfo->ReadLength = Length;
	  break;
	case READ_FILE_ALLOCATE_AND_READ:
	  ReadFileInfo->FileData = AllocatePool (Length);
	  if (!ReadFileInfo->FileData) {
	    return EFI_OUT_OF_RESOURCES;
	  }

	  CopyMem (ReadFileInfo->FileData, Data, Length);
	  ReadFileInfo->ReadLength = Length;
	  break;
	case READ_FILE_SEEK_AND_READ:
	  CopyMem (
	    ReadFileInfo->FileData,
	    (VOID *)((UINT8 *)Data + ReadFileInfo->FilePosition),
	    ReadFileInfo->FileDataSize
	    );
	  ReadFileInfo->FilePosition += ReadFileInfo->FileDataSize;
	  break;
      }
      break;
    case LONG_ADS_SEQUENCE:
      GetAdsInformation (FileEntryData, &Data, &Length);
      AdOffset = 0;
      for (;;) {
	Status = GetLongAdFromAds (
                              Data,
			      &AdOffset,
			      Length,
			      &LongAd
                              );
	if (Status == EFI_DEVICE_ERROR) {
	  return EFI_SUCCESS;
	}

	if (GET_EXTENT_FLAGS (LongAd) == EXTENT_IS_NEXT_EXTENT) {
	  if (!DoFreeAed) {
	    DoFreeAed = TRUE;
	  } else {
	    FreePool (Data);
	  }

	  Status = GetAedAdsData (
                              BlockIo,
			      DiskIo,
			      Volume,
			      LongAd,
			      &Data,
			      &Length
                              );
	  ASSERT (!EFI_ERROR (Status));
	  AdOffset = 0;
	  continue;
	}

	Lsn = GetLongAdLsn (Volume, LongAd);
	switch (ReadFileInfo->Flags) {
	  case READ_FILE_GET_FILESIZE:
	    ReadFileInfo->ReadLength += GET_EXTENT_LENGTH (LongAd);
	    break;
	  case READ_FILE_ALLOCATE_AND_READ:
	    Status = GrowUpBufferToNextAd (
                                     LONG_ADS_SEQUENCE,
				     (VOID *)LongAd,
				     &ReadFileInfo->FileData,
				     ReadFileInfo->ReadLength
                                     );
	    ASSERT (!EFI_ERROR (Status));
	    Status = DiskIo->ReadDisk (
                                    DiskIo,
				    BlockIo->Media->MediaId,
				    MultU64x32 (Lsn, BlockSize),
				    GET_EXTENT_LENGTH (LongAd),
				    (VOID *)((UINT8 *)ReadFileInfo->FileData +
					     ReadFileInfo->ReadLength)
                                  );
	    ASSERT (!EFI_ERROR (Status));
	    ReadFileInfo->ReadLength += GET_EXTENT_LENGTH (LongAd);
	    break;
	  case READ_FILE_SEEK_AND_READ:
	    if (FinishedSeeking) {
	      Offset = 0;
	      goto SkipFileSeek;
	    }

	    if (FilePosition +
		GET_EXTENT_LENGTH (LongAd) < ReadFileInfo->FilePosition) {
	      FilePosition += GET_EXTENT_LENGTH (LongAd);
	      goto SkipLongAd;
	    }

	    if (FilePosition +
		GET_EXTENT_LENGTH (LongAd) > ReadFileInfo->FilePosition) {
	      Offset = ReadFileInfo->FilePosition - FilePosition;
	      if (Offset < 0) {
		Offset = -(Offset);
	      }
	    } else {
	      Offset = 0;
	    }

	    if (GET_EXTENT_LENGTH (LongAd) - Offset > BytesLeft) {
	      DataLength = BytesLeft;
	    } else {
	      DataLength = GET_EXTENT_LENGTH (LongAd) - Offset;
	    }

	    FinishedSeeking = TRUE;
SkipFileSeek:
	    Status = DiskIo->ReadDisk (
                                    DiskIo,
				    BlockIo->Media->MediaId,
				    Offset + MultU64x32 (Lsn, BlockSize),
				    DataLength,
				    (VOID *)((UINT8 *)ReadFileInfo->FileData +
					     DataOffset)
                                    );
	    ASSERT (!EFI_ERROR (Status));
	    DataOffset += DataLength;
	    ReadFileInfo->FilePosition += DataLength;
	    BytesLeft -= DataLength;
	    if (!BytesLeft) {
	      return EFI_SUCCESS;
	    }
	    break;
	}

SkipLongAd:
	AdOffset += sizeof (UDF_LONG_ALLOCATION_DESCRIPTOR);
      }
      break;
    case SHORT_ADS_SEQUENCE:
      GetAdsInformation (FileEntryData, &Data, &Length);
      AdOffset = 0;
      for (;;) {
	Status = GetShortAdFromAds (
                               Data,
			       &AdOffset,
			       Length,
			       &ShortAd
                               );
	if (Status == EFI_DEVICE_ERROR) {
	  return EFI_SUCCESS;
	}

	Lsn = GetShortAdLsn (
	               GetPdFromLongAd (Volume, ParentIcb),
		       ShortAd
                       );
	switch (ReadFileInfo->Flags) {
	  case READ_FILE_GET_FILESIZE:
	    ReadFileInfo->ReadLength += ShortAd->ExtentLength;
	    break;
	  case READ_FILE_ALLOCATE_AND_READ:
	    Status = GrowUpBufferToNextAd (
                                     SHORT_ADS_SEQUENCE,
				     (VOID *)ShortAd,
				     &ReadFileInfo->FileData,
				     ReadFileInfo->ReadLength
                                     );
	    ASSERT (!EFI_ERROR (Status));
	    Status = DiskIo->ReadDisk (
                                    DiskIo,
				    BlockIo->Media->MediaId,
				    MultU64x32 (Lsn, BlockSize),
				    ShortAd->ExtentLength,
				    (VOID *)((UINT8 *)ReadFileInfo->FileData +
					     ReadFileInfo->ReadLength)
                                  );
	    ASSERT (!EFI_ERROR (Status));
	    ReadFileInfo->ReadLength += ShortAd->ExtentLength;
	    break;
	  case READ_FILE_SEEK_AND_READ:
	    if (FinishedSeeking) {
	      Offset = 0;
	      goto SkipFileSeek2;
	    }

	    if (FilePosition +
		ShortAd->ExtentLength < ReadFileInfo->FilePosition) {
	      FilePosition += ShortAd->ExtentLength;
	      goto SkipShortAd;
	    }

	    if (FilePosition +
		ShortAd->ExtentLength > ReadFileInfo->FilePosition) {
	      Offset = ReadFileInfo->FilePosition - FilePosition;
	      if (Offset < 0) {
		Offset = -(Offset);
	      }
	    } else {
	      Offset = 0;
	    }

	    if (ShortAd->ExtentLength - Offset > BytesLeft) {
	      DataLength = BytesLeft;
	    } else {
	      DataLength = ShortAd->ExtentLength - Offset;
	    }

	    FinishedSeeking = TRUE;
SkipFileSeek2:
	    Status = DiskIo->ReadDisk (
                                    DiskIo,
				    BlockIo->Media->MediaId,
				    Offset + MultU64x32 (Lsn, BlockSize),
				    DataLength,
				    (VOID *)((UINT8 *)ReadFileInfo->FileData +
					     DataOffset)
                                    );
	    ASSERT (!EFI_ERROR (Status));
	    DataOffset += DataLength;
	    ReadFileInfo->FilePosition += DataLength;
	    BytesLeft -= DataLength;
	    if (!BytesLeft) {
	      return EFI_SUCCESS;
	    }
	    break;
	}

SkipShortAd:
	AdOffset += sizeof (UDF_SHORT_ALLOCATION_DESCRIPTOR);
      }
      break;
  }

  if (DoFreeAed) {
    FreePool (Data);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
ReadDirectoryEntry (
  IN      EFI_BLOCK_IO_PROTOCOL           *BlockIo,
  IN      EFI_DISK_IO_PROTOCOL            *DiskIo,
  IN      UDF_VOLUME_INFO                 *Volume,
  IN      UDF_LONG_ALLOCATION_DESCRIPTOR  *ParentIcb,
  IN      VOID                            *FileEntryData,
  IN OUT  UDF_READ_DIRECTORY_INFO         *ReadDirInfo,
  OUT     UDF_FILE_IDENTIFIER_DESCRIPTOR  **FoundFileIdentifierDesc
  )
{
  EFI_STATUS                      Status;
  UDF_READ_FILE_INFO              ReadFileInfo;
  UDF_FILE_IDENTIFIER_DESCRIPTOR  *FileIdentifierDesc;

  if (!ReadDirInfo->DirectoryData) {
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

    ReadDirInfo->DirectoryData = ReadFileInfo.FileData;
    ReadDirInfo->DirectoryLength = ReadFileInfo.ReadLength;
  }

  do {
    if (ReadDirInfo->FidOffset >= ReadDirInfo->DirectoryLength) {
      return EFI_DEVICE_ERROR;
    }
    FileIdentifierDesc = GET_FID_FROM_ADS (
                                     ReadDirInfo->DirectoryData,
				     ReadDirInfo->FidOffset
                                     );
    ReadDirInfo->FidOffset += GetFidDescriptorLength (FileIdentifierDesc);
  } while (IS_FID_DELETED_FILE (FileIdentifierDesc));

  DuplicateFid (FileIdentifierDesc, FoundFileIdentifierDesc);
  return EFI_SUCCESS;
}

EFI_STATUS
SetFileInfo (
  IN      UDF_FILE_INFO  *File,
  IN      UINT64         FileSize,
  IN      CHAR16         *FileName,
  IN OUT  UINTN          *BufferSize,
  OUT     VOID           *Buffer
  )
{
  EFI_STATUS               Status;
  UINTN                    FileInfoLength;
  EFI_FILE_INFO            *FileInfo;
  UDF_FILE_ENTRY           *FileEntry;
  UDF_EXTENDED_FILE_ENTRY  *ExtendedFileEntry;

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
GetVolumeSize (
  IN   EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL   *DiskIo,
  IN   UDF_VOLUME_INFO        *Volume,
  OUT  UINT64                 *VolumeSize,
  OUT  UINT64                 *FreeSpaceSize
  )
{
  UINT32                        BlockSize;
  UDF_EXTENT_AD                 ExtentAd;
  EFI_STATUS                    Status;
  UDF_LOGICAL_VOLUME_INTEGRITY  *LogicalVolInt;
  UINTN                         Index;
  UINTN                         Length;
  UINT32                        LsnsNo;

  BlockSize = BlockIo->Media->BlockSize;
  *VolumeSize = 0;
  *FreeSpaceSize = 0;
  for (Index = 0; Index < Volume->LogicalVolDescsNo; Index++) {
    CopyMem (
      (VOID *)&ExtentAd,
      (VOID *)&Volume->LogicalVolDescs[Index]->IntegritySequenceExtent,
      sizeof (UDF_EXTENT_AD)
      );
    if (!ExtentAd.ExtentLength) {
      continue;
    }

ReadNextSequence:
    LogicalVolInt = (UDF_LOGICAL_VOLUME_INTEGRITY *)AllocatePool (
                                                          ExtentAd.ExtentLength
                                                          );
    if (!LogicalVolInt) {
      return EFI_OUT_OF_RESOURCES;
    }

    Status = DiskIo->ReadDisk (
                         DiskIo,
			 BlockIo->Media->MediaId,
			 MultU64x32 (ExtentAd.ExtentLocation, BlockSize),
			 ExtentAd.ExtentLength,
			 (VOID *)LogicalVolInt
                         );
    if (EFI_ERROR (Status)) {
      FreePool ((VOID *)LogicalVolInt);
      return Status;
    }
    ASSERT (IS_LVID (LogicalVolInt));

    if (!IS_LVID (LogicalVolInt)) {
      FreePool ((VOID *)LogicalVolInt);
      return EFI_VOLUME_CORRUPTED;
    }

    Length = LogicalVolInt->NumberOfPartitions;
    for (Index = 0; Index < Length; Index += sizeof (UINT32)) {
      LsnsNo = *(UINT32 *)((UINT8 *)&LogicalVolInt->Data[0] + Index);
      if (LsnsNo == 0xFFFFFFFFUL) {
	//
	// Size not specified
	//
	continue;
      }

      *FreeSpaceSize += MultU64x32 ((UINT64)LsnsNo, BlockSize);
    }

    Length = LogicalVolInt->NumberOfPartitions * sizeof (UINT32) * 2;
    for (; Index < Length; Index += sizeof (UINT32)) {
      LsnsNo = *(UINT32 *)((UINT8 *)&LogicalVolInt->Data[0] + Index);
      if (LsnsNo == 0xFFFFFFFFUL) {
	//
	// Size not specified
	//
	continue;
      }

      *VolumeSize += MultU64x32 ((UINT64)LsnsNo, BlockSize);
    }

    CopyMem (
      (VOID *)&ExtentAd,
      (VOID *)&LogicalVolInt->NextIntegrityExtent,
      sizeof (UDF_EXTENT_AD)
      );
    if (ExtentAd.ExtentLength) {
      FreePool ((VOID *)LogicalVolInt);
      goto ReadNextSequence;
    }

    FreePool ((VOID *)LogicalVolInt);
  }

  return EFI_SUCCESS;
}

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
  EndDiskOffset = BlockIo->Media->LastBlock << UDF_LOGICAL_SECTOR_SHIFT;
  for (Offset = UDF_VRS_START_OFFSET; Offset < EndDiskOffset;
       Offset += 1 << UDF_LOGICAL_SECTOR_SHIFT) {
    Status = DiskIo->ReadDisk (
                           DiskIo,
                           BlockIo->Media->MediaId,
                           Offset,
                           sizeof (UDF_VOLUME_DESCRIPTOR),
                           (VOID *)&VolDescriptor
                           );
    if (EFI_ERROR (Status)) {
      goto Exit;
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
      Status = EFI_UNSUPPORTED;
      goto Exit;
    }
  }
  //
  // Look for either "NSR02" or "NSR03" in the Extended Area
  //
  Offset += 1 << UDF_LOGICAL_SECTOR_SHIFT;
  if (Offset >= EndDiskOffset) {
    Status = EFI_UNSUPPORTED;
    goto Exit;
  }

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       Offset,
                       sizeof (UDF_VOLUME_DESCRIPTOR),
                       (VOID *)&VolDescriptor
                       );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if (CompareMem (
	(VOID *)&VolDescriptor.StandardIdentifier,
	(VOID *)&gUdfStandardIdentifiers[VSD_IDENTIFIER],
	UDF_STANDARD_IDENTIFIER_LENGTH
	)
    ) {
    Status = EFI_UNSUPPORTED;
    goto Exit;
  }
  //
  // Look for "TEA01" in the Extended Area
  //
  Offset += 1 << UDF_LOGICAL_SECTOR_SHIFT;
  if (Offset >= EndDiskOffset) {
    Status = EFI_UNSUPPORTED;
    goto Exit;
  }

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       Offset,
                       sizeof (UDF_VOLUME_DESCRIPTOR),
                       (VOID *)&VolDescriptor
                       );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if (CompareMem (
	(VOID *)&VolDescriptor.StandardIdentifier,
	(VOID *)&gUdfStandardIdentifiers[TEA_IDENTIFIER],
	UDF_STANDARD_IDENTIFIER_LENGTH
	)
    ) {
    Status = EFI_UNSUPPORTED;
    goto Exit;
  }

  Status = FindAnchorVolumeDescriptorPointer (BlockIo, DiskIo, &AnchorPoint);
  if (EFI_ERROR (Status)) {
    Status = EFI_UNSUPPORTED;
  }

Exit:
  return Status;
}
