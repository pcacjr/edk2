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

//#define UDF_DEBUG

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
  PRIVATE_UDF_FILE_DATA                  *PrivFileData;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  PrivFsData = PRIVATE_UDF_SIMPLE_FS_DATA_FROM_THIS (This);

  PrivFileData = AllocatePool (sizeof (PRIVATE_UDF_FILE_DATA));
  if (!PrivFileData) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  Status = FindRootDirectory (
                          PrivFsData->BlockIo,
                          PrivFsData->DiskIo,
			  &PrivFileData->UdfFileSystemData.AnchorPoint,
			  &PrivFileData->UdfFileSystemData.PartitionDesc,
			  &PrivFileData->UdfFileSystemData.LogicalVolDesc,
			  &PrivFileData->UdfFileSystemData.FileSetDesc,
			  &PrivFileData->UdfFileSystemData.FileEntry,
			  &PrivFileData->UdfFileSystemData.FileIdentifierDesc
                          );
  if (EFI_ERROR (Status)) {
    goto FreeExit;
  }

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

  PrivFileData->FilePosition = 0;

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
  UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER   *AnchorPoint;
  UDF_PARTITION_DESCRIPTOR               *PartitionDesc;
  UDF_LOGICAL_VOLUME_DESCRIPTOR          *LogicalVolDesc;
  UDF_FILE_SET_DESCRIPTOR                *FileSetDesc;
  UDF_FILE_ENTRY                         FileEntry;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         ParentFileIdentifierDesc;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         FileIdentifierDesc;
  UINT64                                 NextOffset;
  BOOLEAN                                ReadDone;
  CHAR16                                 *Str;
  CHAR16                                 *StrAux;
  UINT64                                 Offset;
  CHAR16                                 *NextFileName;
  EFI_BLOCK_IO_PROTOCOL                  *BlockIo;
  EFI_DISK_IO_PROTOCOL                   *DiskIo;
  BOOLEAN                                Found;
  PRIVATE_UDF_FILE_DATA                  *NewPrivFileData;
  UDF_LONG_ALLOCATION_DESCRIPTOR         *LongAd;
  UINT32                                 Lsn;
  BOOLEAN                                IsCurrentDir;
  BOOLEAN                                IsRootDir;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  if ((!This) || (!NewHandle) || (!FileName)) {
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  AnchorPoint                = &PrivFileData->UdfFileSystemData.AnchorPoint;
  PartitionDesc              = &PrivFileData->UdfFileSystemData.PartitionDesc;
  LogicalVolDesc             = &PrivFileData->UdfFileSystemData.LogicalVolDesc;
  FileSetDesc                = &PrivFileData->UdfFileSystemData.FileSetDesc;

  CopyMem (
    (VOID *)&ParentFileIdentifierDesc,
    (VOID *)&PrivFileData->UdfFileSystemData.FileIdentifierDesc,
    sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)
    );

#ifdef UDF_DEBUG
  Print (L"UdfOpen: FileName \'%s\'\n", FileName);
