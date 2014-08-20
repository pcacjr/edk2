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

/**
  Open the root directory on a volume.

  @param  This Protocol instance pointer.
  @param  Root Returns an Open file handle for the root directory

  @retval EFI_SUCCESS          The device was opened.
  @retval EFI_UNSUPPORTED      This volume does not support the file system.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_ACCESS_DENIED    The service denied access to the file.
  @retval EFI_OUT_OF_RESOURCES The volume was not opened due to lack of resources.

**/
EFI_STATUS
EFIAPI
UdfOpenVolume (
  IN EFI_SIMPLE_FILE_SYSTEM_PROTOCOL     *This,
  OUT EFI_FILE_PROTOCOL                  **Root
  )
{
  EFI_TPL                                OldTpl;
  EFI_STATUS                             Status;
  PRIVATE_UDF_SIMPLE_FS_DATA             *PrivFsData;
  PRIVATE_UDF_FILE_DATA                  *PrivFileData;
  UDF_PARTITION_DESCRIPTOR               PartitionDesc;
  UDF_LOGICAL_VOLUME_DESCRIPTOR          LogicalVolDesc;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  PrivFsData = PRIVATE_UDF_SIMPLE_FS_DATA_FROM_THIS (This);

  PrivFileData = AllocateZeroPool (sizeof (PRIVATE_UDF_FILE_DATA));
  if (!PrivFileData) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  Status = FindRootDirectory (
                  PrivFsData->BlockIo,
		  PrivFsData->DiskIo,
		  &PartitionDesc,
		  &LogicalVolDesc,
		  &PrivFileData->Root.FileEntry,
		  &PrivFileData->Root.FileIdentifierDesc
                  );
  if (EFI_ERROR (Status)) {
    goto FreeExit;
  }

  PrivFileData->Partition.StartingLocation =
           PartitionDesc.PartitionStartingLocation;
  PrivFileData->Partition.Length =
           PartitionDesc.PartitionLength;
  PrivFileData->Partition.AccessType = PartitionDesc.AccessType;

  CopyMem (
    (VOID *)&PrivFileData->Volume.Identifier,
    (VOID *)&LogicalVolDesc.LogicalVolumeIdentifier,
    LOGICAL_VOLUME_IDENTIFIER_LENGTH
    );

  PrivFileData->Signature   = PRIVATE_UDF_FILE_DATA_SIGNATURE;
  PrivFileData->SimpleFs    = This;
  PrivFileData->BlockIo     = PrivFsData->BlockIo;
  PrivFileData->DiskIo      = PrivFsData->DiskIo;

  PrivFileData->FileIo.Revision      = EFI_FILE_PROTOCOL_REVISION;
  PrivFileData->FileIo.Open          = UdfOpen;
  PrivFileData->FileIo.Close         = UdfClose;
  PrivFileData->FileIo.Delete        = UdfDelete;
  PrivFileData->FileIo.Read          = UdfRead;
  PrivFileData->FileIo.Write         = UdfWrite;
  PrivFileData->FileIo.GetPosition   = UdfGetPosition;
  PrivFileData->FileIo.SetPosition   = UdfSetPosition;
  PrivFileData->FileIo.GetInfo       = UdfGetInfo;
  PrivFileData->FileIo.SetInfo       = UdfSetInfo;
  PrivFileData->FileIo.Flush         = UdfFlush;

  PrivFileData->IsRootDirectory   = TRUE;
  PrivFileData->FilePosition      = 0;
  PrivFileData->NextEntryOffset   = 0;

  *Root = &PrivFileData->FileIo;

Exit:
  gBS->RestoreTPL (OldTpl);

  return Status;

FreeExit:
  FreePool ((VOID *)PrivFileData);

  gBS->RestoreTPL (OldTpl);

  return Status;
}

