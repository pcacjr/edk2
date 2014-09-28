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

  Status = FindFile (
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

  PrivFileData->IsRootDirectory                        = TRUE;
  PrivFileData->FilePosition                           = 0;
  PrivFileData->File.DirectoryEntryInfo.AdOffset       = 0;
  PrivFileData->File.DirectoryEntryInfo.FidOffset      = 0;
  PrivFileData->File.DirectoryEntryInfo.AedAdsOffset   = 0;
  PrivFileData->File.DirectoryEntryInfo.AedAdsLength   = 0;

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
  CHAR16                                 *String;
  CHAR16                                 *TempString;
  CHAR16                                 *FileNameSavedPointer;
  CHAR16                                 *TempFileName;
  EFI_BLOCK_IO_PROTOCOL                  *BlockIo;
  EFI_DISK_IO_PROTOCOL                   *DiskIo;
  UDF_PARTITION_DESCRIPTOR               **PartitionDescs;
  UINTN                                  PartitionDescsNo;
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

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  FileName = MangleFileName (FileName);
  if ((!FileName) || ((FileName) && (!*FileName))) {
    Status = EFI_NOT_FOUND;
    goto Exit;
  }

  String = (CHAR16 *)AllocatePool ((StrLen (FileName) + 1) * sizeof (CHAR16));
  if (!String) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  BlockIo                = PrivFileData->BlockIo;
  DiskIo                 = PrivFileData->DiskIo;
  PartitionDescs         = PrivFileData->Volume.PartitionDescs;
  PartitionDescsNo       = PrivFileData->Volume.PartitionDescsNo;
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
      if (!*FileName) {
	Found             = TRUE;
	IsRootDirectory   = TRUE;
	break;
      } else {
	continue;
      }
    }

    if (StrCmp (String, L".") == 0) {
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

    Status = FindFile (
                   BlockIo,
		   DiskIo,
		   &PrevFileIdentifierDesc->Icb,
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

    NewPrivFileData->IsRootDirectory                     = IsRootDirectory;
    NewPrivFileData->FileSize                            =
                              GetFileSize(NewPrivFileData->File.FileEntry);
    NewPrivFileData->FilePosition                           = 0;
    NewPrivFileData->File.DirectoryEntryInfo.AdOffset       = 0;
    NewPrivFileData->File.DirectoryEntryInfo.FidOffset      = 0;
    NewPrivFileData->File.DirectoryEntryInfo.AedAdsOffset   = 0;
    NewPrivFileData->File.DirectoryEntryInfo.AedAdsLength   = 0;

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
  EFI_TPL                                OldTpl;
  EFI_STATUS                             Status;
  PRIVATE_UDF_FILE_DATA                  *PrivFileData;
  UDF_PARTITION_DESCRIPTOR               **PartitionDescs;
  UINTN                                  PartitionDescsNo;
  UDF_FILE_SET_DESCRIPTOR                **FileSetDescs;
  VOID                                   *FileEntryData;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *FileIdentifierDesc;
  EFI_BLOCK_IO_PROTOCOL                  *BlockIo;
  EFI_DISK_IO_PROTOCOL                   *DiskIo;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *NewFileIdentifierDesc;
  VOID                                   *NewFileEntryData;
  CHAR16                                 *FoundFileName;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  if ((!This) || (!Buffer)) {
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  PartitionDescs          = PrivFileData->Volume.PartitionDescs;
  PartitionDescsNo        = PrivFileData->Volume.PartitionDescsNo;
  FileSetDescs            = PrivFileData->Volume.FileSetDescs;
  FileEntryData           = PrivFileData->File.FileEntry;
  FileIdentifierDesc      = PrivFileData->File.FileIdentifierDesc;
  BlockIo                 = PrivFileData->BlockIo;
  DiskIo                  = PrivFileData->DiskIo;
  NewFileIdentifierDesc   = NULL;
  NewFileEntryData        = NULL;

  if (IS_FID_NORMAL_FILE (FileIdentifierDesc)) {
    if (PrivFileData->FilePosition > PrivFileData->FileSize) {
      //
      // File's position is beyond the EOF
      //
      Status = EFI_DEVICE_ERROR;
      goto Exit;
    } else if (
      PrivFileData->FilePosition == PrivFileData->FileSize
      ) {
      *BufferSize = 0;
      Status = EFI_SUCCESS;
      goto Exit;
    }

    Status = ReadFileData (
                       BlockIo,
		       DiskIo,
		       &FileIdentifierDesc->Icb,
		       PartitionDescs,
		       PartitionDescsNo,
		       FileEntryData,
		       PrivFileData->FileSize,
		       &PrivFileData->FilePosition,
		       Buffer,
		       BufferSize
                       );
  } else if (IS_FID_DIRECTORY_FILE (FileIdentifierDesc)) {
    if ((!PrivFileData->File.DirectoryEntryInfo.FidOffset) &&
	(PrivFileData->FilePosition)) {
      Status = EFI_DEVICE_ERROR;
      *BufferSize = 0;
      goto Exit;
    }

NextEntry:
    Status = ReadDirectoryEntry (
                             BlockIo,
			     DiskIo,
			     &FileIdentifierDesc->Icb,
			     PartitionDescs,
			     PartitionDescsNo,
			     FileEntryData,
			     &PrivFileData->File.DirectoryEntryInfo.AedAdsOffset,
			     &PrivFileData->File.DirectoryEntryInfo.AedAdsLength,
			     &PrivFileData->File.DirectoryEntryInfo.AdOffset,
			     &PrivFileData->File.DirectoryEntryInfo.FidOffset,
			     &NewFileIdentifierDesc
                             );
    if (EFI_ERROR (Status)) {
      if (Status == EFI_DEVICE_ERROR) {
	PrivFileData->File.DirectoryEntryInfo.AdOffset       = 0;
	PrivFileData->File.DirectoryEntryInfo.FidOffset      = 0;
	PrivFileData->File.DirectoryEntryInfo.AedAdsOffset   = 0;
	PrivFileData->File.DirectoryEntryInfo.AedAdsLength   = 0;

	*BufferSize = 0;
	Status = EFI_SUCCESS;
      }

      goto Exit;
    }

    if (IS_FID_PARENT_FILE (NewFileIdentifierDesc)) {
      FreePool (NewFileIdentifierDesc);
      goto NextEntry;
    }

    Status = FindFileEntryFromLongAd (
                                  BlockIo,
				  DiskIo,
				  PartitionDescs,
				  PartitionDescsNo,
				  &NewFileIdentifierDesc->Icb,
				  &NewFileEntryData
                                  );
    if (EFI_ERROR (Status)) {
      goto FreeExit;
    }

    Status = GetFileNameFromFid (NewFileIdentifierDesc, &FoundFileName);
    if (EFI_ERROR (Status)) {
      goto FreeExit;
    }

    Status = SetFileInfo (
                     NewFileIdentifierDesc,
		     NewFileEntryData,
		     GetFileSize (NewFileEntryData),
		     FoundFileName,
		     BufferSize,
		     Buffer
                     );
    if (EFI_ERROR (Status)) {
      goto FreeExit;
    }

    PrivFileData->FilePosition++;
    Status = EFI_SUCCESS;
  } else if (IS_FID_DELETED_FILE (FileIdentifierDesc)) {
    Status = EFI_DEVICE_ERROR;
  } else {
    Status = EFI_VOLUME_CORRUPTED;
  }

FreeExit:
#if 0
  if (NewFileIdentifierDesc) {
    FreePool ((VOID *)NewFileIdentifierDesc);
  }
  if (NewFileEntryData) {
    FreePool (NewFileEntryData);
  }
#endif

Exit:
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

  //
  // crashes when doing `cd \test7`
  //
#if 0
  if (PrivFileData->AbsoluteFileName) {
    FreePool ((VOID *)PrivFileData->AbsoluteFileName);
  }

  if (PrivFileData->FileName) {
    FreePool ((VOID *)PrivFileData->FileName);
  }
#endif

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
      PrivFileData->FilePosition                           = Position;
      PrivFileData->File.DirectoryEntryInfo.AdOffset       = 0;
      PrivFileData->File.DirectoryEntryInfo.FidOffset      = 0;
      PrivFileData->File.DirectoryEntryInfo.AedAdsOffset   = 0;
      PrivFileData->File.DirectoryEntryInfo.AedAdsLength   = 0;
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
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *FileIdentifierDesc;
  VOID                                   *FileEntryData;
  EFI_FILE_SYSTEM_INFO                   *FileSystemInfo;
  UINTN                                  FileSystemInfoLength;
  CHAR16                                 *String;
  UDF_FILE_SET_DESCRIPTOR                *FileSetDesc;
  CHAR16                                 *CharP;
  UINTN                                  Index;

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  FileIdentifierDesc   = PrivFileData->File.FileIdentifierDesc;
  FileEntryData        = PrivFileData->File.FileEntry;

  if (CompareGuid (InformationType, &gEfiFileInfoGuid)) {
    //
    // Check if BufferSize is too small to read the current directory entry
    //
    Status = SetFileInfo (
                     FileIdentifierDesc,
		     FileEntryData,
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

  for (Index = 0; Index < PartitionDescsNo; Index++) {
    PartitionDesc = PartitionDescs[Index];
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
  IN UDF_PARTITION_DESCRIPTOR         **PartitionDescs,
  IN UINTN                            PartitionDescsNo,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR   *LongAd
  )
{
  UDF_PARTITION_DESCRIPTOR            *PartitionDesc;

  if (!GET_EXTENT_LENGTH (LongAd)) {
    return 0;
  }

  PartitionDesc = GetPdFromLongAd (PartitionDescs, PartitionDescsNo, LongAd);
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
  UDF_DESCRIPTOR_TAG                         *DescriptorTag;

  Lsn = GetLsnFromLongAd (PartitionDescs, PartitionDescsNo, LongAd);
  if (!Lsn) {
    Status = EFI_VOLUME_CORRUPTED;
    goto Exit;
  }

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
    DescriptorTag = (UDF_DESCRIPTOR_TAG *)*FileEntry;
    FreePool (*FileEntry);
    Status = EFI_VOLUME_CORRUPTED;
  }

Exit:
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
GetFileNameFromFid (
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR   *FileIdentifierDesc,
  OUT CHAR16                          **FileName
  )
{
  EFI_STATUS                          Status;
  CHAR8                               *AsciiFileName;

  Status = EFI_SUCCESS;

  AsciiFileName = (CHAR8 *)AllocatePool (
                                    FileIdentifierDesc->LengthOfFileIdentifier
                                    );
  if (!AsciiFileName) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

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
                                 AsciiStrLen (AsciiFileName) +
				 sizeof (CHAR16)
                                 );
  if (!*FileName) {
    Status = EFI_OUT_OF_RESOURCES;
    FreePool ((VOID *)AsciiFileName);
    goto Exit;
  }

  *FileName = AsciiStrToUnicodeStr (AsciiFileName, *FileName);

Exit:
  return Status;
}

EFI_STATUS
FindFile (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR          *ParentIcb,
  IN UDF_PARTITION_DESCRIPTOR                **PartitionDescs,
  IN UINTN                                   PartitionDescsNo,
  IN CHAR16                                  *FileName,
  IN VOID                                    *FileEntryData,
  OUT UDF_FILE_IDENTIFIER_DESCRIPTOR         **FoundFileIdentifierDesc,
  OUT VOID                                   **FoundFileEntry
  )
{
  EFI_STATUS                                 Status;
  UDF_FILE_ENTRY                             *FileEntry;
  UDF_EXTENDED_FILE_ENTRY                    *ExtendedFileEntry;
  UDF_FILE_IDENTIFIER_DESCRIPTOR             *FileIdentifierDesc;
  UINT64                                     AedAdsOffset;
  UINT64                                     AedAdsLength;
  UINT64                                     AdOffset;
  UINT64                                     FidOffset;
  BOOLEAN                                    Found;
  UINTN                                      FileNameLength;
  CHAR16                                     *FoundFileName;
  VOID                                       *CompareFileEntry;

  if (IS_EFE (FileEntryData)) {
    ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)FileEntryData;

    if (!IS_EFE_DIRECTORY (ExtendedFileEntry)) {
      Status = EFI_NOT_FOUND;
      goto Exit;
    }
  } else if (IS_FE (FileEntryData)) {
    FileEntry = (UDF_FILE_ENTRY *)FileEntryData;

    if (!IS_FE_DIRECTORY (FileEntry)) {
      Status = EFI_NOT_FOUND;
      goto Exit;
    }
  }

  AdOffset         = 0;
  FidOffset        = 0;
  AedAdsOffset     = 0;
  AedAdsLength     = 0;
  Found            = FALSE;
  FileNameLength   = StrLen (FileName);
  for (;;) {
    Status = ReadDirectoryEntry (
                             BlockIo,
			     DiskIo,
			     ParentIcb,
			     PartitionDescs,
			     PartitionDescsNo,
			     FileEntryData,
			     &AedAdsOffset,
			     &AedAdsLength,
			     &AdOffset,
			     &FidOffset,
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
	goto NextEntry;
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

NextEntry:
    FreePool ((VOID *)FileIdentifierDesc);
  }

  if (Found) {
    *FoundFileIdentifierDesc   = FileIdentifierDesc;
    Status                     = EFI_SUCCESS;

    if (FoundFileEntry) {
      Status = FindFileEntryFromLongAd (
                                    BlockIo,
				    DiskIo,
				    PartitionDescs,
				    PartitionDescsNo,
				    &FileIdentifierDesc->Icb,
				    &CompareFileEntry
                                    );
      if (EFI_ERROR (Status)) {
	FreePool ((VOID *)FileIdentifierDesc);
      }

      if (CompareMem (
	    (VOID *)FileEntryData,
	    (VOID *)CompareFileEntry,
	    BlockIo->Media->BlockSize
	    )
	 ) {
	*FoundFileIdentifierDesc   = FileIdentifierDesc;
	*FoundFileEntry            = CompareFileEntry;
	Status                     = EFI_SUCCESS;
      } else {
	FreePool ((VOID *)FileIdentifierDesc);
	FreePool ((VOID *)CompareFileEntry);
	Status = EFI_NOT_FOUND;
      }
    }
  }

Exit:
  return Status;
}

EFI_STATUS
GetLongAdFromAds (
  IN VOID                              *ExtentData,
  IN OUT UINT64                        *ExtentDataOffset,
  IN UINT64                            ExtentLength,
  OUT UDF_LONG_ALLOCATION_DESCRIPTOR   **FoundLongAd
  )
{
  UINT64                               AdLength;
  UDF_LONG_ALLOCATION_DESCRIPTOR       *LongAd;

  AdLength = sizeof (UDF_LONG_ALLOCATION_DESCRIPTOR);

  for (;;) {
    if (*ExtentDataOffset >= ExtentLength) {
      return EFI_DEVICE_ERROR;
    }

    LongAd = (UDF_LONG_ALLOCATION_DESCRIPTOR *)(
                                           (UINT8 *)ExtentData +
					   *ExtentDataOffset
                                           );
    switch (GET_EXTENT_FLAGS (LongAd)) {
      case EXTENT_NOT_RECORDED_BUT_ALLOCATED:
      case EXTENT_NOT_RECORDED_NOT_ALLOCATED:
	*ExtentDataOffset += AdLength;
	continue;
      case EXTENT_IS_NEXT_EXTENT:
      case EXTENT_RECORDED_AND_ALLOCATED:
	goto Exit;
    }
  }

Exit:
  *FoundLongAd = LongAd;
  return EFI_SUCCESS;
}

UINT64
GetFileSize (
  IN VOID                           *Data
  )
{
  UINT64                            Size;
  UDF_EXTENDED_FILE_ENTRY           *ExtendedFileEntry;
  UDF_FILE_ENTRY                    *FileEntry;
  UINT64                            Length;
  UINT8                             *AdsData;
  UINT64                            Offset;
  UINT64                            AdLength;
  UDF_LONG_ALLOCATION_DESCRIPTOR    *LongAd;
  UDF_SHORT_ALLOCATION_DESCRIPTOR   *ShortAd;

  Size = 0;

  switch (GET_FE_RECORDING_FLAGS (Data)) {
    case INLINE_DATA:
      if (IS_EFE (Data)) {
	Size = ((UDF_EXTENDED_FILE_ENTRY *)Data)->InformationLength;
      } else if (IS_FE (Data)) {
	Size = ((UDF_FILE_ENTRY *)Data)->InformationLength;
      }

      break;
    case LONG_ADS_SEQUENCE:
      if (IS_EFE (Data)) {
	ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)Data;
	Length      = ExtendedFileEntry->LengthOfAllocationDescriptors;
	AdsData     = (UINT8 *)(
                           &ExtendedFileEntry->Data +
                           ExtendedFileEntry->LengthOfExtendedAttributes
                           );
      } else if (IS_FE (Data)) {
	FileEntry = (UDF_FILE_ENTRY *)Data;
	Length      = FileEntry->LengthOfAllocationDescriptors;
	AdsData     = (UINT8 *)(
                           &FileEntry->Data +
                           FileEntry->LengthOfExtendedAttributes
                           );
      }

      AdLength = sizeof (UDF_LONG_ALLOCATION_DESCRIPTOR);
      for (Offset = 0; Offset < Length; Offset += AdLength) {
	LongAd = (UDF_LONG_ALLOCATION_DESCRIPTOR *)(AdsData + Offset);
	switch (GET_EXTENT_FLAGS (LongAd)) {
	  case EXTENT_IS_NEXT_EXTENT:
	  case EXTENT_RECORDED_AND_ALLOCATED:
	    break;
	  case EXTENT_NOT_RECORDED_BUT_ALLOCATED:
	  case EXTENT_NOT_RECORDED_NOT_ALLOCATED:
	    continue;
	}

	Size += LongAd->ExtentLength;
      }

      break;
    case SHORT_ADS_SEQUENCE:
      if (IS_EFE (Data)) {
	ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)Data;
	Length      = ExtendedFileEntry->LengthOfAllocationDescriptors;
	AdsData     = (UINT8 *)(
                           &ExtendedFileEntry->Data +
                           ExtendedFileEntry->LengthOfExtendedAttributes
                           );
      } else if (IS_FE (Data)) {
	FileEntry = (UDF_FILE_ENTRY *)Data;
	Length      = FileEntry->LengthOfAllocationDescriptors;
	AdsData     = (UINT8 *)(
                           &FileEntry->Data +
                           FileEntry->LengthOfExtendedAttributes
                           );
      }

      AdLength = sizeof (UDF_SHORT_ALLOCATION_DESCRIPTOR);
      for (Offset = 0; Offset < Length; Offset += AdLength) {
	ShortAd = (UDF_SHORT_ALLOCATION_DESCRIPTOR *)(AdsData + Offset);
	Size += ShortAd->ExtentLength;
      }

      break;
  }

  return Size;
}

EFI_STATUS
ReadFileData (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR          *ParentIcb,
  IN UDF_PARTITION_DESCRIPTOR                **PartitionDescs,
  IN UINTN                                   PartitionDescsNo,
  IN VOID                                    *FileEntryData,
  IN UINT64                                  FileSize,
  IN OUT UINT64                              *CurrentFilePosition,
  IN OUT VOID                                *Buffer,
  IN OUT UINT64                              *BufferSize
  )
{
  EFI_STATUS                                 Status;
  UINT32                                     BlockSize;
  UDF_EXTENDED_FILE_ENTRY                    *ExtendedFileEntry;
  UDF_FILE_ENTRY                             *FileEntry;
  UINT64                                     Offset;
  UINT64                                     Length;
  UINT8                                      *AdsData;
  UINT64                                     AdLength;
  UDF_LONG_ALLOCATION_DESCRIPTOR             *LongAd;
  UDF_SHORT_ALLOCATION_DESCRIPTOR            *ShortAd;
  UINT64                                     FilePosition;
  INT64                                      ExtStartOffset;
  UINT64                                     ExtLen;
  UINT64                                     BytesLeft;
  UINT64                                     BufferOffset;
  UINT64                                     Lsn;
  UINT64                                     DataOffset;

  Status      = EFI_VOLUME_CORRUPTED;
  BlockSize   = BlockIo->Media->BlockSize;

  switch (GET_FE_RECORDING_FLAGS (FileEntryData)) {
    case INLINE_DATA:
      if (*BufferSize > FileSize - *CurrentFilePosition + 1) {
	*BufferSize = FileSize - *CurrentFilePosition + 1;
      }

      if (IS_EFE (FileEntryData)) {
	ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)FileEntryData;
	CopyMem (
	  Buffer,
	  (VOID *)((UINT8 *)(
                        &ExtendedFileEntry->Data +
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
                        &FileEntry->Data +
			FileEntry->LengthOfExtendedAttributes +
			*CurrentFilePosition
		     )),
	  *BufferSize
          );
      }

      *CurrentFilePosition   += *BufferSize - 1;
      Status                 = EFI_SUCCESS;
      break;
    case LONG_ADS_SEQUENCE:
      if (IS_EFE (FileEntryData)) {
	ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)FileEntryData;
	Length      = ExtendedFileEntry->LengthOfAllocationDescriptors;
	AdsData     = (UINT8 *)(
                           &ExtendedFileEntry->Data +
			   ExtendedFileEntry->LengthOfExtendedAttributes
                           );
      } else if (IS_FE (FileEntryData)) {
	FileEntry = (UDF_FILE_ENTRY *)FileEntryData;
	Length      = FileEntry->LengthOfAllocationDescriptors;
	AdsData     = (UINT8 *)(
                           &FileEntry->Data +
                           FileEntry->LengthOfExtendedAttributes
                           );
      }

      FilePosition     = 0;
      AdLength         = sizeof (UDF_LONG_ALLOCATION_DESCRIPTOR);
      ExtStartOffset   = 0;
      ExtLen           = 0;
      for (Offset = 0; Offset < Length; Offset += AdLength) {
	LongAd = (UDF_LONG_ALLOCATION_DESCRIPTOR *)(AdsData + Offset);
	switch (GET_EXTENT_FLAGS (LongAd)) {
	  case EXTENT_RECORDED_AND_ALLOCATED:
	  case EXTENT_IS_NEXT_EXTENT:
	    break;
	  case EXTENT_NOT_RECORDED_BUT_ALLOCATED:
	  case EXTENT_NOT_RECORDED_NOT_ALLOCATED:
	    continue;
	}

	if (FilePosition == *CurrentFilePosition) {
	  break;
	}

	if (FilePosition + LongAd->ExtentLength == *CurrentFilePosition) {
	  Offset += AdLength;
	  break;
	}

	if (FilePosition + LongAd->ExtentLength > *CurrentFilePosition) {
	  ExtStartOffset =
	    LongAd->ExtentLength - ((FilePosition +
				     LongAd->ExtentLength) -
				    *CurrentFilePosition);
	  if (ExtStartOffset < 0) {
	    ExtStartOffset = -(ExtStartOffset);
	  }

	  ExtLen = ExtStartOffset;
	  break;
	}
      }

      BufferOffset   = 0;
      BytesLeft      = *BufferSize;
      for (; Offset < Length; Offset += AdLength) {
	LongAd = (UDF_LONG_ALLOCATION_DESCRIPTOR *)(AdsData + Offset);
	switch (GET_EXTENT_FLAGS (LongAd)) {
	  case EXTENT_RECORDED_AND_ALLOCATED:
	  case EXTENT_IS_NEXT_EXTENT:
	    break;
	  case EXTENT_NOT_RECORDED_BUT_ALLOCATED:
	  case EXTENT_NOT_RECORDED_NOT_ALLOCATED:
	    continue;
	}

	if (LongAd->ExtentLength - ExtLen > BytesLeft) {
	  ExtLen = BytesLeft;
	} else {
	  ExtLen = LongAd->ExtentLength - ExtLen;
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
	  goto Exit;
	}

	BytesLeft              -= ExtLen;
	BufferOffset           += ExtLen;
	*CurrentFilePosition   += ExtLen;
	ExtStartOffset         = 0;
	ExtLen                 = 0;
      }

      *BufferSize   = BufferOffset;
      Status        = EFI_SUCCESS;
      break;
    case SHORT_ADS_SEQUENCE:
      if (IS_EFE (FileEntryData)) {
	ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)FileEntryData;
	Length      = ExtendedFileEntry->LengthOfAllocationDescriptors;
	AdsData     = (UINT8 *)(
                           &ExtendedFileEntry->Data +
			   ExtendedFileEntry->LengthOfExtendedAttributes
                           );
      } else if (IS_FE (FileEntryData)) {
	FileEntry = (UDF_FILE_ENTRY *)FileEntryData;
	Length      = FileEntry->LengthOfAllocationDescriptors;
	AdsData     = (UINT8 *)(
                           &FileEntry->Data +
                           FileEntry->LengthOfExtendedAttributes
                           );
      }

      FilePosition     = 0;
      AdLength         = sizeof (UDF_SHORT_ALLOCATION_DESCRIPTOR);
      ExtStartOffset   = 0;
      ExtLen           = 0;
      for (Offset = 0; Offset < Length; Offset += AdLength) {
	ShortAd = (UDF_SHORT_ALLOCATION_DESCRIPTOR *)(AdsData + Offset);
	if (!ShortAd->ExtentLength) {
	  continue;
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
      }

      BufferOffset   = 0;
      BytesLeft      = *BufferSize;
      for (; Offset < Length; Offset += AdLength) {
	ShortAd = (UDF_SHORT_ALLOCATION_DESCRIPTOR *)(AdsData + Offset);
	if (!ShortAd->ExtentLength) {
	  continue;
	}

	if (ShortAd->ExtentLength - ExtLen > BytesLeft) {
	  ExtLen = BytesLeft;
	} else {
	  ExtLen = ShortAd->ExtentLength - ExtLen;
	}

        Lsn = GetLsnFromShortAd (
	                    GetPdFromLongAd (
                                       PartitionDescs,
				       PartitionDescsNo,
				       ParentIcb
                                       ),
			    ShortAd
                            );
	if (!Lsn) {
	  Status = EFI_VOLUME_CORRUPTED;
	  goto Exit;
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
	  goto Exit;
	}

	BytesLeft              -= ExtLen;
	BufferOffset           += ExtLen;
	*CurrentFilePosition   += ExtLen;
	ExtStartOffset         = 0;
	ExtLen                 = 0;
      }

      *BufferSize   = BufferOffset;
      Status        = EFI_SUCCESS;
      break;
  }

Exit:
  return Status;
}