#endif

  Str = AllocateZeroPool ((StrLen (FileName) + 1) * sizeof (CHAR16));
  if (!Str) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  BlockIo        = PrivFileData->BlockIo;
  DiskIo         = PrivFileData->DiskIo;
  Found          = FALSE;
  NextFileName   = NULL;

  CopyMem (
    (VOID *)&FileIdentifierDesc,
    (VOID *)&ParentFileIdentifierDesc,
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

    if ((*FileName) && (*FileName == '\\')) {
      FileName++;
    }

    *((UINT8 *)Str + Offset) = '\0';

    StrAux = Str;

    //
    // Discard any trailing whitespaces in the beginning
    //
    for ( ; (*StrAux) && (*StrAux == ' '); StrAux++)
      ;

#ifdef UDF_DEBUG
    Print (
      L"UdfOpen: Start looking up \'%s\' (Length: %d)\n",
      StrAux,
      StrLen (StrAux)
      );
#endif

    IsRootDir      = FALSE;
    IsCurrentDir   = FALSE;
    Found          = FALSE;

    if (!*StrAux) {
#ifdef UDF_DEBUG
      Print (L"UdfOpen: Open root directory\n");
#endif

      Status = FindRootDirectory (
	                      BlockIo,
			      DiskIo,
			      AnchorPoint,
			      PartitionDesc,
			      LogicalVolDesc,
			      FileSetDesc,
			      &FileEntry,
			      &FileIdentifierDesc
	                      );
      if (EFI_ERROR (Status)) {
	goto Exit;
      }

      if (!*FileName) {
	IsRootDir = TRUE;
	Found = TRUE;
	break;
      } else {
	continue;
      }
    }

    if (StrCmp (StrAux, L"..") == 0) {
      //
      // Make sure we're not going to look up Parent FID from a root
      // directory or even if the current FID is not a directory(!)
      //
      if ((IS_FID_PARENT_FILE (&FileIdentifierDesc)) ||
	  (!IS_FID_DIRECTORY_FILE (&FileIdentifierDesc))) {
	break;
      }

      //
      // Ok, we got ".." from the filename. Find Parent FID of the current FID.
      //
      LongAd = &FileIdentifierDesc.Icb;

      //
      // Point to the Parent FID
      //
      Lsn = PartitionDesc->PartitionStartingLocation +
               LongAd->ExtentLocation.LogicalBlockNumber + 1;

      //
      // Calculate offset of the Parent FID
      //
      Offset = Lsn * LOGICAL_BLOCK_SIZE;

      //
      // Read FID
      //
      Status = DiskIo->ReadDisk (
                           DiskIo,
			   BlockIo->Media->MediaId,
			   Offset,
			   sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR),
			   (VOID *)&FileIdentifierDesc
                           );
      if (EFI_ERROR (Status)) {
	goto Exit;
      }

      if (!IS_FID (&FileIdentifierDesc)) {
	Status = EFI_VOLUME_CORRUPTED;
	goto Exit;
      }

      Found = TRUE;
      continue;
    } else if (StrCmp (StrAux, L".") == 0) {
#ifdef UDF_DEBUG
      Print (L"UdfOpen: Found file \'%s\'\n", Str);
#endif
      Found = TRUE;
      IsCurrentDir = TRUE;
      continue;
    }

    //
    // Start lookup
    //
    CopyMem (
      (VOID *)&ParentFileIdentifierDesc,
      (VOID *)&FileIdentifierDesc,
      sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)
      );

    NextOffset = 0;

    for (;;) {
      Status = ReadDirectory (
                          BlockIo,
			  DiskIo,
			  PartitionDesc,
			  &ParentFileIdentifierDesc,
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

      if (IS_FID_PARENT_FILE (&FileIdentifierDesc)) {
	continue;
      }

      Status = FileIdentifierDescToFileName (
                             &FileIdentifierDesc,
			     &NextFileName
	                     );
      if (EFI_ERROR (Status)) {
	goto Exit;
      }

#ifdef UDF_DEBUG
      Print (
	L"UdfOpen: ===> %s (Length: %d)\n",
	NextFileName,
	StrLen (NextFileName)
	);
#endif

      //
      // Check whether FID's File Identifier contains the expected filename
      //
      if (StrCmp (NextFileName, StrAux) == 0) {
#ifdef UDF_DEBUG
	Print (L"UdfOpen: Found file \'%s\'\n", Str);
#endif
	Found = TRUE;
	goto NextLookup;
      }

      if (NextFileName) {
	FreePool ((VOID *)NextFileName);
      }
    }
  }

  if (Found) {
    if (IsCurrentDir) {
      Status = DuplicatePrivateFileData (PrivFileData, &NewPrivFileData);
      if (EFI_ERROR (Status)) {
	goto Exit;
      }

      Status = EFI_SUCCESS;
      goto Done;
    }

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

    if (IsRootDir) {
      goto HandleRootDir;
    }

    //
    // Find FE of the FID
    //
    LongAd = &FileIdentifierDesc.Icb;

    Lsn = PartitionDesc->PartitionStartingLocation +
           LongAd->ExtentLocation.LogicalBlockNumber;

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

    //
    // TODO: check if ICB Tag's flags field contain all valid bits set
    //
    if (!IS_FE (&FileEntry)) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
    }

HandleRootDir:
    CopyMem (
      (VOID *)&NewPrivFileData->UdfFileSystemData.FileEntry,
      (VOID *)&FileEntry,
      sizeof (UDF_FILE_ENTRY)
      );

    CopyMem (
      (VOID *)&NewPrivFileData->UdfFileSystemData.FileIdentifierDesc,
      (VOID *)&FileIdentifierDesc,
      sizeof (UDF_FILE_IDENTIFIER_DESCRIPTOR)
      );

Done:
    NewPrivFileData->FilePosition = 0;

    *NewHandle = &NewPrivFileData->FileIo;

    Status = EFI_SUCCESS;
  } else {
    Status = EFI_NOT_FOUND;
  }

Exit:
#ifdef UDF_DEBUG
  Print (L"UdfOpen: Done (%r)\n", Status);
