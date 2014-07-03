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

//
// OSTA CS0 Character Set
//
// The below byte values represent the following ASCII string:
//        "OSTA Compressed Unicode"
//
// Note also that the byte values are stored in litte-endian.
//
UINT8 gOstaCs0CharSetInfo[] = {
  0x65, 0x64, 0x6F, 0x63, 0x69, 0x6E, 0x55, 0x20, 0x64, 0x65, 0x73, 0x73, 0x65,
  0x72, 0x70, 0x6D, 0x6F, 0x43, 0x20, 0x41, 0x54, 0x53, 0x4F,
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
  UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER   *AnchorPoint;
  UDF_PARTITION_DESCRIPTOR               *PartitionDesc;
  UDF_LOGICAL_VOLUME_DESCRIPTOR          *LogicalVolDesc;
  UDF_FILE_SET_DESCRIPTOR                *FileSetDesc;
  UDF_FILE_ENTRY                         *RootFileEntry;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *RootFileIdentifierDesc;
  PRIVATE_UDF_FILE_DATA                  *PrivFileData;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  PrivFsData = PRIVATE_UDF_SIMPLE_FS_DATA_FROM_THIS (This);

  Status = FindRootDirectory (
                          PrivFsData->BlockIo,
                          PrivFsData->DiskIo,
			  &PrivFsData->BlockSize,
			  &AnchorPoint,
			  &PartitionDesc,
			  &LogicalVolDesc,
			  &FileSetDesc,
			  &RootFileEntry,
			  &RootFileIdentifierDesc
                          );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  PrivFileData = AllocatePool (sizeof (PRIVATE_UDF_FILE_DATA));
  if (!PrivFileData) {
    Status = EFI_OUT_OF_RESOURCES;
    goto FreeExit;
  }

  PrivFileData->UdfFileSystemData.AnchorPoint            = AnchorPoint;
  PrivFileData->UdfFileSystemData.PartitionDesc          = PartitionDesc;
  PrivFileData->UdfFileSystemData.LogicalVolDesc         = LogicalVolDesc;
  PrivFileData->UdfFileSystemData.FileSetDesc            = FileSetDesc;
  PrivFileData->UdfFileSystemData.FileEntry              = RootFileEntry;
  PrivFileData->UdfFileSystemData.FileIdentifierDesc = RootFileIdentifierDesc;

  PrivFileData->Signature   = PRIVATE_UDF_FILE_DATA_SIGNATURE;
  PrivFileData->SimpleFs    = This;
  PrivFileData->BlockIo     = PrivFsData->BlockIo;
  PrivFileData->DiskIo      = PrivFsData->DiskIo;
  PrivFileData->BlockSize   = PrivFsData->BlockSize;

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

  PrivFileData->FilePosition = 0;

  *Root = &PrivFileData->FileIo;

  gBS->RestoreTPL (OldTpl);

  return Status;

FreeExit:
  FreePool ((VOID *)AnchorPoint);
  FreePool ((VOID *)PartitionDesc);
  FreePool ((VOID *)LogicalVolDesc);
  FreePool ((VOID *)FileSetDesc);
  FreePool ((VOID *)RootFileEntry);
  FreePool ((VOID *)RootFileIdentifierDesc);

Exit:
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
  UDF_PARTITION_DESCRIPTOR               *PartitionDesc;
  UDF_FILE_ENTRY                         *FileEntry;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *FileIdentifierDesc;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *PrevFileIdentifierDesc;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *NextFileIdentifierDesc;
  CHAR16                                 *Str;
  CHAR16                                 *StrAux;
  UINT64                                 Offset;
  CHAR16                                 *NextFileName;
  EFI_BLOCK_IO_PROTOCOL                  *BlockIo;
  EFI_DISK_IO_PROTOCOL                   *DiskIo;
  UINT32                                 BlockSize;
  BOOLEAN                                Found;
  PRIVATE_UDF_FILE_DATA                  *NewPrivFileData;
  UDF_LONG_ALLOCATION_DESCRIPTOR         *LongAd;
  UINT32                                 Lsn;
  VOID                                   *Buffer;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  if ((!This) || (!NewHandle) || (!FileName)) {
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  PartitionDesc            = PrivFileData->UdfFileSystemData.PartitionDesc;
  FileIdentifierDesc       = PrivFileData->UdfFileSystemData.FileIdentifierDesc;

  Print (L"UdfOpen: FileName \'%s\'\n", FileName);

  Str = AllocateZeroPool ((StrLen (FileName) + 1) * sizeof (CHAR16));
  if (!Str) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  BlockIo        = PrivFileData->BlockIo;
  DiskIo         = PrivFileData->DiskIo;
  BlockSize      = PrivFileData->BlockSize;
  Found          = FALSE;
  Buffer         = NULL;
  NextFileName   = NULL;

  for (;;) {
NextLookup:
    PrevFileIdentifierDesc = NULL;

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

    //
    // Discard any trailing whitespaces in the beginning
    //
    for ( ; (*FileName) && (*FileName == ' '); FileName++)
      ;

    Offset = 0;

    while ((*FileName) && (*FileName != '\\')) {
      CopyMem (
	(VOID *)((UINT8 *)Str + Offset),
	(VOID *)FileName,
	sizeof (CHAR16)
	);

      Offset += sizeof (CHAR16);
      FileName++;
    }

    if (*FileName == '\\') {
      FileName++;
    }

    *((UINT8 *)Str + Offset) = '\0';

    StrAux = Str;

    //
    // Discard any trailing whitespaces in the beginning
    //
    for ( ; (*StrAux) && (*StrAux == ' '); StrAux++)
      ;

    if (!*StrAux) {
      continue;
    }

    Found = FALSE;

#ifdef UDF_DEBUG
    Print (
      L"UdfOpen: Start looking up \'%s\' (Length: %d)\n",
      StrAux,
      StrLen (StrAux)
      );
#endif

    if (StrnCmp (StrAux, L"..", 2) == 0) {
      //
      // Make sure we're not going to look up Parent FID from a root
      // directory or even if the current FID is not a directory(!)
      //
      if ((IS_FID_PARENT_FILE (FileIdentifierDesc)) ||
	  (!IS_FID_DIRECTORY_FILE (FileIdentifierDesc))) {
	break;
      }

      //
      // Ok, we got ".." from the filename. Find Parent FID of the current FID.
      //
      LongAd = &FileIdentifierDesc->Icb;

      //
      // Point to the Parent FID
      //
      Lsn = PartitionDesc->PartitionStartingLocation +
               LongAd->ExtentLocation.LogicalBlockNumber + 1;

      //
      // Calculate offset of the Parent FID
      //
      Offset = Lsn * BlockSize;

      if (!Buffer) {
	Buffer = AllocatePool (BlockSize);
	if (!Buffer) {
	  Status = EFI_OUT_OF_RESOURCES;
	  goto FreeExit;
	}
      }

      //
      // Read FID
      //
      Status = DiskIo->ReadDisk (
                           DiskIo,
			   BlockIo->Media->MediaId,
			   Offset,
			   BlockSize,
			   Buffer
                           );
      if (EFI_ERROR (Status)) {
	FreePool (Buffer);
	goto FreeExit;
      }

      FileIdentifierDesc = (UDF_FILE_IDENTIFIER_DESCRIPTOR *)Buffer;

      if (!IS_FID (FileIdentifierDesc)) {
	FreePool (Buffer);
	Status = EFI_VOLUME_CORRUPTED;
	goto FreeExit;
      }

      Found = TRUE;
      continue;
    }

    //
    // Start lookup
    //
    for (;;) {
      Status = ReadDirectory (
	    BlockIo,
	    DiskIo,
	    BlockSize,
	    PartitionDesc,
	    FileIdentifierDesc,
	    PrevFileIdentifierDesc,
	    &NextFileIdentifierDesc
	    );
      if (EFI_ERROR (Status)) {
	goto FreeExit;
      }

      if (!NextFileIdentifierDesc) {
	Status = EFI_NOT_FOUND;
	goto FreeExit;
      }

      //
      // Ignore Parent files
      //
      if (IS_FID_PARENT_FILE (NextFileIdentifierDesc)) {
	goto ReadNextFid;
      }

      Status = FileIdentifierDescToFileName (
	                  NextFileIdentifierDesc,
			  &NextFileName
	                  );
      if (EFI_ERROR (Status)) {
	goto FreeExit;
      }

      Print (
	L"UdfOpen: ===> %s (Length: %d)\n",
	NextFileName,
	StrLen (NextFileName)
	);

      //
      // Check whether FID's File Identifier contains the expected filename
      //
      if (StrnCmp (NextFileName, StrAux, StrLen (StrAux)) == 0) {
	Print (L"UdfOpen: Found file \'%s\'\n", Str);

	FileIdentifierDesc = NextFileIdentifierDesc;

	if (PrevFileIdentifierDesc) {
	  FreePool ((VOID *) PrevFileIdentifierDesc);
	}

	Found = TRUE;
	goto NextLookup;
      }

ReadNextFid:
      if (PrevFileIdentifierDesc) {
	FreePool ((VOID *) PrevFileIdentifierDesc);
      }

      PrevFileIdentifierDesc = NextFileIdentifierDesc;
    }
  }

  if (Found) {
    //
    // Found file
    //
    NewPrivFileData = AllocatePool (sizeof (PRIVATE_UDF_FILE_DATA));
    if (!NewPrivFileData) {
      Status = EFI_OUT_OF_RESOURCES;
      goto FreeExit;
    }

    CopyMem (
      (VOID *)NewPrivFileData,
      (VOID *)PrivFileData,
      sizeof (PRIVATE_UDF_FILE_DATA)
      );

    //
    // Find FE of the FID
    //
    FileEntry = AllocatePool (BlockSize);
    if (!FileEntry) {
      Status = EFI_OUT_OF_RESOURCES;
      goto FreeExit;
    }

    LongAd = &FileIdentifierDesc->Icb;

    Lsn = PartitionDesc->PartitionStartingLocation +
           LongAd->ExtentLocation.LogicalBlockNumber;

    Offset = Lsn * BlockSize;

    //
    // Read FE
    //
    Status = DiskIo->ReadDisk (
                         DiskIo,
			 BlockIo->Media->MediaId,
			 Offset,
			 BlockSize,
			 (VOID *)FileEntry
                         );
    if (EFI_ERROR (Status)) {
      FreePool (FileEntry);
      goto FreeExit;
    }

    //
    // TODO: check if ICB Tag's flags field contain all valid bits set
    //
    if (!IS_FE (FileEntry)) {
      FreePool (FileEntry);
      Status = EFI_VOLUME_CORRUPTED;
      goto FreeExit;
    }

    NewPrivFileData->UdfFileSystemData.FileEntry          = FileEntry;
    NewPrivFileData->UdfFileSystemData.FileIdentifierDesc = FileIdentifierDesc;

    NewPrivFileData->FilePosition = 0;

    *NewHandle = &NewPrivFileData->FileIo;

    Status = EFI_SUCCESS;
  } else {
    Status = EFI_NOT_FOUND;
  }

FreeExit:
  FreePool ((VOID *) Str);

  if (PrevFileIdentifierDesc) {
    FreePool ((VOID *) PrevFileIdentifierDesc);
  }

Exit:
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
  UDF_PARTITION_DESCRIPTOR               *PartitionDesc;
  UDF_FILE_ENTRY                         *FileEntry;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *FileIdentifierDesc;
  EFI_BLOCK_IO_PROTOCOL                  *BlockIo;
  EFI_DISK_IO_PROTOCOL                   *DiskIo;
  UINT32                                 BlockSize;
  UINT64                                 FilePosition;
  INT64                                  ExtStartOffset;
  UINT32                                 ExtLen;
  UINT32                                 ShortAdsNo;
  UDF_SHORT_ALLOCATION_DESCRIPTOR        *ShortAd;
  UINT64                                 BufferOffset;
  UINTN                                  BytesLeft;
  UINTN                                  DirsCount;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *PrevFileIdentifierDesc;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *NextFileIdentifierDesc;
  UINTN                                  FileInfoLength;
  EFI_FILE_INFO                          *FileInfo;
  CHAR16                                 *FileName;
  UINT64                                 FileSize;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  if ((!This) || (!Buffer)) {
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  PartitionDesc        = PrivFileData->UdfFileSystemData.PartitionDesc;
  FileEntry            = PrivFileData->UdfFileSystemData.FileEntry;
  FileIdentifierDesc   = PrivFileData->UdfFileSystemData.FileIdentifierDesc;

  BlockIo                  = PrivFileData->BlockIo;
  DiskIo                   = PrivFileData->DiskIo;
  BlockSize                = PrivFileData->BlockSize;
  FileName                 = NULL;
  PrevFileIdentifierDesc   = NULL;
  NextFileIdentifierDesc   = NULL;

  if (IS_FID_NORMAL_FILE (FileIdentifierDesc)) {
    //
    // Check if the current position is beyond the EOF
    //
    if (PrivFileData->FilePosition >= FileEntry->InformationLength) {
      Status = EFI_DEVICE_ERROR;
      goto Exit;
    }

    //
    // File Type should be 5 for a standard byte addressable file
    //
    if (FileEntry->IcbTag.FileType != 5) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
    }

    //
    // The Type of Allocation Descriptor (bit 0-2) in Flags field of ICB Tag
    // shall be set to 0 which means that Short Allocation Descriptors are used.
    //
    if (FileEntry->IcbTag.Flags & 0x07) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
    }

    //
    // Strategy Type of 4 is the only supported
    //
    if (FileEntry->IcbTag.StrategyType != 4) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
    }

    //
    // OK, now read file's extents to find recorded data.
    //
    ShortAdsNo = FileEntry->LengthOfAllocationDescriptors /
                 sizeof (UDF_SHORT_ALLOCATION_DESCRIPTOR);

    //
    // NOTE: The total length of a File Entry shall not exceed the size of one
    // logical block, so it's OK.
    //
    ShortAd = (UDF_SHORT_ALLOCATION_DESCRIPTOR *)(
                            (UINT8 *)&FileEntry->Data +
			    FileEntry->LengthOfExtendedAttributes
                            );

    ExtStartOffset   = 0;
    ExtLen           = 0;

    if (!PrivFileData->FilePosition) {
      //
      // OK. Start reading file from its first extent.
      //
      goto ReadFile;
    }

    //
    // Find extent which corresponds to the current file's position
    //
    FilePosition     = 0;

    do {
      if (FilePosition + ShortAd->ExtentLength == PrivFileData->FilePosition) {
	break;
      }

      if (FilePosition + ShortAd->ExtentLength > PrivFileData->FilePosition) {
	ExtStartOffset =
	  ShortAd->ExtentLength - ((FilePosition +
				    ShortAd->ExtentLength) -
				   PrivFileData->FilePosition);
	if (ExtStartOffset < 0) {
	  ExtStartOffset = -(ExtStartOffset);
	}

	ExtLen = ExtStartOffset;
	break;
      }

      FilePosition += ShortAd->ExtentLength;
    } while (--ShortAdsNo);

    if (!ShortAdsNo) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
    }

