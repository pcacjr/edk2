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
  UDF_FILE_SET_DESCRIPTOR                **FileSetDescs;
  UINTN                                  Index;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  if (!This || !Root) {
    Status = EFI_INVALID_PARAMETER;
    goto ErrorInvalidParams;
  }

  PrivFsData = PRIVATE_UDF_SIMPLE_FS_DATA_FROM_THIS (This);

  PrivFileData = AllocateZeroPool (sizeof (PRIVATE_UDF_FILE_DATA));
  if (!PrivFileData) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ErrorAllocPrivFileData;
  }

  Status = ReadVolumeFileStructure (
                            PrivFsData->BlockIo,
			    PrivFsData->DiskIo,
			    &PrivFileData->Volume
                            );
  if (EFI_ERROR (Status)) {
    goto ErrorReadVolFileStructure;
  }

  Status = GetFileSetDescriptors (
                            PrivFsData->BlockIo,
                            PrivFsData->DiskIo,
			    &PrivFileData->Volume
                            );
  if (EFI_ERROR (Status)) {
    goto ErrorGetFsds;
  }

  FileSetDescs = PrivFileData->Volume.FileSetDescs;

  Status = FindFileEntry (
                      PrivFsData->BlockIo,
		      PrivFsData->DiskIo,
                      &PrivFileData->Volume,
                      &FileSetDescs[0]->RootDirectoryIcb,
                      &PrivFileData->Root.FileEntry
                      );
  if (EFI_ERROR (Status)) {
    goto ErrorFindFe;
  }

  Status = FindFile (
                 PrivFsData->BlockIo,
		 PrivFsData->DiskIo,
		 &PrivFileData->Volume,
		 &FileSetDescs[0]->RootDirectoryIcb,
		 L"/",
		 PrivFileData->Root.FileEntry,
		 &PrivFileData->Root
                 );
  if (EFI_ERROR (Status)) {
    goto ErrorFindFile;
  }

  CopyMem (
    (VOID *)&PrivFileData->File,
    (VOID *)&PrivFileData->Root,
    sizeof (UDF_FILE_INFO)
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

  PrivFileData->IsRootDirectory = TRUE;

  *Root = &PrivFileData->FileIo;

  gBS->RestoreTPL (OldTpl);
  return Status;

ErrorFindFile:
  FreePool (PrivFileData->Root.FileEntry);

ErrorFindFe:
ErrorGetFsds:
  for (Index = 0; Index < PrivFileData->Volume.LogicalVolDescsNo; Index++) {
    FreePool ((VOID *)PrivFileData->Volume.LogicalVolDescs[Index]);
  }

  FreePool ((VOID *)PrivFileData->Volume.LogicalVolDescs);

  for (Index = 0; Index < PrivFileData->Volume.PartitionDescsNo; Index++) {
    FreePool ((VOID *)PrivFileData->Volume.PartitionDescs[Index]);
  }

  FreePool ((VOID *)PrivFileData->Volume.PartitionDescs);

ErrorReadVolFileStructure:
  FreePool ((VOID *)PrivFileData);

ErrorAllocPrivFileData:
ErrorInvalidParams:
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
  UDF_FILE_INFO                          File;
  CHAR16                                 *TempString;
  CHAR16                                 *FileNameSavedPointer;
  CHAR16                                 *TempFileName;
  EFI_BLOCK_IO_PROTOCOL                  *BlockIo;
  EFI_DISK_IO_PROTOCOL                   *DiskIo;
  UDF_VOLUME_INFO                        *Volume;
  VOID                                   *ParentFileEntry;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *PrevFileIdentifierDesc;
  BOOLEAN                                FreeFileEntry;
  UINT32                                 BlockSize;
  BOOLEAN                                Found;
  PRIVATE_UDF_FILE_DATA                  *NewPrivFileData;
  BOOLEAN                                IsRootDirectory;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);
  String = NULL;

  if (!This || !NewHandle || !FileName) {
    Status = EFI_INVALID_PARAMETER;
    goto ErrorInvalidParams;
  }

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  FileName = MangleFileName (FileName);
  if ((!FileName) || (!*FileName)) {
    Status = EFI_NOT_FOUND;
    goto ErrorBadFileName;
  }

  String = (CHAR16 *)AllocatePool ((StrLen (FileName) + 1) * sizeof (CHAR16));
  if (!String) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ErrorAllocString;
  }

  ZeroMem ((VOID *)&File, sizeof (UDF_FILE_INFO));

  BlockIo                = PrivFileData->BlockIo;
  DiskIo                 = PrivFileData->DiskIo;
  Volume                 = &PrivFileData->Volume;
  BlockSize              = BlockIo->Media->BlockSize;
  Found                  = FALSE;
  NewPrivFileData        = NULL;
  FileNameSavedPointer   = FileName;

  for (;;) {
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
      if (!*FileName) {
	Found             = TRUE;
	IsRootDirectory   = TRUE;
	break;
      } else {
	continue;
      }
    }

    if (!StrCmp (String, L".")) {
      Found = TRUE;
      continue;
    }

    PrevFileIdentifierDesc   = File.FileIdentifierDesc;
    FreeFileEntry            = TRUE;

    if (!File.FileEntry) {
      File.FileEntry = PrivFileData->File.FileEntry;
      FreeFileEntry = FALSE;
    }

    ParentFileEntry = File.FileEntry;

    Status = FindFile (
                   BlockIo,
		   DiskIo,
		   Volume,
		   &PrevFileIdentifierDesc->Icb,
		   String,
		   ParentFileEntry,
		   &File
                   );

    if (FreeFileEntry) {
      FreePool ((VOID *)ParentFileEntry);
    }

    if (PrevFileIdentifierDesc) {
      FreePool ((VOID *)PrevFileIdentifierDesc);
    }

    if (EFI_ERROR (Status)) {
      goto ErrorFindFile;
    }

    if (!File.FileIdentifierDesc && !File.FileEntry) {
      break;
    }

    Found = TRUE;
  }

  if (!Found) {
    Status = EFI_NOT_FOUND;
    goto ErrorFindFile;
  }

  NewPrivFileData = AllocateZeroPool (sizeof (PRIVATE_UDF_FILE_DATA));
  if (!NewPrivFileData) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ErrorAllocNewPrivFileData;
  }

  CopyMem (
    (VOID *)NewPrivFileData,
    (VOID *)PrivFileData,
    sizeof (PRIVATE_UDF_FILE_DATA)
    );

  if (!File.FileEntry && !File.FileIdentifierDesc) {
    if (IsRootDirectory) {
      NewPrivFileData->AbsoluteFileName   = NULL;
      NewPrivFileData->FileName           = NULL;

      Status = DuplicateFileEntry (
                               BlockIo,
			       PrivFileData->Root.FileEntry,
			       &NewPrivFileData->Root.FileEntry
                               );
      if (EFI_ERROR (Status)) {
	goto ErrorDupFe;
      }

      Status = DuplicateFileIdentifierDescriptor (
			       PrivFileData->Root.FileIdentifierDesc,
			       &NewPrivFileData->Root.FileIdentifierDesc
                               );
      if (EFI_ERROR (Status)) {
	FreePool ((VOID *)NewPrivFileData->Root.FileEntry);
	goto ErrorDupFid;
      }

      CopyMem (
	(VOID *)&NewPrivFileData->File,
	(VOID *)&NewPrivFileData->Root,
	sizeof (UDF_FILE_INFO)
	);
      goto HandleRootDirectory;
    }

    if ((PrivFileData->AbsoluteFileName) && (PrivFileData->FileName)) {
      NewPrivFileData->AbsoluteFileName =
	(CHAR16 *)AllocatePool (
                           (StrLen (PrivFileData->AbsoluteFileName) + 1) *
                           sizeof(CHAR16)
                           );
      if (!NewPrivFileData->AbsoluteFileName) {
	Status = EFI_OUT_OF_RESOURCES;
	goto ErrorAllocAbsFileName;
      }

      NewPrivFileData->FileName =
	(CHAR16 *)AllocatePool (
                           (StrLen (PrivFileData->FileName) + 1) *
                           sizeof(CHAR16)
                           );
      if (!NewPrivFileData->FileName) {
	Status = EFI_OUT_OF_RESOURCES;
	goto ErrorAllocFileName;
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
      goto ErrorDupFe;
    }

    Status = DuplicateFileIdentifierDescriptor (
			     PrivFileData->File.FileIdentifierDesc,
			     &NewPrivFileData->File.FileIdentifierDesc
                             );
    if (EFI_ERROR (Status)) {
      FreePool ((VOID *)NewPrivFileData->File.FileEntry);
      goto ErrorDupFid;
    }

    goto HandleCurrentDirectory;
  }

  FileName = FileNameSavedPointer;

  NewPrivFileData->AbsoluteFileName =
    (CHAR16 *)AllocatePool (
                      (((PrivFileData->AbsoluteFileName ?
			 StrLen (PrivFileData->AbsoluteFileName) :
			 0) +
			StrLen (FileName)) *
		       sizeof (CHAR16)) +
		      (2 * sizeof (CHAR16))
                      );
  if (!NewPrivFileData->AbsoluteFileName) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ErrorAllocAbsFileName;
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
                                                (StrLen (FileName) + 1) *
						sizeof (CHAR16)
                                                );
  if (!NewPrivFileData->FileName) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ErrorAllocFileName;
  }

  NewPrivFileData->FileName[0] = L'\0';
  StrCat (NewPrivFileData->FileName, FileName);

  CopyMem (
    (VOID *)&NewPrivFileData->File,
    &File,
    sizeof (UDF_FILE_INFO)
    );