#endif

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
  UDF_FILE_ENTRY                         *ParentFileEntry;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *ParentFileIdentifierDesc;
  EFI_BLOCK_IO_PROTOCOL                  *BlockIo;
  EFI_DISK_IO_PROTOCOL                   *DiskIo;
  UINT64                                 FilePosition;
  INT64                                  ExtStartOffset;
  UINT32                                 ExtLen;
  UINT32                                 ShortAdsNo;
  UDF_SHORT_ALLOCATION_DESCRIPTOR        *ShortAd;
  UINT64                                 BufferOffset;
  UINTN                                  BytesLeft;
  UINTN                                  DirsCount;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         FileIdentifierDesc;
  UINT64                                 NextOffset;
  BOOLEAN                                ReadDone;
  UINTN                                  FileInfoLength;
  EFI_FILE_INFO                          *FileInfo;
  CHAR16                                 *FileName;
  UINT64                                 FileSize;
  UDF_LONG_ALLOCATION_DESCRIPTOR         *LongAd;
  UINT32                                 Lsn;
  UINT64                                 Offset;
  UDF_FILE_ENTRY                         FileEntry;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

#ifdef UDF_DEBUG
  Print (L"UdfRead: in\n");
#endif

  if ((!This) || (!Buffer)) {
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  PartitionDesc            = &PrivFileData->UdfFileSystemData.PartitionDesc;
  ParentFileEntry          = &PrivFileData->UdfFileSystemData.FileEntry;
  ParentFileIdentifierDesc = &PrivFileData->UdfFileSystemData.FileIdentifierDesc;

  BlockIo                  = PrivFileData->BlockIo;
  DiskIo                   = PrivFileData->DiskIo;
  FileName                 = NULL;

#ifdef UDF_DEBUG
  Print (L"UdfRead: BufferSize: %d\n", *BufferSize);
#endif

  if (IS_FID_NORMAL_FILE (ParentFileIdentifierDesc)) {
    //
    // Check if the current position is beyond the EOF
    //
    if (PrivFileData->FilePosition > ParentFileEntry->InformationLength) {
#ifdef UDF_DEBUG
      Print (
	L"UdfRead: FilePosition (%d) - InfoLen (%d)\n",
	PrivFileData->FilePosition,
	ParentFileEntry->InformationLength
	);
#endif
      Status = EFI_DEVICE_ERROR;
      goto Exit;
    } else if (
      PrivFileData->FilePosition == ParentFileEntry->InformationLength
      ) {
      *BufferSize = 0;
      Status = EFI_SUCCESS;
      goto Exit;
    }

    //
    // File Type should be 5 for a standard byte addressable file
    //
    if (ParentFileEntry->IcbTag.FileType != 5) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
    }

    //
    // The Type of Allocation Descriptor (bit 0-2) in Flags field of ICB Tag
    // shall be set to 0 which means that Short Allocation Descriptors are used.
    //
    if (ParentFileEntry->IcbTag.Flags & 0x07) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
    }

    //
    // Strategy Type of 4 is the only supported
    //
    if (ParentFileEntry->IcbTag.StrategyType != 4) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
    }

    //
    // OK, now read file's extents to find recorded data.
    //
    ShortAdsNo = ParentFileEntry->LengthOfAllocationDescriptors /
                    sizeof (UDF_SHORT_ALLOCATION_DESCRIPTOR);

    //
    // NOTE: The total length of a File Entry shall not exceed the size of one
    // logical block, so it's OK.
    //
    ShortAd = (UDF_SHORT_ALLOCATION_DESCRIPTOR *)(
                            (UINT8 *)&ParentFileEntry->Data +
			    ParentFileEntry->LengthOfExtendedAttributes
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
			     ShortAd->ExtentPosition) * LOGICAL_BLOCK_SIZE),
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
  } else if (IS_FID_DIRECTORY_FILE (ParentFileIdentifierDesc)) {
    DirsCount = PrivFileData->FilePosition + 1;

    //
    // Find current directory entry
    //
    NextOffset = 0;

    for (;;) {
      Status = ReadDirectory (
	    BlockIo,
	    DiskIo,
	    PartitionDesc,
	    ParentFileIdentifierDesc,
	    &FileIdentifierDesc,
	    &NextOffset,
	    &ReadDone
	    );
      if (EFI_ERROR (Status)) {
	goto Exit;
      }

      if (!ReadDone) {
	break;
      }

      if (IS_FID_PARENT_FILE (&FileIdentifierDesc)) {
	continue;
      }

      if (!--DirsCount) {
	break;
      }
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
    Status = FileIdentifierDescToFileName (&FileIdentifierDesc, &FileName);
    if (EFI_ERROR (Status)) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
    }

    //
    // Find FE of the current directory entry
    //
    LongAd = &FileIdentifierDesc.Icb;

    Lsn = PartitionDesc->PartitionStartingLocation +
      LongAd->ExtentLocation.LogicalBlockNumber;

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

    //
    // TODO: check if ICB Tag's flags field contain all valid bits set
    //
    if (!IS_FE (&FileEntry)) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
    }

#ifdef UDF_DEBUG
    Print (L"UdfRead: FileName: %s\n", FileName);
