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

  BlockSize  = BlockIo->Media->BlockSize;
  EndLBA     = BlockIo->Media->LastBlock;

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
  UINT32                         StartingLsn;
  UINT32                         EndingLsn;
  VOID                           *Buffer;
  UDF_LOGICAL_VOLUME_DESCRIPTOR  *LogicalVolDesc;
  UDF_PARTITION_DESCRIPTOR       *PartitionDesc;
  UINTN                          Index;

  BlockSize    = BlockIo->Media->BlockSize;
  ExtentAd     = &AnchorPoint->MainVolumeDescriptorSequenceExtent;
  StartingLsn  = ExtentAd->ExtentLocation;
  EndingLsn    = StartingLsn + DivU64x32 (
                                     ExtentAd->ExtentLength,
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
      break;
    }

    if (IS_LVD (Buffer)) {
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
    }

    StartingLsn++;
  }

  FreePool (Buffer);

  return Status;

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

UINT64
GetLongAdLsn (
  IN UDF_VOLUME_INFO                 *Volume,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR  *LongAd
  )
{
  UDF_PARTITION_DESCRIPTOR *PartitionDesc;

  PartitionDesc = GetPdFromLongAd (Volume, LongAd);

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

  for (Index = 0; Index < Volume->LogicalVolDescsNo; Index++) {
    FileSetDesc = AllocateZeroPool (sizeof (UDF_FILE_SET_DESCRIPTOR));
    if (!FileSetDesc) {
      Status = EFI_OUT_OF_RESOURCES;
      goto Error_Alloc_Fsd;
    }

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

  return Status;
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

  Length = ReadFileInfo.ReadLength;

  Data = (UINT8 *)ReadFileInfo.FileData;
  EndData = Data + Length;

  CopyMem ((VOID *)&PreviousFile, (VOID *)Parent, sizeof (UDF_FILE_INFO));

  for (;;) {
    PathComp = (UDF_PATH_COMPONENT *)Data;

    PathCompLength = PathComp->LengthOfComponentIdentifier;

    switch (PathComp->ComponentType) {
      case 1:
      case 2:
	goto Next_Path_Component;
      case 3:
	CopyMem ((VOID *)FileName, L"..", 6);
	break;
      case 4:
	DuplicateFe (BlockIo, Volume, PreviousFile.FileEntry, &File->FileEntry);
	DuplicateFid (PreviousFile.FileIdentifierDesc, &File->FileIdentifierDesc);
	goto Next_Path_Component;
      case 5:
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

  *FileEntry = AllocateZeroPool (1 << UDF_LOGICAL_SECTOR_SHIFT);
  if (!*FileEntry) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = DiskIo->ReadDisk (
                          DiskIo,
			  BlockIo->Media->MediaId,
			  MultU64x32 (Lsn, LogicalBlockSize),
			  1 << UDF_LOGICAL_SECTOR_SHIFT,
			  *FileEntry
                          );
  if (EFI_ERROR (Status)) {
    goto Error_Read_Disk_Blk;
  }

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

VOID
DuplicateFid (
  IN   UDF_FILE_IDENTIFIER_DESCRIPTOR  *FileIdentifierDesc,
  OUT  UDF_FILE_IDENTIFIER_DESCRIPTOR  **NewFileIdentifierDesc
  )
{
  *NewFileIdentifierDesc = AllocateCopyPool (
                                   GetFidDescriptorLength (FileIdentifierDesc),
				   FileIdentifierDesc
                                   );
}

VOID
DuplicateFe (
  IN   EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN   UDF_VOLUME_INFO        *Volume,
  IN   VOID                   *FileEntry,
  OUT  VOID                   **NewFileEntry
  )
{
  *NewFileEntry = AllocateCopyPool (1 << UDF_LOGICAL_SECTOR_SHIFT, FileEntry);
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
  if (!IS_VALID_COMPRESSION_ID (CompressionId)) {
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
    DuplicateFe (BlockIo, Volume, Parent->FileEntry, &File->FileEntry);
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
      if (!StrCmp (FileName, L"..") || !StrCmp (FileName, L"\\")) {
	Found = TRUE;
	break;
      }
    } else {
      if (FileNameLength != FileIdentifierDesc->LengthOfFileIdentifier - 1) {
	goto Skip_Fid;
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

Skip_Fid:
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
	goto Error_Find_Fe;
      }

      if (CompareMem (
	    (VOID *)Parent->FileEntry,
	    (VOID *)CompareFileEntry,
	    1 << UDF_LOGICAL_SECTOR_SHIFT
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
	DuplicateFe (BlockIo, Volume, Root->FileEntry, &File->FileEntry);
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
      CleanupFileInformation (&PreviousFile);
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
  UDF_EXTENT_FLAGS                ExtentFlags;

  for (;;) {
    if (*Offset >= Length) {
      return EFI_DEVICE_ERROR;
    }

    ShortAd = (UDF_SHORT_ALLOCATION_DESCRIPTOR *)((UINT8 *)Data +
						  *Offset);

    ExtentFlags = GET_EXTENT_FLAGS (SHORT_ADS_SEQUENCE, ShortAd);
    if (ExtentFlags == EXTENT_IS_NEXT_EXTENT ||
	ExtentFlags == EXTENT_RECORDED_AND_ALLOCATED) {
      break;
    }

    *Offset += AD_LENGTH (SHORT_ADS_SEQUENCE);
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
  UDF_LONG_ALLOCATION_DESCRIPTOR  *LongAd;
  UDF_EXTENT_FLAGS                ExtentFlags;

  for (;;) {
    if (*Offset >= Length) {
      return EFI_DEVICE_ERROR;
    }

    LongAd = (UDF_LONG_ALLOCATION_DESCRIPTOR *)(
                                           (UINT8 *)Data +
					   *Offset
                                           );

    ExtentFlags = GET_EXTENT_FLAGS (LONG_ADS_SEQUENCE, LongAd);
    if (ExtentFlags == EXTENT_IS_NEXT_EXTENT ||
	ExtentFlags == EXTENT_RECORDED_AND_ALLOCATED) {
      break;
    }

    *Offset += AD_LENGTH (LONG_ADS_SEQUENCE);
  }

  *FoundLongAd = LongAd;

  return EFI_SUCCESS;
}

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

  *BufferSize = ReadFileInfo.FileDataSize;
  *FilePosition = ReadFileInfo.FilePosition;

  return EFI_SUCCESS;
}

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

  AllocExtDesc = (UDF_ALLOCATION_EXTENT_DESCRIPTOR *)Data;
  if (!IS_AED (AllocExtDesc)) {
    Status = EFI_VOLUME_CORRUPTED;
    goto Exit;
  }

  *Offset = (UINT64)(MultU64x32 (Lsn, LogicalBlockSize) +
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
  IN   UDF_LONG_ALLOCATION_DESCRIPTOR  *ParentIcb,
  IN   UDF_FE_RECORDING_FLAGS          RecordingFlags,
  IN   VOID                            *Ad,
  OUT  VOID                            **Data,
  OUT  UINT64                          *Length
  )
{
  EFI_STATUS  Status;
  UINT64      Offset;

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
      ReadFileInfo->ReadLength = 0;
      ReadFileInfo->FileData = NULL;
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

  RecordingFlags = GET_FE_RECORDING_FLAGS (FileEntryData);
  switch (RecordingFlags) {
    case INLINE_DATA:
      GetFileEntryData (FileEntryData, &Data, &Length);

      if (ReadFileInfo->Flags == READ_FILE_GET_FILESIZE) {
	  ReadFileInfo->ReadLength = Length;
      } else if (ReadFileInfo->Flags == READ_FILE_ALLOCATE_AND_READ) {
	  ReadFileInfo->FileData = AllocatePool (Length);
	  if (!ReadFileInfo->FileData) {
	    return EFI_OUT_OF_RESOURCES;
	  }

	  CopyMem (ReadFileInfo->FileData, Data, Length);
	  ReadFileInfo->ReadLength = Length;
      } else if (ReadFileInfo->Flags == READ_FILE_SEEK_AND_READ) {
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
      GetAdsInformation (FileEntryData, &Data, &Length);
      AdOffset = 0;

      for (;;) {
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
	    Status = GrowUpBufferToNextAd (
                                     RecordingFlags,
				     Ad,
				     &ReadFileInfo->FileData,
				     ReadFileInfo->ReadLength
                                     );
	    if (EFI_ERROR (Status)) {
	      goto Error_Alloc_Buffer_To_Next_Ad;
	    }

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

	    FinishedSeeking = TRUE;

Skip_File_Seek:
	    if (ExtentLength - Offset > BytesLeft) {
	      DataLength = BytesLeft;
	    } else {
	      DataLength = ExtentLength - Offset;
	    }

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

	    DataOffset += DataLength;
	    ReadFileInfo->FilePosition += DataLength;

	    BytesLeft -= DataLength;
	    if (!BytesLeft) {
	      Status = EFI_SUCCESS;
	      goto Done;
	    }

	    break;
	}

Skip_Ad:
	AdOffset += AD_LENGTH (RecordingFlags);
      }

      break;
    case EXTENDED_ADS_SEQUENCE:
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

  FileInfo->CreateTime.TimeZone   = EFI_UNSPECIFIED_TIMEZONE;
  FileInfo->CreateTime.Daylight   = EFI_TIME_ADJUST_DAYLIGHT;

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
  UDF_EXTENT_AD                 ExtentAd;
  UINT32                        LogicalBlockSize;
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

    Status = DiskIo->ReadDisk (
                         DiskIo,
			 BlockIo->Media->MediaId,
			 MultU64x32 (ExtentAd.ExtentLocation, LogicalBlockSize),
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
	// Size not specified
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
	// Size not specified
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