HandleRootDirectory:
HandleCurrentDirectory:
  NewPrivFileData->IsRootDirectory = IsRootDirectory;

  Status = GetFileSize (
                    BlockIo,
                    DiskIo,
		    Volume,
		    &NewPrivFileData->File,
		    &NewPrivFileData->FileSize
                    );
  if (EFI_ERROR (Status)) {
    goto ErrorGetFileSize;
  }

  NewPrivFileData->FilePosition = 0;
  ZeroMem (
    (VOID *)&NewPrivFileData->ReadDirInfo,
    sizeof (UDF_READ_DIRECTORY_INFO)
    );

  *NewHandle = &NewPrivFileData->FileIo;

  gBS->RestoreTPL (OldTpl);
  return Status;

ErrorGetFileSize:
ErrorAllocFileName:
ErrorAllocAbsFileName:
ErrorDupFid:
ErrorDupFe:
  if (NewPrivFileData->AbsoluteFileName) {
    FreePool ((VOID *)NewPrivFileData->AbsoluteFileName);
  }
  if (NewPrivFileData->FileName) {
    FreePool ((VOID *)NewPrivFileData->FileName);
  }

  FreePool ((VOID *)NewPrivFileData);

ErrorAllocNewPrivFileData:
  if (File.FileEntry) {
    FreePool (File.FileEntry);
  }
  if (File.FileIdentifierDesc) {
    FreePool ((VOID *)File.FileIdentifierDesc);
  }

