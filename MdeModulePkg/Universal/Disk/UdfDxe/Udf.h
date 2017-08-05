/** @file
  UDF/ECMA-167 file system driver.

  Copyright (C) 2014-2017 Paulo Alcantara <pcacjr@zytor.com>

  This program and the accompanying materials are licensed and made available
  under the terms and conditions of the BSD License which accompanies this
  distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS, WITHOUT
  WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/

#ifndef _UDF_H_
#define _UDF_H_

#include <Uefi.h>
#include <Base.h>

#include <Protocol/BlockIo.h>
#include <Protocol/ComponentName.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/DiskIo.h>
#include <Protocol/SimpleFileSystem.h>

#include <Guid/FileInfo.h>
#include <Guid/FileSystemInfo.h>
#include <Guid/FileSystemVolumeLabelInfo.h>

#include <Library/DebugLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DevicePathLib.h>

#include <IndustryStandard/ElTorito.h>
#include <IndustryStandard/Udf.h>

//
// C5BD4D42-1A76-4996-8956-73CDA326CD0A
//
#define EFI_UDF_DEVICE_PATH_GUID                        \
  { 0xC5BD4D42, 0x1A76, 0x4996,                         \
    { 0x89, 0x56, 0x73, 0xCD, 0xA3, 0x26, 0xCD, 0x0A }  \
  }

#define UDF_DEFAULT_LV_NUM 0

#define IS_PVD(_Pointer) \
  ((BOOLEAN)(_GET_TAG_ID (_Pointer) == 1))
#define IS_PD(_Pointer) \
  ((BOOLEAN)(_GET_TAG_ID (_Pointer) == 5))
#define IS_LVD(_Pointer) \
  ((BOOLEAN)(_GET_TAG_ID (_Pointer) == 6))
#define IS_TD(_Pointer) \
  ((BOOLEAN)(_GET_TAG_ID (_Pointer) == 8))
#define IS_FSD(_Pointer) \
  ((BOOLEAN)(_GET_TAG_ID (_Pointer) == 256))
#define IS_FE(_Pointer) \
  ((BOOLEAN)(_GET_TAG_ID (_Pointer) == 261))
#define IS_EFE(_Pointer) \
  ((BOOLEAN)(_GET_TAG_ID (_Pointer) == 266))
#define IS_FID(_Pointer) \
  ((BOOLEAN)(_GET_TAG_ID (_Pointer) == 257))
#define IS_AED(_Pointer) \
  ((BOOLEAN)(_GET_TAG_ID (_Pointer) == 258))
#define IS_LVID(_Pointer) \
  ((BOOLEAN)(_GET_TAG_ID (_Pointer) == 9))

#define _GET_FILETYPE(_Pointer) \
  (IS_FE (_Pointer) ? \
   (((UDF_FILE_ENTRY *)(_Pointer))->IcbTag.FileType) \
   : \
   (((UDF_EXTENDED_FILE_ENTRY *)(_Pointer))->IcbTag.FileType))

#define IS_FE_DIRECTORY(_Pointer) \
  ((BOOLEAN)(_GET_FILETYPE (_Pointer) == 4))
#define IS_FE_STANDARD_FILE(_Pointer) \
  ((BOOLEAN)(_GET_FILETYPE (_Pointer) == 5))
#define IS_FE_SYMLINK(_Pointer) \
  ((BOOLEAN)(_GET_FILETYPE (_Pointer) == 12))

#define HIDDEN_FILE     (1 << 0)
#define DIRECTORY_FILE  (1 << 1)
#define DELETED_FILE    (1 << 2)
#define PARENT_FILE     (1 << 3)

#define _GET_FILE_CHARS(_Pointer) \
  (((UDF_FILE_IDENTIFIER_DESCRIPTOR *)(_Pointer))->FileCharacteristics)

#define IS_FID_HIDDEN_FILE(_Pointer) \
  ((BOOLEAN)(_GET_FILE_CHARS (_Pointer) & HIDDEN_FILE))
#define IS_FID_DIRECTORY_FILE(_Pointer) \
  ((BOOLEAN)(_GET_FILE_CHARS (_Pointer) & DIRECTORY_FILE))
#define IS_FID_DELETED_FILE(_Pointer) \
  ((BOOLEAN)(_GET_FILE_CHARS (_Pointer) & DELETED_FILE))
#define IS_FID_PARENT_FILE(_Pointer) \
  ((BOOLEAN)(_GET_FILE_CHARS (_Pointer) & PARENT_FILE))
#define IS_FID_NORMAL_FILE(_Pointer) \
  ((BOOLEAN)(!IS_FID_DIRECTORY_FILE (_Pointer) && \
         !IS_FID_PARENT_FILE (_Pointer)))

typedef enum {
  SHORT_ADS_SEQUENCE,
  LONG_ADS_SEQUENCE,
  EXTENDED_ADS_SEQUENCE,
  INLINE_DATA
} UDF_FE_RECORDING_FLAGS;

#define GET_FE_RECORDING_FLAGS(_Fe) \
  ((UDF_FE_RECORDING_FLAGS)((UDF_ICB_TAG *)( \
                  (UINT8 *)(_Fe) + \
                  sizeof (UDF_DESCRIPTOR_TAG)))->Flags & 0x07)

typedef enum {
  EXTENT_RECORDED_AND_ALLOCATED,
  EXTENT_NOT_RECORDED_BUT_ALLOCATED,
  EXTENT_NOT_RECORDED_NOT_ALLOCATED,
  EXTENT_IS_NEXT_EXTENT,
} UDF_EXTENT_FLAGS;

#define AD_LENGTH(_RecFlags) \
  ((_RecFlags) == SHORT_ADS_SEQUENCE ? \
   ((UINT64)(sizeof (UDF_SHORT_ALLOCATION_DESCRIPTOR))) : \
   ((UINT64)(sizeof (UDF_LONG_ALLOCATION_DESCRIPTOR))))

#define GET_EXTENT_FLAGS(_RecFlags, _Ad) \
  ((_RecFlags) == SHORT_ADS_SEQUENCE ? \
   ((UDF_EXTENT_FLAGS)((((UDF_SHORT_ALLOCATION_DESCRIPTOR *)(_Ad))->ExtentLength >> \
            30) & 0x3)) : \
   ((UDF_EXTENT_FLAGS)((((UDF_LONG_ALLOCATION_DESCRIPTOR *)(_Ad))->ExtentLength >> \
            30) & 0x3)))

#define GET_EXTENT_LENGTH(_RecFlags, _Ad) \
  ((_RecFlags) == SHORT_ADS_SEQUENCE ? \
   ((UINT32)((((UDF_SHORT_ALLOCATION_DESCRIPTOR *)(_Ad))->ExtentLength & \
          ~0xC0000000UL))) : \
   ((UINT32)((((UDF_LONG_ALLOCATION_DESCRIPTOR *)(_Ad))->ExtentLength & \
          ~0xC0000000UL))))

#define UDF_FILENAME_LENGTH  128
#define UDF_PATH_LENGTH      512

#define GET_FID_FROM_ADS(_Data, _Offs) \
  ((UDF_FILE_IDENTIFIER_DESCRIPTOR *)((UINT8 *)(_Data) + (_Offs)))

#define IS_VALID_COMPRESSION_ID(_CompId) \
  ((BOOLEAN)((_CompId) == 8 || (_CompId) == 16))

#define LV_BLOCK_SIZE(_Vol, _LvNum) \
  (_Vol)->LogicalVolDescs[(_LvNum)]->LogicalBlockSize

#define UDF_STANDARD_IDENTIFIER_LENGTH   5

#define LV_UDF_REVISION(_Lv) \
  *(UINT16 *)(UINTN)(_Lv)->DomainIdentifier.IdentifierSuffix

#pragma pack(1)

typedef struct {
  UINT8 StandardIdentifier[UDF_STANDARD_IDENTIFIER_LENGTH];
} UDF_STANDARD_IDENTIFIER;

#pragma pack()

typedef enum {
  READ_FILE_GET_FILESIZE,
  READ_FILE_ALLOCATE_AND_READ,
  READ_FILE_SEEK_AND_READ,
} UDF_READ_FILE_FLAGS;

typedef struct {
  VOID                 *FileData;
  UDF_READ_FILE_FLAGS  Flags;
  UINT64               FileDataSize;
  UINT64               FilePosition;
  UINT64               FileSize;
  UINT64               ReadLength;
} UDF_READ_FILE_INFO;

#pragma pack(1)

typedef struct {
  UINT8           CharacterSetType;
  UINT8           CharacterSetInfo[63];
} UDF_CHAR_SPEC;

typedef struct {
  UINT8           Flags;
  UINT8           Identifier[23];
  UINT8           IdentifierSuffix[8];
} UDF_ENTITY_ID;

typedef struct {
  UINT16          TypeAndTimezone;
  INT16           Year;
  UINT8           Month;
  UINT8           Day;
  UINT8           Hour;
  UINT8           Minute;
  UINT8           Second;
  UINT8           Centiseconds;
  UINT8           HundredsOfMicroseconds;
  UINT8           Microseconds;
} UDF_TIMESTAMP;

typedef struct {
  UINT32        LogicalBlockNumber;
  UINT16        PartitionReferenceNumber;
} UDF_LB_ADDR;

typedef struct {
  UINT32                           ExtentLength;
  UDF_LB_ADDR                      ExtentLocation;
  UINT8                            ImplementationUse[6];
} UDF_LONG_ALLOCATION_DESCRIPTOR;

typedef struct {
  UDF_DESCRIPTOR_TAG                 DescriptorTag;
  UINT32                             PrevAllocationExtentDescriptor;
  UINT32                             LengthOfAllocationDescriptors;
} UDF_ALLOCATION_EXTENT_DESCRIPTOR;

typedef struct {
  UINT8                   StructureType;
  UINT8                   StandardIdentifier[UDF_STANDARD_IDENTIFIER_LENGTH];
  UINT8                   StructureVersion;
  UINT8                   Reserved;
  UINT8                   StructureData[2040];
} UDF_VOLUME_DESCRIPTOR;

typedef struct {
  UDF_DESCRIPTOR_TAG         DescriptorTag;
  UINT32                     VolumeDescriptorSequenceNumber;
  UINT16                     PartitionFlags;
  UINT16                     PartitionNumber;
  UDF_ENTITY_ID              PartitionContents;
  UINT8                      PartitionContentsUse[128];
  UINT32                     AccessType;
  UINT32                     PartitionStartingLocation;
  UINT32                     PartitionLength;
  UDF_ENTITY_ID              ImplementationIdentifier;
  UINT8                      ImplementationUse[128];
  UINT8                      Reserved[156];
} UDF_PARTITION_DESCRIPTOR;

typedef struct {
  UDF_DESCRIPTOR_TAG              DescriptorTag;
  UINT32                          VolumeDescriptorSequenceNumber;
  UDF_CHAR_SPEC                   DescriptorCharacterSet;
  UINT8                           LogicalVolumeIdentifier[128];
  UINT32                          LogicalBlockSize;
  UDF_ENTITY_ID                   DomainIdentifier;
  UDF_LONG_ALLOCATION_DESCRIPTOR  LogicalVolumeContentsUse;
  UINT32                          MapTableLength;
  UINT32                          NumberOfPartitionMaps;
  UDF_ENTITY_ID                   ImplementationIdentifier;
  UINT8                           ImplementationUse[128];
  UDF_EXTENT_AD                   IntegritySequenceExtent;
  UINT8                           PartitionMaps[6];
} UDF_LOGICAL_VOLUME_DESCRIPTOR;

typedef struct {
  UDF_DESCRIPTOR_TAG             DescriptorTag;
  UDF_TIMESTAMP                  RecordingDateTime;
  UINT32                         IntegrityType;
  UDF_EXTENT_AD                  NextIntegrityExtent;
  UINT8                          LogicalVolumeContentsUse[32];
  UINT32                         NumberOfPartitions;
  UINT32                         LengthOfImplementationUse;
  UINT8                          Data[0];
} UDF_LOGICAL_VOLUME_INTEGRITY;

typedef struct {
  UDF_DESCRIPTOR_TAG              DescriptorTag;
  UDF_TIMESTAMP                   RecordingDateAndTime;
  UINT16                          InterchangeLevel;
  UINT16                          MaximumInterchangeLevel;
  UINT32                          CharacterSetList;
  UINT32                          MaximumCharacterSetList;
  UINT32                          FileSetNumber;
  UINT32                          FileSetDescriptorNumber;
  UDF_CHAR_SPEC                   LogicalVolumeIdentifierCharacterSet;
  UINT8                           LogicalVolumeIdentifier[128];
  UDF_CHAR_SPEC                   FileSetCharacterSet;
  UINT8                           FileSetIdentifier[32];
  UINT8                           CopyrightFileIdentifier[32];
  UINT8                           AbstractFileIdentifier[32];
  UDF_LONG_ALLOCATION_DESCRIPTOR  RootDirectoryIcb;
  UDF_ENTITY_ID                   DomainIdentifier;
  UDF_LONG_ALLOCATION_DESCRIPTOR  NextExtent;
  UDF_LONG_ALLOCATION_DESCRIPTOR  SystemStreamDirectoryIcb;
  UINT8                           Reserved[32];
} UDF_FILE_SET_DESCRIPTOR;

typedef struct {
  UINT32                            ExtentLength;
  UINT32                            ExtentPosition;
} UDF_SHORT_ALLOCATION_DESCRIPTOR;

typedef struct {
  UDF_DESCRIPTOR_TAG               DescriptorTag;
  UINT16                           FileVersionNumber;
  UINT8                            FileCharacteristics;
  UINT8                            LengthOfFileIdentifier;
  UDF_LONG_ALLOCATION_DESCRIPTOR   Icb;
  UINT16                           LengthOfImplementationUse;
  UINT8                            Data[0];
} UDF_FILE_IDENTIFIER_DESCRIPTOR;

typedef struct {
  UINT32        PriorRecordNumberOfDirectEntries;
  UINT16        StrategyType;
  UINT16        StrategyParameter;
  UINT16        MaximumNumberOfEntries;
  UINT8         Reserved;
  UINT8         FileType;
  UDF_LB_ADDR   ParentIcbLocation;
  UINT16        Flags;
} UDF_ICB_TAG;

typedef struct {
  UDF_DESCRIPTOR_TAG              DescriptorTag;
  UDF_ICB_TAG                     IcbTag;
  UINT32                          Uid;
  UINT32                          Gid;
  UINT32                          Permissions;
  UINT16                          FileLinkCount;
  UINT8                           RecordFormat;
  UINT8                           RecordDisplayAttributes;
  UINT32                          RecordLength;
  UINT64                          InformationLength;
  UINT64                          LogicalBlocksRecorded;
  UDF_TIMESTAMP                   AccessTime;
  UDF_TIMESTAMP                   ModificationTime;
  UDF_TIMESTAMP                   AttributeTime;
  UINT32                          CheckPoint;
  UDF_LONG_ALLOCATION_DESCRIPTOR  ExtendedAttributeIcb;
  UDF_ENTITY_ID                   ImplementationIdentifier;
  UINT64                          UniqueId;
  UINT32                          LengthOfExtendedAttributes;
  UINT32                          LengthOfAllocationDescriptors;
  UINT8                           Data[0]; // L_EA + L_AD
} UDF_FILE_ENTRY;

typedef struct {
  UDF_DESCRIPTOR_TAG              DescriptorTag;
  UDF_ICB_TAG                     IcbTag;
  UINT32                          Uid;
  UINT32                          Gid;
  UINT32                          Permissions;
  UINT16                          FileLinkCount;
  UINT8                           RecordFormat;
  UINT8                           RecordDisplayAttributes;
  UINT32                          RecordLength;
  UINT64                          InformationLength;
  UINT64                          ObjectSize;
  UINT64                          LogicalBlocksRecorded;
  UDF_TIMESTAMP                   AccessTime;
  UDF_TIMESTAMP                   ModificationTime;
  UDF_TIMESTAMP                   CreationTime;
  UDF_TIMESTAMP                   AttributeTime;
  UINT32                          CheckPoint;
  UINT32                          Reserved;
  UDF_LONG_ALLOCATION_DESCRIPTOR  ExtendedAttributeIcb;
  UDF_LONG_ALLOCATION_DESCRIPTOR  StreamDirectoryIcb;
  UDF_ENTITY_ID                   ImplementationIdentifier;
  UINT64                          UniqueId;
  UINT32                          LengthOfExtendedAttributes;
  UINT32                          LengthOfAllocationDescriptors;
  UINT8                           Data[0]; // L_EA + L_AD
} UDF_EXTENDED_FILE_ENTRY;

typedef struct {
  UINT8                ComponentType;
  UINT8                LengthOfComponentIdentifier;
  UINT16               ComponentFileVersionNumber;
  UINT8                ComponentIdentifier[0];
} UDF_PATH_COMPONENT;

#pragma pack()

//
// UDF filesystem driver's private data
//
typedef struct {
  UDF_LOGICAL_VOLUME_DESCRIPTOR  **LogicalVolDescs;
  UINTN                          LogicalVolDescsNo;
  UDF_PARTITION_DESCRIPTOR       **PartitionDescs;
  UINTN                          PartitionDescsNo;
  UDF_FILE_SET_DESCRIPTOR        **FileSetDescs;
  UINTN                          FileSetDescsNo;
  UINTN                          FileEntrySize;
} UDF_VOLUME_INFO;

typedef struct {
  VOID                            *FileEntry;
  UDF_FILE_IDENTIFIER_DESCRIPTOR  *FileIdentifierDesc;
} UDF_FILE_INFO;

typedef struct {
  VOID                      *DirectoryData;
  UINT64                    DirectoryLength;
  UINT64                    FidOffset;
} UDF_READ_DIRECTORY_INFO;

#define PRIVATE_UDF_FILE_DATA_SIGNATURE SIGNATURE_32 ('U', 'd', 'f', 'f')

#define PRIVATE_UDF_FILE_DATA_FROM_THIS(a) \
  CR ( \
      a, \
      PRIVATE_UDF_FILE_DATA, \
      FileIo, \
      PRIVATE_UDF_FILE_DATA_SIGNATURE \
      )

typedef struct {
  UINTN                            Signature;
  BOOLEAN                          IsRootDirectory;
  UDF_FILE_INFO                    *Root;
  UDF_FILE_INFO                    File;
  UDF_READ_DIRECTORY_INFO          ReadDirInfo;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *SimpleFs;
  EFI_FILE_PROTOCOL                FileIo;
  CHAR16                           AbsoluteFileName[UDF_PATH_LENGTH];
  CHAR16                           FileName[UDF_FILENAME_LENGTH];
  UINT64                           FileSize;
  UINT64                           FilePosition;
} PRIVATE_UDF_FILE_DATA;

#define PRIVATE_UDF_SIMPLE_FS_DATA_SIGNATURE SIGNATURE_32 ('U', 'd', 'f', 's')

#define PRIVATE_UDF_SIMPLE_FS_DATA_FROM_THIS(a) \
  CR ( \
      a, \
      PRIVATE_UDF_SIMPLE_FS_DATA, \
      SimpleFs, \
      PRIVATE_UDF_SIMPLE_FS_DATA_SIGNATURE \
      )

typedef struct {
  UINTN                            Signature;
  EFI_BLOCK_IO_PROTOCOL            *BlockIo;
  EFI_DISK_IO_PROTOCOL             *DiskIo;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  SimpleFs;
  UDF_VOLUME_INFO                  Volume;
  UDF_FILE_INFO                    Root;
  UINTN                            OpenFiles;
  EFI_HANDLE                       Handle;
} PRIVATE_UDF_SIMPLE_FS_DATA;

//
// Global Variables
//
extern EFI_DRIVER_BINDING_PROTOCOL   gUdfDriverBinding;
extern EFI_COMPONENT_NAME_PROTOCOL   gUdfComponentName;
extern EFI_COMPONENT_NAME2_PROTOCOL  gUdfComponentName2;

//
// Function Prototypes
//

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
  );

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
  );

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
  );

/**
  Close the file handle.

  @param  This Protocol instance pointer.

  @retval EFI_SUCCESS The file was closed.

**/
EFI_STATUS
EFIAPI
UdfClose (
  IN EFI_FILE_PROTOCOL *This
  );

