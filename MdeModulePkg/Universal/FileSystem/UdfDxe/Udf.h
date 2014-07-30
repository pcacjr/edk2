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

#ifndef _UDF_H_
#define _UDF_H_

#include <Uefi.h>

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

#define UDF_DEBUG

//
// As specified in ECMA-167 specification, the logical sector size and logical
// block size shall be 2048 bytes.
//
#define LOGICAL_SECTOR_SIZE                   0x800
#define LOGICAL_BLOCK_SIZE                    0x800

#define FIRST_ANCHOR_POINT_LSN                ((UINT64)0x0000000000000100ULL)
#define NSR_DESCRIPTOR_LSN                    ((UINT64)0x0000000000000013ULL)

#define IS_PVD(_Pointer) \
  (((UDF_DESCRIPTOR_TAG *)(_Pointer))->TagIdentifier == 1)
#define IS_AVDP(_Pointer) \
  (((UDF_DESCRIPTOR_TAG *)(_Pointer))->TagIdentifier == 2)
#define IS_PD(_Pointer) \
  (((UDF_DESCRIPTOR_TAG *)(_Pointer))->TagIdentifier == 5)
#define IS_LVD(_Pointer) \
  (((UDF_DESCRIPTOR_TAG *)(_Pointer))->TagIdentifier == 6)
#define IS_FSD(_Pointer) \
  (((UDF_DESCRIPTOR_TAG *)(_Pointer))->TagIdentifier == 256)
#define IS_FE(_Pointer) \
  (((UDF_DESCRIPTOR_TAG *)(_Pointer))->TagIdentifier == 261)
#define IS_FID(_Pointer) \
  (((UDF_DESCRIPTOR_TAG *)(_Pointer))->TagIdentifier == 257)

#define IS_FE_DIRECTORY(_Pointer) \
  (((UDF_FILE_ENTRY *)(_Pointer))->IcbTag.FileType == 4)
#define IS_FE_STANDARD_FILE(_Pointer) \
  (((UDF_FILE_ENTRY *)(_Pointer))->IcbTag.FileType == 5)

#define HIDDEN_FILE                           (1 << 0)
#define DIRECTORY_FILE                        (1 << 1)
#define DELETED_FILE                          (1 << 2)
#define PARENT_FILE                           (1 << 3)

#define IS_FID_HIDDEN_FILE(_Pointer) \
  (((UDF_FILE_IDENTIFIER_DESCRIPTOR *)(_Pointer))->FileCharacteristics & \
   HIDDEN_FILE)
#define IS_FID_DIRECTORY_FILE(_Pointer) \
  (((UDF_FILE_IDENTIFIER_DESCRIPTOR *)(_Pointer))->FileCharacteristics & \
   DIRECTORY_FILE)
#define IS_FID_DELETED_FILE(_Pointer) \
  (((UDF_FILE_IDENTIFIER_DESCRIPTOR *)(_Pointer))->FileCharacteristics & \
   DELETED_FILE)
#define IS_FID_PARENT_FILE(_Pointer) \
  (((UDF_FILE_IDENTIFIER_DESCRIPTOR *)(_Pointer))->FileCharacteristics & \
   PARENT_FILE)
#define IS_FID_NORMAL_FILE(_Pointer) \
  ((!IS_FID_DIRECTORY_FILE (_Pointer)) && (!IS_FID_PARENT_FILE (_Pointer)))

#pragma pack(1)

//
// UDF's Volume Structures
//
typedef struct {
  UINT16                TagIdentifier;
  UINT16                DescriptorVersion;
  UINT8                 TagChecksum;
  UINT8                 Reserved;
  UINT16                TagSerialNumber; // Ignored. Intended for disaster recovery.
  UINT16                DescriptorCRC;
  UINT16                DescriptorCRCLength;
  UINT32                TagLocation;
} UDF_DESCRIPTOR_TAG;

//
// ECMA 167 1/7.2.1
//
typedef struct {
  UINT8            CharacterSetType;
  UINT8            CharacterSetInfo[63];
} UDF_CHAR_SPEC;

//
// ECMA 167 1/7.4
//
typedef struct {
  UINT8            Flags;
  UINT8            Identifier[23];
  UINT8            IdentifierSuffix[8];
} UDF_ENTITY_ID;

//
// ECMA 167 1/7.3
//
typedef struct {
  UINT16           TypeAndTimezone;
  INT16            Year;
  UINT8            Month;
  UINT8            Day;
  UINT8            Hour;
  UINT8            Minute;
  UINT8            Second;
  UINT8            Centiseconds;
  UINT8            HundredsOfMicroseconds;
  UINT8            Microseconds;
} UDF_TIMESTAMP;