ReadFile:
    //
    // Found file position through the extents. Now, start reading file.
    //
    BufferOffset   = 0;
    BytesLeft      = *BufferSize;

    while ((ShortAdsNo--) && (BytesLeft)) {
      if (ShortAd->ExtentLength > BytesLeft) {
	//
	// Truncate it in case of reading beyond the EOF
	//
	BytesLeft = ShortAd->ExtentLength;
      }

      Status = DiskIo->ReadDisk (
	                   DiskIo,
			   BlockIo->Media->MediaId,
			   ExtStartOffset +
			   ((PartitionDesc->PartitionStartingLocation +
			     ShortAd->ExtentPosition) * BlockSize),
			   ShortAd->ExtentLength - ExtLen,
			   (VOID *)((UINT8 *)Buffer + BufferOffset)
	                   );
      if (EFI_ERROR (Status)) {
	Status = EFI_DEVICE_ERROR;
	goto Exit;
      }

      BytesLeft                    -= ShortAd->ExtentLength - ExtLen;
      BufferOffset                 += ShortAd->ExtentLength - ExtLen;
      PrivFileData->FilePosition   += ShortAd->ExtentLength - ExtLen;
      ExtStartOffset               = 0;
      ExtLen                       = 0;
    }

    *BufferSize = BufferOffset;
    Status = EFI_SUCCESS;
  } else if (IS_FID_DIRECTORY_FILE (FileIdentifierDesc)) {
    DirsCount = PrivFileData->FilePosition + 1;

    //
    // Find current directory entry
    //
    for (;;) {
      Status = ReadDirectory (
	    BlockIo,
	    DiskIo,
	    BlockSize,
	    PartitionDesc,
	    FileIdentifierDesc,
	    PrevFileIdentifierDesc,
	    &NextFileIdentifierDesc
	    );
      if (EFI_ERROR (Status)) {
	goto Exit;
      }

      if (!NextFileIdentifierDesc) {
	break;
      }

      //
      // Ignore Parent FID
      //
      if (IS_FID_PARENT_FILE (NextFileIdentifierDesc)) {
	goto ReadNextFid;
      }

      if (!--DirsCount) {
	break;
      }

ReadNextFid:
      if (PrevFileIdentifierDesc) {
	FreePool ((VOID *)PrevFileIdentifierDesc);
      }

      PrevFileIdentifierDesc = NextFileIdentifierDesc;
    }

    if (DirsCount) {
      //
      // Directory entry not found
      //
      *BufferSize = 0;
      Status = EFI_SUCCESS;
      goto Exit;
    }

    //
    // Found current directory entry. Set up EFI_FILE_INFO structure.
    //
    FileIdentifierDesc = NextFileIdentifierDesc;

    Status = FileIdentifierDescToFileName (FileIdentifierDesc, &FileName);
    if (EFI_ERROR (Status)) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
    }

    //
    // Check if BufferSize is too small to read the current directory entry
    //
    FileInfoLength = sizeof (EFI_FILE_INFO) + StrLen (FileName) + 1;
    if (*BufferSize < FileInfoLength) {
      *BufferSize = FileInfoLength;
      Status = EFI_BUFFER_TOO_SMALL;
      goto Exit;
    }

    FileInfo = (EFI_FILE_INFO *) Buffer;

    FileInfo->Size = FileInfoLength;

    FileInfo->Attribute |= EFI_FILE_READ_ONLY;

    if (IS_FID_DIRECTORY_FILE (FileIdentifierDesc)) {
      Status = GetDirectorySize (
                             BlockIo,
			     DiskIo,
			     BlockSize,
			     PartitionDesc,
			     FileIdentifierDesc,
			     &FileSize
			     );
      if (EFI_ERROR (Status)) {
	goto Exit;
      }

      FileInfo->Attribute |= EFI_FILE_DIRECTORY;
    } else if (IS_FID_NORMAL_FILE (FileIdentifierDesc)) {
      FileSize = FileEntry->InformationLength;
      FileInfo->Attribute |= EFI_FILE_ARCHIVE;
    }

    if (IS_FID_HIDDEN_FILE (FileIdentifierDesc)) {
      FileInfo->Attribute |= EFI_FILE_HIDDEN;
    }

    //
    // Check if file has System bit set (bit 10)
    //
    if (FileEntry->IcbTag.Flags & (1 << 10)) {
      FileInfo->Attribute |= EFI_FILE_SYSTEM;
    }

    FileInfo->FileSize = FileSize;

    FileInfo->PhysicalSize = FileEntry->InformationLength;

    FileInfo->CreateTime.Year         = FileEntry->AccessTime.Year;
    FileInfo->CreateTime.Month        = FileEntry->AccessTime.Month;
    FileInfo->CreateTime.Day          = FileEntry->AccessTime.Day;
    FileInfo->CreateTime.Hour         = FileEntry->AccessTime.Hour;
    FileInfo->CreateTime.Minute       = FileEntry->AccessTime.Second;
    FileInfo->CreateTime.Second       = FileEntry->AccessTime.Second;
    FileInfo->CreateTime.Nanosecond   =
         FileEntry->AccessTime.HundredsOfMicroseconds;

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
      (VOID *)&FileInfo->CreateTime,
      sizeof (EFI_TIME)
      );

    //
    // Since we're accessing a DVD read-only disc - the Last Access Time
    // field, obviously, should be the same as Create Time.
    //
    CopyMem (
      (VOID *)&FileInfo->LastAccessTime,
      (VOID *)&FileInfo->CreateTime,
      sizeof (EFI_TIME)
      );

    StrCpy (FileInfo->FileName, FileName);

    //
    // Update the current position to the next directory entry
    //
    PrivFileData->FilePosition++;

    *BufferSize = FileInfoLength;
    Status = EFI_SUCCESS;
  } else if (IS_FID_DELETED_FILE (FileIdentifierDesc)) {
    Status = EFI_DEVICE_ERROR;
  } else {
    Status = EFI_VOLUME_CORRUPTED;
  }