/**
  Close and delete the file handle.

  @param  This Protocol instance pointer.

  @retval EFI_SUCCESS              The file was closed and deleted.
  @retval EFI_WARN_DELETE_FAILURE  The handle was closed but the file was not
                                   deleted.

**/
EFI_STATUS
EFIAPI
UdfDelete (
  IN EFI_FILE_PROTOCOL  *This
  );

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
  );

/**
  Get file's current position.

  @param  This      Protocol instance pointer.
  @param  Position  Byte position from the start of the file.

  @retval EFI_SUCCESS     Position was updated.
  @retval EFI_UNSUPPORTED Seek request for directories is not valid.

**/
EFI_STATUS
EFIAPI
UdfGetPosition (
  IN   EFI_FILE_PROTOCOL  *This,
  OUT  UINT64             *Position
  );

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
  );

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
  );

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
  );

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
  );

/**
  Read volume information on a medium which contains a valid UDF file system.

  @param[in]   BlockIo  BlockIo interface.
  @param[in]   DiskIo   DiskIo interface.
  @param[out]  Volume   UDF volume information structure.

  @retval EFI_SUCCESS          Volume information read.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The volume was not read due to lack of resources.

**/
EFI_STATUS
ReadUdfVolumeInformation (
  IN   EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL   *DiskIo,
  OUT  UDF_VOLUME_INFO        *Volume
  );