//
// ECMA 167 3/7.1
//
typedef struct {
  UINT32           ExtentLength;
  UINT32           ExtentLocation;
} UDF_EXTENT_AD;

//
// ECMA 167 4/7.1
//
typedef struct {
  UINT32         LogicalBlockNumber;
  UINT16         PartitionReferenceNumber;
} UDF_LB_ADDR;

//
// ECMA 167 4/14.14.2
//
typedef struct {
  UINT32                            ExtentLength;
  UDF_LB_ADDR                       ExtentLocation;
  UINT8                             ImplementationUse[6];
} UDF_LONG_ALLOCATION_DESCRIPTOR;

typedef struct {
  UDF_DESCRIPTOR_TAG               DescriptorTag;
  UINT32                           VolumeDescriptorSequenceNumber;
  UINT32                           PrimaryVolumeDescriptorNumber;
  UINT8                            VolumeIdentifier[32];
  UINT16                           VolumeSequenceNumber;
  UINT16                           MaximumVolumeSequenceNumber;
  UINT16                           InterchangeLevel;
  UINT16                           MaximumInterchangeLevel;
  UINT32                           CharacterSetList;
  UINT32                           MaximumCharacterSetList;
  UINT8                            VolumeSetIdentifier[128];
  UDF_CHAR_SPEC                    DescriptorCharacterSet;
  UDF_CHAR_SPEC                    ExplanatoryCharacterSet;
  UDF_EXTENT_AD                    VolumeAbstract;
  UDF_EXTENT_AD                    VolumeCopyrightNotice;
  UDF_ENTITY_ID                    ApplicationIdentifier;
  UDF_TIMESTAMP                    RecordingDateAndTime;
  UDF_ENTITY_ID                    ImplementationIdentifier;
  UINT8                            ImplementationUse[64];
  UINT32                           PredecessorVolumeDescriptorSequenceLocation;
  UINT16                           Flags;
  UINT8                            Reserved[22];
} UDF_PRIMARY_VOLUME_DESCRIPTOR;

//
// ECMA 167 3/9.1
//
typedef struct {
  UINT8                 StructureType;
  UINT8                 StandardIdentifier[5];
  UINT8                 StructureVersion;
  UINT8                 Reserved;
  UINT8                 StructureData[2040];
} UDF_NSR_DESCRIPTOR;

typedef struct {
  UDF_DESCRIPTOR_TAG                      DescriptorTag;
  UDF_EXTENT_AD                           MainVolumeDescriptorSequenceExtent;
  UDF_EXTENT_AD                           ReserveVolumeDescriptorSequenceExtent;
  UINT8                                   Reserved[480];
} UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER;

//
// ECMA 167 3/10.5
//
typedef struct {
  UDF_DESCRIPTOR_TAG          DescriptorTag;
  UINT32                      VolumeDescriptorSequenceNumber;
  UINT16                      PartitionFlags;
  UINT16                      PartitionNumber;
  UDF_ENTITY_ID               PartitionContents;
  UINT8                       PartitionContentsUse[128];
  UINT32                      AccessType;
  UINT32                      PartitionStartingLocation;
  UINT32                      PartitionLength;
  UDF_ENTITY_ID               ImplementationIdentifier;
  UINT8                       ImplementationUse[128];
  UINT8                       Reserved[156];
} UDF_PARTITION_DESCRIPTOR;

//
// ECMA 167 3/10.6
//
typedef struct {
  UDF_DESCRIPTOR_TAG               DescriptorTag;
  UINT32                           VolumeDescriptorSequenceNumber;
  UDF_CHAR_SPEC                    DescriptorCharacterSet;
  UINT8                            LogicalVolumeIdentifier[128];
  UINT32                           LogicalBlockSize;
  UDF_ENTITY_ID                    DomainIdentifier;
  UDF_LONG_ALLOCATION_DESCRIPTOR   LogicalVolumeContentsUse;
  UINT32                           MapTableLength;
  UINT32                           NumberOfPartitionMaps;
  UDF_ENTITY_ID                    ImplementationIdentifier;
  UINT8                            ImplementationUse[128];
  UDF_EXTENT_AD                    IntegritySequenceExtent;
  UINT8                            PartitionMaps[6];
} UDF_LOGICAL_VOLUME_DESCRIPTOR;

