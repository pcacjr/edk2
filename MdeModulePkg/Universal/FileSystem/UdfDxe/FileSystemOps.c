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

#pragma pack(1)

typedef struct {
  UINT8                      StandardIdentifier[5];
} UDF_STANDARD_IDENTIFIER;

#pragma pack()

#define STANDARD_IDENTIFIERS_NO   3

UDF_STANDARD_IDENTIFIER gUdfStandardIdentifiers[STANDARD_IDENTIFIERS_NO] = {
  { 'B', 'E', 'A', '0', '1' },
  { 'N', 'S', 'R', '0', '2' },
  { 'T', 'E', 'A', '0', '1' },
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
    128
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
  UINT32                                 Lsn;
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
				    StrLen (PrivFileData->AbsoluteFileName) : 0) +
				   StrLen (FileName)) *
				  sizeof (CHAR16) +
				  sizeof (CHAR16) +
				  sizeof (CHAR16)
                                   );
    if (!NewPrivFileData->AbsoluteFileName) {
      Status = EFI_OUT_OF_RESOURCES;
      goto Exit;
    }

    NewPrivFileData->AbsoluteFileName[0] = '\0';
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

    Lsn = PartitionStartingLocation +
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
  EFI_TPL                                OldTpl;
  EFI_STATUS                             Status;
  PRIVATE_UDF_FILE_DATA                  *PrivFileData;
  UINT32                                 PartitionStartingLocation;
  UINT32                                 PartitionLength;
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
  UDF_FILE_IDENTIFIER_DESCRIPTOR         FileIdentifierDesc;
  BOOLEAN                                ReadDone;
  UINTN                                  FileInfoLength;
  EFI_FILE_INFO                          *FileInfo;
  CHAR16                                 *FileName;
  UDF_LONG_ALLOCATION_DESCRIPTOR         *LongAd;
  UINT32                                 Lsn;
  UINT64                                 Offset;
  UDF_FILE_ENTRY                         FileEntry;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  if ((!This) || (!Buffer)) {
    Status = EFI_INVALID_PARAMETER;
    goto Exit;
  }

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  PartitionStartingLocation   = PrivFileData->Partition.StartingLocation;
  PartitionLength             = PrivFileData->Partition.Length;

  if (PrivFileData->IsRootDirectory) {
    ParentFileEntry = &PrivFileData->Root.FileEntry;
    ParentFileIdentifierDesc =
            &PrivFileData->Root.FileIdentifierDesc;
  } else {
    ParentFileEntry = &PrivFileData->File.FileEntry;
    ParentFileIdentifierDesc =
            &PrivFileData->File.FileIdentifierDesc;
  }

  BlockIo                  = PrivFileData->BlockIo;
  DiskIo                   = PrivFileData->DiskIo;
  FileName                 = NULL;

  if (IS_FID_NORMAL_FILE (ParentFileIdentifierDesc)) {
    //
    // Check if the current position is beyond the EOF
    //
    if (PrivFileData->FilePosition > ParentFileEntry->InformationLength) {
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
    FilePosition = 0;

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
      ShortAd++;
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
      if (ShortAd->ExtentLength - ExtLen > BytesLeft) {
	ExtLen = BytesLeft;
      } else {
	ExtLen = ShortAd->ExtentLength - ExtLen;
      }

      Status = DiskIo->ReadDisk (
                              DiskIo,
				BlockIo->Media->MediaId,
				ExtStartOffset +
				((PartitionStartingLocation +
	                         ShortAd->ExtentPosition) * LOGICAL_BLOCK_SIZE),
				ExtLen,
				(VOID *)((UINT8 *)Buffer + BufferOffset)
				);
      if (EFI_ERROR (Status)) {
	Status = EFI_DEVICE_ERROR;
	goto Exit;
      }

      BytesLeft                    -= ExtLen;
      BufferOffset                 += ExtLen;
      PrivFileData->FilePosition   += ExtLen;
      ExtStartOffset               = 0;
      ExtLen                       = 0;
      ShortAd++;
    }

    *BufferSize = BufferOffset;
    Status = EFI_SUCCESS;
  } else if (IS_FID_DIRECTORY_FILE (ParentFileIdentifierDesc)) {
    //
    // Read next directory entry
    //
    Status = ReadDirectory (
                        BlockIo,
			DiskIo,
			PartitionStartingLocation,
			PartitionLength,
			ParentFileIdentifierDesc,
			&FileIdentifierDesc,
			&PrivFileData->NextEntryOffset,
			&ReadDone
                        );
    if (EFI_ERROR (Status)) {
      goto Exit;
    }

    if (!ReadDone) {
      //
      // Directory entry not found
      //
      *BufferSize = 0;
      Status = EFI_SUCCESS;
      goto Exit;
    }

    //
    // Set up EFI_FILE_INFO structure
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

    Lsn = PartitionStartingLocation +
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
      FileInfo->Attribute |= EFI_FILE_DIRECTORY;
    } else if (IS_FID_NORMAL_FILE (&FileIdentifierDesc)) {
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

    FileInfo->FileSize       = FileEntry.InformationLength;
    FileInfo->PhysicalSize   = FileEntry.InformationLength;

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

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  if (PrivFileData->AbsoluteFileName) {
    FreePool ((VOID *)PrivFileData->AbsoluteFileName);
  }

  if (PrivFileData->FileName) {
    FreePool ((VOID *)PrivFileData->FileName);
  }

  FreePool ((VOID *)PrivFileData);

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

  Status = EFI_SUCCESS;

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  if (PrivFileData->IsRootDirectory) {
    FileIdentifierDesc =
            &PrivFileData->Root.FileIdentifierDesc;
    FileEntry = &PrivFileData->Root.FileEntry;
  } else {
    FileIdentifierDesc = &PrivFileData->File.FileIdentifierDesc;
    FileEntry = &PrivFileData->File.FileEntry;
  }

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
  UINT32                                 PartitionStartingLocation;
  UINT32                                 PartitionLength;
  UINT32                                 PartitionAccessType;
  UDF_FILE_ENTRY                         *FileEntry;
  EFI_FILE_INFO                          *FileInfo;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *FileIdentifierDesc;
  EFI_BLOCK_IO_PROTOCOL                  *BlockIo;
  EFI_DISK_IO_PROTOCOL                   *DiskIo;
  UINTN                                  FileInfoLength;
  EFI_FILE_SYSTEM_INFO                   *FileSystemInfo;
  UINTN                                  FileSystemInfoLength;
  CHAR16                                 *String;
  CHAR16                                 *CharP;
  UINTN                                  Index;

  PrivFileData = PRIVATE_UDF_FILE_DATA_FROM_THIS (This);

  PartitionStartingLocation   = PrivFileData->Partition.StartingLocation;
  PartitionLength             = PrivFileData->Partition.Length;
  PartitionAccessType         = PrivFileData->Partition.AccessType;

  if (PrivFileData->IsRootDirectory) {
    FileEntry = &PrivFileData->Root.FileEntry;
    FileIdentifierDesc =
            &PrivFileData->Root.FileIdentifierDesc;
  } else {
    FileEntry = &PrivFileData->File.FileEntry;
    FileIdentifierDesc = &PrivFileData->File.FileIdentifierDesc;
  }

  BlockIo     = PrivFileData->BlockIo;
  DiskIo      = PrivFileData->DiskIo;

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

    //
    // Check if file has System bit set (bit 10)
    //
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

    for (Index = 0; Index < 128; Index += 2) {
      CharP = (CHAR16 *)&PrivFileData->Volume.Identifier[Index];
      if (!*CharP) {
	break;
      }

      *String++ = *CharP;
    }

    *String = '\0';

    FileSystemInfo->Size        = FileSystemInfoLength;
    FileSystemInfo->ReadOnly    = (BOOLEAN)(PartitionAccessType == 1);
    FileSystemInfo->VolumeSize  = (PartitionStartingLocation +
				   PartitionLength) * LOGICAL_BLOCK_SIZE;
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
  UDF_CHAR_SPEC                              *CharSpec;

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
  UINT32                                     Lsn;

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
  UINT32                                     Lsn;

  LongAd = &FileSetDesc->RootDirectoryIcb;

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
  UINT32                                     Lsn;

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
  UINT32                                    Lsn;
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
  Lsn = PartitionStartingLocation +
           LongAd->ExtentLocation.LogicalBlockNumber + 1;

  //
  // Calculate offset of the Parent FID
  //
  ParentOffset = Lsn * LOGICAL_BLOCK_SIZE;

  EndingPartitionOffset = (PartitionStartingLocation +
			   PartitionLength) * LOGICAL_BLOCK_SIZE;

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

EFI_STATUS
IsSupportedUdfVolume (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  OUT BOOLEAN                                *Supported
  )
{
  EFI_STATUS                                 Status;
  UDF_NSR_DESCRIPTOR                         NsrDescriptor;
  UINTN                                      Index;

  Status = FindNsrDescriptor (
                          BlockIo,
			  DiskIo,
			  (VOID *)&NsrDescriptor
                          );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  *Supported = FALSE;

  if ((NsrDescriptor.StructureType == 0) &&
      (NsrDescriptor.StructureVersion == 1)) {
    for (Index = 0; Index < STANDARD_IDENTIFIERS_NO; Index++) {
      if (CompareMem (
	    (VOID *)&NsrDescriptor.StandardIdentifier,
	    (VOID *)&gUdfStandardIdentifiers[Index],
	    5
	    )
	 )
      {
	*Supported = TRUE;
	break;
      }
    }
  }

Exit:
  return Status;
}
