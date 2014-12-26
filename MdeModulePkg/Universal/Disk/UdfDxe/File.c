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

EFI_FILE_PROTOCOL gUdfFileIoOps = {
  EFI_FILE_PROTOCOL_REVISION,
  UdfOpen,
  UdfClose,
  UdfDelete,
  UdfRead,
  UdfWrite,
  UdfGetPosition,
  UdfSetPosition,
  UdfGetInfo,
  UdfSetInfo,
  UdfFlush,
  NULL,
  NULL,
  NULL,
  NULL
};

#define _ROOT_FILE(_PrivData) (_PrivData)->Root
#define _PARENT_FILE(_PrivData) \
  ((_PrivData)->IsRootDirectory ? (_PrivData)->Root : &(_PrivData)->File)
#define _FILE(_PrivData) _PARENT_FILE(_PrivData)

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

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  if (!This || !Root) {
    Status = EFI_INVALID_PARAMETER;
    goto Error_Invalid_Params;
  }

  PrivFsData = PRIVATE_UDF_SIMPLE_FS_DATA_FROM_THIS (This);

  if (!PrivFsData->OpenFiles) {
    //
    // There is no more open files. Read volume information again since it was
    // cleaned up on the last UdfClose() call.
    //
    Status = ReadUdfVolumeInformation (
                               PrivFsData->BlockIo,
                               PrivFsData->DiskIo,
                               &PrivFsData->Volume
                               );
    if (EFI_ERROR (Status)) {
      goto Error_Read_Udf_Volume;
    }
  }

  CleanupFileInformation (&PrivFsData->Root);

  //
  // Find root directory file.
  //
  Status = FindRootDirectory (
                      PrivFsData->BlockIo,
                      PrivFsData->DiskIo,
                      &PrivFsData->Volume,
                      &PrivFsData->Root
                      );
  if (EFI_ERROR (Status)) {
    goto Error_Find_Root_Dir;
  }

  PrivFileData =
    (PRIVATE_UDF_FILE_DATA *)AllocateZeroPool (
                                         sizeof (PRIVATE_UDF_FILE_DATA)
                                         );
  if (!PrivFileData) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Error_Alloc_Priv_File_Data;
  }

  PrivFileData->Signature        = PRIVATE_UDF_FILE_DATA_SIGNATURE;
  PrivFileData->SimpleFs         = This;
  PrivFileData->Root             = &PrivFsData->Root;
  PrivFileData->IsRootDirectory  = TRUE;

  CopyMem (
    (VOID *)&PrivFileData->FileIo,
    (VOID *)&gUdfFileIoOps,
    sizeof (EFI_FILE_PROTOCOL)
    );

  *Root = &PrivFileData->FileIo;

  PrivFsData->OpenFiles++;

  gBS->RestoreTPL (OldTpl);

  return EFI_SUCCESS;

Error_Alloc_Priv_File_Data:
  CleanupFileInformation (&PrivFsData->Root);

Error_Find_Root_Dir:
  CleanupVolumeInformation (&PrivFsData->Volume);

Error_Read_Udf_Volume:
Error_Invalid_Params:
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
  EFI_TPL                     OldTpl;
  EFI_STATUS                  Status;
  PRIVATE_UDF_FILE_DATA       *PrivFileData;
  PRIVATE_UDF_SIMPLE_FS_DATA  *PrivFsData;
  CHAR16                      FilePath[UDF_PATH_LENGTH] = { 0 };
  UDF_FILE_INFO               File;
  PRIVATE_UDF_FILE_DATA       *NewPrivFileData;
  CHAR16                      *TempFileName;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  if (!This || !NewHandle || !FileName) {
    Status = EFI_INVALID_PARAMETER;
    goto Error_Invalid_Params;
  }

  if (OpenMode != EFI_FILE_MODE_READ) {
    Status = EFI_WRITE_PROTECTED;
    goto Error_Invalid_Params;
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
    goto Error_Bad_FileName;
  }

  Status = FindFile (
                 PrivFsData->BlockIo,
                 PrivFsData->DiskIo,
                 &PrivFsData->Volume,
                 FilePath,
                 _ROOT_FILE (PrivFileData),
                 _PARENT_FILE (PrivFileData),
                 &_PARENT_FILE(PrivFileData)->FileIdentifierDesc->Icb,
                 &File
                 );
  if (EFI_ERROR (Status)) {
    goto Error_Find_File;
  }

  NewPrivFileData =
    (PRIVATE_UDF_FILE_DATA *)AllocateZeroPool (
                                         sizeof (PRIVATE_UDF_FILE_DATA)
                                         );
  if (!NewPrivFileData) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Error_Alloc_New_Priv_File_Data;
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
    goto Error_Get_File_Size;
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

