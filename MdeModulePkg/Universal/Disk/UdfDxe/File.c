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
  IN   EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *This,
  OUT  EFI_FILE_PROTOCOL                **Root
  )
{
  EFI_TPL                     OldTpl;
  EFI_STATUS                  Status;
  PRIVATE_UDF_SIMPLE_FS_DATA  *PrivFsData;
  PRIVATE_UDF_FILE_DATA       *PrivFileData;
  UDF_FILE_SET_DESCRIPTOR     **FileSetDescs;
  UINTN                       Index;
  UDF_FILE_INFO               Parent;
  UDF_FILE_INFO               File;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  if (!This || !Root) {
    Status = EFI_INVALID_PARAMETER;
    goto ErrorInvalidParams;
  }

  PrivFileData = AllocateZeroPool (sizeof (PRIVATE_UDF_FILE_DATA));
  if (!PrivFileData) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ErrorAllocPrivFileData;
  }

  PrivFsData = PRIVATE_UDF_SIMPLE_FS_DATA_FROM_THIS (This);

  if (!PrivFsData->OpenFiles) {
    Status = ReadVolumeFileStructure (
                              PrivFsData->BlockIo,
			      PrivFsData->DiskIo,
			      &PrivFsData->Volume
                              );
    if (EFI_ERROR (Status)) {
      goto ErrorReadVolFileStructure;
    }

    Status = GetFileSetDescriptors (
                              PrivFsData->BlockIo,
                              PrivFsData->DiskIo,
			      &PrivFsData->Volume
                              );
    if (EFI_ERROR (Status)) {
      goto ErrorGetFsds;
    }
  }

  if (PrivFsData->Root.FileEntry) {
    FreePool (PrivFsData->Root.FileEntry);
    PrivFsData->Root.FileEntry = NULL;
  }

  if (PrivFsData->Root.FileIdentifierDesc) {
    FreePool ((VOID *)PrivFsData->Root.FileIdentifierDesc);
    PrivFsData->Root.FileIdentifierDesc = NULL;
  }

  FileSetDescs = PrivFsData->Volume.FileSetDescs;

  Status = FindFileEntry (
                      PrivFsData->BlockIo,
		      PrivFsData->DiskIo,
                      &PrivFsData->Volume,
                      &FileSetDescs[0]->RootDirectoryIcb,
                      &PrivFsData->Root.FileEntry
                      );
  if (EFI_ERROR (Status)) {
    goto ErrorFindFe;
  }

  ZeroMem ((VOID *)&Parent, sizeof (UDF_FILE_INFO));

  Parent.FileEntry = PrivFsData->Root.FileEntry;

  Status = FindFile (
                 PrivFsData->BlockIo,
		 PrivFsData->DiskIo,
		 &PrivFsData->Volume,
		 L"\\",
		 NULL,
		 &Parent,
		 &FileSetDescs[0]->RootDirectoryIcb,
		 &File
                 );
  if (EFI_ERROR (Status)) {
    goto ErrorFindFile;
  }

  PrivFsData->Root.FileIdentifierDesc = File.FileIdentifierDesc;

  PrivFileData->Root = &PrivFsData->Root;
  PrivFileData->IsRootDirectory = TRUE;

  PrivFileData->Signature  = PRIVATE_UDF_FILE_DATA_SIGNATURE;
  PrivFileData->SimpleFs   = This;

  PrivFileData->FileIo.Revision     = EFI_FILE_PROTOCOL_REVISION;
  PrivFileData->FileIo.Open         = UdfOpen;
  PrivFileData->FileIo.Close        = UdfClose;
  PrivFileData->FileIo.Delete       = UdfDelete;
  PrivFileData->FileIo.Read         = UdfRead;
  PrivFileData->FileIo.Write        = UdfWrite;
  PrivFileData->FileIo.GetPosition  = UdfGetPosition;
  PrivFileData->FileIo.SetPosition  = UdfSetPosition;
  PrivFileData->FileIo.GetInfo      = UdfGetInfo;
  PrivFileData->FileIo.SetInfo      = UdfSetInfo;
  PrivFileData->FileIo.Flush        = UdfFlush;

  *Root = &PrivFileData->FileIo;

  PrivFsData->OpenFiles++;

  gBS->RestoreTPL (OldTpl);

  return Status;