Exit:
  if (FileName) {
    FreePool ((VOID *)FileName);
  }

  if (PrevFileIdentifierDesc) {
    FreePool ((VOID *)PrevFileIdentifierDesc);
  }

  if (NextFileIdentifierDesc) {
    FreePool ((VOID *)NextFileIdentifierDesc);
  }

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
  IN EFI_FILE_PROTOCOL    *This
  )
{
  return EFI_SUCCESS;
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
  return EFI_SUCCESS;
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
	   PrivFileData->UdfFileSystemData.FileIdentifierDesc
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

  Status = EFI_SUCCESS;

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  FileIdentifierDesc = PrivFileData->UdfFileSystemData.FileIdentifierDesc;
  FileEntry          = PrivFileData->UdfFileSystemData.FileEntry;

  if (IS_FID_DIRECTORY_FILE (FileIdentifierDesc)) {
    //
    // If the file handle is a directory, the _only_ position that may be set is
    // zero. This has no effect of starting the read proccess of the directory
    // entries over.
    //
    if (Position != 0) {
      Status = EFI_UNSUPPORTED;
    } else {
      PrivFileData->FilePosition = Position;
    }
  } else if (IS_FID_NORMAL_FILE (FileIdentifierDesc)) {
    //
    // Seeking to position 0xFFFFFFFFFFFFFFFF causes the current position to be
    // set to the EOF.
    //
    if (Position == 0xFFFFFFFFFFFFFFFF) {
      PrivFileData->FilePosition = FileEntry->InformationLength - 1;
    } else {
      PrivFileData->FilePosition = Position;
    }
  } else {
    Status = EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
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
  EFI_FILE_INFO                          *FileInfo;
  CHAR16                                 *FileName;
  UDF_PARTITION_DESCRIPTOR               *PartitionDesc;
  UDF_LOGICAL_VOLUME_DESCRIPTOR          *LogicalVolDesc;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *FileIdentifierDesc;
  EFI_BLOCK_IO_PROTOCOL                  *BlockIo;
  EFI_DISK_IO_PROTOCOL                   *DiskIo;
  UINT32                                 BlockSize;
  UINT64                                 FileSize;
  UINTN                                  FileInfoLength;
  EFI_FILE_SYSTEM_INFO                   *FileSystemInfo;
  UDF_CHAR_SPEC                          *CharSpec;
  UINTN                                  FileSystemInfoLength;
  CHAR16                                 *String;
  CHAR16                                 *CharP;
  UINTN                                  Index;

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  PartitionDesc            = PrivFileData->UdfFileSystemData.PartitionDesc;
  LogicalVolDesc           = PrivFileData->UdfFileSystemData.LogicalVolDesc;
  FileEntry                = PrivFileData->UdfFileSystemData.FileEntry;
  FileIdentifierDesc       = PrivFileData->UdfFileSystemData.FileIdentifierDesc;

  BlockIo     = PrivFileData->BlockIo;
  DiskIo      = PrivFileData->DiskIo;
  BlockSize   = PrivFileData->BlockSize;
  FileName    = NULL;

  if (CompareGuid (InformationType, &gEfiFileInfoGuid)) {
    Status = FileIdentifierDescToFileName (FileIdentifierDesc, &FileName);
    if (EFI_ERROR (Status)) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
    }

    //
    // Check if BufferSize is too small to read the current directory entry
    //
    FileInfoLength = sizeof (EFI_FILE_INFO) + StrLen (FileName) + 1;
    if (*BufferSize < FileInfoLength) {
      *BufferSize = FileInfoLength;
      Status = EFI_BUFFER_TOO_SMALL;
      goto Exit;
    }

    FileInfo = (EFI_FILE_INFO *) Buffer;

    FileInfo->Size = FileInfoLength;

    FileInfo->Attribute |= EFI_FILE_READ_ONLY;

    if (IS_FID_DIRECTORY_FILE (FileIdentifierDesc)) {
      Status = GetDirectorySize (
                             BlockIo,
			     DiskIo,
			     BlockSize,
			     PartitionDesc,
			     FileIdentifierDesc,
			     &FileSize
			     );
      if (EFI_ERROR (Status)) {
	goto Exit;
      }

      FileInfo->Attribute |= EFI_FILE_DIRECTORY;
    } else if (IS_FID_NORMAL_FILE (FileIdentifierDesc)) {
      FileSize = FileEntry->InformationLength;
      FileInfo->Attribute |= EFI_FILE_ARCHIVE;
    }

    if (IS_FID_HIDDEN_FILE (FileIdentifierDesc)) {
      FileInfo->Attribute |= EFI_FILE_HIDDEN;
    }

    //
    // Check if file has System bit set (bit 10)
    //
    if (FileEntry->IcbTag.Flags & (1 << 10)) {
      FileInfo->Attribute |= EFI_FILE_SYSTEM;
    }

    FileInfo->FileSize = FileSize;

    FileInfo->PhysicalSize = FileEntry->InformationLength;

    FileInfo->CreateTime.Year         = FileEntry->AccessTime.Year;
    FileInfo->CreateTime.Month        = FileEntry->AccessTime.Month;
    FileInfo->CreateTime.Day          = FileEntry->AccessTime.Day;
    FileInfo->CreateTime.Hour         = FileEntry->AccessTime.Hour;
    FileInfo->CreateTime.Minute       = FileEntry->AccessTime.Second;
    FileInfo->CreateTime.Second       = FileEntry->AccessTime.Second;
    FileInfo->CreateTime.Nanosecond   =
         FileEntry->AccessTime.HundredsOfMicroseconds;

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
      (VOID *)&FileInfo->CreateTime,
      sizeof (EFI_TIME)
      );

    //
    // Since we're accessing a DVD read-only disc - the Last Access Time
    // field, obviously, should be the same as Create Time.
    //
    CopyMem (
      (VOID *)&FileInfo->LastAccessTime,
      (VOID *)&FileInfo->CreateTime,
      sizeof (EFI_TIME)
      );

    StrCpy (FileInfo->FileName, FileName);

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

    //
    // The encoding character set should be in "OSTA CS0". See OSTA UDF 2.1.2.
    //
    CharSpec = &LogicalVolDesc->DescriptorCharacterSet;

    if ((CharSpec->CharacterSetType != 0) ||
	(!CompareMem (
	     (VOID *)&CharSpec->CharacterSetInfo,
	     (VOID *)&gOstaCs0CharSetInfo,
	     sizeof (gOstaCs0CharSetInfo)
	     )
	)
      )
    {
      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
    }

    String = (CHAR16 *)&FileSystemInfo->VolumeLabel[0];

    for (Index = 0; Index < 128; Index += 2) {
      CharP = (CHAR16 *)&LogicalVolDesc->LogicalVolumeIdentifier[Index];
      if (!*CharP) {
	break;
      }

      *String++ = *CharP;
    }

    *String = '\0';

    FileSystemInfo->Size        = FileSystemInfoLength;
    FileSystemInfo->ReadOnly    = (BOOLEAN)(PartitionDesc->AccessType == 1);
    FileSystemInfo->VolumeSize  =
         (PartitionDesc->PartitionStartingLocation +
	  PartitionDesc->PartitionLength) * BlockSize;
    FileSystemInfo->FreeSpace   = 0;
    FileSystemInfo->BlockSize   = BlockSize;

    *BufferSize = FileSystemInfoLength;
    Status = EFI_SUCCESS;
  } else {
    Status = EFI_UNSUPPORTED;
  }