/**
  Opens a new file relative to the source file's location.

  @param  This       The protocol instance pointer.
  @param  NewHandle  Returns File Handle for FileName.
  @param  FileName   Null terminated string. "\", ".", and ".." are supported.
  @param  OpenMode   Open mode for file.
  @param  Attributes Only used for EFI_FILE_MODE_CREATE.

  @retval EFI_SUCCESS          The device was opened.
  @retval EFI_NOT_FOUND        The specified file could not be found on the device.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_MEDIA_CHANGED    The media has changed.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_ACCESS_DENIED    The service denied access to the file.
  @retval EFI_OUT_OF_RESOURCES The volume was not opened due to lack of resources.
  @retval EFI_VOLUME_FULL      The volume is full.

**/
EFI_STATUS
EFIAPI
UdfOpen (
  IN  EFI_FILE_PROTOCOL                  *This,
  OUT EFI_FILE_PROTOCOL                  **NewHandle,
  IN  CHAR16                             *FileName,
  IN  UINT64                             OpenMode,
  IN  UINT64                             Attributes
  )
{
  EFI_TPL                                OldTpl;
  EFI_STATUS                             Status;
  PRIVATE_UDF_FILE_DATA                  *PrivFileData;
  UINT32                                 PartitionStartingLocation;
  UINT32                                 PartitionLength;
  UDF_FILE_ENTRY                         FileEntry;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         CurFileIdentifierDesc;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         ParentFileIdentifierDesc;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         FileIdentifierDesc;
  UINT64                                 NextOffset;
  BOOLEAN                                ReadDone;
  CHAR16                                 *String;
  CHAR16                                 *TempString;
  UINT64                                 Offset;
  CHAR16                                 *FileNameSavedPointer;
  CHAR16                                 *NextFileName;
  CHAR16                                 *TempFileName;
  EFI_BLOCK_IO_PROTOCOL                  *BlockIo;
  EFI_DISK_IO_PROTOCOL                   *DiskIo;
  BOOLEAN                                Found;
  PRIVATE_UDF_FILE_DATA                  *NewPrivFileData;
  UDF_LONG_ALLOCATION_DESCRIPTOR         *LongAd;
  UINT64                                 Lsn;
  BOOLEAN                                IsRootDirectory;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);
  String = NULL;

  if ((!This) || (!NewHandle) || (!FileName)) {
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  PartitionStartingLocation   = PrivFileData->Partition.StartingLocation;
  PartitionLength             = PrivFileData->Partition.Length;

  FileName = MangleFileName (FileName);
  if ((!FileName) || ((FileName) && (!*FileName))) {
    Status = EFI_NOT_FOUND;
    goto Exit;
  }

  String = AllocatePool ((StrLen (FileName) + 1) * sizeof (CHAR16));
  if (!String) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  BlockIo                = PrivFileData->BlockIo;
  DiskIo                 = PrivFileData->DiskIo;
  Found                  = FALSE;
  NextFileName           = NULL;
  NewPrivFileData        = NULL;
  FileNameSavedPointer   = FileName;

  if (PrivFileData->IsRootDirectory) {
    CopyMem (
      (VOID *)&CurFileIdentifierDesc,
      (VOID *)&PrivFileData->Root.FileIdentifierDesc,
      sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)
      );
    CopyMem (
      (VOID *)&ParentFileIdentifierDesc,
      (VOID *)&PrivFileData->Root.FileIdentifierDesc,
      sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)
      );
  } else {
    CopyMem (
      (VOID *)&CurFileIdentifierDesc,
      (VOID *)&PrivFileData->File.FileIdentifierDesc,
      sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)
      );
    CopyMem (
      (VOID *)&ParentFileIdentifierDesc,
      (VOID *)&PrivFileData->File.ParentFileIdentifierDesc,
      sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)
      );
  }

  CopyMem (
    (VOID *)&FileIdentifierDesc,
    (VOID *)&CurFileIdentifierDesc,
    sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)
    );

  for (;;) {
NextLookup:
    if (NextFileName) {
      FreePool ((VOID *)NextFileName);
      NextFileName = NULL;
    }

    //
    // Parse FileName
    //
    if (!*FileName) {
      break;
    }

    Offset = 0;

    TempString = String;
    while ((*FileName) && (*FileName != L'\\')) {
      *TempString++ = *FileName++;
    }

    *TempString = L'\0';

    if ((*FileName) && (*FileName == L'\\')) {
      FileName++;
    }

    Found             = FALSE;
    IsRootDirectory   = FALSE;

    if (!*String) {
      CopyMem (
	(VOID *)&FileIdentifierDesc,
	(VOID *)&PrivFileData->Root.FileIdentifierDesc,
	sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)
	);
      if (!*FileName) {
	Found             = TRUE;
	IsRootDirectory   = TRUE;
	break;
      } else {
	continue;
      }
    }

    if (StrCmp (String, L"..") == 0) {
      //
      // Make sure we're not going to look up Parent FID from a root
      // directory or even if the current FID is not a directory(!)
      //
      if ((IS_FID_PARENT_FILE (&FileIdentifierDesc)) ||
	  (!IS_FID_DIRECTORY_FILE (&FileIdentifierDesc))) {
	break;
      }

      CopyMem (
	(VOID *)&FileIdentifierDesc,
	(VOID *)&ParentFileIdentifierDesc,
	sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)
	);

      Found = TRUE;
      continue;
    } else if (StrCmp (String, L".") == 0) {
      Found = TRUE;
      continue;
    }

    //
    // Start lookup
    //
    CopyMem (
      (VOID *)&CurFileIdentifierDesc,
      (VOID *)&FileIdentifierDesc,
      sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)
      );

    NextOffset = 0;

    for (;;) {
      Status = ReadDirectory (
                          BlockIo,
			  DiskIo,
			  PartitionStartingLocation,
			  PartitionLength,
			  &CurFileIdentifierDesc,
			  &FileIdentifierDesc,
			  &NextOffset,
			  &ReadDone
	                  );
      if (EFI_ERROR (Status)) {
	goto Exit;
      }

      if (!ReadDone) {
	Status = EFI_NOT_FOUND;
	goto Exit;
      }

      Status = FileIdentifierDescToFileName (
                             &FileIdentifierDesc,
			     &NextFileName
	                     );
      if (EFI_ERROR (Status)) {
	goto Exit;
      }

      //
      // Check whether FID's File Identifier contains the expected filename
      //
      if (StrCmp (NextFileName, String) == 0) {
	CopyMem (
	  (VOID *)&ParentFileIdentifierDesc,
	  (VOID *)&CurFileIdentifierDesc,
	  sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)
	  );

	Found = TRUE;
	goto NextLookup;
      }

      if (NextFileName) {
	FreePool ((VOID *)NextFileName);
      }
    }
  }

  if (Found) {
    NewPrivFileData = AllocateZeroPool (sizeof (PRIVATE_UDF_FILE_DATA));
    if (!NewPrivFileData) {
      Status = EFI_OUT_OF_RESOURCES;
      goto Exit;
    }

    CopyMem (
      (VOID *)NewPrivFileData,
      (VOID *)PrivFileData,
      sizeof (PRIVATE_UDF_FILE_DATA)
      );

    if (IsRootDirectory) {
      NewPrivFileData->AbsoluteFileName   = NULL;
      NewPrivFileData->FileName           = NULL;
      goto HandleRootDirectory;
    }

    FileName = FileNameSavedPointer;

    NewPrivFileData->AbsoluteFileName = AllocatePool (
                                  ((PrivFileData->AbsoluteFileName ?
				    StrLen (PrivFileData->AbsoluteFileName) :
				    0) +
				   StrLen (FileName)) *
				  sizeof (CHAR16) +
				  sizeof (CHAR16) +
				  sizeof (CHAR16)
                                   );
    if (!NewPrivFileData->AbsoluteFileName) {
      Status = EFI_OUT_OF_RESOURCES;
      goto Exit;
    }

    NewPrivFileData->AbsoluteFileName[0] = L'\0';
    if (PrivFileData->AbsoluteFileName) {
      StrCat (
	NewPrivFileData->AbsoluteFileName,
	PrivFileData->AbsoluteFileName
	);
      StrCat (NewPrivFileData->AbsoluteFileName, L"\\");
    }

    StrCat (NewPrivFileData->AbsoluteFileName, FileName);

    NewPrivFileData->AbsoluteFileName = MangleFileName (
                                           NewPrivFileData->AbsoluteFileName
                                           );

    FileName = NewPrivFileData->AbsoluteFileName;
    while ((TempFileName = StrStr (FileName, L"\\"))) {
      FileName = TempFileName + 1;
    }

    NewPrivFileData->FileName = AllocatePool (
                                   StrLen (FileName) * sizeof (CHAR16) +
				   sizeof (CHAR16)
                                   );

    NewPrivFileData->FileName[0] = L'\0';
    StrCat (NewPrivFileData->FileName, FileName);

    //
    // Find FE of the FID
    //
    LongAd = &FileIdentifierDesc.Icb;

    Lsn = (UINT64)(PartitionStartingLocation +
		   LongAd->ExtentLocation.LogicalBlockNumber);

    Offset = Lsn * LOGICAL_BLOCK_SIZE;

    //
    // Read FE
    //
    Status = DiskIo->ReadDisk (
                         DiskIo,
			 BlockIo->Media->MediaId,
			 Offset,
			 sizeof (UDF_FILE_ENTRY),
			 (VOID *)&FileEntry
                         );
    if (EFI_ERROR (Status)) {
      goto Exit;
    }

    if (!IS_FE (&FileEntry)) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
    }

    CopyMem (
      (VOID *)&NewPrivFileData->File.FileEntry,
      (VOID *)&FileEntry,
      sizeof (UDF_FILE_ENTRY)
      );
    CopyMem (
      (VOID *)&NewPrivFileData->File.FileIdentifierDesc,
      (VOID *)&FileIdentifierDesc,
      sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)
      );
    CopyMem (
      (VOID *)&NewPrivFileData->File.ParentFileIdentifierDesc,
      (VOID *)&ParentFileIdentifierDesc,
      sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)
      );