/**
  Find the root directory on an UDF volume.

  @param[in]   BlockIo  BlockIo interface.
  @param[in]   DiskIo   DiskIo interface.
  @param[in]   Volume   UDF volume information structure.
  @param[out]  File     Root directory file.

  @retval EFI_SUCCESS          Root directory found.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The root directory was not found due to lack of
                               resources.

**/
EFI_STATUS
FindRootDirectory (
  IN   EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL   *DiskIo,
  IN   UDF_VOLUME_INFO        *Volume,
  OUT  UDF_FILE_INFO          *File
  );

/**
  Find either a File Entry or a Extended File Entry from a given ICB.

  @param[in]   BlockIo    BlockIo interface.
  @param[in]   DiskIo     DiskIo interface.
  @param[in]   Volume     UDF volume information structure.
  @param[in]   Icb        ICB of the FID.
  @param[out]  FileEntry  File Entry or Extended File Entry.

  @retval EFI_SUCCESS          File Entry or Extended File Entry found.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The FE/EFE entry was not found due to lack of
                               resources.

**/
EFI_STATUS
FindFileEntry (
  IN   EFI_BLOCK_IO_PROTOCOL           *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL            *DiskIo,
  IN   UDF_VOLUME_INFO                 *Volume,
  IN   UDF_LONG_ALLOCATION_DESCRIPTOR  *Icb,
  OUT  VOID                            **FileEntry
  );