ErrorFindFile:
  FreePool ((VOID *)String);

ErrorAllocString:
ErrorBadFileName:
ErrorInvalidParams:
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
  EFI_TPL                                OldTpl;
  EFI_STATUS                             Status;
  PRIVATE_UDF_FILE_DATA                  *PrivFileData;
  UDF_VOLUME_INFO                        *Volume;
  UDF_FILE_INFO                          *File;
  UDF_READ_DIRECTORY_INFO                *ReadDirInfo;
  EFI_BLOCK_IO_PROTOCOL                  *BlockIo;
  EFI_DISK_IO_PROTOCOL                   *DiskIo;
  UDF_FILE_INFO                          FoundFile;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *NewFileIdentifierDesc;
  VOID                                   *NewFileEntryData;
  CHAR16                                 *FoundFileName;
  UINT64                                 FileSize;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  if ((!This) || (!Buffer)) {
    Status = EFI_INVALID_PARAMETER;
    goto ErrorInvalidParams;
  }

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  BlockIo                 = PrivFileData->BlockIo;
  DiskIo                  = PrivFileData->DiskIo;
  Volume                  = &PrivFileData->Volume;
  File                    = &PrivFileData->File;
  ReadDirInfo             = &PrivFileData->ReadDirInfo;
  NewFileIdentifierDesc   = NULL;
  FoundFileName           = NULL;
  NewFileEntryData        = NULL;

  if (IS_FID_NORMAL_FILE (File->FileIdentifierDesc)) {
    if (PrivFileData->FilePosition > PrivFileData->FileSize) {
      //
      // File's position is beyond the EOF
      //
      Status = EFI_DEVICE_ERROR;
      goto ErrorFileBeyondTheEof;
    }
    if (PrivFileData->FilePosition == PrivFileData->FileSize) {
      *BufferSize = 0;
      Status = EFI_SUCCESS;
      goto DoneFileAtEof;
    }

    Status = ReadFileData (
                       BlockIo,
		       DiskIo,
		       Volume,
		       File,
		       PrivFileData->FileSize,
		       &PrivFileData->FilePosition,
		       Buffer,
		       BufferSize
                       );
    Status = EFI_SUCCESS;
  } else if (IS_FID_DIRECTORY_FILE (File->FileIdentifierDesc)) {
    if ((!ReadDirInfo->FidOffset) && (PrivFileData->FilePosition)) {
      Status = EFI_DEVICE_ERROR;
      *BufferSize = 0;
      goto DoneWithNoMoreDirEnts;
    }

ReadNextEntry:
    Status = ReadDirectoryEntry (
                             BlockIo,
			     DiskIo,
			     Volume,
			     &File->FileIdentifierDesc->Icb,
			     File->FileEntry,
			     ReadDirInfo,
			     &NewFileIdentifierDesc
                             );
    if (EFI_ERROR (Status)) {
      if (Status == EFI_DEVICE_ERROR) {
	ZeroMem ((VOID *)ReadDirInfo, sizeof (UDF_READ_DIRECTORY_INFO));
	*BufferSize = 0;
	Status = EFI_SUCCESS;
      }

      goto DoneReadDirEnt;
    }

    if (IS_FID_PARENT_FILE (NewFileIdentifierDesc)) {
      FreePool (NewFileIdentifierDesc);
      goto ReadNextEntry;
    }

    Status = FindFileEntry (
                        BlockIo,
			DiskIo,
			Volume,
			&NewFileIdentifierDesc->Icb,
			&NewFileEntryData
                        );
    if (EFI_ERROR (Status)) {
      goto ErrorFindFe;
    }

    Status = GetFileNameFromFid (NewFileIdentifierDesc, &FoundFileName);
    if (EFI_ERROR (Status)) {
      goto ErrorGetFileName;
    }

    FoundFile.FileIdentifierDesc   = NewFileIdentifierDesc;
    FoundFile.FileEntry            = NewFileEntryData;

    Status = GetFileSize (
                      BlockIo,
                      DiskIo,
		      Volume,
		      &FoundFile,
		      &FileSize
                      );
    if (EFI_ERROR (Status)) {
      goto ErrorGetFileSize;
    }

    Status = SetFileInfo (
		     &FoundFile,
		     FileSize,
		     FoundFileName,
		     BufferSize,
		     Buffer
                     );
    if (EFI_ERROR (Status)) {
      goto ErrorSetFileInfo;
    }

    PrivFileData->FilePosition++;
    Status = EFI_SUCCESS;
  } else if (IS_FID_DELETED_FILE (File->FileIdentifierDesc)) {
    Status = EFI_DEVICE_ERROR;
  } else {
    Status = EFI_VOLUME_CORRUPTED;
  }