EFI_STATUS
GetAedAdsData (
  IN EFI_BLOCK_IO_PROTOCOL            *BlockIo,
  IN EFI_DISK_IO_PROTOCOL             *DiskIo,
  IN UDF_PARTITION_DESCRIPTOR         **PartitionDescs,
  IN UINTN                            PartitionDescsNo,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR   *LongAd,
  OUT UINT64                          *Offset,
  OUT UINT64                          *Length
  )
{
  EFI_STATUS                          Status;
  UINT32                              ExtentLength;
  UINT8                               *Data;
  UINT64                              Lsn;
  UINT32                              BlockSize;
  UDF_ALLOCATION_EXTENT_DESCRIPTOR    *AllocExtDesc;

  Data           = NULL;
  ExtentLength   = GET_EXTENT_LENGTH (LongAd);

  Data = (UINT8 *)AllocatePool (ExtentLength);
  if (!Data) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
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

  BlockSize = BlockIo->Media->BlockSize;

  Status = DiskIo->ReadDisk (
                          DiskIo,
			  BlockIo->Media->MediaId,
			  MultU64x32 (Lsn, BlockSize),
			  ExtentLength,
                          (VOID *)Data
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
    FreePool ((VOID *)Data);
  }

  return Status;
}

EFI_STATUS
ReadDirectoryEntry (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_LONG_ALLOCATION_DESCRIPTOR          *ParentIcb,
  IN UDF_PARTITION_DESCRIPTOR                **PartitionDescs,
  IN UINTN                                   PartitionDescsNo,
  IN VOID                                    *FileEntryData,
  IN OUT UINT64                              *AedAdsOffset,
  IN OUT UINT64                              *AedAdsLength,
  IN OUT UINT64                              *AdOffset,
  IN OUT UINT64                              *FidOffset,
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

  Status       = EFI_VOLUME_CORRUPTED;
  BlockSize    = BlockIo->Media->BlockSize;
  ExtentData   = NULL;

  switch (GET_FE_RECORDING_FLAGS (FileEntryData)) {
    case INLINE_DATA:
      if (IS_EFE (FileEntryData)) {
	ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)FileEntryData;

	Length   = ExtendedFileEntry->InformationLength;
	Data     = (UINT8 *)(
                        &ExtendedFileEntry->Data +
			ExtendedFileEntry->LengthOfExtendedAttributes
                        );
      } else if (IS_FE (FileEntryData)) {
	FileEntry = (UDF_FILE_ENTRY *)FileEntryData;

	Length   = FileEntry->InformationLength;
	Data     = (UINT8 *)(
                        &FileEntry->Data +
			FileEntry->LengthOfExtendedAttributes
                        );
      }

ExcludeDeletedFile0:
      if (*FidOffset >= Length) {
	Status = EFI_DEVICE_ERROR;
	goto Exit;
      }

      FileIdentifierDesc = (UDF_FILE_IDENTIFIER_DESCRIPTOR *)(
                                                            Data +
							    *FidOffset
                                                            );
      *FidOffset += GetFidDescriptorLength (FileIdentifierDesc);

      if (IS_FID_DELETED_FILE (FileIdentifierDesc)) {
	goto ExcludeDeletedFile0;
      }

      Status = EFI_SUCCESS;
      break;
    case LONG_ADS_SEQUENCE:
      if (!*AedAdsLength) {
        if (IS_EFE (FileEntryData)) {
	  ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)FileEntryData;
	  Length      = ExtendedFileEntry->LengthOfAllocationDescriptors;
	  AdsData     = (UINT8 *)(
                             &ExtendedFileEntry->Data +
			     ExtendedFileEntry->LengthOfExtendedAttributes
                             );
	} else if (IS_FE (FileEntryData)) {
	  FileEntry = (UDF_FILE_ENTRY *)FileEntryData;
	  Length      = FileEntry->LengthOfAllocationDescriptors;
	  AdsData     = (UINT8 *)(
                             &FileEntry->Data +
                             FileEntry->LengthOfExtendedAttributes
                             );
	}
      } else {
HandleIndirectExt:
	Length = *AedAdsLength;

	AdsData = (UINT8 *)AllocatePool (Length);
	if (!AdsData) {
	  Status = EFI_OUT_OF_RESOURCES;
	  goto Exit;
	}

        Status = DiskIo->ReadDisk (
                                DiskIo,
			        BlockIo->Media->MediaId,
			        *AedAdsOffset,
			        Length,
			        (VOID *)AdsData
	                        );
	if (EFI_ERROR (Status)) {
	  goto Exit;
	}
      }

      AdLength = sizeof (UDF_LONG_ALLOCATION_DESCRIPTOR);