/**
  Find a file given its absolute path on an UDF volume.

  @param[in]   BlockIo   BlockIo interface.
  @param[in]   DiskIo    DiskIo interface.
  @param[in]   Volume    UDF volume information structure.
  @param[in]   FilePath  File's absolute path.
  @param[in]   Root      Root directory file.
  @param[in]   Parent    Parent directory file.
  @param[out]  File      Found file.

  @retval EFI_SUCCESS          @p FilePath was found.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The @p FilePath file was not found due to lack of
                               resources.

**/
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
  );

/**
  Read a directory entry at a time on an UDF volume.

  @param[in]      BlockIo        BlockIo interface.
  @param[in]      DiskIo         DiskIo interface.
  @param[in]      Volume         UDF volume information structure.
  @param[in]      ParentIcb      ICB of the parent file.
  @param[in]      FileEntryData  FE/EFE of the parent file.
  @param[in out]  ReadDirInfo    Next read directory listing structure
                                 information.
  @param[out]     FoundFid       File Identifier Descriptor pointer.

  @retval EFI_SUCCESS          Directory entry read.
  @retval EFI_UNSUPPORTED      Extended Allocation Descriptors not supported.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The directory entry was not read due to lack of
                               resources.

**/
EFI_STATUS
ReadDirectoryEntry (
  IN      EFI_BLOCK_IO_PROTOCOL           *BlockIo,
  IN      EFI_DISK_IO_PROTOCOL            *DiskIo,
  IN      UDF_VOLUME_INFO                 *Volume,
  IN      UDF_LONG_ALLOCATION_DESCRIPTOR  *ParentIcb,
  IN      VOID                            *FileEntryData,
  IN OUT  UDF_READ_DIRECTORY_INFO         *ReadDirInfo,
  OUT     UDF_FILE_IDENTIFIER_DESCRIPTOR  **FoundFid
  );