#endif

    //
    // Check if BufferSize is too small to read the current directory entry
    //
    FileInfoLength = sizeof (EFI_FILE_INFO) + StrLen (FileName) + 1;
    if (*BufferSize < FileInfoLength) {
      *BufferSize = FileInfoLength;
      Status = EFI_BUFFER_TOO_SMALL;
      goto Exit;
    }

    FileInfo = (EFI_FILE_INFO *)Buffer;

    FileInfo->Size        = FileInfoLength;
    FileInfo->Attribute   &= ~EFI_FILE_VALID_ATTR;
    FileInfo->Attribute   |= EFI_FILE_READ_ONLY;

    if (IS_FID_DIRECTORY_FILE (&FileIdentifierDesc)) {
      Status = GetDirectorySize (
                             BlockIo,
			     DiskIo,
			     PartitionDesc,
			     &FileIdentifierDesc,
			     &FileSize
			     );
      if (EFI_ERROR (Status)) {
	goto Exit;
      }

      FileInfo->Attribute |= EFI_FILE_DIRECTORY;
    } else if (IS_FID_NORMAL_FILE (&FileIdentifierDesc)) {
      FileSize = FileEntry.InformationLength;
      FileInfo->Attribute |= EFI_FILE_ARCHIVE;
    }

    if (IS_FID_HIDDEN_FILE (&FileIdentifierDesc)) {
      FileInfo->Attribute |= EFI_FILE_HIDDEN;
    }

    //
    // Check if file has System bit set (bit 10)
    //
    if (FileEntry.IcbTag.Flags & (1 << 10)) {
      FileInfo->Attribute |= EFI_FILE_SYSTEM;
    }

    FileInfo->FileSize       = FileSize;
    FileInfo->PhysicalSize   = FileSize;

    FileInfo->CreateTime.Year         = FileEntry.AccessTime.Year;
    FileInfo->CreateTime.Month        = FileEntry.AccessTime.Month;
    FileInfo->CreateTime.Day          = FileEntry.AccessTime.Day;
    FileInfo->CreateTime.Hour         = FileEntry.AccessTime.Hour;
    FileInfo->CreateTime.Minute       = FileEntry.AccessTime.Second;
    FileInfo->CreateTime.Second       = FileEntry.AccessTime.Second;
    FileInfo->CreateTime.Nanosecond   =
         FileEntry.AccessTime.HundredsOfMicroseconds;

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
  } else if (IS_FID_DELETED_FILE (ParentFileIdentifierDesc)) {
    Status = EFI_DEVICE_ERROR;
  } else {
    Status = EFI_VOLUME_CORRUPTED;
  }

Exit:
  if (FileName) {
    FreePool ((VOID *)FileName);
  }

#ifdef UDF_DEBUG
  Print (L"UdfRead: Done (%r)\n", Status);
#endif

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
  PRIVATE_UDF_FILE_DATA                  *PrivFileData;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

#ifdef UDF_DEBUG
  Print (L"UdfClose: in\n");
#endif

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  FreePool ((VOID *)PrivFileData);

#if 0
  Print (L"UdfClose: Done (%r)\n", EFI_SUCCESS);
#endif

  gBS->RestoreTPL (OldTpl);

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
  Print (L"UdfDelete: in\n");

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

  Print (L"UdfGetPosition: in\n");

  Status = EFI_SUCCESS;

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  //
  // As per UEFI spec, if the file handle is a directory, then the current file
  // position has no meaning and the operation is not supported.
  //
  if (IS_FID_DIRECTORY_FILE (
	   &PrivFileData->UdfFileSystemData.FileIdentifierDesc
	   )
    ) {
    Status = EFI_UNSUPPORTED;
  } else {
    //
    // The file is not a directory. So, return its position.
    //
    *Position = PrivFileData->FilePosition;
  }

  Print (L"UdfGetPosition: Done (%r)\n", Status);

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

#ifdef UDF_DEBUG
  Print (L"UdfSetPosition: in\n");
#endif

  Status = EFI_SUCCESS;

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  FileIdentifierDesc = &PrivFileData->UdfFileSystemData.FileIdentifierDesc;
  FileEntry          = &PrivFileData->UdfFileSystemData.FileEntry;

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

#ifdef UDF_DEBUG
  Print (L"UdfSetPosition: Done (%r)\n", Status);
#endif

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
  EFI_FILE_INFO                          *FileInfo;
  CHAR16                                 *FileName;
  UDF_PARTITION_DESCRIPTOR               *PartitionDesc;
  UDF_LOGICAL_VOLUME_DESCRIPTOR          *LogicalVolDesc;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *FileIdentifierDesc;
  EFI_BLOCK_IO_PROTOCOL                  *BlockIo;
  EFI_DISK_IO_PROTOCOL                   *DiskIo;
  UINT64                                 FileSize;
  UINTN                                  FileInfoLength;
  EFI_FILE_SYSTEM_INFO                   *FileSystemInfo;
  UDF_CHAR_SPEC                          *CharSpec;
  UINTN                                  FileSystemInfoLength;
  CHAR16                                 *String;
  CHAR16                                 *CharP;
  UINTN                                  Index;