//
// ECMA 167 3/10.8
//
typedef struct {
  UDF_DESCRIPTOR_TAG                  DescriptorTag;
  UINT32                              VolumeDescriptorSequenceNumber;
  UINT32                              NumberOfAllocationDescriptors;
  UDF_EXTENT_AD                       AllocationDescriptors[0];
} UDF_UNALLOCATED_SPACE_DESCRIPTOR;

typedef struct {
  UDF_DESCRIPTOR_TAG                         DescriptorTag;
  UDF_TIMESTAMP                              RecordingDateAndTime;
  UINT32                                     IntegrityType;
  UDF_EXTENT_AD                              NextIntegrityExtent;
  UINT8                                      LogicalVolumeContentsUse[32];
  UINT32                                     NumberOfPartitions;
  UINT32                                     LengthOfImplementationUse; // L_UI
  UINT32                                     FreeSpaceTable;
  UINT32                                     SizeTable;
  UINT8                                      ImplementationUse[0]; // See 2.2.6.4
} UDF_LOGICAL_VOLUME_INTEGRITY_DESCRIPTOR;

//
// ECMA 167 3/10.4
//
typedef struct {
  UDF_DESCRIPTOR_TAG                          DescriptorTag;
  UINT32                                      VolumeDescriptorSequenceNumber;
  UDF_ENTITY_ID                               ImplementationIdentifier;
  struct {
    UDF_CHAR_SPEC    LvICharset;
    UINT8            LogicalVolumeIdentifier[128];
    UINT8            LvInfo1[36];
    UINT8            LvInfo2[36];
    UINT8            LvInfo3[36];
    UDF_ENTITY_ID    ImplementationId;
    UINT8            ImplementationUse[128];
  } LvInformation;
} UDF_IMPLEMENTATION_USE_VOLUME_DESCRIPTOR;

//
// ECMA 167 4/14.1
//
typedef struct {
  UDF_DESCRIPTOR_TAG               DescriptorTag;
  UDF_TIMESTAMP                    RecordingDateAndTime;
  UINT16                           InterchangeLevel;
  UINT16                           MaximumInterchangeLevel;
  UINT32                           CharacterSetList;
  UINT32                           MaximumCharacterSetList;
  UINT32                           FileSetNumber;
  UINT32                           FileSetDescriptorNumber;
  UDF_CHAR_SPEC                    LogicalVolumeIdentifierCharacterSet;
  UINT8                            LogicalVolumeIdentifier[128];
  UDF_CHAR_SPEC                    FileSetCharacterSet;
  UINT8                            FileSetIdentifier[32];
  UINT8                            CopyrightFileIdentifier[32];
  UINT8                            AbstractFileIdentifier[32];
  UDF_LONG_ALLOCATION_DESCRIPTOR   RootDirectoryIcb;
  UDF_ENTITY_ID                    DomainIdentifier;
  UDF_LONG_ALLOCATION_DESCRIPTOR   NextExtent;
  UDF_LONG_ALLOCATION_DESCRIPTOR   SystemStreamDirectoryIcb;
  UINT8                            Reserved[32];
} UDF_FILE_SET_DESCRIPTOR;

//
// ECMA 167 4/14.14.1
//
typedef struct {
  UINT32                             ExtentLength;
  UINT32                             ExtentPosition;
} UDF_SHORT_ALLOCATION_DESCRIPTOR;

//
// ECMA 167 4/14.3
//
typedef struct {
  UDF_SHORT_ALLOCATION_DESCRIPTOR    UnallocatedSpaceTable;
  UDF_SHORT_ALLOCATION_DESCRIPTOR    UnallocatedSpaceBitmap;
  UDF_SHORT_ALLOCATION_DESCRIPTOR    PartitionIntegrityTable;
  UDF_SHORT_ALLOCATION_DESCRIPTOR    FreedSpaceTable;
  UDF_SHORT_ALLOCATION_DESCRIPTOR    FreedSpaceBitmap;
  UINT8                              Reserved[88];
} UDF_PARTITION_HEADER_DESCRIPTOR;

//
// ECMA 167 4/14.3
//
typedef struct {
  UDF_DESCRIPTOR_TAG                DescriptorTag;
  UINT16                            FileVersionNumber;
  UINT8                             FileCharacteristics;
  UINT8                             LengthOfFileIdentifier;
  UDF_LONG_ALLOCATION_DESCRIPTOR    Icb;
  UINT16                            LengthOfImplementationUse;
  UINT8                             Data[2010];
} UDF_FILE_IDENTIFIER_DESCRIPTOR;