HandleRootDirectory:
    NewPrivFileData->IsRootDirectory   = IsRootDirectory;
    NewPrivFileData->FilePosition      = 0;
    NewPrivFileData->NextEntryOffset   = 0;

    *NewHandle = &NewPrivFileData->FileIo;
    Status = EFI_SUCCESS;
  } else {
    Status = EFI_NOT_FOUND;
  }

Exit:
  if (String) {
    FreePool ((VOID *)String);
  }

  gBS->RestoreTPL (OldTpl);

  return Status;
}

/**
  Read data from the file.

  @param  This       Protocol instance pointer.
  @param  BufferSize On input size of buffer, on output amount of data in buffer.
  @param  Buffer     The buffer in which data is read.

  @retval EFI_SUCCESS          Data was read.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_BUFFER_TO_SMALL  BufferSize is too small. BufferSize contains required size.

**/
EFI_STATUS
EFIAPI
UdfRead (
  IN     EFI_FILE_PROTOCOL               *This,
  IN OUT UINTN                           *BufferSize,
  OUT    VOID                            *Buffer
  )
{
  return EFI_DEVICE_ERROR;
}

/**
  Close the file handle

  @param  This          Protocol instance pointer.

  @retval EFI_SUCCESS   The file was closed.

**/
EFI_STATUS
EFIAPI
UdfClose (
  IN EFI_FILE_PROTOCOL                   *This
  )
{
  EFI_TPL                                OldTpl;
  EFI_STATUS                             Status;
  PRIVATE_UDF_FILE_DATA                  *PrivFileData;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  if (!This) {
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  Status = EFI_SUCCESS;

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  if (PrivFileData->AbsoluteFileName) {
    FreePool ((VOID *)PrivFileData->AbsoluteFileName);
  }

  if (PrivFileData->FileName) {
    FreePool ((VOID *)PrivFileData->FileName);
  }

  FreePool ((VOID *)PrivFileData);

Exit:
  gBS->RestoreTPL (OldTpl);

  return Status;
}

/**
  Close and delete the file handle.

  @param  This                     Protocol instance pointer.

  @retval EFI_SUCCESS              The file was closed and deleted.
  @retval EFI_WARN_DELETE_FAILURE  The handle was closed but the file was not deleted.

**/
EFI_STATUS
EFIAPI
UdfDelete (
  IN EFI_FILE_PROTOCOL  *This
  )
{
  PRIVATE_UDF_FILE_DATA *PrivFileData;

  if (!This) {
    return EFI_INVALID_PARAMETER;
  }

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  (VOID)PrivFileData->FileIo.Close(This);

  return EFI_WARN_DELETE_FAILURE;
}

/**
  Write data to a file.

  @param  This       Protocol instance pointer.
  @param  BufferSize On input size of buffer, on output amount of data in buffer.
  @param  Buffer     The buffer in which data to write.

  @retval EFI_SUCCESS          Data was written.
  @retval EFI_UNSUPPORTED      Writes to Open directory are not supported.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_DEVICE_ERROR     An attempt was made to write to a deleted file.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_WRITE_PROTECTED  The device is write protected.
  @retval EFI_ACCESS_DENIED    The file was open for read only.
  @retval EFI_VOLUME_FULL      The volume is full.

**/
EFI_STATUS
EFIAPI
UdfWrite (
  IN     EFI_FILE_PROTOCOL  *This,
  IN OUT UINTN              *BufferSize,
  IN     VOID               *Buffer
  )
{
  return EFI_UNSUPPORTED;
}

/**
  Get a file's current position

  @param  This            Protocol instance pointer.
  @param  Position        Byte position from the start of the file.

  @retval EFI_SUCCESS     Position was updated.
  @retval EFI_UNSUPPORTED Seek request for directories is not valid.

**/
EFI_STATUS
EFIAPI
UdfGetPosition (
  IN  EFI_FILE_PROTOCOL   *This,
  OUT UINT64              *Position
  )
{
  return EFI_UNSUPPORTED;
}

/**
  Set file's current position

  @param  This            Protocol instance pointer.
  @param  Position        Byte position from the start of the file.

  @retval EFI_SUCCESS     Position was updated.
  @retval EFI_UNSUPPORTED Seek request for non-zero is not valid on open..

**/
EFI_STATUS
EFIAPI
UdfSetPosition (
  IN EFI_FILE_PROTOCOL             *This,
  IN UINT64                        Position
  )
{
  return EFI_UNSUPPORTED;
}

/**
  Get information about a file.

  @param  This            Protocol instance pointer.
  @param  InformationType Type of information to return in Buffer.
  @param  BufferSize      On input size of buffer, on output amount of data in buffer.
  @param  Buffer          The buffer to return data.

  @retval EFI_SUCCESS          Data was returned.
  @retval EFI_UNSUPPORTED      InformationType is not supported.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_WRITE_PROTECTED  The device is write protected.
  @retval EFI_ACCESS_DENIED    The file was open for read only.
  @retval EFI_BUFFER_TOO_SMALL Buffer was too small; required size returned in BufferSize.

**/
EFI_STATUS
EFIAPI
UdfGetInfo (
  IN     EFI_FILE_PROTOCOL               *This,
  IN     EFI_GUID                        *InformationType,
  IN OUT UINTN                           *BufferSize,
  OUT    VOID                            *Buffer
  )
{
  return EFI_DEVICE_ERROR;
}

/**
  Set information about a file

  @param  File            Protocol instance pointer.
  @param  InformationType Type of information in Buffer.
  @param  BufferSize      Size of buffer.
  @param  Buffer          The data to write.

  @retval EFI_SUCCESS          Data was set.
  @retval EFI_UNSUPPORTED      InformationType is not supported.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_WRITE_PROTECTED  The device is write protected.
  @retval EFI_ACCESS_DENIED    The file was open for read only.

**/
EFI_STATUS
EFIAPI
UdfSetInfo (
  IN EFI_FILE_PROTOCOL*This,
  IN EFI_GUID         *InformationType,
  IN UINTN            BufferSize,
  IN VOID             *Buffer
  )
{
  return EFI_WRITE_PROTECTED;
}

/**
  Flush data back for the file handle.

  @param  This Protocol instance pointer.

  @retval EFI_SUCCESS          Data was flushed.
  @retval EFI_UNSUPPORTED      Writes to Open directory are not supported.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_WRITE_PROTECTED  The device is write protected.
  @retval EFI_ACCESS_DENIED    The file was open for read only.
  @retval EFI_VOLUME_FULL      The volume is full.

**/
EFI_STATUS
EFIAPI
UdfFlush (
  IN EFI_FILE_PROTOCOL  *This
  )
{
  return EFI_WRITE_PROTECTED;
}

STATIC
EFI_STATUS
FindAnchorVolumeDescriptorPointer (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  OUT UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER   *AnchorPoint
  )
{
  EFI_STATUS                                 Status;

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       FIRST_ANCHOR_POINT_LSN * LOGICAL_SECTOR_SIZE,
                       sizeof (UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER),
                       (VOID *)AnchorPoint
                       );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  //
  // TODO: In case of failure, look for the other AVDPs at N or N - 256
  //
  if (!IS_AVDP (AnchorPoint)) {
    Status = EFI_VOLUME_CORRUPTED;
    goto Exit;
  }

Exit:
  return Status;
}