Exit:
  if (FileName) {
    FreePool ((VOID *)FileName);
  }

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
  return EFI_ACCESS_DENIED;
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
  return EFI_UNSUPPORTED;
}

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
  UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER       *AnchorPoint;
  UDF_DESCRIPTOR_TAG                         *DescriptorTag;

  *Buffer = AllocatePool (BlockSize);
  if (!*Buffer) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       FIRST_ANCHOR_POINT_LSN * BlockSize,
                       BlockSize,
                       *Buffer
                       );
  if (EFI_ERROR (Status)) {
    goto FreeExit;
  }

  Print (
    L"UdfDriverStart: Get AVDP\n"
    );

  AnchorPoint = (UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER *)*Buffer;

  DescriptorTag = &AnchorPoint->DescriptorTag;

  Print (
    L"UdfDriverStart: [AVDP] Tag Identifier: %d (0x%08x)\n",
    DescriptorTag->TagIdentifier,
    DescriptorTag->TagIdentifier
    );

  //
  // TODO: In case of failure, look for the other AVDPs at N or N - 256
  //
  if (!IS_AVDP (AnchorPoint)) {
    Print (
      L"UdfDriverStart: [AVDP] Invalid Tag Identifier number\n"
      );

    Status = EFI_VOLUME_CORRUPTED;
    goto FreeExit;
  }

  return Status;