//
// ECMA 167 4/14.6
//
typedef struct {
  UINT32         PriorRecordNumberOfDirectEntries;
  UINT16         StrategyType;
  UINT16         StrategyParameter;
  UINT16         MaximumNumberOfEntries;
  UINT8          Reserved;
  UINT8          FileType;
  UDF_LB_ADDR    ParentIcbLocation;
  UINT16         Flags;
} UDF_ICB_TAG;

//
// ECMA 167 4/14.9
//
// NOTE: The total length of a FE shall not exceed the size of one logical block
// (2048 bytes).
//
typedef struct {
  UDF_DESCRIPTOR_TAG               DescriptorTag;
  UDF_ICB_TAG                      IcbTag;
  UINT32                           Uid;
  UINT32                           Gid;
  UINT32                           Permissions;
  UINT16                           FileLinkCount;
  UINT8                            RecordFormat;
  UINT8                            RecordDisplayAttributes;
  UINT32                           RecordLength;
  UINT64                           InformationLength;
  UINT64                           LogicalBlocksRecorded;
  UDF_TIMESTAMP                    AccessTime;
  UDF_TIMESTAMP                    ModificationTime;
  UDF_TIMESTAMP                    AttributeTime;
  UINT32                           CheckPoint;
  UDF_LONG_ALLOCATION_DESCRIPTOR   ExtendedAttributeIcb;
  UDF_ENTITY_ID                    ImplementationIdentifier;
  UINT64                           UniqueId;
  UINT32                           LengthOfExtendedAttributes;
  UINT32                           LengthOfAllocationDescriptors;
  UINT8                            Data[1872]; // L_EAs and L_ADs
} UDF_FILE_ENTRY;

//
// ECMA 167 4/14.11
//
typedef struct {
  UDF_DESCRIPTOR_TAG             DescriptorTag;
  UDF_ICB_TAG                    IcbTag;
  UINT32                         LengthOfAllocationDescriptors;
  UINT8                          AllocationDescriptors[0];
} UDF_UNALLOCATED_SPACE_ENTRY;

//
// ECMA 167 4/14.12
//
typedef struct {
  UDF_DESCRIPTOR_TAG   DescriptorTag;
  UINT32               NumberOfBits;
  UINT32               NumberOfBytes;
  UINT8                Bitmap[0];
} UDF_SPACE_BITMAP;

//
// ECMA 167 4/14.5
//
typedef struct {
  UDF_DESCRIPTOR_TAG                  DescriptorTag;
  UINT32                              PreviousAllocationExtentLocation;
  UINT32                              LengthOfAllocationDescriptors;
} UDF_ALLOCATION_EXTENT_DESCRIPTOR;

//
// ECMA 167 4/16.1
//
typedef struct {
  UINT8                 ComponentType;
  UINT8                 LengthOfComponentIdentifier;
  UINT16                ComponentFileVersionNumber;
  UINT8                 ComponentIdentifier[0];
} UDF_PATH_COMPONENT;

//
// ECMA 167 4/14.15
//
typedef struct {
  UINT64                                  UniqueId;
  UINT8                                   Reserved[24];
} UDF_LOGICAL_VOLUME_HEADER_DESCRIPTOR;

#pragma pack()

//
// UDF filesystem driver's private data
//
#define PRIVATE_UDF_FILE_DATA_SIGNATURE SIGNATURE_32 ('u', 'd', 'f', 'f')

#define PRIVATE_UDF_FILE_DATA_FROM_THIS(a) \
  CR ( \
      a, \
      PRIVATE_UDF_FILE_DATA, \
      FileIo, \
      PRIVATE_UDF_FILE_DATA_SIGNATURE \
      )

typedef struct {
  UINTN                                  Signature;

  struct {
    UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER   AnchorPoint;
    UDF_PARTITION_DESCRIPTOR               PartitionDesc;
    UDF_LOGICAL_VOLUME_DESCRIPTOR          LogicalVolDesc;
    UDF_FILE_SET_DESCRIPTOR                FileSetDesc;
    UDF_FILE_ENTRY                         FileEntry;
    UDF_FILE_IDENTIFIER_DESCRIPTOR         FileIdentifierDesc;
    UDF_FILE_IDENTIFIER_DESCRIPTOR         ParentFileIdentifierDesc;
  } UdfFileSystemData;

  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL        *SimpleFs;
  EFI_BLOCK_IO_PROTOCOL                  *BlockIo;
  EFI_DISK_IO_PROTOCOL                   *DiskIo;
  EFI_FILE_PROTOCOL                      FileIo;
  UINT64                                 FilePosition;
  CHAR16                                 *FileName;
} PRIVATE_UDF_FILE_DATA;

#define PRIVATE_UDF_SIMPLE_FS_DATA_SIGNATURE SIGNATURE_32 ('u', 'd', 'f', 's')