ErrorSetFileInfo:
ErrorGetFileSize:
  if (FoundFileName) {
    FreePool ((VOID *)FoundFileName);
  }
ErrorGetFileName:
  if (NewFileEntryData) {
    FreePool (NewFileEntryData);
  }

ErrorFindFe:
  if (NewFileIdentifierDesc) {
    FreePool ((VOID *)NewFileIdentifierDesc);
  }

DoneReadDirEnt:
DoneWithNoMoreDirEnts:
DoneFileAtEof:
ErrorFileBeyondTheEof:
ErrorInvalidParams:
  gBS->RestoreTPL (OldTpl);
  return Status;
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
  EFI_STATUS               Status;
  PRIVATE_UDF_FILE_DATA   *PrivFileData;

  if ((!This) || (!Position)) {
    return EFI_INVALID_PARAMETER;
  }

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

  if (!This) {
    return EFI_INVALID_PARAMETER;
  }

  Status = EFI_SUCCESS;

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  FileIdentifierDesc = PrivFileData->File.FileIdentifierDesc;
  if (IS_FID_DIRECTORY_FILE (FileIdentifierDesc)) {
    //
    // If the file handle is a directory, the _only_ position that may be set is
    // zero. This has no effect of starting the read proccess of the directory
    // entries over.
    //
    if (Position) {
      Status = EFI_UNSUPPORTED;
    } else {
      PrivFileData->FilePosition = Position;
      ZeroMem (
	(VOID *)&PrivFileData->ReadDirInfo,
	sizeof (UDF_READ_DIRECTORY_INFO)
	);
    }
  } else if (IS_FID_NORMAL_FILE (FileIdentifierDesc)) {
    //
    // Seeking to position 0xFFFFFFFFFFFFFFFF causes the current position to be
    // set to the EOF.
    //
    if (Position == 0xFFFFFFFFFFFFFFFF) {
      PrivFileData->FilePosition = PrivFileData->FileSize - 1;
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
  EFI_FILE_SYSTEM_INFO                   *FileSystemInfo;
  UINTN                                  FileSystemInfoLength;
  CHAR16                                 *String;
  UDF_FILE_SET_DESCRIPTOR                *FileSetDesc;
  CHAR16                                 *CharP;
  UINTN                                  Index;

  if ((!This) || (!InformationType) || (!BufferSize) || (!Buffer)) {
    return EFI_INVALID_PARAMETER;
  }

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  if (CompareGuid (InformationType, &gEfiFileInfoGuid)) {
    Status = SetFileInfo (
                     &PrivFileData->File,
		     PrivFileData->FileSize,
		     PrivFileData->FileName,
		     BufferSize,
		     Buffer
                     );
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
    for (Index = 0; Index < 128; Index += sizeof (CHAR16)) {
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
  for (Index = 0; Index < Volume->PartitionDescsNo; Index++) {
    FreePool ((VOID *)Volume->PartitionDescs[Index]);
  }

ErrorAllocLvd:
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
FindFileEntry (
  IN EFI_BLOCK_IO_PROTOCOL            *BlockIo,
  IN EFI_DISK_IO_PROTOCOL             *DiskIo,
  IN UDF_VOLUME_INFO                  *Volume,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR   *Icb,
  OUT VOID                            **FileEntry
  )
{
  EFI_STATUS                                 Status;
  UINT64                                     Lsn;
  UINT32                                     BlockSize;

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

  if ((!IS_FE (*FileEntry)) && (!IS_EFE (*FileEntry))) {
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

EFI_STATUS
DuplicateFileIdentifierDescriptor (
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR    *FileIdentifierDesc,
  OUT UDF_FILE_IDENTIFIER_DESCRIPTOR   **NewFileIdentifierDesc
  )
{
  UINT64                               FidLength;

  FidLength = GetFidDescriptorLength (FileIdentifierDesc);

  *NewFileIdentifierDesc =
    (UDF_FILE_IDENTIFIER_DESCRIPTOR *)AllocateZeroPool (FidLength);
  if (!*NewFileIdentifierDesc) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem (
    (VOID *)*NewFileIdentifierDesc,
    (VOID *)FileIdentifierDesc,
    FidLength
    );

  return EFI_SUCCESS;
}

EFI_STATUS
DuplicateFileEntry (
  IN EFI_BLOCK_IO_PROTOCOL   *BlockIo,
  IN VOID                    *FileEntry,
  OUT VOID                   **NewFileEntry
  )
{
  UINT32                     BlockSize;

  BlockSize = BlockIo->Media->BlockSize;

  *NewFileEntry = AllocateZeroPool (BlockSize);
  if (!*NewFileEntry) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem (*NewFileEntry, FileEntry, BlockSize);

  return EFI_SUCCESS;
}

EFI_STATUS
GetFileNameFromFid (
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR   *FileIdentifierDesc,
  OUT CHAR16                          **FileName
  )
{
  CHAR8                               *AsciiFileName;
  EFI_STATUS                          Status;

  AsciiFileName = (CHAR8 *)AllocatePool (
                                    FileIdentifierDesc->LengthOfFileIdentifier *
                                    sizeof (CHAR8)
                                    );
  if (!AsciiFileName) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = EFI_SUCCESS;

  AsciiStrnCpy (
    AsciiFileName,
    (CONST CHAR8 *)(
      (UINT8 *)&FileIdentifierDesc->Data[0] +
      FileIdentifierDesc->LengthOfImplementationUse + 1
      ),
    FileIdentifierDesc->LengthOfFileIdentifier - 1
    );

  AsciiFileName[FileIdentifierDesc->LengthOfFileIdentifier - 1] = '\0';

  *FileName = (CHAR16 *)AllocatePool (
                                 FileIdentifierDesc->LengthOfFileIdentifier *
                                 sizeof (CHAR16)
                                 );
  if (!*FileName) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ErrorAllocFileName;
  }

  *FileName = AsciiStrToUnicodeStr (AsciiFileName, *FileName);

ErrorAllocFileName:
  FreePool ((VOID *)AsciiFileName);
  return Status;
}

EFI_STATUS
FindFile (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_VOLUME_INFO                         *Volume,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR          *ParentIcb,
  IN CHAR16                                  *FileName,
  IN VOID                                    *FileEntryData,
  OUT UDF_FILE_INFO                          *File
  )
{
  EFI_STATUS                                 Status;
  UDF_FILE_ENTRY                             *FileEntry;
  UDF_EXTENDED_FILE_ENTRY                    *ExtendedFileEntry;
  UDF_FILE_IDENTIFIER_DESCRIPTOR             *FileIdentifierDesc;
  UDF_READ_DIRECTORY_INFO                    ReadDirInfo;
  BOOLEAN                                    Found;
  UINTN                                      FileNameLength;
  CHAR16                                     *FoundFileName;
  VOID                                       *CompareFileEntry;

  if (IS_EFE (FileEntryData)) {
    ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)FileEntryData;

    if (!IS_EFE_DIRECTORY (ExtendedFileEntry)) {
      return EFI_NOT_FOUND;
    }
  } else if (IS_FE (FileEntryData)) {
    FileEntry = (UDF_FILE_ENTRY *)FileEntryData;

    if (!IS_FE_DIRECTORY (FileEntry)) {
      return EFI_NOT_FOUND;
    }
  }

  ZeroMem ((VOID *)&ReadDirInfo, sizeof (UDF_READ_DIRECTORY_INFO));

  Found            = FALSE;
  FileNameLength   = StrLen (FileName);
  for (;;) {
    Status = ReadDirectoryEntry (
                             BlockIo,
			     DiskIo,
			     Volume,
			     ParentIcb,
			     FileEntryData,
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
      if ((!StrCmp (FileName, L"..")) || (!StrCmp (FileName, L"/"))) {
	Found = TRUE;
	break;
      }
    } else {
      if (FileNameLength != FileIdentifierDesc->LengthOfFileIdentifier - 1) {
	goto ReadNextEntry;
      }

      Status = GetFileNameFromFid (FileIdentifierDesc, &FoundFileName);
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
    if (StrCmp (FileName, L"/")) {
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
	    (VOID *)FileEntryData,
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
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
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
      Status = DuplicateFileIdentifierDescriptor (
                                             FileIdentifierDesc,
					     FoundFileIdentifierDesc
                                             );
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