STATIC
EFI_STATUS
StartMainVolumeDescriptorSequence (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER    *AnchorPoint,
  OUT UDF_PARTITION_DESCRIPTOR               *PartitionDesc,
  OUT UDF_LOGICAL_VOLUME_DESCRIPTOR          *LogicalVolDesc
  )
{
  EFI_STATUS                                 Status;
  UDF_EXTENT_AD                              *ExtentAd;
  UINT32                                     StartingLsn;
  UINT32                                     EndingLsn;
  UINT8                                      Buffer[LOGICAL_SECTOR_SIZE];

  ExtentAd = &AnchorPoint->MainVolumeDescriptorSequenceExtent;

  if (ExtentAd->ExtentLength / LOGICAL_SECTOR_SIZE < 16) {
    Status = EFI_VOLUME_CORRUPTED;
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
  StartingLsn = ExtentAd->ExtentLocation;
  EndingLsn   = StartingLsn + (ExtentAd->ExtentLength / LOGICAL_SECTOR_SIZE);

  while (StartingLsn <= EndingLsn) {
    Status = DiskIo->ReadDisk (
                         DiskIo,
			 BlockIo->Media->MediaId,
			 StartingLsn * LOGICAL_SECTOR_SIZE,
			 LOGICAL_SECTOR_SIZE,
			 (VOID *)&Buffer
                         );
    if (EFI_ERROR (Status)) {
      goto Exit;
    }

    //
    // Stop VDS when found Terminating Descriptor
    //
    if (IS_TD (Buffer)) {
      break;
    }

    if (IS_PD (Buffer)) {
      CopyMem (
	(VOID *)PartitionDesc,
	(VOID *)&Buffer,
	sizeof (UDF_PARTITION_DESCRIPTOR)
	);
    } else if (IS_LVD (Buffer)) {
      CopyMem (
	(VOID *)LogicalVolDesc,
	(VOID *)&Buffer,
	sizeof (UDF_LOGICAL_VOLUME_DESCRIPTOR)
	);
    }

    StartingLsn++;
  }

Exit:
  return Status;
}

STATIC
EFI_STATUS
FindFileSetDescriptor (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_PARTITION_DESCRIPTOR                *PartitionDesc,
  IN UDF_LOGICAL_VOLUME_DESCRIPTOR           *LogicalVolDesc,
  OUT UDF_FILE_SET_DESCRIPTOR                *FileSetDesc
  )
{
  EFI_STATUS                                 Status;
  UDF_LONG_ALLOCATION_DESCRIPTOR             *LongAd;
  UINT64                                     Lsn;

  LongAd = &LogicalVolDesc->LogicalVolumeContentsUse;

  Lsn = (UINT64)(PartitionDesc->PartitionStartingLocation +
		 LongAd->ExtentLocation.LogicalBlockNumber);

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       Lsn * LOGICAL_BLOCK_SIZE,
                       sizeof (UDF_FILE_SET_DESCRIPTOR),
                       (VOID *)FileSetDesc
                       );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if (!IS_FSD (FileSetDesc)) {
      Status = EFI_VOLUME_CORRUPTED;
  }

Exit:
  return Status;
}