#ifdef UDF_DEBUG
  Print (L"UdfGetInfo: in\n");
#endif

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  PartitionDesc            = &PrivFileData->UdfFileSystemData.PartitionDesc;
  LogicalVolDesc           = &PrivFileData->UdfFileSystemData.LogicalVolDesc;
  FileEntry                = &PrivFileData->UdfFileSystemData.FileEntry;
  FileIdentifierDesc       = &PrivFileData->UdfFileSystemData.FileIdentifierDesc;

  BlockIo     = PrivFileData->BlockIo;
  DiskIo      = PrivFileData->DiskIo;
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

    FileInfo = (EFI_FILE_INFO *)Buffer;

    FileInfo->Size         = FileInfoLength;
    FileInfo->Attribute    &= ~EFI_FILE_VALID_ATTR;
    FileInfo->Attribute    |= EFI_FILE_READ_ONLY;

    if (IS_FID_DIRECTORY_FILE (FileIdentifierDesc)) {
      Status = GetDirectorySize (
                             BlockIo,
			     DiskIo,
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

#ifdef UDF_DEBUG
    Print (L"UdfGetInfo: FileSize: %d\n", FileSize);
#endif

    //
    // Check if file has System bit set (bit 10)
    //
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
	  PartitionDesc->PartitionLength) * LOGICAL_BLOCK_SIZE;
    FileSystemInfo->FreeSpace   = 0;

    *BufferSize = FileSystemInfoLength;
    Status = EFI_SUCCESS;
  } else {
    Status = EFI_UNSUPPORTED;
  }

Exit:
  if (FileName) {
    FreePool ((VOID *)FileName);
  }

#ifdef UDF_DEBUG
  Print (L"UdfGetInfo: Done (%r)\n", Status);
#endif

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
#ifdef UDF_DEBUG
  Print (L"UdfSetInfo: in\n");
  Print (L"UdfSetInfo: Done (%r)\n", EFI_WRITE_PROTECTED);
#endif

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
#ifdef UDF_DEBUG
  Print (L"UdfFlush: in\n");
  Print (L"UdfFlush: Done (%r)\n", EFI_WRITE_PROTECTED);
#endif

  return EFI_WRITE_PROTECTED;
}

