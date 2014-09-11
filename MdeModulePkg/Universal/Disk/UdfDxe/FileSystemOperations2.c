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

EFI_STATUS
FindFileEntry (
  IN EFI_BLOCK_IO_PROTOCOL                  *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                   *DiskIo,
  IN UINT32                                 PartitionStartingLocation,
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR         *FileIdentifierDesc,
  OUT VOID                                  *FileEntry
  );

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
  UDF_PARTITION_DESCRIPTOR               **PartitionDescs;
  UINTN                                  PartitionDescsNo;
  UDF_FILE_SET_DESCRIPTOR                **FileSetDescs;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  Print (L"UdfOpenVolume: in\n");

  PrivFsData = PRIVATE_UDF_SIMPLE_FS_DATA_FROM_THIS (This);

  PrivFileData = AllocateZeroPool (sizeof (PRIVATE_UDF_FILE_DATA));
  if (!PrivFileData) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  Status = ReadVolumeFileStructure (
                            PrivFsData->BlockIo,
			    PrivFsData->DiskIo,
			    &PrivFileData->Volume.LogicalVolDescs,
			    &PrivFileData->Volume.LogicalVolDescsNo,
			    &PrivFileData->Volume.PartitionDescs,
			    &PrivFileData->Volume.PartitionDescsNo
                            );
  if (EFI_ERROR (Status)) {
    goto FreeExit;
  }

  PartitionDescs     = PrivFileData->Volume.PartitionDescs;
  PartitionDescsNo   = PrivFileData->Volume.PartitionDescsNo;

  Status = GetFileSetDescriptors (
                            PrivFsData->BlockIo,
                            PrivFsData->DiskIo,
                            PrivFileData->Volume.LogicalVolDescs,
                            PrivFileData->Volume.LogicalVolDescsNo,
                            PrivFileData->Volume.PartitionDescs,
                            PrivFileData->Volume.PartitionDescsNo,
                            &PrivFileData->Volume.FileSetDescs,
                            &PrivFileData->Volume.FileSetDescsNo
                            );
  if (EFI_ERROR (Status)) {
    goto FreeExit;
  }

  FileSetDescs = PrivFileData->Volume.FileSetDescs;

  Status = FindFileEntryFromLongAd (
                                PrivFsData->BlockIo,
				PrivFsData->DiskIo,
				PartitionDescs,
				PartitionDescsNo,
				&FileSetDescs[0]->RootDirectoryIcb,
				&PrivFileData->Root.FileEntry
                                );
  if (EFI_ERROR (Status)) {
    goto FreeExit;
  }

  Status = FindFileFromFileEntry (
                            PrivFsData->BlockIo,
			    PrivFsData->DiskIo,
			    &FileSetDescs[0]->RootDirectoryIcb,
			    PartitionDescs,
			    PartitionDescsNo,
			    L"/",
			    PrivFileData->Root.FileEntry,
			    &PrivFileData->Root.FileIdentifierDesc,
			    NULL
                            );
  if (EFI_ERROR (Status)) {
    goto FreeExit;
  }

  if (!PrivFileData->Root.FileIdentifierDesc) {
    Status = EFI_VOLUME_CORRUPTED;
    goto FreeExit;
  }

  PrivFileData->File.FileEntry            = PrivFileData->Root.FileEntry;
  PrivFileData->File.FileIdentifierDesc   =
                                   PrivFileData->Root.FileIdentifierDesc;

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
  Print (L"UdfOpenVolume: exit: %r\n", Status);

  gBS->RestoreTPL (OldTpl);

  return Status;