STATIC
EFI_STATUS
FindFileEntryRootDirectory (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_PARTITION_DESCRIPTOR                *PartitionDesc,
  IN UDF_FILE_SET_DESCRIPTOR                 *FileSetDesc,
  OUT UDF_FILE_ENTRY                         *FileEntry
  )
{
  EFI_STATUS                                 Status;
  UDF_LONG_ALLOCATION_DESCRIPTOR             *LongAd;
  UINT64                                     Lsn;

  LongAd = &FileSetDesc->RootDirectoryIcb;

  Lsn = (UINT64)(PartitionDesc->PartitionStartingLocation +
		 LongAd->ExtentLocation.LogicalBlockNumber);

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       Lsn * LOGICAL_BLOCK_SIZE,
		       sizeof (UDF_FILE_ENTRY),
                       (VOID *)FileEntry
                       );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if (!IS_FE (FileEntry)) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
  }

  //
  // Root Directory cannot be file, obivously. Check its file type.
  //
  if (!IS_FE_DIRECTORY (FileEntry)) {
    Status = EFI_VOLUME_CORRUPTED;
  }

Exit:
  return Status;
}

EFI_STATUS
FindFileIdentifierDescriptorRootDirectory (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_PARTITION_DESCRIPTOR                *PartitionDesc,
  IN UDF_FILE_SET_DESCRIPTOR                 *FileSetDesc,
  OUT UDF_FILE_IDENTIFIER_DESCRIPTOR         *FileIdentifierDesc
  )
{
  UDF_LONG_ALLOCATION_DESCRIPTOR            *LongAd;
  EFI_STATUS                                 Status;
  UINT64                                     Lsn;

  LongAd = &FileSetDesc->RootDirectoryIcb;

  //
  // TODO: Handle strategy type of 4096 as well.
  //
  // For ICB strategy type of 4, the File Identifier Descriptor of the Root
  // Directory immediately follows the File Entry (Root Directory).
  //
  Lsn = (UINT64)(PartitionDesc->PartitionStartingLocation +
		 LongAd->ExtentLocation.LogicalBlockNumber + 1);

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       Lsn * LOGICAL_BLOCK_SIZE,
                       LOGICAL_BLOCK_SIZE,
                       (VOID *)FileIdentifierDesc
                       );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if (!IS_FID (FileIdentifierDesc)) {
    Status = EFI_VOLUME_CORRUPTED;
  }