ErrorFindFile:
  FreePool (PrivFileData->File.FileEntry);

ErrorFindFe:
  for (Index = 0; Index < PrivFsData->Volume.FileSetDescsNo; Index++) {
    FreePool ((VOID *)PrivFsData->Volume.FileSetDescs[Index]);
  }

  FreePool ((VOID *)PrivFsData->Volume.FileSetDescs);

ErrorGetFsds:
  for (Index = 0; Index < PrivFsData->Volume.LogicalVolDescsNo; Index++) {
    FreePool ((VOID *)PrivFsData->Volume.LogicalVolDescs[Index]);
  }

  FreePool ((VOID *)PrivFsData->Volume.LogicalVolDescs);

  for (Index = 0; Index < PrivFsData->Volume.PartitionDescsNo; Index++) {
    FreePool ((VOID *)PrivFsData->Volume.PartitionDescs[Index]);
  }

  FreePool ((VOID *)PrivFsData->Volume.PartitionDescs);

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
  IN   EFI_FILE_PROTOCOL  *This,
  OUT  EFI_FILE_PROTOCOL  **NewHandle,
  IN   CHAR16             *FileName,
  IN   UINT64             OpenMode,
  IN   UINT64             Attributes
  )
{
  EFI_TPL                         OldTpl;
  EFI_STATUS                      Status;
  PRIVATE_UDF_FILE_DATA           *PrivFileData;
  PRIVATE_UDF_SIMPLE_FS_DATA      *PrivFsData;
  CHAR16                          FilePath[UDF_PATH_LENGTH] = { 0 };
  UDF_FILE_INFO                   File;
  PRIVATE_UDF_FILE_DATA           *NewPrivFileData;
  CHAR16                          *TempFileName;
  UDF_FILE_INFO                   *Root;
  UDF_FILE_INFO                   *Parent;
  UDF_LONG_ALLOCATION_DESCRIPTOR  *Icb;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  if (!This || !NewHandle || !FileName) {
    Status = EFI_INVALID_PARAMETER;
    goto ErrorInvalidParams;
  }

  if (OpenMode != EFI_FILE_MODE_READ) {
    Status = EFI_WRITE_PROTECTED;
    goto ErrorInvalidParams;
  }

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  PrivFsData = PRIVATE_UDF_SIMPLE_FS_DATA_FROM_THIS (PrivFileData->SimpleFs);

  //
  // Build full path
  //
  if (FileName[0] == L'\\') {
    StrCpy (FilePath, FileName);
  } else {
    StrCpy (FilePath, PrivFileData->AbsoluteFileName);
    StrCat (FilePath, L"\\");
    StrCat (FilePath, FileName);
  }

  MangleFileName (FilePath);
  if (!FilePath[0]) {
    Status = EFI_NOT_FOUND;
    goto ErrorBadFileName;
  }

  Root = PrivFileData->Root;

  if (Root) {
    Parent = Root;
  } else {
    Parent = &PrivFileData->File;
  }

  Icb = &Parent->FileIdentifierDesc->Icb;

  Status = FindFile (
                 PrivFsData->BlockIo,
		 PrivFsData->DiskIo,
		 &PrivFsData->Volume,
		 FilePath,
		 Root,
		 Parent,
		 Icb,
		 &File
                 );
  if (EFI_ERROR (Status)) {
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
  CopyMem (
    (VOID *)&NewPrivFileData->File,
    &File,
    sizeof (UDF_FILE_INFO)
    );

  NewPrivFileData->IsRootDirectory = FALSE;

  StrCpy (NewPrivFileData->AbsoluteFileName, FilePath);
  FileName = NewPrivFileData->AbsoluteFileName;

  while ((TempFileName = StrStr (FileName, L"\\"))) {
    FileName = TempFileName + 1;
  }

  StrCpy (NewPrivFileData->FileName, FileName);
  Status = GetFileSize (
                    PrivFsData->BlockIo,
                    PrivFsData->DiskIo,
		    &PrivFsData->Volume,
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

  PrivFsData->OpenFiles++;

  gBS->RestoreTPL (OldTpl);

  return Status;

ErrorGetFileSize:
  FreePool ((VOID *)NewPrivFileData);

ErrorAllocNewPrivFileData:
  if (File.FileEntry) {
    FreePool (File.FileEntry);
  }

  if (File.FileIdentifierDesc) {
    FreePool ((VOID *)File.FileIdentifierDesc);
  }

ErrorFindFile:
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
  IN      EFI_FILE_PROTOCOL  *This,
  IN OUT  UINTN              *BufferSize,
  OUT     VOID               *Buffer
  )
{
  EFI_TPL                         OldTpl;
  EFI_STATUS                      Status;
  PRIVATE_UDF_FILE_DATA           *PrivFileData;
  PRIVATE_UDF_SIMPLE_FS_DATA      *PrivFsData;
  UDF_VOLUME_INFO                 *Volume;
  UDF_FILE_INFO                   *Parent;
  UDF_READ_DIRECTORY_INFO         *ReadDirInfo;
  EFI_BLOCK_IO_PROTOCOL           *BlockIo;
  EFI_DISK_IO_PROTOCOL            *DiskIo;
  UDF_FILE_INFO                   FoundFile;
  UDF_FILE_IDENTIFIER_DESCRIPTOR  *NewFileIdentifierDesc;
  VOID                            *NewFileEntryData;
  CHAR16                          FileName[UDF_FILENAME_LENGTH] = { 0 };
  UINT64                          FileSize;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  if (!This || !BufferSize || (*BufferSize && !Buffer)) {
    Status = EFI_INVALID_PARAMETER;
    goto ErrorInvalidParams;
  }

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  PrivFsData = PRIVATE_UDF_SIMPLE_FS_DATA_FROM_THIS (PrivFileData->SimpleFs);

  BlockIo                = PrivFsData->BlockIo;
  DiskIo                 = PrivFsData->DiskIo;
  Volume                 = &PrivFsData->Volume;
  ReadDirInfo            = &PrivFileData->ReadDirInfo;
  NewFileIdentifierDesc  = NULL;
  NewFileEntryData       = NULL;

  if (PrivFileData->IsRootDirectory) {
    Parent = PrivFileData->Root;
  } else {
    Parent = &PrivFileData->File;
  }

  Status = EFI_VOLUME_CORRUPTED;

  if (IS_FID_NORMAL_FILE (Parent->FileIdentifierDesc)) {
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
		       Parent,
		       PrivFileData->FileSize,
		       &PrivFileData->FilePosition,
		       Buffer,
		       BufferSize
                       );
  } else if (IS_FID_DIRECTORY_FILE (Parent->FileIdentifierDesc)) {
    if (!ReadDirInfo->FidOffset && PrivFileData->FilePosition) {
      Status = EFI_DEVICE_ERROR;
      *BufferSize = 0;
      goto DoneWithNoMoreDirEnts;
    }

    for (;;) {
      Status = ReadDirectoryEntry (
                               BlockIo,
			       DiskIo,
			       Volume,
			       &Parent->FileIdentifierDesc->Icb,
			       Parent->FileEntry,
			       ReadDirInfo,
			       &NewFileIdentifierDesc
                               );
      if (EFI_ERROR (Status)) {
	if (Status == EFI_DEVICE_ERROR) {
	  FreePool (ReadDirInfo->DirectoryData);
	  ZeroMem ((VOID *)ReadDirInfo, sizeof (UDF_READ_DIRECTORY_INFO));
	  *BufferSize = 0;
	  Status = EFI_SUCCESS;
	}

	goto DoneReadDirEnt;
      }

      if (!IS_FID_PARENT_FILE (NewFileIdentifierDesc)) {
	break;
      }

      FreePool ((VOID *)NewFileIdentifierDesc);
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

    if (IS_FE_SYMLINK (NewFileEntryData)) {
      Status = ResolveSymlink (
                          BlockIo,
			  DiskIo,
			  Volume,
			  Parent,
			  NewFileEntryData,
			  &FoundFile
                          );
      if (EFI_ERROR (Status)) {
	goto ErrorFindFileFromSymlink;
      }

      FreePool ((VOID *)NewFileEntryData);
      NewFileEntryData = FoundFile.FileEntry;

      Status = GetFileNameFromFid (NewFileIdentifierDesc, FileName);
      if (EFI_ERROR (Status)) {
	FreePool ((VOID *)FoundFile.FileIdentifierDesc);
	goto ErrorGetFileName;
      }

      FreePool ((VOID *)NewFileIdentifierDesc);
      NewFileIdentifierDesc = FoundFile.FileIdentifierDesc;
    } else {
      FoundFile.FileIdentifierDesc  = NewFileIdentifierDesc;
      FoundFile.FileEntry           = NewFileEntryData;

      Status = GetFileNameFromFid (FoundFile.FileIdentifierDesc, FileName);
      if (EFI_ERROR (Status)) {
	goto ErrorGetFileName;
      }
    }

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
		     FileName,
		     BufferSize,
		     Buffer
                     );
    if (EFI_ERROR (Status)) {
      goto ErrorSetFileInfo;
    }

    PrivFileData->FilePosition++;
    Status = EFI_SUCCESS;
  } else if (IS_FID_DELETED_FILE (Parent->FileIdentifierDesc)) {
    Status = EFI_DEVICE_ERROR;
  }

ErrorSetFileInfo:
ErrorGetFileSize:
ErrorGetFileName:
ErrorFindFileFromSymlink:
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
  IN EFI_FILE_PROTOCOL  *This
  )
{
  EFI_TPL                     OldTpl;
  EFI_STATUS                  Status;
  PRIVATE_UDF_FILE_DATA       *PrivFileData;
  PRIVATE_UDF_SIMPLE_FS_DATA  *PrivFsData;
  UINTN                       Index;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  Status = EFI_SUCCESS;

  if (!This) {
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  PrivFsData = PRIVATE_UDF_SIMPLE_FS_DATA_FROM_THIS (PrivFileData->SimpleFs);

  if (!PrivFileData->IsRootDirectory) {
    if (PrivFileData->File.FileEntry) {
      FreePool (PrivFileData->File.FileEntry);
    }

    if (PrivFileData->File.FileIdentifierDesc) {
      FreePool ((VOID *)PrivFileData->File.FileIdentifierDesc);
    }

    if (PrivFileData->ReadDirInfo.DirectoryData) {
      FreePool (PrivFileData->ReadDirInfo.DirectoryData);
    }
  }

  if (!--PrivFsData->OpenFiles) {
    for (Index = 0; Index < PrivFsData->Volume.LogicalVolDescsNo; Index++) {
      FreePool ((VOID *)PrivFsData->Volume.LogicalVolDescs[Index]);
    }

    FreePool ((VOID *)PrivFsData->Volume.LogicalVolDescs);

    for (Index = 0; Index < PrivFsData->Volume.PartitionDescsNo; Index++) {
      FreePool ((VOID *)PrivFsData->Volume.PartitionDescs[Index]);
    }

    FreePool ((VOID *)PrivFsData->Volume.PartitionDescs);

    for (Index = 0; Index < PrivFsData->Volume.FileSetDescsNo; Index++) {
      FreePool ((VOID *)PrivFsData->Volume.FileSetDescs[Index]);
    }

    FreePool ((VOID *)PrivFsData->Volume.FileSetDescs);
    ZeroMem ((VOID *)&PrivFsData->Volume, sizeof (UDF_VOLUME_INFO));
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
  IN      EFI_FILE_PROTOCOL  *This,
  IN OUT  UINTN              *BufferSize,
  IN      VOID               *Buffer
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
  IN   EFI_FILE_PROTOCOL  *This,
  OUT  UINT64             *Position
  )
{
  EFI_STATUS             Status;
  PRIVATE_UDF_FILE_DATA  *PrivFileData;

  if (!This || !Position) {
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
  IN EFI_FILE_PROTOCOL  *This,
  IN UINT64             Position
  )
{
  EFI_STATUS                      Status;
  PRIVATE_UDF_FILE_DATA           *PrivFileData;
  UDF_FILE_IDENTIFIER_DESCRIPTOR  *FileIdentifierDesc;

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
      PrivFileData->ReadDirInfo.FidOffset = 0;
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
  IN      EFI_FILE_PROTOCOL  *This,
  IN      EFI_GUID           *InformationType,
  IN OUT  UINTN              *BufferSize,
  OUT     VOID               *Buffer
  )
{
  EFI_STATUS                  Status;
  PRIVATE_UDF_FILE_DATA       *PrivFileData;
  PRIVATE_UDF_SIMPLE_FS_DATA  *PrivFsData;
  UDF_FILE_INFO               *File;
  EFI_FILE_SYSTEM_INFO        *FileSystemInfo;
  UINTN                       FileSystemInfoLength;
  CHAR16                      *String;
  UDF_FILE_SET_DESCRIPTOR     *FileSetDesc;
  UINTN                       Index;
  UINT8                       *OstaCompressed;
  UINT8                       CompressionId;
  UINT64                      VolumeSize;
  UINT64                      FreeSpaceSize;
  CHAR16                      VolumeLabel[64];

  if (!This || !InformationType || !BufferSize || (*BufferSize && !Buffer)) {
    return EFI_INVALID_PARAMETER;
  }

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  PrivFsData = PRIVATE_UDF_SIMPLE_FS_DATA_FROM_THIS (PrivFileData->SimpleFs);

  if (PrivFileData->IsRootDirectory) {
    File = PrivFileData->Root;
  } else {
    File = &PrivFileData->File;
  }

  Status = EFI_UNSUPPORTED;

  if (CompareGuid (InformationType, &gEfiFileInfoGuid)) {
    Status = SetFileInfo (
                     File,
		     PrivFileData->FileSize,
		     PrivFileData->FileName,
		     BufferSize,
		     Buffer
                     );
  } else if (CompareGuid (InformationType, &gEfiFileSystemInfoGuid)) {
    String = VolumeLabel;

    FileSetDesc = PrivFsData->Volume.FileSetDescs[0];

    OstaCompressed = &FileSetDesc->LogicalVolumeIdentifier[0];
    CompressionId = FileSetDesc->LogicalVolumeIdentifier[0];

    //
    // Check for valid compression ID
    //
    if (CompressionId != 8 && CompressionId != 16) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
    }

    for (Index = 1; Index < 128; Index++) {
      if (CompressionId == 16) {
	*String = *(UINT8 *)(OstaCompressed + Index) << 8;
	Index++;
      } else {
	*String = 0;
      }

      if (Index < 128) {
	*String |= *(UINT8 *)(OstaCompressed + Index);
      }

      //
      // Unlike FID Identifiers, Logical Volume Identifier is in a
      // NULL-terminated OSTA compressed format, so we must check for the NULL
      // character.
      //
      if (!*String) {
	break;
      }

      String++;
    }

    *String = L'\0';

    FileSystemInfoLength = StrSize (VolumeLabel) +
                           sizeof (EFI_FILE_SYSTEM_INFO);
    if (*BufferSize < FileSystemInfoLength) {
      *BufferSize = FileSystemInfoLength;
      Status = EFI_BUFFER_TOO_SMALL;
      goto Exit;
    }

    FileSystemInfo = (EFI_FILE_SYSTEM_INFO *)Buffer;
    StrCpy (FileSystemInfo->VolumeLabel, VolumeLabel);
    Status = GetVolumeSize (
                        PrivFsData->BlockIo,
			PrivFsData->DiskIo,
			&PrivFsData->Volume,
			&VolumeSize,
			&FreeSpaceSize);
    if (EFI_ERROR (Status)) {
      goto Exit;
    }

    FileSystemInfo->Size        = FileSystemInfoLength;
    FileSystemInfo->ReadOnly    = TRUE;
    FileSystemInfo->BlockSize   = PrivFsData->BlockIo->Media->BlockSize;
    FileSystemInfo->VolumeSize  = VolumeSize;
    FileSystemInfo->FreeSpace   = FreeSpaceSize;

    *BufferSize = FileSystemInfoLength;
    Status = EFI_SUCCESS;
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
  IN EFI_FILE_PROTOCOL  *This,
  IN EFI_GUID           *InformationType,
  IN UINTN              BufferSize,
  IN VOID               *Buffer
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