ExcludeDeletedFile1:
      for (;;) {
	Status = GetLongAdFromAds (AdsData, AdOffset, Length, &LongAd);
	if (EFI_ERROR (Status)) {
	  goto Exit;
	}

	if (GET_EXTENT_FLAGS (LongAd) == EXTENT_IS_NEXT_EXTENT) {
	  Status = GetAedAdsData (
                              BlockIo,
			      DiskIo,
			      PartitionDescs,
			      PartitionDescsNo,
			      LongAd,
			      AedAdsOffset,
			      AedAdsLength
                              );
	  if (EFI_ERROR (Status)) {
	    goto Exit;
	  }

	  *AdOffset = 0;
	  goto HandleIndirectExt;
	}

	ExtentLength = GET_EXTENT_LENGTH (LongAd);
	if (*FidOffset < ExtentLength) {
	  break;
	}

	*AdOffset    += AdLength;
	*FidOffset   -= ExtentLength;
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

      RemBytes = ExtentLength - *FidOffset;
      if (RemBytes < sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)) {
	*AdOffset += AdLength;
	Status = GetLongAdFromAds (AdsData, AdOffset, Length, &LongAd);
	if (EFI_ERROR (Status)) {
	  goto FreeExit;
	}

	Buffer = AllocatePool (RemBytes);
	if (!Buffer) {
	  Status = EFI_OUT_OF_RESOURCES;
	  goto FreeExit;
	}

	CopyMem (
	  Buffer,
	  (VOID *)((UINT8 *)(ExtentData + *FidOffset)),
	  RemBytes
	  );

	FreePool (ExtentData);

	if (GET_EXTENT_FLAGS (LongAd) == EXTENT_IS_NEXT_EXTENT) {
	  Status = GetAedAdsData (
                              BlockIo,
			      DiskIo,
			      PartitionDescs,
			      PartitionDescsNo,
			      LongAd,
			      AedAdsOffset,
			      AedAdsLength
                              );
	  if (EFI_ERROR (Status)) {
	    goto Exit;
	  }

	  Length = *AedAdsLength;

	  Status = DiskIo->ReadDisk (
                                  DiskIo,
				  BlockIo->Media->MediaId,
				  *AedAdsOffset,
				  Length,
				  (VOID *)AdsData
	                          );
	  if (EFI_ERROR (Status)) {
	    goto Exit;
	  }

	  *AdOffset = 0;
	  Status = GetLongAdFromAds (AdsData, AdOffset, Length, &LongAd);
	  if (EFI_ERROR (Status)) {
	    goto Exit;
	  }
	}

	ExtentLength = GET_EXTENT_LENGTH (LongAd);

        Lsn = GetLsnFromLongAd (
			    PartitionDescs,
			    PartitionDescsNo,
			    LongAd
                            );
	if (!Lsn) {
	  Status = EFI_VOLUME_CORRUPTED;
	  goto Exit;
	}

	ExtentData = AllocatePool (ExtentLength + RemBytes);
	if (!ExtentData) {
	  FreePool (Buffer);
	  Status = EFI_OUT_OF_RESOURCES;
	  goto Exit;
	}

	CopyMem (ExtentData, Buffer, RemBytes);
	FreePool (Buffer);

	Status = DiskIo->ReadDisk (
                                DiskIo,
				BlockIo->Media->MediaId,
				MultU64x32 (Lsn, BlockSize),
				ExtentLength,
				(VOID *)((UINT8 *)(ExtentData + RemBytes))
                                );
	if (EFI_ERROR (Status)) {
	  goto FreeExit;
	}

	*FidOffset = 0;
      } else {
	RemBytes = 0;
      }

      FileIdentifierDesc = (UDF_FILE_IDENTIFIER_DESCRIPTOR *)((UINT8 *)(
                                                                   ExtentData +
								   *FidOffset
                                                                   )
	                                                     );
      *FidOffset += GetFidDescriptorLength (FileIdentifierDesc) - RemBytes;

      if (IS_FID_DELETED_FILE (FileIdentifierDesc)) {
	FreePool (ExtentData);
	ExtentData = NULL;
	goto ExcludeDeletedFile1;
      }

      Status = EFI_SUCCESS;
      break;
    case SHORT_ADS_SEQUENCE:
      if (IS_EFE (FileEntryData)) {
	ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)FileEntryData;
	Length      = ExtendedFileEntry->LengthOfAllocationDescriptors;
	AdsData     = (UINT8 *)(
                           &ExtendedFileEntry->Data +
			   ExtendedFileEntry->LengthOfExtendedAttributes
                           );
      } else if (IS_FE (FileEntryData)) {
	FileEntry = (UDF_FILE_ENTRY *)FileEntryData;
	Length      = FileEntry->LengthOfAllocationDescriptors;
	AdsData     = (UINT8 *)(
                           &FileEntry->Data +
                           FileEntry->LengthOfExtendedAttributes
                           );
      }

      AdLength = sizeof (UDF_SHORT_ALLOCATION_DESCRIPTOR);