#define PRIVATE_UDF_SIMPLE_FS_DATA_FROM_THIS(a) \
  CR ( \
      a, \
      PRIVATE_UDF_SIMPLE_FS_DATA, \
      SimpleFs, \
      PRIVATE_UDF_SIMPLE_FS_DATA_SIGNATURE \
      )

typedef struct {
  UINTN                             Signature;
  EFI_BLOCK_IO_PROTOCOL             *BlockIo;
  EFI_DISK_IO_PROTOCOL              *DiskIo;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL   SimpleFs;
  EFI_HANDLE                        Handle;
} PRIVATE_UDF_SIMPLE_FS_DATA;

typedef struct {
  VENDOR_DEVICE_PATH         DevicePath;
  EFI_DEVICE_PATH_PROTOCOL   End;
} UDF_DEVICE_PATH;

// C5BD4D42-1A76-4996-8956-73CDA326CD0A
#define EFI_UDF_DEVICE_PATH_GUID \
  { 0xC5BD4D42, 0x1A76, 0x4996, \
    { 0x89, 0x56, 0x73, 0xCD, 0xA3, 0x26, 0xCD, 0x0A } \
  }

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
  IN EFI_SIMPLE_FILE_SYSTEM_PROTOCOL     *This,
  OUT EFI_FILE_PROTOCOL                  **Root
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
  IN  EFI_FILE_PROTOCOL                  *This,
  OUT EFI_FILE_PROTOCOL                  **NewHandle,
  IN  CHAR16                             *FileName,
  IN  UINT64                             OpenMode,
  IN  UINT64                             Attributes
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
  IN     EFI_FILE_PROTOCOL  *This,
  IN OUT UINTN              *BufferSize,
  OUT    VOID               *Buffer
  );

/**
  Close the file handle

  @param  This          Protocol instance pointer.

  @retval EFI_SUCCESS   The file was closed.

**/
EFI_STATUS
EFIAPI
UdfClose (
  IN EFI_FILE_PROTOCOL    *This
  );

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
  IN     EFI_FILE_PROTOCOL  *This,
  IN OUT UINTN              *BufferSize,
  IN     VOID               *Buffer
  );

/**
  Get a file's current position

  @param  This            Protocol instance pointer.
  @param  Position        Byte position from the start of the file.

  @retval EFI_SUCCESS     Position was updated.
  @retval EFI_UNSUPPORTED Seek request for non-zero is not valid on open.

**/
EFI_STATUS
EFIAPI
UdfGetPosition (
  IN  EFI_FILE_PROTOCOL   *This,
  OUT UINT64              *Position
  );

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
  IN     EFI_FILE_PROTOCOL  *This,
  IN     EFI_GUID           *InformationType,
  IN OUT UINTN              *BufferSize,
  OUT    VOID               *Buffer
  );

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
  IN EFI_FILE_PROTOCOL  *This
  );

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
  );

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
  );

EFI_STATUS
EFIAPI
FileIdentifierDescToFileName (
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR   *FileIdentifierDesc,
  OUT UINT16                          **FileName
  );

EFI_STATUS
EFIAPI
GetDirectorySize (
  IN EFI_BLOCK_IO_PROTOCOL                  *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                   *DiskIo,
  IN UDF_PARTITION_DESCRIPTOR               *PartitionDesc,
  IN UDF_FILE_IDENTIFIER_DESCRIPTOR         *ParentFileIdentifierDesc,
  OUT UINT64                                *Size
  );

EFI_STATUS
IsSupportedUdfVolume (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  OUT BOOLEAN                                *Supported
  );

EFI_STATUS
FindFileIdentifierDescriptorRootDir (
  IN EFI_BLOCK_IO_PROTOCOL                   *BlockIo,
  IN EFI_DISK_IO_PROTOCOL                    *DiskIo,
  IN UDF_PARTITION_DESCRIPTOR                *PartitionDesc,
  IN UDF_FILE_SET_DESCRIPTOR                 *FileSetDesc,
  OUT UDF_FILE_IDENTIFIER_DESCRIPTOR         *FileIdentifierDesc
  );

CHAR16 *
MangleFileName (
  CHAR16           *FileName
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
  IN  EFI_COMPONENT_NAME_PROTOCOL                     *This,
  IN  EFI_HANDLE                                      ControllerHandle,
  IN  EFI_HANDLE                                      ChildHandle        OPTIONAL,
  IN  CHAR8                                           *Language,
  OUT CHAR16                                          **ControllerName
  );

#endif // _UDF_H_