STATIC
EFI_STATUS
FindNsrDescriptor (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  OUT UDF_NSR_DESCRIPTOR                     *NsrDescriptor
  )
{
  EFI_STATUS                                 Status;

  Status = DiskIo->ReadDisk (
                       DiskIo,
                       BlockIo->Media->MediaId,
                       NSR_DESCRIPTOR_LSN * LOGICAL_SECTOR_SIZE,
                       sizeof (UDF_NSR_DESCRIPTOR),
                       (VOID *)NsrDescriptor
                       );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

#ifdef UDF_DEBUG
  Print (L"FindNsrDescriptor: Get NSR Descriptor\n");
#endif

Exit:
  return Status;
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
  UDF_DESCRIPTOR_TAG                         *DescriptorTag;

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

#ifdef UDF_DEBUG
  Print (L"UdfDriverStart: Get AVDP\n");
#endif

  DescriptorTag = &AnchorPoint->DescriptorTag;

#ifdef UDF_DEBUG
  Print (
    L"UdfDriverStart: [AVDP] Tag Identifier: %d (0x%08x)\n",
    DescriptorTag->TagIdentifier,
    DescriptorTag->TagIdentifier
    );
#endif

  //
  // TODO: In case of failure, look for the other AVDPs at N or N - 256
  //
  if (!IS_AVDP (AnchorPoint)) {
    Print (
      L"UdfDriverStart: [AVDP] Invalid Tag Identifier number\n"
      );
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
  UDF_DESCRIPTOR_TAG                         *DescriptorTag;
  UDF_LONG_ALLOCATION_DESCRIPTOR             *LongAd;

  ExtentAd = &AnchorPoint->MainVolumeDescriptorSequenceExtent;

#ifdef UDF_DEBUG
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
#endif

  if (ExtentAd->ExtentLength / LOGICAL_SECTOR_SIZE < 16) {
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
#ifdef UDF_DEBUG
  Print (
    L"UdfDriverStart: Start Main Volume Descriptor Sequence\n"
    );
#endif

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

    if (IS_PD (Buffer)) {
#ifdef UDF_DEBUG
      Print (L"UdfDriverStart: Get PD\n");
#endif

      CopyMem (
	(VOID *)PartitionDesc,
	(VOID *)&Buffer,
	sizeof (UDF_PARTITION_DESCRIPTOR)
	);
    } else if (IS_LVD (Buffer)) {
#ifdef UDF_DEBUG
      Print (L"UdfDriverStart: Get LVD\n");
#endif

      CopyMem (
	(VOID *)LogicalVolDesc,
	(VOID *)&Buffer,
	sizeof (UDF_LOGICAL_VOLUME_DESCRIPTOR)
	);
    }

    StartingLsn++;
  }

#ifdef UDF_DEBUG
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
#endif

  DescriptorTag = &LogicalVolDesc->DescriptorTag;

#ifdef UDF_DEBUG
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
#endif

  LongAd = &LogicalVolDesc->LogicalVolumeContentsUse;

#ifdef UDF_DEBUG
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
#endif

#ifdef UDF_DEBUG
  Print (
    L"UdfDriverStart: [LVD] Partition Ref # of FSD:       %d (0x%08x)\n",
    LongAd->ExtentLocation.PartitionReferenceNumber,
    LongAd->ExtentLocation.PartitionReferenceNumber
    );
#endif

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
  UINT32                                     Lsn;
  UDF_DESCRIPTOR_TAG                         *DescriptorTag;

  LongAd = &LogicalVolDesc->LogicalVolumeContentsUse;

  Lsn = PartitionDesc->PartitionStartingLocation +
           LongAd->ExtentLocation.LogicalBlockNumber;

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

#ifdef UDF_DEBUG
  Print (L"UdfDriverStart: Get FSD\n");
#endif

  DescriptorTag = &FileSetDesc->DescriptorTag;

#ifdef UDF_DEBUG
  Print (
    L"UdfDriverStart: [FSD] Tag Identifier:                      %d (0x%08x)\n",
    DescriptorTag->TagIdentifier,
    DescriptorTag->TagIdentifier
    );
#endif

  if (!IS_FSD (FileSetDesc)) {
      Print (
	L"UdfDriverStart: [FSD] Invalid Tag Identifier number\n"
	);
      Status = EFI_VOLUME_CORRUPTED;
  }

Exit:
  return Status;
}

STATIC
EFI_STATUS
FindFileEntryRootDir (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_PARTITION_DESCRIPTOR                *PartitionDesc,
  IN UDF_FILE_SET_DESCRIPTOR                 *FileSetDesc,
  OUT UDF_FILE_ENTRY                         *FileEntry
  )
{
  EFI_STATUS                                 Status;
  UDF_LONG_ALLOCATION_DESCRIPTOR             *LongAd;
  UINT32                                     Lsn;
  UDF_DESCRIPTOR_TAG                         *DescriptorTag;

  LongAd = &FileSetDesc->RootDirectoryIcb;

#ifdef UDF_DEBUG
  Print (
    L"UdfDriverStart: [FSD] LBN of Root directory:               %d (0x%08x)\n",
    LongAd->ExtentLocation.LogicalBlockNumber,
    LongAd->ExtentLocation.LogicalBlockNumber
    );
#endif

#ifdef UDF_DEBUG
  Print (
    L"UdfDriverStart: [FSD] Partition Ref # of Root directory:   %d (0x%08x)\n",
    LongAd->ExtentLocation.PartitionReferenceNumber,
    LongAd->ExtentLocation.PartitionReferenceNumber
    );
#endif

  Lsn = PartitionDesc->PartitionStartingLocation +
           LongAd->ExtentLocation.LogicalBlockNumber;

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

#ifdef UDF_DEBUG
  Print (L"UdfDriverStart: Get File Entry (Root Directory)\n");
#endif

  DescriptorTag = &FileEntry->DescriptorTag;

#ifdef UDF_DEBUG
  Print (
    L"UdfDriverStart: [ROOT] Tag Identifier:               %d (0x%08x)\n",
    DescriptorTag->TagIdentifier,
    DescriptorTag->TagIdentifier
    );
#endif

  if (!IS_FE (FileEntry)) {
      Print (
	L"UdfDriverStart: [ROOT] Invalid Tag Identifier number\n"
	);

      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
  }

  //
  // Root Directory cannot be file, obivously. Check its file type.
  //
  if (!IS_FE_DIRECTORY (FileEntry)) {
    Print (L"UdfDriverStart: [ROOT] Root Directory is NOT a directory!\n");

    Status = EFI_VOLUME_CORRUPTED;
  }

#ifdef UDF_DEBUG
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
#endif

Exit:
  return Status;
}

EFI_STATUS
FindFileIdentifierDescriptorRootDir (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_PARTITION_DESCRIPTOR                *PartitionDesc,
  IN UDF_FILE_SET_DESCRIPTOR                 *FileSetDesc,
  OUT UDF_FILE_IDENTIFIER_DESCRIPTOR         *FileIdentifierDesc
  )
{
  EFI_STATUS                                 Status;
  UDF_LONG_ALLOCATION_DESCRIPTOR             *LongAd;
  UINT32                                     Lsn;
  UDF_DESCRIPTOR_TAG                         *DescriptorTag;

  LongAd = &FileSetDesc->RootDirectoryIcb;

  //
  // TODO: Handle strategy type of 4096 as well.
  //
  // For ICB strategy type of 4, the File Identifier Descriptor of the Root
  // Directory immediately follows the File Entry (Root Directory).
  //
  Lsn = PartitionDesc->PartitionStartingLocation +
           LongAd->ExtentLocation.LogicalBlockNumber + 1;

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

#ifdef UDF_DEBUG
  Print (L"UdfDriverStart: [ROOT] Get FID\n");
#endif

  DescriptorTag = &FileIdentifierDesc->DescriptorTag;

#ifdef UDF_DEBUG
  Print (
    L"UdfDriverStart: [ROOT-FID] Tag Identifier:           %d (0x%08x)\n",
    DescriptorTag->TagIdentifier,
    DescriptorTag->TagIdentifier
    );
#endif

  if (!IS_FID (FileIdentifierDesc)) {
      Print (
	L"UdfDriverStart: [ROOT-FID] Invalid tag identifier number\n"
	);

      Status = EFI_VOLUME_CORRUPTED;
  }

  LongAd = &FileIdentifierDesc->Icb;

#ifdef UDF_DEBUG
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
#endif

Exit:
  return Status;
}

EFI_STATUS
EFIAPI
FindRootDirectory (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  OUT UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER   *AnchorPoint,
  OUT UDF_PARTITION_DESCRIPTOR               *PartitionDesc,
  OUT UDF_LOGICAL_VOLUME_DESCRIPTOR          *LogicalVolDesc,
  OUT UDF_FILE_SET_DESCRIPTOR                *FileSetDesc,
  OUT UDF_FILE_ENTRY                         *FileEntry,
  OUT UDF_FILE_IDENTIFIER_DESCRIPTOR         *FileIdentifierDesc
  )
{
  EFI_STATUS                                 Status;

#ifdef UDF_DEBUG
  Print (
    L"FindRootDirectory: Start reading UDF Volume and File Structure\n"
    );
#endif

  Status = FindAnchorVolumeDescriptorPointer (
                                          BlockIo,
					  DiskIo,
					  AnchorPoint
                                          );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = StartMainVolumeDescriptorSequence (
                                          BlockIo,
					  DiskIo,
					  AnchorPoint,
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
			      FileSetDesc
                              );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = FindFileEntryRootDir (
                             BlockIo,
			     DiskIo,
			     PartitionDesc,
			     FileSetDesc,
			     FileEntry
                             );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = FindFileIdentifierDescriptorRootDir (
                                            BlockIo,
					    DiskIo,
					    PartitionDesc,
					    FileSetDesc,
					    FileIdentifierDesc
                                            );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

Exit:
#ifdef UDF_DEBUG
  Print (L"FindRootDirectory: Done (%r)\n", Status);
#endif

  return Status;
}

EFI_STATUS
EFIAPI
ReadDirectory (
  IN EFI_BLOCK_IO_PROTOCOL                  *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                   *DiskIo,
  IN UDF_PARTITION_DESCRIPTOR               *PartitionDesc,
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR         *ParentFileIdentifierDesc,
  OUT UDF_FILE_IDENTIFIER_DESCRIPTOR        *ReadFileIdentifierDesc,
  IN OUT UINT64                             *NextOffset,
  OUT BOOLEAN                               *ReadDone
  )
{
  EFI_STATUS                                Status;
  UDF_LONG_ALLOCATION_DESCRIPTOR            *LongAd;
  UINT32                                    Lsn;
  UINT64                                    ParentOffset;
  UINT64                                    FidLength;
  UINT64                                    Offset;
  UINT64                                    EndingPartitionOffset;
  UDF_DESCRIPTOR_TAG                        *DescriptorTag;

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
  Lsn = PartitionDesc->PartitionStartingLocation +
           LongAd->ExtentLocation.LogicalBlockNumber + 1;

  //
  // Calculate offset of the Parent FID
  //
  ParentOffset = Lsn * LOGICAL_BLOCK_SIZE;

  EndingPartitionOffset =
    (PartitionDesc->PartitionStartingLocation +
     PartitionDesc->PartitionLength) * LOGICAL_BLOCK_SIZE;

  if (!*NextOffset) {
    Offset = ParentOffset;
  } else {
    Offset = *NextOffset;
  }

  //
  // Make sure we don't across a partition boundary
  //
  if (Offset > EndingPartitionOffset) {
    Print (L"[ReadDirectory] Reached End of Partition\n");
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

  DescriptorTag = &ReadFileIdentifierDesc->DescriptorTag;

#ifdef UDF_DEBUG
  Print (
    L"[ReadDirectory] [FID] Tag Identifier: %d (0x%08x)\n",
    DescriptorTag->TagIdentifier,
    DescriptorTag->TagIdentifier
    );
#endif

  if (!IS_FID (ReadFileIdentifierDesc)) {
    goto Exit;
  }

#ifdef UDF_DEBUG
  Print (
    L"[ReadDirectory] Cur ImpUseLen: %d\n",
    ReadFileIdentifierDesc->LengthOfImplementationUse
    );

  Print (
    L"[ReadDirectory] Cur FiLen: %d\n",
    ReadFileIdentifierDesc->LengthOfFileIdentifier
    );
#endif

  FidLength = 38 + // Offset of Implementation Use field
    ReadFileIdentifierDesc->LengthOfFileIdentifier +
    ReadFileIdentifierDesc->LengthOfImplementationUse +
    (4 * ((ReadFileIdentifierDesc->LengthOfFileIdentifier +
	   ReadFileIdentifierDesc->LengthOfImplementationUse + 38 + 3) / 4) -
     (ReadFileIdentifierDesc->LengthOfFileIdentifier +
      ReadFileIdentifierDesc->LengthOfImplementationUse + 38));

#ifdef UDF_DEBUG
  Print (L"[ReadDirectory] Cur FidLength: %d\n", FidLength);
#endif

  *NextOffset   = Offset + FidLength;
  *ReadDone     = TRUE;

Exit:
#ifdef UDF_DEBUG
  Print (L"ReadDirectory: ReadDone (%d) - Status (%r)\n", *ReadDone, Status);
#endif

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
  IN UDF_PARTITION_DESCRIPTOR               *PartitionDesc,
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR         *ParentFileIdentifierDesc,
  OUT UINT64                                *Size
  )
{
  EFI_STATUS                                Status;
  UDF_FILE_IDENTIFIER_DESCRIPTOR            FileIdentifierDesc;
  UINT64                                    NextOffset;
  BOOLEAN                                   ReadDone;
  UDF_FILE_ENTRY                            FileEntry;
  UDF_LONG_ALLOCATION_DESCRIPTOR            *LongAd;
  UINT32                                    Lsn;
  UINT64                                    Offset;

  NextOffset   = 0;
  *Size        = 0;

  for (;;) {
    Status = ReadDirectory (
                        BlockIo,
			DiskIo,
		        PartitionDesc,
		        ParentFileIdentifierDesc,
		        &FileIdentifierDesc,
			&NextOffset,
			&ReadDone
			);
    if (EFI_ERROR (Status)) {
      goto Exit;
    }

    if (!ReadDone) {
      break;
    }

    if (IS_FID_PARENT_FILE (&FileIdentifierDesc)) {
      continue;
    }

    LongAd = &FileIdentifierDesc.Icb;

    Lsn = PartitionDesc->PartitionStartingLocation +
           LongAd->ExtentLocation.LogicalBlockNumber;

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

    //
    // TODO: check if ICB Tag's flags field contain all valid bits set
    //
    if (!IS_FE (&FileEntry)) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Exit;
    }

    *Size += FileEntry.InformationLength;
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

  Status = FindNsrDescriptor (
                          BlockIo,
			  DiskIo,
			  (VOID *)&NsrDescriptor
                          );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if ((NsrDescriptor.StructureType == 0) &&
      ((NsrDescriptor.StandardIdentifier[0] == 'B') &&
       (NsrDescriptor.StandardIdentifier[1] == 'E') &&
       (NsrDescriptor.StandardIdentifier[2] == 'A') &&
       (NsrDescriptor.StandardIdentifier[3] == '0') &&
       (NsrDescriptor.StandardIdentifier[4] == '1')) &&
      (NsrDescriptor.StructureVersion == 1)) {
    *Supported = TRUE;
  } else {
    *Supported = FALSE;
  }

Exit:
  return Status;
}

EFI_STATUS
DuplicatePrivateFileData (
  IN PRIVATE_UDF_FILE_DATA                   *PrivFileData,
  OUT PRIVATE_UDF_FILE_DATA                  **NewPrivFileData
  )
{
  EFI_STATUS                                 Status;

  Status = EFI_SUCCESS;

  *NewPrivFileData = AllocateZeroPool (sizeof (PRIVATE_UDF_FILE_DATA));
  if (!*NewPrivFileData) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  CopyMem (
    (VOID *)*NewPrivFileData,
    (VOID *)PrivFileData,
    sizeof (PRIVATE_UDF_FILE_DATA)
    );

Exit:
  return Status;
}