FreeExit:
  FreePool (*Buffer);
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

    if (IS_PD (Buffer)) {
      Print (
	L"UdfDriverStart: Get PD\n"
	);

      CopyMem (*Buffer0, Buffer, BlockSize);
    } else if (IS_LVD (Buffer)) {
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

  FreePool (Buffer);

  return Status;

FreeExit2:
  FreePool (Buffer);

FreeExit1:
  FreePool (*Buffer1);
  *Buffer1 = NULL;

FreeExit0:
  FreePool (*Buffer0);
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
  UINT32                                     Lsn;
  UDF_FILE_SET_DESCRIPTOR                    *FileSetDesc;
  UDF_DESCRIPTOR_TAG                         *DescriptorTag;

  *Buffer = AllocatePool (BlockSize);
  if (!*Buffer) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  LongAd = &LogicalVolDesc->LogicalVolumeContentsUse;

  Lsn = PartitionDesc->PartitionStartingLocation +
           LongAd->ExtentLocation.LogicalBlockNumber;

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       Lsn * BlockSize,
                       BlockSize,
                       *Buffer
                       );
  if (EFI_ERROR (Status)) {
    goto FreeExit;
  }

  Print (L"UdfDriverStart: Get FSD\n");

  FileSetDesc = (UDF_FILE_SET_DESCRIPTOR *)*Buffer;

  DescriptorTag = &FileSetDesc->DescriptorTag;

  Print (
    L"UdfDriverStart: [FSD] Tag Identifier:                      %d (0x%08x)\n",
    DescriptorTag->TagIdentifier,
    DescriptorTag->TagIdentifier
    );

  if (!IS_FSD (FileSetDesc)) {
      Print (
	L"UdfDriverStart: [FSD] Invalid Tag Identifier number\n"
	);
      Status = EFI_VOLUME_CORRUPTED;
      goto FreeExit;
  }

  return Status;