FreeExit:
  FreePool ((VOID *)PrivFileData);

  Print (L"UdfOpenVolume: exit: %r\n", Status);

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
  CHAR16                                 *String;
  CHAR16                                 *TempString;
  CHAR16                                 *FileNameSavedPointer;
  CHAR16                                 *TempFileName;
  EFI_BLOCK_IO_PROTOCOL                  *BlockIo;
  EFI_DISK_IO_PROTOCOL                   *DiskIo;
  UDF_PARTITION_DESCRIPTOR               **PartitionDescs;
  UINTN                                  PartitionDescsNo;
  UDF_FILE_SET_DESCRIPTOR                **FileSetDescs;
  VOID                                   *FileEntry;
  VOID                                   *ParentFileEntry;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *FileIdentifierDesc;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *PrevFileIdentifierDesc;
  BOOLEAN                                FreeFileEntry;
  UINT32                                 BlockSize;
  BOOLEAN                                Found;
  PRIVATE_UDF_FILE_DATA                  *NewPrivFileData;
  BOOLEAN                                IsRootDirectory;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);
  String = NULL;

  if ((!This) || (!NewHandle) || (!FileName)) {
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  Print (L"UdfOpen(): in\n");

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  FileName = MangleFileName (FileName);
  if ((!FileName) || ((FileName) && (!*FileName))) {
    Status = EFI_NOT_FOUND;
    goto Exit;
  }

  Print (L"UdfOpen: filename: %s\n", FileName);

  String = (CHAR16 *)AllocatePool ((StrLen (FileName) + 1) * sizeof (CHAR16));
  if (!String) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  BlockIo                = PrivFileData->BlockIo;
  DiskIo                 = PrivFileData->DiskIo;
  PartitionDescs         = PrivFileData->Volume.PartitionDescs;
  PartitionDescsNo       = PrivFileData->Volume.PartitionDescsNo;
  FileSetDescs           = PrivFileData->Volume.FileSetDescs;
  BlockSize              = BlockIo->Media->BlockSize;
  Found                  = FALSE;
  FileIdentifierDesc     = NULL;
  FileEntry              = NULL;
  NewPrivFileData        = NULL;
  FileNameSavedPointer   = FileName;

  for (;;) {
    //
    // Parse filename
    //
    if (!*FileName) {
      break;
    }

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
      Print (L"UdfOpen: open root directory\n");
      if (!*FileName) {
	Found             = TRUE;
	IsRootDirectory   = TRUE;
	break;
      } else {
	continue;
      }
    }

    if (StrCmp (String, L".") == 0) {
      Print (L"UdfOpen: open current directory\n");
      Found = TRUE;
      continue;
    }

    PrevFileIdentifierDesc = FileIdentifierDesc;

    FreeFileEntry = TRUE;

    if (!FileEntry) {
      FileEntry = PrivFileData->File.FileEntry;
      FreeFileEntry = FALSE;
    }

    ParentFileEntry = FileEntry;

    Status = FindFileFromFileEntry (
                                BlockIo,
				DiskIo,
				&FileSetDescs[0]->RootDirectoryIcb,
				PartitionDescs,
				PartitionDescsNo,
				String,
				ParentFileEntry,
				&FileIdentifierDesc,
				&FileEntry
                                );

    if (FreeFileEntry) {
      FreePool ((VOID *)ParentFileEntry);
    }

    if (PrevFileIdentifierDesc) {
      FreePool ((VOID *)PrevFileIdentifierDesc);
    }

    if (EFI_ERROR (Status)) {
      goto Exit;
    }

    if ((!FileIdentifierDesc) && (!FileEntry)) {
      break;
    }

    Found = TRUE;
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

    if ((!FileEntry) && (!FileIdentifierDesc)) {
      if (IsRootDirectory) {
	Print (L"UdfOpen: handle root dir\n");
	NewPrivFileData->AbsoluteFileName   = NULL;
	NewPrivFileData->FileName           = NULL;

        Status = DuplicateFileEntry (
                                 BlockIo,
			         PrivFileData->Root.FileEntry,
			         &NewPrivFileData->Root.FileEntry
                                 );
	if (EFI_ERROR (Status)) {
	  goto FreePrivDataExit_2;
	}

        Status = DuplicateFileIdentifierDescriptor (
			         PrivFileData->Root.FileIdentifierDesc,
			         &NewPrivFileData->Root.FileIdentifierDesc
                                 );
	if (EFI_ERROR (Status)) {
	  FreePool ((VOID *)NewPrivFileData->Root.FileEntry);
	  goto FreePrivDataExit_2;
	}

        NewPrivFileData->File.FileEntry            =
                                       NewPrivFileData->Root.FileEntry;
        NewPrivFileData->File.FileIdentifierDesc   =
                                       NewPrivFileData->Root.FileIdentifierDesc;
        goto HandleRootDirectory;
      } else {
	Print (L"UdfOpen: handle current dir\n");
	if ((PrivFileData->AbsoluteFileName) && (PrivFileData->FileName)) {
	  NewPrivFileData->AbsoluteFileName =
	    (CHAR16 *)AllocatePool (
                              StrLen (PrivFileData->AbsoluteFileName) +
			      sizeof(CHAR16)
	                      );
	  if (!NewPrivFileData->AbsoluteFileName) {
	    Status = EFI_OUT_OF_RESOURCES;
	    goto FreePrivDataExit_0;
	  }

	  NewPrivFileData->FileName =
	    (CHAR16 *)AllocatePool (
                              StrLen (PrivFileData->FileName) +
			      sizeof(CHAR16)
	                      );
	  if (!NewPrivFileData->FileName) {
	    Status = EFI_OUT_OF_RESOURCES;
	    goto FreePrivDataExit_1;
	  }

	  StrCpy (
	    NewPrivFileData->AbsoluteFileName,
	    PrivFileData->AbsoluteFileName
	    );
	  StrCpy (NewPrivFileData->FileName, PrivFileData->FileName);
	}

        Status = DuplicateFileEntry (
                                 BlockIo,
			         PrivFileData->File.FileEntry,
			         &NewPrivFileData->File.FileEntry
                                 );
	if (EFI_ERROR (Status)) {
	  goto FreePrivDataExit_2;
	}

        Status = DuplicateFileIdentifierDescriptor (
			         PrivFileData->File.FileIdentifierDesc,
			         &NewPrivFileData->File.FileIdentifierDesc
                                 );
	if (EFI_ERROR (Status)) {
	  FreePool ((VOID *)NewPrivFileData->File.FileEntry);
	  goto FreePrivDataExit_2;
	}

	goto HandleCurrentDirectory;
      }
    }

    FileName = FileNameSavedPointer;

    NewPrivFileData->AbsoluteFileName =
      (CHAR16 *)AllocatePool (
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
      goto FreePrivDataExit_0;
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

    NewPrivFileData->FileName = (CHAR16 *)AllocatePool (
                                                  StrLen (FileName) *
						  sizeof (CHAR16) +
						  sizeof (CHAR16)
                                                  );
    if (!NewPrivFileData->FileName) {
      Status = EFI_OUT_OF_RESOURCES;
      goto FreePrivDataExit_1;
    }

    NewPrivFileData->FileName[0] = L'\0';
    StrCat (NewPrivFileData->FileName, FileName);

    NewPrivFileData->File.FileEntry            = FileEntry;
    NewPrivFileData->File.FileIdentifierDesc   = FileIdentifierDesc;

HandleRootDirectory:

HandleCurrentDirectory:
    NewPrivFileData->IsRootDirectory   = IsRootDirectory;
    NewPrivFileData->FilePosition      = 0;

    *NewHandle = &NewPrivFileData->FileIo;
    Status = EFI_SUCCESS;
  } else {
    Status = EFI_NOT_FOUND;
  }

Exit:
  if (String) {
    FreePool ((VOID *)String);
  }

  Print (L"UdfOpen(): exit: %r\n", Status);

  gBS->RestoreTPL (OldTpl);

  return Status;

FreePrivDataExit_2:
  if (NewPrivFileData->FileName) {
    FreePool ((VOID *)NewPrivFileData->FileName);
  }

FreePrivDataExit_1:
  if (NewPrivFileData->AbsoluteFileName) {
    FreePool ((VOID *)NewPrivFileData->AbsoluteFileName);
  }

FreePrivDataExit_0:
  FreePool ((VOID *)NewPrivFileData);

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

  Print (L"UdfClose(): in\n");
  Print (L"UdfClose(): close file handle: 0x%016lx\n", This);

  if (!This) {
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  Status = EFI_SUCCESS;

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

#if 0
  if (PrivFileData->AbsoluteFileName) {
    FreePool ((VOID *)PrivFileData->AbsoluteFileName);
  }

  if (PrivFileData->FileName) {
    FreePool ((VOID *)PrivFileData->FileName);
  }

  if (PrivFileData->IsRootDirectory) {
    if (PrivFileData->Root.FileEntry) {
      FreePool ((VOID *)PrivFileData->Root.FileEntry);
    }
    if (PrivFileData->Root.FileIdentifierDesc) {
      FreePool ((VOID *)PrivFileData->Root.FileIdentifierDesc);
    }
  } else {
    if (PrivFileData->File.FileEntry) {
      FreePool ((VOID *)PrivFileData->File.FileEntry);
    }
    if (PrivFileData->File.FileIdentifierDesc) {
      FreePool ((VOID *)PrivFileData->File.FileIdentifierDesc);
    }
  }

  FreePool ((VOID *)PrivFileData);
#endif

Exit:
  gBS->RestoreTPL (OldTpl);

  Print (L"UdfClose: exit: %r\n", Status);

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
  EFI_STATUS               Status;
  PRIVATE_UDF_FILE_DATA   *PrivFileData;

  Status = EFI_SUCCESS;

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  //
  // As per UEFI spec, if the file handle is a directory, then the current file
  // position has no meaning and the operation is not supported.
  //
  if (IS_FID_DIRECTORY_FILE (
	   &PrivFileData->File.FileIdentifierDesc
	   )
    ) {
    Status = EFI_UNSUPPORTED;
  } else {
    //
    // The file is not a directory. So, return its position.
    //
    *Position = PrivFileData->FilePosition;
  }

  return Status;
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
  EFI_STATUS                       Status;
  PRIVATE_UDF_FILE_DATA            *PrivFileData;
  UDF_FILE_IDENTIFIER_DESCRIPTOR   *FileIdentifierDesc;
  UDF_FILE_ENTRY                   *FileEntry;
  UDF_EXTENDED_FILE_ENTRY          *ExtendedFileEntry;

  Status = EFI_SUCCESS;

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  FileIdentifierDesc = PrivFileData->File.FileIdentifierDesc;
  if (IS_FID_DIRECTORY_FILE (FileIdentifierDesc)) {
    //
    // If the file handle is a directory, the _only_ position that may be set is
    // zero. This has no effect of starting the read proccess of the directory
    // entries over.
    //
    if (Position != 0) {
      Status = EFI_UNSUPPORTED;
    } else {
      PrivFileData->FilePosition      = Position;
      PrivFileData->NextEntryOffset   = 0;
    }
  } else if (IS_FID_NORMAL_FILE (FileIdentifierDesc)) {
    //
    // Seeking to position 0xFFFFFFFFFFFFFFFF causes the current position to be
    // set to the EOF.
    //
    if (Position == 0xFFFFFFFFFFFFFFFF) {
      if (IS_FE (PrivFileData->File.FileEntry)) {
	FileEntry = (UDF_FILE_ENTRY *)PrivFileData->File.FileEntry;
	PrivFileData->FilePosition = FileEntry->InformationLength - 1;
      } else if (IS_EFE (PrivFileData->File.FileEntry)) {
	ExtendedFileEntry =
	  (UDF_EXTENDED_FILE_ENTRY *)PrivFileData->File.FileEntry;
	PrivFileData->FilePosition = ExtendedFileEntry->InformationLength - 1;
      }
    } else {
      PrivFileData->FilePosition = Position;
    }
  } else {
    Status = EFI_UNSUPPORTED;
  }

  return Status;
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
  EFI_STATUS                             Status;
  PRIVATE_UDF_FILE_DATA                  *PrivFileData;
  UDF_FILE_ENTRY                         *FileEntry;
  UDF_EXTENDED_FILE_ENTRY                *ExtendedFileEntry;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *FileIdentifierDesc;
  EFI_FILE_INFO                          *FileInfo;
  UINTN                                  FileInfoLength;
  EFI_FILE_SYSTEM_INFO                   *FileSystemInfo;
  UINTN                                  FileSystemInfoLength;
  CHAR16                                 *String;
  UDF_FILE_SET_DESCRIPTOR                *FileSetDesc;
  CHAR16                                 *CharP;
  UINTN                                  Index;

  Print (L"UdfGetInfo(): in\n");

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  FileIdentifierDesc   = PrivFileData->File.FileIdentifierDesc;

  if (CompareGuid (InformationType, &gEfiFileInfoGuid)) {
    //
    // Check if BufferSize is too small to read the current directory entry
    //
    FileInfoLength = sizeof (EFI_FILE_INFO) +
                     (PrivFileData->FileName ?
		      StrLen (PrivFileData->FileName) : 0) + sizeof (CHAR16);
    if (*BufferSize < FileInfoLength) {
      *BufferSize = FileInfoLength;
      Status = EFI_BUFFER_TOO_SMALL;
      goto Exit;
    }

    FileInfo = (EFI_FILE_INFO *)Buffer;

    FileInfo->Size         = FileInfoLength;
    FileInfo->Attribute    &= ~EFI_FILE_VALID_ATTR;
    FileInfo->Attribute    |= EFI_FILE_READ_ONLY;

    if (IS_FID_DIRECTORY_FILE (FileIdentifierDesc)) {
      FileInfo->Attribute |= EFI_FILE_DIRECTORY;
    } else if (IS_FID_NORMAL_FILE (FileIdentifierDesc)) {
      FileInfo->Attribute |= EFI_FILE_ARCHIVE;
    }

    if (IS_FID_HIDDEN_FILE (FileIdentifierDesc)) {
      FileInfo->Attribute |= EFI_FILE_HIDDEN;
    }

    if (IS_FE (PrivFileData->File.FileEntry)) {
      FileEntry = (UDF_FILE_ENTRY *)PrivFileData->File.FileEntry;

      if (FileEntry->IcbTag.Flags & (1 << 10)) {
	FileInfo->Attribute |= EFI_FILE_SYSTEM;
      }

      FileInfo->FileSize       = FileEntry->InformationLength;
      FileInfo->PhysicalSize   = FileEntry->InformationLength;

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
    } else if (IS_EFE (PrivFileData->File.FileEntry)) {
      ExtendedFileEntry =
	(UDF_EXTENDED_FILE_ENTRY *)PrivFileData->File.FileEntry;

      if (ExtendedFileEntry->IcbTag.Flags & (1 << 10)) {
	FileInfo->Attribute |= EFI_FILE_SYSTEM;
      }

      FileInfo->FileSize       = ExtendedFileEntry->InformationLength;
      FileInfo->PhysicalSize   = ExtendedFileEntry->InformationLength;

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

    if (PrivFileData->FileName) {
      StrCpy (FileInfo->FileName, PrivFileData->FileName);
    } else {
      FileInfo->FileName[0] = '\0';
    }

    *BufferSize = FileInfoLength;
    Status = EFI_SUCCESS;
  } else if (CompareGuid (InformationType, &gEfiFileSystemInfoGuid)) {
    //
    // Logical Volume Identifier field is 128 bytes long
    //
    FileSystemInfoLength = 128 + sizeof (EFI_FILE_SYSTEM);

    if (*BufferSize < FileSystemInfoLength) {
      *BufferSize = FileSystemInfoLength;
      Status = EFI_BUFFER_TOO_SMALL;
      goto Exit;
    }

    FileSystemInfo = (EFI_FILE_SYSTEM_INFO *)Buffer;

    String = (CHAR16 *)&FileSystemInfo->VolumeLabel[0];
    FileSetDesc = PrivFileData->Volume.FileSetDescs[0];
    for (Index = 0; Index < 128; Index += 2) {
      CharP = (CHAR16 *)&FileSetDesc->LogicalVolumeIdentifier[Index];
      if (!*CharP) {
	break;
      }

      *String++ = *CharP;
    }

    *String = '\0';

    FileSystemInfo->Size        = FileSystemInfoLength;
    FileSystemInfo->ReadOnly    = TRUE;
    FileSystemInfo->VolumeSize  = 0;
    FileSystemInfo->FreeSpace   = 0;

    *BufferSize = FileSystemInfoLength;
    Status = EFI_SUCCESS;
  } else {
    Status = EFI_UNSUPPORTED;
  }

Exit:
  Print (L"UdfGetInfo: exit: %r\n", Status);

  return Status;
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
    goto Exit;
  }

Exit:
  return Status;
}

EFI_STATUS
StartMainVolumeDescriptorSequence (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER    *AnchorPoint,
  OUT UDF_LOGICAL_VOLUME_DESCRIPTOR          ***LogicalVolDescs,
  OUT UINTN                                  *LogicalVolDescsNo,
  OUT UDF_PARTITION_DESCRIPTOR               ***PartitionDescs,
  OUT UINTN                                  *PartitionDescsNo
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

  BlockSize     = BlockIo->Media->BlockSize;
  ExtentAd      = &AnchorPoint->MainVolumeDescriptorSequenceExtent;
  StartingLsn   = ExtentAd->ExtentLocation;
  EndingLsn     = StartingLsn + DivU64x32 (
                                       ExtentAd->ExtentLength,
				       BlockSize
                                       );

  *LogicalVolDescs =
    (UDF_LOGICAL_VOLUME_DESCRIPTOR **)AllocateZeroPool (ExtentAd->ExtentLength);
  if (!*LogicalVolDescs) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  *PartitionDescs =
    (UDF_PARTITION_DESCRIPTOR **)AllocateZeroPool (ExtentAd->ExtentLength);
  if (!*PartitionDescs) {
    Status = EFI_OUT_OF_RESOURCES;
    goto FreeExit0;
  }

  Buffer = AllocateZeroPool (BlockSize);
  if (!Buffer) {
    Status = EFI_OUT_OF_RESOURCES;
    goto FreeExit1;
  }

  *LogicalVolDescsNo   = 0;
  *PartitionDescsNo    = 0;
  while (StartingLsn <= EndingLsn) {
    Status = DiskIo->ReadDisk (
                         DiskIo,
			 BlockIo->Media->MediaId,
			 MultU64x32 (StartingLsn, BlockSize),
			 BlockSize,
			 Buffer
                         );
    if (EFI_ERROR (Status)) {
      goto FreeExit2;
    }

    if (IS_TD (Buffer)) {
      break;
    }

    //
    // TODO: check if allocations went OK
    //
    if (IS_LVD (Buffer)) {
      LogicalVolDesc =
	(UDF_LOGICAL_VOLUME_DESCRIPTOR *)
	AllocateZeroPool (sizeof (UDF_LOGICAL_VOLUME_DESCRIPTOR));
      CopyMem (
	(VOID *)LogicalVolDesc,
	Buffer,
	sizeof (UDF_LOGICAL_VOLUME_DESCRIPTOR)
	);
      (*LogicalVolDescs)[*LogicalVolDescsNo] = LogicalVolDesc;
      (*LogicalVolDescsNo)++;
    } else if (IS_PD (Buffer)) {
      PartitionDesc =
	(UDF_PARTITION_DESCRIPTOR *)
	AllocateZeroPool (sizeof (UDF_PARTITION_DESCRIPTOR));
      CopyMem (
	(VOID *)PartitionDesc,
	Buffer,
	sizeof (UDF_PARTITION_DESCRIPTOR)
	);
      (*PartitionDescs)[*PartitionDescsNo] = PartitionDesc;
      (*PartitionDescsNo)++;
    }

    StartingLsn++;
  }

  FreePool ((VOID *)Buffer);

Exit:
  return Status;

FreeExit2:
  FreePool ((VOID *)Buffer);

FreeExit1:
  FreePool ((VOID *)*PartitionDescs);

FreeExit0:
  FreePool ((VOID *)*LogicalVolDescs);

  return Status;
}

UDF_PARTITION_DESCRIPTOR *
GetPdFromLongAd (
  IN UDF_PARTITION_DESCRIPTOR         **PartitionDescs,
  IN UINTN                            PartitionDescsNo,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR   *LongAd
  )
{
  UINTN                               Index;
  UDF_PARTITION_DESCRIPTOR            *PartitionDesc;

  Print (L"GetPdFromLongAd: Part ref no: %d\n",
	 LongAd->ExtentLocation.PartitionReferenceNumber);

  for (Index = 0; Index < PartitionDescsNo; Index++) {
    PartitionDesc = PartitionDescs[Index];
    Print (L"GetPdFromLongAd: Partition no: %d\n",
	   PartitionDesc->PartitionNumber);
    if (
      PartitionDesc->PartitionNumber ==
      LongAd->ExtentLocation.PartitionReferenceNumber
      )
    {
      Print (L"GetPdFromLongAd: ->>> FOUND PARTITION (0x%016lx)\n", PartitionDesc);
      return PartitionDesc;
    }
  }

  return NULL;
}

UINT64
GetLsnFromLongAd (
  IN UDF_PARTITION_DESCRIPTOR         **PartitionDescs,
  IN UINTN                            PartitionDescsNo,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR   *LongAd
  )
{
  UDF_PARTITION_DESCRIPTOR            *PartitionDesc;

  Print (L"GetLsnFromLongAd: here 0\n");

  if (!LongAd->ExtentLength) {
    return 0;
  }

  Print (L"GetLsnFromLongAd: here 1\n");

  PartitionDesc = GetPdFromLongAd (PartitionDescs, PartitionDescsNo, LongAd);
  if (!PartitionDesc) {
    return 0;
  }

  Print (L"GetLsnFromLongAd: here 2\n");

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
  if ((!ShortAd->ExtentLength) || (!PartitionDesc)) {
    return 0;
  }

  return (UINT64)(
              PartitionDesc->PartitionStartingLocation +
	      ShortAd->ExtentPosition
              );
}

EFI_STATUS
FindFileSetDescriptor (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_LOGICAL_VOLUME_DESCRIPTOR           *LogicalVolDesc,
  IN UDF_PARTITION_DESCRIPTOR                **PartitionDescs,
  IN UINTN                                   PartitionDescsNo,
  OUT UDF_FILE_SET_DESCRIPTOR                *FileSetDesc
  )
{
  EFI_STATUS                                 Status;
  UINT64                                     Lsn;
  UINT32                                     BlockSize;

  Lsn = GetLsnFromLongAd (
                   PartitionDescs,
		   PartitionDescsNo,
		   &LogicalVolDesc->LogicalVolumeContentsUse
                   );
  if (!Lsn) {
    Status = EFI_VOLUME_CORRUPTED;
    goto Exit;
  }

  Print (L"FindFileSetDesc: FSD at %d\n", Lsn);

  BlockSize = BlockIo->Media->BlockSize;

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       MultU64x32 (Lsn, BlockSize),
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

EFI_STATUS
GetFileSetDescriptors (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_LOGICAL_VOLUME_DESCRIPTOR           **LogicalVolDescs,
  IN UINTN                                   LogicalVolDescsNo,
  IN UDF_PARTITION_DESCRIPTOR                **PartitionDescs,
  IN UINTN                                   PartitionDescsNo,
  OUT UDF_FILE_SET_DESCRIPTOR                ***FileSetDescs,
  OUT UINTN                                  *FileSetDescsNo
  )
{
  EFI_STATUS                                 Status;
  UINTN                                      Index;
  UDF_FILE_SET_DESCRIPTOR                    *FileSetDesc;

  Status        = EFI_SUCCESS;
  *FileSetDescs =
    (UDF_FILE_SET_DESCRIPTOR **)
    AllocateZeroPool (LogicalVolDescsNo * sizeof (UDF_FILE_SET_DESCRIPTOR));
  if (!*FileSetDescs) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  for (Index = 0; Index < LogicalVolDescsNo; Index++) {
    FileSetDesc = AllocateZeroPool (sizeof (UDF_FILE_SET_DESCRIPTOR));

    Status = FindFileSetDescriptor (
                                BlockIo,
                                DiskIo,
                                LogicalVolDescs[Index],
                                PartitionDescs,
                                PartitionDescsNo,
				FileSetDesc
                                );
    if (EFI_ERROR (Status)) {
      goto Exit;
    }

    (*FileSetDescs)[Index] = FileSetDesc;
  }

  *FileSetDescsNo = LogicalVolDescsNo;

Exit:
  return Status;
}

EFI_STATUS
FindRootDirectoryFile (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_PARTITION_DESCRIPTOR                *PartitionDesc,
  IN UDF_FILE_SET_DESCRIPTOR                 *FileSetDesc,
  OUT VOID                                   *FileEntry
  )
{
  UDF_LONG_ALLOCATION_DESCRIPTOR             *LongAd;
  EFI_STATUS                                 Status;
  UINT64                                     Lsn;
  UINT32                                     BlockSize;
  UINT64                                     Offset;

  LongAd = &FileSetDesc->RootDirectoryIcb;
  if (!LongAd->ExtentLength) {
    Status = EFI_VOLUME_CORRUPTED;
    goto Exit;
  }

  BlockSize   = BlockIo->Media->BlockSize;
  Lsn         = (UINT64)(PartitionDesc->PartitionStartingLocation +
			 LongAd->ExtentLocation.LogicalBlockNumber);
  Offset      = MultU64x32 (Lsn, BlockSize);

  Status = DiskIo->ReadDisk (
                          DiskIo,
			  BlockIo->Media->MediaId,
			  Offset,
			  BlockSize,
			  FileEntry
                          );
  if (EFI_ERROR (Status)) {
    goto FreeExit;
  }

  if ((!IS_FE (FileEntry)) && (!IS_EFE (FileEntry))) {
    Status = EFI_VOLUME_CORRUPTED;
  }

FreeExit:

Exit:
  return Status;
}

//
// TODO: handle FSDs in multiple LVDs
//
EFI_STATUS
ReadVolumeFileStructure (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  OUT UDF_LOGICAL_VOLUME_DESCRIPTOR          ***LogicalVolDescs,
  OUT UINTN                                  *LogicalVolDescsNo,
  OUT UDF_PARTITION_DESCRIPTOR               ***PartitionDescs,
  OUT UINTN                                  *PartitionDescsNo
  )
{
  EFI_STATUS                                 Status;
  UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER       AnchorPoint;

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
					  LogicalVolDescs,
					  LogicalVolDescsNo,
					  PartitionDescs,
					  PartitionDescsNo
                                          );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Print (L"ReadVolumeFileStructure: LVDs: %d\n", *LogicalVolDescsNo);
  Print (L"ReadVolumeFileStructure: PDs: %d\n", *PartitionDescsNo);

  ASSERT (*LogicalVolDescsNo == 1);
  ASSERT (*PartitionDescsNo);

Exit:
  return Status;
}

EFI_STATUS
FindFileEntryFromLongAd (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_PARTITION_DESCRIPTOR                **PartitionDescs,
  IN UINTN                                   PartitionDescsNo,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR          *LongAd,
  OUT VOID                                   **FileEntry
  )
{
  EFI_STATUS                                 Status;
  UINT64                                     Lsn;
  UINT32                                     BlockSize;

  Print (L"FindFEFromLAD: in\n");

  Print (L"FindFEFromLAD: here 0\n");

  Lsn = GetLsnFromLongAd (PartitionDescs, PartitionDescsNo, LongAd);
  if (!Lsn) {
    Status = EFI_VOLUME_CORRUPTED;
    goto Exit;
  }

  Print (L"FindFEFromLAD: here 1\n");

  Print (L"FindFEFromLAD: FE at %d\n", Lsn);

  BlockSize = BlockIo->Media->BlockSize;

  *FileEntry = AllocateZeroPool (BlockSize);
  if (!*FileEntry) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  Status = DiskIo->ReadDisk (
                          DiskIo,
			  BlockIo->Media->MediaId,
			  MultU64x32 (Lsn, BlockSize),
			  BlockSize,
			  *FileEntry
                          );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if ((!IS_FE (*FileEntry)) && (!IS_EFE (*FileEntry))) {
    FreePool (*FileEntry);
    Status = EFI_VOLUME_CORRUPTED;
  }

Exit:
  Print (L"FindFEFromLDA: exit: %r\n", Status);

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

EFI_STATUS
DuplicateFileIdentifierDescriptor (
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR    *FileIdentifierDesc,
  OUT UDF_FILE_IDENTIFIER_DESCRIPTOR   **NewFileIdentifierDesc
  )
{
  EFI_STATUS                           Status;
  UINT64                               FidLength;

  Status      = EFI_SUCCESS;
  FidLength   = GetFidDescriptorLength (FileIdentifierDesc);

  *NewFileIdentifierDesc =
    (UDF_FILE_IDENTIFIER_DESCRIPTOR *)AllocateZeroPool (FidLength);
  if (!*NewFileIdentifierDesc) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  CopyMem (
    (VOID *)*NewFileIdentifierDesc,
    (VOID *)FileIdentifierDesc,
    FidLength
    );

Exit:
  return Status;
}

EFI_STATUS
DuplicateFileEntry (
  IN EFI_BLOCK_IO_PROTOCOL   *BlockIo,
  IN VOID                    *FileEntry,
  OUT VOID                   **NewFileEntry
  )
{
  EFI_STATUS                 Status;
  UINT32                     BlockSize;

  Status      = EFI_SUCCESS;
  BlockSize   = BlockIo->Media->BlockSize;

  *NewFileEntry = AllocateZeroPool (BlockSize);
  if (!*NewFileEntry) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  CopyMem (*NewFileEntry, FileEntry, BlockSize);

Exit:
  return Status;
}

EFI_STATUS
FindFileIdentifierDescriptor (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN CHAR16                                  *FileName,
  IN VOID                                    *Data,
  IN UINT64                                  Length,
  OUT UDF_FILE_IDENTIFIER_DESCRIPTOR          **FoundFileIdentifierDesc,
  OUT UINT64                                 *RemBytes
  )
{
  EFI_STATUS                                 Status;
  UINT64                                     Offset;
  UDF_FILE_IDENTIFIER_DESCRIPTOR             *FileIdentifierDesc;
  CHAR8                                      *AsciiFileName;
  CHAR8                                      *NewFileName;

  Status                   = EFI_SUCCESS;
  *FoundFileIdentifierDesc = NULL;
  *RemBytes                = 0;

  AsciiFileName = (CHAR8 *)AllocatePool (StrLen (FileName) + 1);
  if (!AsciiFileName) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  AsciiFileName   = UnicodeStrToAsciiStr (FileName, AsciiFileName);
  Offset          = 0;
  do {
    FileIdentifierDesc = (UDF_FILE_IDENTIFIER_DESCRIPTOR *)(
                                                       (UINT8 *)Data +
						       Offset
                                                       );
    if (!IS_FID (FileIdentifierDesc)) {
      Status = EFI_VOLUME_CORRUPTED;
      goto FreeExit;
    }

    if (IS_FID_PARENT_FILE (FileIdentifierDesc)) {
      if ((StrCmp (FileName, L"..") == 0) || (StrCmp (FileName, L"/") == 0)) {
	Status = DuplicateFileIdentifierDescriptor (
                                               FileIdentifierDesc,
					       FoundFileIdentifierDesc
                                               );
	break;
      }
    } else {
      //
      // TODO: handle both UTF-8 and Unicode filenames
      //
      NewFileName =
	(CHAR8 *)AllocatePool (
	                 FileIdentifierDesc->LengthOfFileIdentifier
	                 );
      if (!NewFileName) {
	Status = EFI_OUT_OF_RESOURCES;
	goto FreeExit;
      }

      AsciiStrnCpy (
	NewFileName,
	(CONST CHAR8 *)(
	            (UINT8 *)&FileIdentifierDesc->Data[0] +
		    FileIdentifierDesc->LengthOfImplementationUse + 1
	            ),
	FileIdentifierDesc->LengthOfFileIdentifier - 1
	);

      NewFileName[FileIdentifierDesc->LengthOfFileIdentifier - 1] = '\0';

      Print (
	L"FID: (0x%016lx) filename: %a - asciifilename: %a\n",
	Offset,
	NewFileName,
	AsciiFileName
        );

      if (AsciiStrCmp (NewFileName, AsciiFileName) == 0) {
	FreePool ((VOID *)NewFileName);
	Status = DuplicateFileIdentifierDescriptor (
                                               FileIdentifierDesc,
					       FoundFileIdentifierDesc
                                               );
	break;
      }

      FreePool ((VOID *)NewFileName);
    }

    Offset += GetFidDescriptorLength (FileIdentifierDesc);
    //Print (L"fid lookup: new offs: %d - length: %d\n", Offset, Length);

    if (Length - Offset < sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)) {
      *RemBytes = Length - Offset;
      break;
    }
  } while (Offset < Length);

FreeExit:
  FreePool ((VOID *)AsciiFileName);

Exit:
  return Status;
}

EFI_STATUS
FindFileFromFileEntry (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR          *RootDirectoryIcb,
  IN UDF_PARTITION_DESCRIPTOR                **PartitionDescs,
  IN UINTN                                   PartitionDescsNo,
  IN CHAR16                                  *FileName,
  IN VOID                                    *ParentFileEntry,
  OUT UDF_FILE_IDENTIFIER_DESCRIPTOR         **FoundFileIdentifierDesc,
  OUT VOID                                   **FoundFileEntry
  )
{
  EFI_STATUS                                 Status;
  UDF_FILE_ENTRY                             *FileEntry;
  UDF_EXTENDED_FILE_ENTRY                    *ExtendedFileEntry;
  UDF_ICB_TAG                                *IcbTag;
  UINT8                                      *Data;
  UINT64                                     Length;
  UINT64                                     Offset;
  UDF_FILE_IDENTIFIER_DESCRIPTOR             *FileIdentifierDesc;
  VOID                                       *CompareFileEntry;
  UDF_LONG_ALLOCATION_DESCRIPTOR             *LongAd;
  UDF_SHORT_ALLOCATION_DESCRIPTOR            *ShortAd;
  UINT64                                     Lsn;
  VOID                                       *ExtentData;
  VOID                                       *NewExtentData;
  UINT32                                     BlockSize;
  BOOLEAN                                    Found;
  UINT64                                     RemBytes;
  UINT64                                     ExtentLength;

  *FoundFileIdentifierDesc   = NULL;
  *FoundFileEntry            = NULL;

  if (IS_EFE (ParentFileEntry)) {
    ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)ParentFileEntry;

    if (!IS_EFE_DIRECTORY (ExtendedFileEntry)) {
      Status = EFI_NOT_FOUND;
      goto Exit;
    }

#if 0
    Print (L"LengthOfAllocationDescriptors: %d\n", ExtendedFileEntry->LengthOfAllocationDescriptors);
    Print (L"---> %d\n",
	   ExtendedFileEntry->LengthOfAllocationDescriptors /
	   sizeof (UDF_SHORT_ALLOCATION_DESCRIPTOR));
    Print (L"ObjectSize: %d\n", ExtendedFileEntry->ObjectSize);
    Print (L"InformationLength: %d\n", ExtendedFileEntry->InformationLength);
    Print (L"StreamDir: ExtLen: %d\n", ExtendedFileEntry->StreamDirectoryIcb.ExtentLength);
    Print (L"Flags: %d\n", ExtendedFileEntry->IcbTag.Flags);
    Print (L"File Type: %d\n", ExtendedFileEntry->IcbTag.FileType);
    Print (L"Strategy Type: %d\n", ExtendedFileEntry->IcbTag.StrategyType);
#endif

    IcbTag   = &ExtendedFileEntry->IcbTag;
    Data     = (UINT8 *)&ExtendedFileEntry->Data;
  } else if (IS_FE (ParentFileEntry)) {
    FileEntry = (UDF_FILE_ENTRY *)ParentFileEntry;

    if (!IS_FE_DIRECTORY (FileEntry)) {
      Status = EFI_NOT_FOUND;
      goto Exit;
    }

    IcbTag   = &FileEntry->IcbTag;
    Data     = (UINT8 *)&FileEntry->Data;
  }

  BlockSize          = BlockIo->Media->BlockSize;
  ExtentData         = NULL;
  NewExtentData      = NULL;
  Found              = FALSE;

  //
  // ECMA 167 4/14.6.8
  //
  switch (IcbTag->Flags & 0x07) {
    case 3:
      if (IS_EFE (ParentFileEntry)) {
	Length = ExtendedFileEntry->InformationLength;
      } else {
	Length = FileEntry->InformationLength;
      }

      Print (L"----> data is inline\n");
      //
      // Immediate allocation (e.g. embedded FIDs)
      //
      Status = FindFileIdentifierDescriptor (
                                         BlockIo,
					 DiskIo,
					 FileName,
					 (VOID *)Data,
					 Length,
					 &FileIdentifierDesc,
					 &RemBytes
                                         );
      if (EFI_ERROR (Status)) {
	goto FreeExit;
      }

      if (FileIdentifierDesc) {
	Found = TRUE;
      }

      break;
    case 1:
      //
      // Sequence of Long Allocation Descriptors
      //
      if (IS_EFE (ParentFileEntry)) {
	Length = ExtendedFileEntry->LengthOfAllocationDescriptors;
      } else if (IS_FE (ParentFileEntry)) {
	Length = FileEntry->LengthOfAllocationDescriptors;
      }

      Print (
	L"----> sequence of %d LongAd(s)\n",
	Length / sizeof (UDF_LONG_ALLOCATION_DESCRIPTOR)
	);

      Offset = 0;
      do {
	LongAd = (UDF_LONG_ALLOCATION_DESCRIPTOR *)(Data + Offset);
	if (!IS_EXTENT_RECORDED (LongAd)) {
	  Print (L"LongAd is NOT recorded!!\n");
	  goto NextLongAd0;
	}

        Lsn = GetLsnFromLongAd (
			    PartitionDescs,
			    PartitionDescsNo,
			    LongAd
                            );
	if (!Lsn) {
	  Status = EFI_VOLUME_CORRUPTED;
	  goto Exit;
	}

	ExtentLength = LongAd->ExtentLength;

	Print (L"LongAd: LSN: %d (len: %d)\n", Lsn, LongAd->ExtentLength);

	ExtentData = AllocateZeroPool (ExtentLength);
	if (!ExtentData) {
	  Status = EFI_OUT_OF_RESOURCES;
	  goto Exit;
	}

        Status = DiskIo->ReadDisk (
                                DiskIo,
				BlockIo->Media->MediaId,
				MultU64x32 (Lsn, BlockSize),
				ExtentLength,
				ExtentData
	                        );
	if (EFI_ERROR (Status)) {
	  goto FreeExit;
	}

TryNextFidsStream0:
        Status = FindFileIdentifierDescriptor (
                                           BlockIo,
					   DiskIo,
					   FileName,
					   ExtentData,
					   ExtentLength,
					   &FileIdentifierDesc,
					   &RemBytes
                                           );
	if (EFI_ERROR (Status)) {
	  Print (L"FindFileIdentifierDescriptor: error: %r\n", Status);
	  goto FreeExit;
	}

	if (FileIdentifierDesc) {
	  Found = TRUE;
	  break;
	}

	if (RemBytes) {
	  ExtentLength = LongAd->ExtentLength;

NextLongAd1:
	  Offset += sizeof (UDF_LONG_ALLOCATION_DESCRIPTOR);
	  if (Offset >= Length) {
	    break;
	  }

	  LongAd = (UDF_LONG_ALLOCATION_DESCRIPTOR *)(Data + Offset);

	  if (!IS_EXTENT_RECORDED (LongAd)) {
	    goto NextLongAd1;
	  }

          Lsn = GetLsnFromLongAd (
			      PartitionDescs,
			      PartitionDescsNo,
			      LongAd
                              );
	  if (!Lsn) {
	    Status = EFI_VOLUME_CORRUPTED;
	    goto FreeExit;
	  }

	  NewExtentData = AllocatePool (RemBytes);
	  if (!NewExtentData) {
	    Status = EFI_OUT_OF_RESOURCES;
	    goto FreeExit;
	  }

	  CopyMem (
	    NewExtentData,
	    (VOID *)((UINT8 *)(ExtentData + (ExtentLength - RemBytes))),
	    RemBytes
	    );

	  FreePool (ExtentData);

	  ExtentData = AllocatePool (LongAd->ExtentLength + RemBytes);
	  if (!ExtentData) {
	    FreePool (NewExtentData);
	    Status = EFI_OUT_OF_RESOURCES;
	    goto Exit;
	  }

	  CopyMem (ExtentData, NewExtentData, RemBytes);

	  FreePool (NewExtentData);
	  NewExtentData = NULL;

	  Status = DiskIo->ReadDisk (
                                  DiskIo,
				  BlockIo->Media->MediaId,
				  MultU64x32 (Lsn, BlockSize),
				  LongAd->ExtentLength,
				  (VOID *)((UINT8 *)(ExtentData + RemBytes))
                                  );
	  if (EFI_ERROR (Status)) {
	    goto FreeExit;
	  }

	  ExtentLength = LongAd->ExtentLength + RemBytes;
	  goto TryNextFidsStream0;
	}

	FreePool (ExtentData);
	ExtentData = NULL;

NextLongAd0:
	Offset += sizeof (UDF_LONG_ALLOCATION_DESCRIPTOR);
      } while (Offset < Length);

      break;
    case 2:
      if (IS_EFE (ParentFileEntry)) {
	Length = ExtendedFileEntry->LengthOfAllocationDescriptors;
      } else if (IS_FE (ParentFileEntry)) {
	Length = FileEntry->LengthOfAllocationDescriptors;
      }

      Print (
	L"----> sequence of %d ShortAd(s)\n",
	Length / sizeof (UDF_SHORT_ALLOCATION_DESCRIPTOR)
	);
      //
      // Sequence of Short Allocation Descriptors
      //
      Offset = 0;
      do {
	ShortAd = (UDF_SHORT_ALLOCATION_DESCRIPTOR *)(Data + Offset);
        Lsn = GetLsnFromShortAd (
	                    GetPdFromLongAd (
                                       PartitionDescs,
				       PartitionDescsNo,
				       RootDirectoryIcb
                                       ),
			    ShortAd
                            );
	if (!Lsn) {
	  Status = EFI_VOLUME_CORRUPTED;
	  goto Exit;
	}

	ExtentData = AllocateZeroPool (ShortAd->ExtentLength);
	if (!ExtentData) {
	  Status = EFI_OUT_OF_RESOURCES;
	  goto Exit;
	}

        Status = DiskIo->ReadDisk (
                                DiskIo,
				BlockIo->Media->MediaId,
				MultU64x32 (Lsn, BlockSize),
				ShortAd->ExtentLength,
				ExtentData
	                        );
	if (EFI_ERROR (Status)) {
	  goto FreeExit;
	}

TryNextFidsStream1:
        Status = FindFileIdentifierDescriptor (
                                           BlockIo,
					   DiskIo,
					   FileName,
					   ExtentData,
					   ExtentLength,
					   &FileIdentifierDesc,
					   &RemBytes
                                           );
	if (EFI_ERROR (Status)) {
	  Print (L"FindFileIdentifierDescriptor: error: %r\n", Status);
	  goto FreeExit;
	}

	if (FileIdentifierDesc) {
	  Found = TRUE;
	  break;
	}

	if (RemBytes) {
	  ExtentLength = ShortAd->ExtentLength;

	  Offset += sizeof (UDF_SHORT_ALLOCATION_DESCRIPTOR);
	  if (Offset >= Length) {
	    break;
	  }

	  ShortAd = (UDF_SHORT_ALLOCATION_DESCRIPTOR *)(Data + Offset);
          Lsn = GetLsnFromShortAd (
	                      GetPdFromLongAd (
                                         PartitionDescs,
				         PartitionDescsNo,
				         RootDirectoryIcb
                                         ),
			      ShortAd
                              );
	  if (!Lsn) {
	    Status = EFI_VOLUME_CORRUPTED;
	    goto Exit;
	  }

	  NewExtentData = AllocatePool (RemBytes);
	  if (!NewExtentData) {
	    Status = EFI_OUT_OF_RESOURCES;
	    goto FreeExit;
	  }

	  CopyMem (
	    NewExtentData,
	    (VOID *)((UINT8 *)(ExtentData + (ExtentLength - RemBytes))),
	    RemBytes
	    );

	  FreePool (ExtentData);

	  ExtentData = AllocatePool (ShortAd->ExtentLength + RemBytes);
	  if (!ExtentData) {
	    FreePool (NewExtentData);
	    Status = EFI_OUT_OF_RESOURCES;
	    goto Exit;
	  }

	  CopyMem (ExtentData, NewExtentData, RemBytes);

	  FreePool (NewExtentData);
	  NewExtentData = NULL;

	  Status = DiskIo->ReadDisk (
                                  DiskIo,
				  BlockIo->Media->MediaId,
				  MultU64x32 (Lsn, BlockSize),
				  ShortAd->ExtentLength,
				  (VOID *)((UINT8 *)(ExtentData + RemBytes))
                                  );
	  if (EFI_ERROR (Status)) {
	    goto FreeExit;
	  }

	  ExtentLength = ShortAd->ExtentLength + RemBytes;
	  goto TryNextFidsStream1;
	}

	FreePool (ExtentData);
	ExtentData = NULL;

	Offset += sizeof (UDF_SHORT_ALLOCATION_DESCRIPTOR);
      } while (Offset < Length);

      break;
  }

  if (Found) {
    if (!FoundFileEntry) {
      Print (L"(1) %s has been found!\n", FileName);
      *FoundFileIdentifierDesc   = FileIdentifierDesc;
      Status                     = EFI_SUCCESS;
      goto FreeExit;
    }

    Status = FindFileEntryFromLongAd (
                                  BlockIo,
				  DiskIo,
				  PartitionDescs,
				  PartitionDescsNo,
				  &FileIdentifierDesc->Icb,
				  &CompareFileEntry
                                  );
    if (EFI_ERROR (Status)) {
      goto FreeExit;
    }

    if (CompareMem (
	  (VOID *)ParentFileEntry,
	  (VOID *)CompareFileEntry,
	  BlockSize
	  )
       ) {
      Print (L"(2) %s has been found!\n", FileName);
      *FoundFileIdentifierDesc   = FileIdentifierDesc;
      *FoundFileEntry            = CompareFileEntry;
      Status                     = EFI_SUCCESS;
    } else {
      FreePool ((VOID *)FileIdentifierDesc);
      Status = EFI_NOT_FOUND;
    }
  } else {
    Status = EFI_NOT_FOUND;
  }

FreeExit:
  if (ExtentData) {
    FreePool (ExtentData);
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

EFI_STATUS
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
     (UINT8 *)&FileIdentifierDesc->Data[0] +
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