Error_Get_File_Size:
  FreePool ((VOID *)NewPrivFileData);

Error_Alloc_New_Priv_File_Data:
  CleanupFileInformation (&File);

Error_Find_File:
Error_Bad_FileName:
Error_Invalid_Params:
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
  return EFI_VOLUME_CORRUPTED;
}

/**
  Close the file handle.

  @param  This Protocol instance pointer.

  @retval EFI_SUCCESS The file was closed.

**/
EFI_STATUS
EFIAPI
UdfClose (
  IN EFI_FILE_PROTOCOL *This
  )
{
  EFI_TPL                     OldTpl;
  EFI_STATUS                  Status;
  PRIVATE_UDF_FILE_DATA       *PrivFileData;
  PRIVATE_UDF_SIMPLE_FS_DATA  *PrivFsData;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  Status = EFI_SUCCESS;

  if (!This) {
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  PrivFsData = PRIVATE_UDF_SIMPLE_FS_DATA_FROM_THIS (PrivFileData->SimpleFs);

  if (!PrivFileData->IsRootDirectory) {
    CleanupFileInformation (&PrivFileData->File);

    if (PrivFileData->ReadDirInfo.DirectoryData) {
      FreePool (PrivFileData->ReadDirInfo.DirectoryData);
    }
  }

  if (!--PrivFsData->OpenFiles) {
    CleanupVolumeInformation (&PrivFsData->Volume);
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
  Get file's current position.

  @param  This      Protocol instance pointer.
  @param  Position  Byte position from the start of the file.

  @retval EFI_SUCCESS      Position was updated.
  @retval EFI_UNSUPPORTED  Seek request for directories is not valid.

**/
EFI_STATUS
EFIAPI
UdfGetPosition (
  IN   EFI_FILE_PROTOCOL  *This,
  OUT  UINT64             *Position
  )
{
  return EFI_UNSUPPORTED;
}

/**
  Set file's current position.

  @param  This      Protocol instance pointer.
  @param  Position  Byte position from the start of the file.

  @retval EFI_SUCCESS      Position was updated.
  @retval EFI_UNSUPPORTED  Seek request for non-zero is not valid on open.

**/
EFI_STATUS
EFIAPI
UdfSetPosition (
  IN EFI_FILE_PROTOCOL  *This,
  IN UINT64             Position
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
  IN      EFI_FILE_PROTOCOL  *This,
  IN      EFI_GUID           *InformationType,
  IN OUT  UINTN              *BufferSize,
  OUT     VOID               *Buffer
  )
{
  EFI_STATUS                  Status;
  PRIVATE_UDF_FILE_DATA       *PrivFileData;
  PRIVATE_UDF_SIMPLE_FS_DATA  *PrivFsData;
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

  Status = EFI_UNSUPPORTED;

  if (CompareGuid (InformationType, &gEfiFileInfoGuid)) {
    Status = SetFileInfo (
                    _FILE (PrivFileData),
                    PrivFileData->FileSize,
                    PrivFileData->FileName,
                    BufferSize,
                    Buffer
                    );
  } else if (CompareGuid (InformationType, &gEfiFileSystemInfoGuid)) {
    String = VolumeLabel;

    FileSetDesc = PrivFsData->Volume.FileSetDescs[0];

    OstaCompressed = &FileSetDesc->LogicalVolumeIdentifier[0];

    CompressionId = OstaCompressed[0];
    if (!IS_VALID_COMPRESSION_ID (CompressionId)) {
      return EFI_VOLUME_CORRUPTED;
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
      // Unlike FID Identifiers, Logical Volume Identifier is stored in a
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
      return EFI_BUFFER_TOO_SMALL;
    }

    FileSystemInfo = (EFI_FILE_SYSTEM_INFO *)Buffer;
    StrCpy (FileSystemInfo->VolumeLabel, VolumeLabel);
    Status = GetVolumeSize (
                     PrivFsData->BlockIo,
                     PrivFsData->DiskIo,
                     &PrivFsData->Volume,
                     &VolumeSize,
                     &FreeSpaceSize
                     );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    FileSystemInfo->Size        = FileSystemInfoLength;
    FileSystemInfo->ReadOnly    = TRUE;
    FileSystemInfo->BlockSize   = LV_BLOCK_SIZE (
                                          &PrivFsData->Volume,
                                          UDF_DEFAULT_LV_NUM
                                          );
    FileSystemInfo->VolumeSize  = VolumeSize;
    FileSystemInfo->FreeSpace   = FreeSpaceSize;

    *BufferSize = FileSystemInfoLength;
    Status = EFI_SUCCESS;
  }

  return Status;
}

/**
  Set information about a file.

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
  IN EFI_FILE_PROTOCOL *This
  )
{
  return EFI_WRITE_PROTECTED;
}