/**
  Get a filename (encoded in OSTA-compressed format) from a File Identifier
  Descriptor on an UDF volume.

  @param[in]   FileIdentifierDesc  File Identifier Descriptor pointer.
  @param[out]  FileName            Decoded filename.

  @retval EFI_SUCCESS           Filename decoded and read.
  @retval EFI_VOLUME_CORRUPTED  The file system structures are corrupted.
**/
EFI_STATUS
GetFileNameFromFid (
  IN   UDF_FILE_IDENTIFIER_DESCRIPTOR  *FileIdentifierDesc,
  OUT  CHAR16                          *FileName
  );

/**
  Resolve a symlink file on an UDF volume.

  @param[in]   BlockIo        BlockIo interface.
  @param[in]   DiskIo         DiskIo interface.
  @param[in]   Volume         UDF volume information structure.
  @param[in]   Parent         Parent file.
  @param[in]   FileEntryData  FE/EFE structure pointer.
  @param[out]  File           Resolved file.

  @retval EFI_SUCCESS          Symlink file resolved.
  @retval EFI_UNSUPPORTED      Extended Allocation Descriptors not supported.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The symlink file was not resolved due to lack of
                               resources.

**/
EFI_STATUS
ResolveSymlink (
  IN   EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL   *DiskIo,
  IN   UDF_VOLUME_INFO        *Volume,
  IN   UDF_FILE_INFO          *Parent,
  IN   VOID                   *FileEntryData,
  OUT  UDF_FILE_INFO          *File
  );