ExcludeDeletedFile2:
      for (;;) {
	if (*AdOffset >= Length) {
	  Status = EFI_DEVICE_ERROR;
	  goto Exit;
	}

        ShortAd = (UDF_SHORT_ALLOCATION_DESCRIPTOR *)(
                                                AdsData +
					        *AdOffset
                                                );
        if (!ShortAd->ExtentLength) {
	  continue;
	}

	ExtentLength = ShortAd->ExtentLength;
	if (*FidOffset < ExtentLength) {
	  break;
	}

	*AdOffset    += AdLength;
	*FidOffset   -= ExtentLength;
      }

      Lsn = GetLsnFromShortAd (
	                  GetPdFromLongAd (
                                     PartitionDescs,
				     PartitionDescsNo,
				     ParentIcb
                                     ),
			  ShortAd
                          );
      if (!Lsn) {
	Status = EFI_VOLUME_CORRUPTED;
	goto Exit;
      }

      ExtentLength = ShortAd->ExtentLength;

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

      RemBytes = ExtentLength - *FidOffset;
      if (RemBytes < sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)) {
	do {
	  *AdOffset += AdLength;
	  if (*AdOffset >= Length) {
	    Status = EFI_DEVICE_ERROR;
	    goto FreeExit;
	  }

          ShortAd = (UDF_SHORT_ALLOCATION_DESCRIPTOR *)(
                                                  AdsData +
					          *AdOffset
                                                  );
	} while (!ShortAd->ExtentLength);

        Lsn = GetLsnFromShortAd (
	                    GetPdFromLongAd (
                                       PartitionDescs,
				       PartitionDescsNo,
				       ParentIcb
                                       ),
			    ShortAd
                            );
	if (!Lsn) {
	  Status = EFI_VOLUME_CORRUPTED;
	  goto Exit;
	}

	Buffer = AllocatePool (RemBytes);
	if (!Buffer) {
	  Status = EFI_OUT_OF_RESOURCES;
	  goto FreeExit;
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
	  goto Exit;
	}

	CopyMem (ExtentData, Buffer, RemBytes);
	FreePool (Buffer);

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

	*FidOffset = 0;
      } else {
	RemBytes = 0;
      }

      FileIdentifierDesc = (UDF_FILE_IDENTIFIER_DESCRIPTOR *)((UINT8 *)(
                                                                   ExtentData +
								   *FidOffset
                                                                   )
	                                                     );
      *FidOffset += GetFidDescriptorLength (FileIdentifierDesc) - RemBytes;

      if (IS_FID_DELETED_FILE (FileIdentifierDesc)) {
	FreePool (ExtentData);
	ExtentData = NULL;
	goto ExcludeDeletedFile2;
      }

      Status = EFI_SUCCESS;
      break;
  }

  if (!EFI_ERROR (Status)) {
    Status = DuplicateFileIdentifierDescriptor (
                                           FileIdentifierDesc,
					   FoundFileIdentifierDesc
                                           );
    if (EFI_ERROR (Status)) {
      goto FreeExit;
    }
  }

FreeExit:
  if (ExtentData) {
    FreePool (ExtentData);
  }

Exit:
  return Status;
}

EFI_STATUS
SetFileInfo (
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR   *FileIdentifierDesc,
  IN VOID                             *FileEntryData,
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
    sizeof (EFI_FILE_INFO) + (FileName ? StrLen (FileName) : 0) +
    sizeof (CHAR16);
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

  if (IS_FE (FileEntryData)) {
    FileEntry = (UDF_FILE_ENTRY *)FileEntryData;

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
  } else if (IS_EFE (FileEntryData)) {
    ExtendedFileEntry = (UDF_EXTENDED_FILE_ENTRY *)FileEntryData;

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