Exit:
  return Status;
}

EFI_STATUS
EFIAPI
FindRootDirectory (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  OUT UDF_PARTITION_DESCRIPTOR               *PartitionDesc,
  OUT UDF_LOGICAL_VOLUME_DESCRIPTOR          *LogicalVolDesc,
  OUT UDF_FILE_ENTRY                         *FileEntry,
  OUT UDF_FILE_IDENTIFIER_DESCRIPTOR         *FileIdentifierDesc
  )
{
  EFI_STATUS                                 Status;
  UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER       AnchorPoint;
  UDF_FILE_SET_DESCRIPTOR                    FileSetDesc;

  Status = FindAnchorVolumeDescriptorPointer (
                                          BlockIo,
					  DiskIo,
					  &AnchorPoint
                                          );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = StartMainVolumeDescriptorSequence (
                                          BlockIo,
					  DiskIo,
					  &AnchorPoint,
					  PartitionDesc,
					  LogicalVolDesc
                                          );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = FindFileSetDescriptor (
                              BlockIo,
			      DiskIo,
			      PartitionDesc,
			      LogicalVolDesc,
			      &FileSetDesc
                              );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = FindFileEntryRootDirectory (
                                   BlockIo,
				   DiskIo,
				   PartitionDesc,
				   &FileSetDesc,
				   FileEntry
                                   );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = FindFileIdentifierDescriptorRootDirectory (
                                                  BlockIo,
						  DiskIo,
						  PartitionDesc,
						  &FileSetDesc,
						  FileIdentifierDesc
                                                  );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

Exit:
  return Status;
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
                       BEA_DESCRIPTOR_LSN * LOGICAL_SECTOR_SIZE,
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
                       (BEA_DESCRIPTOR_LSN + 1) * LOGICAL_SECTOR_SIZE,
                       sizeof (UDF_NSR_DESCRIPTOR),
                       (VOID *)&NsrDescriptor
                       );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if ((CompareMem (
	 (VOID *)&NsrDescriptor.StandardIdentifier,
	 (VOID *)&gUdfStandardIdentifiers[VSD_IDENTIFIER_0],
	 UDF_STANDARD_IDENTIFIER_LENGTH
	 )
      ) &&
      (CompareMem (
	(VOID *)&NsrDescriptor.StandardIdentifier,
	(VOID *)&gUdfStandardIdentifiers[VSD_IDENTIFIER_1],
	UDF_STANDARD_IDENTIFIER_LENGTH
	)
      )
    ) {
    goto Exit;
  }

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       (BEA_DESCRIPTOR_LSN + 2) * LOGICAL_SECTOR_SIZE,
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