/**
  Clean up in-memory UDF volume information.

  @param[in] Volume Volume information pointer.

**/
VOID
CleanupVolumeInformation (
  IN UDF_VOLUME_INFO *Volume
  );

/**
  Clean up in-memory UDF file information.

  @param[in] File File information pointer.

**/
VOID
CleanupFileInformation (
  IN UDF_FILE_INFO *File
  );

/**
  Find a file from its absolute path on an UDF volume.

  @param[in]   BlockIo  BlockIo interface.
  @param[in]   DiskIo   DiskIo interface.
  @param[in]   Volume   UDF volume information structure.
  @param[in]   File     File information structure.
  @param[out]  Size     Size of the file.

  @retval EFI_SUCCESS          File size calculated and set in @p Size.
  @retval EFI_UNSUPPORTED      Extended Allocation Descriptors not supported.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The file size was not calculated due to lack of
                               resources.

**/
EFI_STATUS
GetFileSize (
  IN   EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL   *DiskIo,
  IN   UDF_VOLUME_INFO        *Volume,
  IN   UDF_FILE_INFO          *File,
  OUT  UINT64                 *Size
  );

/**
  Set information about a file on an UDF volume.

  @param[in]      File        File pointer.
  @param[in]      FileSize    Size of the file.
  @param[in]      FileName    Filename of the file.
  @param[in out]  BufferSize  Size of the returned file infomation.
  @param[out]     Buffer      Data of the returned file information.

  @retval EFI_SUCCESS          File information set.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The file information was not set due to lack of
                               resources.

**/
EFI_STATUS
SetFileInfo (
  IN      UDF_FILE_INFO  *File,
  IN      UINT64         FileSize,
  IN      CHAR16         *FileName,
  IN OUT  UINTN          *BufferSize,
  OUT     VOID           *Buffer
  );