FreeExit:
  FreePool (*Buffer);
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
  UINT32                                     Lsn;
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

  Lsn = PartitionDesc->PartitionStartingLocation +
           LongAd->ExtentLocation.LogicalBlockNumber;

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       Lsn * BlockSize,
                       BlockSize,
                       *Buffer
                       );
  if (EFI_ERROR (Status)) {
    goto FreeExit;
  }

  Print (L"UdfDriverStart: Get File Entry (Root Directory)\n");

  FileEntry = (UDF_FILE_ENTRY *)*Buffer;

  DescriptorTag = &FileEntry->DescriptorTag;

  Print (
    L"UdfDriverStart: [ROOT] Tag Identifier:               %d (0x%08x)\n",
    DescriptorTag->TagIdentifier,
    DescriptorTag->TagIdentifier
    );

  if (!IS_FE (FileEntry)) {
      Print (
	L"UdfDriverStart: [ROOT] Invalid Tag Identifier number\n"
	);

      Status = EFI_VOLUME_CORRUPTED;
      goto FreeExit;
  }

  //
  // Root Directory cannot be file, obivously. Check its file type.
  //
  if (!IS_FE_DIRECTORY (FileEntry)) {
    Print (L"UdfDriverStart: [ROOT] Root Directory is NOT a directory!\n");

    Status = EFI_VOLUME_CORRUPTED;
    goto FreeExit;
  }

  Print (
    L"UdfDriverStart: [ROOT] Record Format:                %d (0x%08x)\n",
    FileEntry->RecordFormat,
    FileEntry->RecordFormat
    );

  Print (
    L"UdfDriverStart: [ROOT] Record Length:                %d (0x%08x)\n",
    FileEntry->RecordFormat,
    FileEntry->RecordFormat
    );

  Print (
    L"UdfDriverStart: [ROOT] Logical Blocks Recorded:      %d (0x%08x)\n",
    FileEntry->LogicalBlocksRecorded,
    FileEntry->LogicalBlocksRecorded
    );

  return Status;