EFI_STATUS
EFIAPI
ReadDirectory (
  IN EFI_BLOCK_IO_PROTOCOL                  *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                   *DiskIo,
  IN UINT32                                 PartitionStartingLocation,
  IN UINT32                                 PartitionLength,
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR         *ParentFileIdentifierDesc,
  OUT UDF_FILE_IDENTIFIER_DESCRIPTOR        *ReadFileIdentifierDesc,
  IN OUT UINT64                             *NextOffset,
  OUT BOOLEAN                               *ReadDone
  )
{
  EFI_STATUS                                Status;
  UDF_LONG_ALLOCATION_DESCRIPTOR            *LongAd;
  UINT64                                    Lsn;
  UINT64                                    ParentOffset;
  UINT64                                    FidLength;
  UINT64                                    Offset;
  UINT64                                    EndingPartitionOffset;

  Status       = EFI_SUCCESS;
  *ReadDone    = FALSE;

  //
  // Check if Parent is _really_ a directory. Otherwise, do nothing.
  //
  if (!IS_FID_DIRECTORY_FILE (ParentFileIdentifierDesc)) {
    Status = EFI_NOT_FOUND;
    goto Exit;
  }

  LongAd = &ParentFileIdentifierDesc->Icb;

  //
  // Point to the Parent FID
  //
  Lsn = (UINT64)(PartitionStartingLocation +
		 LongAd->ExtentLocation.LogicalBlockNumber + 1);

  //
  // Calculate offset of the Parent FID
  //
  ParentOffset = Lsn * LOGICAL_BLOCK_SIZE;

  EndingPartitionOffset = (UINT64)((UINT64)(PartitionStartingLocation +
					    PartitionLength) *
				   LOGICAL_BLOCK_SIZE);

  if (!*NextOffset) {
    Offset = ParentOffset;
  } else {
    Offset = *NextOffset;
  }

  //
  // Make sure we don't across a partition boundary
  //
  if (Offset > EndingPartitionOffset) {
    goto Exit;
  }

  Status = DiskIo->ReadDisk (
                          DiskIo,
			  BlockIo->Media->MediaId,
			  Offset,
			  sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR),
			  (VOID *)ReadFileIdentifierDesc
                          );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if (!IS_FID (ReadFileIdentifierDesc)) {
    goto Exit;
  }

  FidLength = 38 + // Offset of Implementation Use field
    ReadFileIdentifierDesc->LengthOfFileIdentifier +
    ReadFileIdentifierDesc->LengthOfImplementationUse +
    (4 * ((ReadFileIdentifierDesc->LengthOfFileIdentifier +
	   ReadFileIdentifierDesc->LengthOfImplementationUse + 38 + 3) / 4) -
     (ReadFileIdentifierDesc->LengthOfFileIdentifier +
      ReadFileIdentifierDesc->LengthOfImplementationUse + 38));

  *NextOffset   = Offset + FidLength;
  *ReadDone     = TRUE;

Exit:
  return Status;
}

EFI_STATUS
EFIAPI
FileIdentifierDescToFileName (
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR   *FileIdentifierDesc,
  OUT CHAR16                          **FileName
  )
{
  EFI_STATUS                          Status;
  CHAR16                              *FileIdentifier;
  CHAR16                              *String;
  UINTN                               Index;

  Status = EFI_SUCCESS;

  *FileName = AllocatePool (
                 FileIdentifierDesc->LengthOfFileIdentifier +
		 sizeof (CHAR16)
                 );
  if (!*FileName) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  //
  // +2 == discards the Compression ID found in the File Identifier field
  //
  FileIdentifier = (CHAR16 *)(
     (UINT8 *)&FileIdentifierDesc->Data +
     FileIdentifierDesc->LengthOfImplementationUse + 2
     );

  String = *FileName;

  for (Index = 2;
       Index < FileIdentifierDesc->LengthOfFileIdentifier;
       Index += 2
      ) {
    *String++ = *FileIdentifier++;
  }

  *String = '\0';

Exit:
  return Status;
}