/**
  Get volume and free space size information of an UDF volume.

  @param[in]   BlockIo        BlockIo interface.
  @param[in]   DiskIo         DiskIo interface.
  @param[in]   Volume         UDF volume information structure.
  @param[out]  VolumeSize     Volume size.
  @param[out]  FreeSpaceSize  Free space size.

  @retval EFI_SUCCESS          Volume and free space size calculated.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The volume and free space size were not
                               calculated due to lack of resources.

**/
EFI_STATUS
GetVolumeSize (
  IN   EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL   *DiskIo,
  IN   UDF_VOLUME_INFO        *Volume,
  OUT  UINT64                 *VolumeSize,
  OUT  UINT64                 *FreeSpaceSize
  );

/**
  Seek a file and read its data into memory on an UDF volume.

  @param[in]      BlockIo       BlockIo interface.
  @param[in]      DiskIo        DiskIo interface.
  @param[in]      Volume        UDF volume information structure.
  @param[in]      File          File information structure.
  @param[in]      FileSize      Size of the file.
  @param[in out]  FilePosition  File position.
  @param[in out]  Buffer        File data.
  @param[in out]  BufferSize    Read size.

  @retval EFI_SUCCESS          File seeked and read.
  @retval EFI_UNSUPPORTED      Extended Allocation Descriptors not supported.
  @retval EFI_NO_MEDIA         The device has no media.
  @retval EFI_DEVICE_ERROR     The device reported an error.
  @retval EFI_VOLUME_CORRUPTED The file system structures are corrupted.
  @retval EFI_OUT_OF_RESOURCES The file's recorded data was not read due to lack
                               of resources.

**/
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
  );

/**
  Check if ControllerHandle supports an UDF file system.

  @param[in]  This                Protocol instance pointer.
  @param[in]  ControllerHandle    Handle of device to test.

  @retval EFI_SUCCESS             UDF file system found.
  @retval EFI_UNSUPPORTED         UDF file system not found.

**/
EFI_STATUS
SupportUdfFileSystem (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle
  );

/**
  Mangle a filename by cutting off trailing whitespaces, "\\", "." and "..".

  @param[in] FileName Filename.

  @retval @p FileName Filename mangled.

**/
CHAR16 *
MangleFileName (
  IN CHAR16        *FileName
  );

/**
  Test to see if this driver supports ControllerHandle. Any ControllerHandle
  than contains a BlockIo and DiskIo protocol can be supported.

  @param  This                Protocol instance pointer.
  @param  ControllerHandle    Handle of device to test
  @param  RemainingDevicePath Optional parameter use to pick a specific child
                              device to start.

  @retval EFI_SUCCESS         This driver supports this device
  @retval EFI_ALREADY_STARTED This driver is already running on this device
  @retval other               This driver does not support this device

**/
EFI_STATUS
EFIAPI
UdfDriverBindingSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  );

/**
  Start this driver on ControllerHandle by opening a Block IO and Disk IO
  protocol, reading Device Path, and creating a child handle with a
  Disk IO and device path protocol.

  @param  This                 Protocol instance pointer.
  @param  ControllerHandle     Handle of device to bind driver to
  @param  RemainingDevicePath  Optional parameter use to pick a specific child
                               device to start.

  @retval EFI_SUCCESS          This driver is added to ControllerHandle
  @retval EFI_ALREADY_STARTED  This driver is already running on ControllerHandle
  @retval other                This driver does not support this device

**/
EFI_STATUS
EFIAPI
UdfDriverBindingStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  );

/**
  Stop this driver on ControllerHandle. Support stopping any child handles
  created by this driver.

  @param  This              Protocol instance pointer.
  @param  ControllerHandle  Handle of device to stop driver on
  @param  NumberOfChildren  Number of Handles in ChildHandleBuffer. If number of
                            children is zero stop the entire bus driver.
  @param  ChildHandleBuffer List of Child Handles to Stop.

  @retval EFI_SUCCESS       This driver is removed ControllerHandle
  @retval other             This driver was not removed from this device

**/
EFI_STATUS
EFIAPI
UdfDriverBindingStop (
  IN  EFI_DRIVER_BINDING_PROTOCOL   *This,
  IN  EFI_HANDLE                    ControllerHandle,
  IN  UINTN                         NumberOfChildren,
  IN  EFI_HANDLE                    *ChildHandleBuffer
  );