FreeExit:
  FreePool (*Buffer);
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
  UINT32                                     Lsn;
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
  Lsn = PartitionDesc->PartitionStartingLocation +
           LongAd->ExtentLocation.LogicalBlockNumber + 1;

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       Lsn * BlockSize,
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

  if (!IS_FID (FileIdentifierDesc)) {
      Print (
	L"UdfDriverStart: [ROOT-FID] Invalid tag identifier number\n"
	);

      Status = EFI_VOLUME_CORRUPTED;
      goto FreeExit;
  }

  LongAd = &FileIdentifierDesc->Icb;

  Print (
    L"UdfDriverStart: [ROOT-FID] LBN of FE:                %d (0x%08x)\n",
    LongAd->ExtentLocation.LogicalBlockNumber,
    LongAd->ExtentLocation.LogicalBlockNumber
    );

  Print (
    L"UdfDriverStart: [ROOT-FID] Extent length of FE:      %d (0x%08x)\n",
    LongAd->ExtentLength,
    LongAd->ExtentLength
    );

  return Status;

FreeExit:
  FreePool (*Buffer);
  *Buffer = NULL;

Exit:
  return Status;
}

EFI_STATUS
EFIAPI
FindRootDirectory (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN OUT UINT32                              *BlockSize,
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
					  *BlockSize,
					  (VOID **)AnchorPoint
                                          );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = StartMainVolumeDescriptorSequence (
                                          BlockIo,
					  DiskIo,
					  *BlockSize,
					  *AnchorPoint,
					  (VOID **)PartitionDesc,
					  (VOID **)LogicalVolDesc
                                          );
  if (EFI_ERROR (Status)) {
    goto FreeExit0;
  }

  *BlockSize = (*LogicalVolDesc)->LogicalBlockSize;

  Status = FindFileSetDescriptor (
                              BlockIo,
			      DiskIo,
			      *BlockSize,
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
			     *BlockSize,
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
					    *BlockSize,
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
  FreePool ((VOID *)*FileEntry);
  *FileEntry = NULL;

FreeExit2:
  FreePool ((VOID *)*FileSetDesc);
  *FileSetDesc = NULL;

FreeExit1:
  FreePool ((VOID *)*PartitionDesc);
  FreePool ((VOID *)*LogicalVolDesc);
  *PartitionDesc   = NULL;
  *LogicalVolDesc  = NULL;

FreeExit0:
  FreePool ((VOID *)*AnchorPoint);
  *AnchorPoint = NULL;

Exit:
  return Status;
}

EFI_STATUS
EFIAPI
ReadDirectory (
  IN EFI_BLOCK_IO_PROTOCOL                  *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                   *DiskIo,
  IN UINT32                                 BlockSize,
  IN UDF_PARTITION_DESCRIPTOR               *PartitionDesc,
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR         *ParentFileIdentifierDesc,
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR         *PrevFileIdentifierDesc,
  OUT UDF_FILE_IDENTIFIER_DESCRIPTOR        **ReadFileIdentifierDesc
  )
{
  EFI_STATUS                                Status;
  UDF_LONG_ALLOCATION_DESCRIPTOR            *LongAd;
  UINT32                                    Lsn;
  UINT64                                    ParentOffset;
  UINT64                                    FidLength;
  UINT64                                    Offset;
  UINT64                                    EndingPartitionOffset;
  VOID                                      *Buffer;
  UDF_FILE_IDENTIFIER_DESCRIPTOR            *FileIdentifierDesc;
  UDF_DESCRIPTOR_TAG                        *DescriptorTag;
  VOID                                      *PrevFileIdentifier;
  VOID                                      *FileIdentifier;

  Status = EFI_SUCCESS;

  *ReadFileIdentifierDesc = NULL;

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
  Lsn = PartitionDesc->PartitionStartingLocation +
           LongAd->ExtentLocation.LogicalBlockNumber + 1;

  //
  // Calculate offset of the Parent FID
  //
  ParentOffset = Lsn * BlockSize;

  EndingPartitionOffset =
    (PartitionDesc->PartitionStartingLocation +
     PartitionDesc->PartitionLength) * BlockSize;

  Offset = ParentOffset;

  //
  // Make sure we don't across a partition boundary
  //
  if (Offset > EndingPartitionOffset) {
    Print (L"UdfDriverStart: [ReadDirectory] Reached End of Partition\n");
    goto Exit;
  }

  Buffer = AllocatePool (BlockSize);
  if (!Buffer) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  //
  // First FID to be read
  //
  if (!PrevFileIdentifierDesc) {
    goto ReadNextFid;
  }

#ifdef UDF_DEBUG
  Print (
    L"UdfDriverStart: [ReadDirectory] Prev ImpUseLen: %d\n",
    PrevFileIdentifierDesc->LengthOfImplementationUse
    );

  Print (
    L"UdfDriverStart: [ReadDirectory] Prev FiLen: %d\n",
    PrevFileIdentifierDesc->LengthOfFileIdentifier
    );
#endif

  //
  // Next FID is not first one of the reading.
  //
  do {
    //
    // Read FID
    //
    Status = DiskIo->ReadDisk (
                         DiskIo,
			 BlockIo->Media->MediaId,
			 Offset,
			 BlockSize,
			 Buffer
                         );
    if (EFI_ERROR (Status)) {
      goto FreeExit;
    }

    FileIdentifierDesc = (UDF_FILE_IDENTIFIER_DESCRIPTOR *)Buffer;

    DescriptorTag = &FileIdentifierDesc->DescriptorTag;

#ifdef UDF_DEBUG
    Print (
      L"UdfDriverStart: [FID] Tag Identifier: %d (0x%08x)\n",
      DescriptorTag->TagIdentifier,
      DescriptorTag->TagIdentifier
      );
#endif

    if (!IS_FID (Buffer)) {
      goto FreeExit;
    }

#ifdef UDF_DEBUG
    Print (
      L"UdfDriverStart: [ReadDirectory] Cur ImpUseLen: %d\n",
      FileIdentifierDesc->LengthOfImplementationUse
      );

    Print (
      L"UdfDriverStart: [ReadDirectory] Cur FiLen: %d\n",
      FileIdentifierDesc->LengthOfFileIdentifier
      );
#endif

    FidLength = sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR) +
      FileIdentifierDesc->LengthOfFileIdentifier +
      FileIdentifierDesc->LengthOfImplementationUse +
      (4 * ((FileIdentifierDesc->LengthOfFileIdentifier +
	     FileIdentifierDesc->LengthOfImplementationUse + 38 + 3) / 4) -
       (FileIdentifierDesc->LengthOfFileIdentifier +
	FileIdentifierDesc->LengthOfImplementationUse + 38));

#ifdef UDF_DEBUG
    Print (L"UdfDriverStart: [ReadDirectory] Cur FidLength: %d\n", FidLength);
#endif

    if (
      PrevFileIdentifierDesc->LengthOfFileIdentifier ==
      FileIdentifierDesc->LengthOfFileIdentifier
      ) {
      PrevFileIdentifier = (VOID *)(
         (UINT8 *)&PrevFileIdentifierDesc->Data +
	 PrevFileIdentifierDesc->LengthOfImplementationUse
         );

      FileIdentifier = (VOID *)(
         (UINT8 *)&FileIdentifierDesc->Data +
	 FileIdentifierDesc->LengthOfImplementationUse
	 );

      if (CompareMem (
	    (VOID *)PrevFileIdentifier,
	    (VOID *)FileIdentifier,
	    PrevFileIdentifierDesc->LengthOfFileIdentifier) == 0
	) {
	//
	// Prepare to read FID following the current one
	//
	Offset += FidLength;

	goto ReadNextFid;
      }
    }

    Offset += FidLength;
  } while (Offset < EndingPartitionOffset);

  //
  // End of directory listing
  //
  FreePool(Buffer);

  return Status;

ReadNextFid:
  //
  // Read FID
  //
  Status = DiskIo->ReadDisk (
                       DiskIo,
		       BlockIo->Media->MediaId,
		       Offset,
		       BlockSize,
		       Buffer
                       );
  if (EFI_ERROR (Status)) {
    goto FreeExit;
  }

  FileIdentifierDesc = (UDF_FILE_IDENTIFIER_DESCRIPTOR *)Buffer;

  DescriptorTag = &FileIdentifierDesc->DescriptorTag;

#ifdef UDF_DEBUG
  Print (
    L"UdfDriverStart: [FID] Tag Identifier: %d (0x%08x)\n",
    DescriptorTag->TagIdentifier,
    DescriptorTag->TagIdentifier
    );
#endif

  if (!IS_FID (FileIdentifierDesc)) {
    goto FreeExit;
  }

#ifdef UDF_DEBUG
  Print (
    L"UdfDriverStart: [ReadDirectory] ImpUseLen: %d\n",
    FileIdentifierDesc->LengthOfImplementationUse
    );

  Print (
    L"UdfDriverStart: [ReadDirectory] FiLen: %d\n",
    FileIdentifierDesc->LengthOfFileIdentifier
    );
#endif

  *ReadFileIdentifierDesc = FileIdentifierDesc;

  return Status;

FreeExit:
  FreePool(Buffer);

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

EFI_STATUS
EFIAPI
GetDirectorySize (
  IN EFI_BLOCK_IO_PROTOCOL                  *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                   *DiskIo,
  IN UINT32                                 BlockSize,
  IN UDF_PARTITION_DESCRIPTOR               *PartitionDesc,
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR         *ParentFileIdentifierDesc,
  OUT UINT64                                *Size
  )
{
  EFI_STATUS                                Status;
  UDF_FILE_IDENTIFIER_DESCRIPTOR            *PrevFileIdentifierDesc;
  UDF_FILE_IDENTIFIER_DESCRIPTOR            *NextFileIdentifierDesc;
  UDF_FILE_ENTRY                            *FileEntry;
  UDF_LONG_ALLOCATION_DESCRIPTOR            *LongAd;
  UINT32                                    Lsn;
  UINT64                                    Offset;

  FileEntry = AllocatePool (BlockSize);
  if (!FileEntry) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  PrevFileIdentifierDesc   = NULL;
  *Size                    = 0;

  for (;;) {
    Status = ReadDirectory (
                        BlockIo,
			DiskIo,
			BlockSize,
		        PartitionDesc,
		        ParentFileIdentifierDesc,
			PrevFileIdentifierDesc,
		        &NextFileIdentifierDesc
			);
    if (EFI_ERROR (Status)) {
      goto Exit;
    }

    if (!NextFileIdentifierDesc) {
      break;
    }

    if (IS_FID_PARENT_FILE (NextFileIdentifierDesc)) {
      goto ReadNextFid;
    }

    LongAd = &NextFileIdentifierDesc->Icb;

    Lsn = PartitionDesc->PartitionStartingLocation +
           LongAd->ExtentLocation.LogicalBlockNumber;

    Offset = Lsn * BlockSize;

    //
    // Read FE
    //
    Status = DiskIo->ReadDisk (
                         DiskIo,
			 BlockIo->Media->MediaId,
			 Offset,
			 BlockSize,
			 (VOID *)FileEntry
                         );
    if (EFI_ERROR (Status)) {
      FreePool (FileEntry);
      goto FreeExit;
    }

    //
    // TODO: check if ICB Tag's flags field contain all valid bits set
    //
    if (!IS_FE (FileEntry)) {
      FreePool (FileEntry);
      Status = EFI_VOLUME_CORRUPTED;
      goto FreeExit;
    }

    *Size += FileEntry->InformationLength;

ReadNextFid:
    if (PrevFileIdentifierDesc) {
      FreePool ((VOID *)PrevFileIdentifierDesc);
    }

    PrevFileIdentifierDesc = NextFileIdentifierDesc;
  }

FreeExit:
  FreePool ((VOID *)FileEntry);
  FreePool ((VOID *)PrevFileIdentifierDesc);

Exit:
  return Status;
}