//
// EFI Component Name Functions
//
/**
  Retrieves a Unicode string that is the user readable name of the driver.

  This function retrieves the user readable name of a driver in the form of a
  Unicode string. If the driver specified by This has a user readable name in
  the language specified by Language, then a pointer to the driver name is
  returned in DriverName, and EFI_SUCCESS is returned. If the driver specified
  by This does not support the language specified by Language,
  then EFI_UNSUPPORTED is returned.

  @param  This[in]              A pointer to the EFI_COMPONENT_NAME2_PROTOCOL or
                                EFI_COMPONENT_NAME_PROTOCOL instance.

  @param  Language[in]          A pointer to a Null-terminated ASCII string
                                array indicating the language. This is the
                                language of the driver name that the caller is
                                requesting, and it must match one of the
                                languages specified in SupportedLanguages. The
                                number of languages supported by a driver is up
                                to the driver writer. Language is specified
                                in RFC 4646 or ISO 639-2 language code format.

  @param  DriverName[out]       A pointer to the Unicode string to return.
                                This Unicode string is the name of the
                                driver specified by This in the language
                                specified by Language.

  @retval EFI_SUCCESS           The Unicode string for the Driver specified by
                                This and the language specified by Language was
                                returned in DriverName.

  @retval EFI_INVALID_PARAMETER Language is NULL.

  @retval EFI_INVALID_PARAMETER DriverName is NULL.

  @retval EFI_UNSUPPORTED       The driver specified by This does not support
                                the language specified by Language.

**/
EFI_STATUS
EFIAPI
UdfComponentNameGetDriverName (
  IN  EFI_COMPONENT_NAME_PROTOCOL  *This,
  IN  CHAR8                        *Language,
  OUT CHAR16                       **DriverName
  );

/**
  Retrieves a Unicode string that is the user readable name of the controller
  that is being managed by a driver.

  This function retrieves the user readable name of the controller specified by
  ControllerHandle and ChildHandle in the form of a Unicode string. If the
  driver specified by This has a user readable name in the language specified by
  Language, then a pointer to the controller name is returned in ControllerName,
  and EFI_SUCCESS is returned.  If the driver specified by This is not currently
  managing the controller specified by ControllerHandle and ChildHandle,
  then EFI_UNSUPPORTED is returned.  If the driver specified by This does not
  support the language specified by Language, then EFI_UNSUPPORTED is returned.

  @param  This[in]              A pointer to the EFI_COMPONENT_NAME2_PROTOCOL or
                                EFI_COMPONENT_NAME_PROTOCOL instance.

  @param  ControllerHandle[in]  The handle of a controller that the driver
                                specified by This is managing.  This handle
                                specifies the controller whose name is to be
                                returned.

  @param  ChildHandle[in]       The handle of the child controller to retrieve
                                the name of.  This is an optional parameter that
                                may be NULL.  It will be NULL for device
                                drivers.  It will also be NULL for a bus drivers
                                that wish to retrieve the name of the bus
                                controller.  It will not be NULL for a bus
                                driver that wishes to retrieve the name of a
                                child controller.

  @param  Language[in]          A pointer to a Null-terminated ASCII string
                                array indicating the language.  This is the
                                language of the driver name that the caller is
                                requesting, and it must match one of the
                                languages specified in SupportedLanguages. The
                                number of languages supported by a driver is up
                                to the driver writer. Language is specified in
                                RFC 4646 or ISO 639-2 language code format.

  @param  ControllerName[out]   A pointer to the Unicode string to return.
                                This Unicode string is the name of the
                                controller specified by ControllerHandle and
                                ChildHandle in the language specified by
                                Language from the point of view of the driver
                                specified by This.

  @retval EFI_SUCCESS           The Unicode string for the user readable name in
                                the language specified by Language for the
                                driver specified by This was returned in
                                DriverName.

  @retval EFI_INVALID_PARAMETER ControllerHandle is not a valid EFI_HANDLE.

  @retval EFI_INVALID_PARAMETER ChildHandle is not NULL and it is not a valid
                                EFI_HANDLE.

  @retval EFI_INVALID_PARAMETER Language is NULL.

  @retval EFI_INVALID_PARAMETER ControllerName is NULL.

  @retval EFI_UNSUPPORTED       The driver specified by This is not currently
                                managing the controller specified by
                                ControllerHandle and ChildHandle.

  @retval EFI_UNSUPPORTED       The driver specified by This does not support
                                the language specified by Language.

**/
EFI_STATUS
EFIAPI
UdfComponentNameGetControllerName (
  IN   EFI_COMPONENT_NAME_PROTOCOL  *This,
  IN   EFI_HANDLE                   ControllerHandle,
  IN   EFI_HANDLE                   ChildHandle OPTIONAL,
  IN   CHAR8                        *Language,
  OUT  CHAR16                       **ControllerName
  );

#endif // _UDF_H_
