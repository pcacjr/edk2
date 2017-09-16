/** @file
  Scan for an UDF file system on a formatted media.

  Copyright (C) 2014-2017 Paulo Alcantara <pcacjr@zytor.com>

  This program and the accompanying materials are licensed and made available
  under the terms and conditions of the BSD License which accompanies this
  distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS, WITHOUT
  WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/

#include "Partition.h"

//
// C5BD4D42-1A76-4996-8956-73CDA326CD0A
//
#define EFI_UDF_DEVICE_PATH_GUID                        \
  { 0xC5BD4D42, 0x1A76, 0x4996,                         \
    { 0x89, 0x56, 0x73, 0xCD, 0xA3, 0x26, 0xCD, 0x0A }  \
  }

typedef struct {
  VENDOR_DEVICE_PATH        DevicePath;
  EFI_DEVICE_PATH_PROTOCOL  End;
} UDF_DEVICE_PATH;

//
// Vendor-Defined Media Device Path for UDF file system
//
UDF_DEVICE_PATH gUdfDevicePath = {
  { { MEDIA_DEVICE_PATH, MEDIA_VENDOR_DP,
      { sizeof (VENDOR_DEVICE_PATH), 0 } },
    EFI_UDF_DEVICE_PATH_GUID
  },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE,
    { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 }
  }
};

EFI_STATUS
FindAnchorVolumeDescriptorPointer (
  IN   EFI_BLOCK_IO_PROTOCOL                 *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL                  *DiskIo,
  OUT  UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER  *AnchorPoint
  )
{
  EFI_STATUS  Status;
  UINT32      BlockSize;
  EFI_LBA     EndLBA;
  EFI_LBA     DescriptorLBAs[4];
  UINTN       Index;

  BlockSize = BlockIo->Media->BlockSize;
  EndLBA = BlockIo->Media->LastBlock;
  DescriptorLBAs[0] = 256;
  DescriptorLBAs[1] = EndLBA - 256;
  DescriptorLBAs[2] = EndLBA;
  DescriptorLBAs[3] = 512;

  for (Index = 0; Index < ARRAY_SIZE (DescriptorLBAs); Index++) {
    Status = DiskIo->ReadDisk (
      DiskIo,
      BlockIo->Media->MediaId,
      MultU64x32 (DescriptorLBAs[Index], BlockSize),
      sizeof (UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER),
      (VOID *)AnchorPoint
      );
    if (EFI_ERROR (Status)) {
      return Status;
    }
    //
    // Check if read LBA has a valid AVDP descriptor.
    //
    if (IS_AVDP (AnchorPoint)) {
      return EFI_SUCCESS;
    }
  }
  //
  // No AVDP found.
  //
  return EFI_VOLUME_CORRUPTED;
}

EFI_STATUS
FindUdfVolumeIdentifiers (
  IN EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN EFI_DISK_IO_PROTOCOL   *DiskIo
  )
{
  EFI_STATUS                            Status;
  UINT64                                Offset;
  UINT64                                EndDiskOffset;
  CDROM_VOLUME_DESCRIPTOR               VolDescriptor;
  CDROM_VOLUME_DESCRIPTOR               TerminatingVolDescriptor;

  ZeroMem ((VOID *)&TerminatingVolDescriptor, sizeof (CDROM_VOLUME_DESCRIPTOR));

  //
  // Start Volume Recognition Sequence
  //
  EndDiskOffset = MultU64x32 (BlockIo->Media->LastBlock,
                              BlockIo->Media->BlockSize);

  for (Offset = UDF_VRS_START_OFFSET; Offset < EndDiskOffset;
       Offset += UDF_LOGICAL_SECTOR_SIZE) {
    //
    // Check if block device has a Volume Structure Descriptor and an Extended
    // Area.
    //
    Status = DiskIo->ReadDisk (
      DiskIo,
      BlockIo->Media->MediaId,
      Offset,
      sizeof (CDROM_VOLUME_DESCRIPTOR),
      (VOID *)&VolDescriptor
      );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (CompareMem ((VOID *)VolDescriptor.Unknown.Id,
                    (VOID *)UDF_BEA_IDENTIFIER,
                    sizeof (VolDescriptor.Unknown.Id)) == 0) {
      break;
    }

    if ((CompareMem ((VOID *)VolDescriptor.Unknown.Id,
                     (VOID *)CDVOL_ID,
                     sizeof (VolDescriptor.Unknown.Id)) != 0) ||
        (CompareMem ((VOID *)&VolDescriptor,
                     (VOID *)&TerminatingVolDescriptor,
                     sizeof (CDROM_VOLUME_DESCRIPTOR)) == 0)) {
      return EFI_UNSUPPORTED;
    }
  }

  //
  // Look for "NSR0{2,3}" identifiers in the Extended Area.
  //
  Offset += UDF_LOGICAL_SECTOR_SIZE;
  if (Offset >= EndDiskOffset) {
    return EFI_UNSUPPORTED;
  }

  Status = DiskIo->ReadDisk (
    DiskIo,
    BlockIo->Media->MediaId,
    Offset,
    sizeof (CDROM_VOLUME_DESCRIPTOR),
    (VOID *)&VolDescriptor
    );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((CompareMem ((VOID *)VolDescriptor.Unknown.Id,
                   (VOID *)UDF_NSR2_IDENTIFIER,
                   sizeof (VolDescriptor.Unknown.Id)) != 0) &&
      (CompareMem ((VOID *)VolDescriptor.Unknown.Id,
                   (VOID *)UDF_NSR3_IDENTIFIER,
                   sizeof (VolDescriptor.Unknown.Id)) != 0)) {
    return EFI_UNSUPPORTED;
  }

  //
  // Look for "TEA01" identifier in the Extended Area
  //
  Offset += UDF_LOGICAL_SECTOR_SIZE;
  if (Offset >= EndDiskOffset) {
    return EFI_UNSUPPORTED;
  }

  Status = DiskIo->ReadDisk (
    DiskIo,
    BlockIo->Media->MediaId,
    Offset,
    sizeof (CDROM_VOLUME_DESCRIPTOR),
    (VOID *)&VolDescriptor
    );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (CompareMem ((VOID *)VolDescriptor.Unknown.Id,
                  (VOID *)UDF_TEA_IDENTIFIER,
                  sizeof (VolDescriptor.Unknown.Id)) != 0) {
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
GetPartitionNumber (
  IN   UDF_LOGICAL_VOLUME_DESCRIPTOR  *LogicalVolDesc,
  OUT  UINT16                         *PartitionNum
  )
{
  EFI_STATUS                      Status;
  UDF_LONG_ALLOCATION_DESCRIPTOR  *LongAd;

  Status = EFI_SUCCESS;

  switch (LV_UDF_REVISION (LogicalVolDesc)) {
  case 0x0102:
    //
    // UDF 1.20 only supports Type 1 Partition
    //
    *PartitionNum = *(UINT16 *)((UINTN)&LogicalVolDesc->PartitionMaps[4]);
    break;
  case 0x0150:
    //
    // Ensure Type 1 Partition map
    //
    ASSERT (LogicalVolDesc->PartitionMaps[0] == 1 &&
            LogicalVolDesc->PartitionMaps[1] == 6);
    *PartitionNum = *(UINT16 *)((UINTN)&LogicalVolDesc->PartitionMaps[4]);
    break;
  case 0x0260:
    LongAd = &LogicalVolDesc->LogicalVolumeContentsUse;
    *PartitionNum = LongAd->ExtentLocation.PartitionReferenceNumber;
    break;
  default:
    //
    // Unhandled UDF revision
    //
    Status = EFI_VOLUME_CORRUPTED;
    break;
  }

  return Status;
}

EFI_STATUS
FindLogicalVolumeLocation (
  IN   EFI_BLOCK_IO_PROTOCOL                 *BlockIo,
  IN   EFI_DISK_IO_PROTOCOL                  *DiskIo,
  IN   UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER  *AnchorPoint,
  OUT  UINT64                                *MainVdsStartLsn,
  OUT  UINT64                                *LogicalVolEndLsn
  )
{
  EFI_STATUS                     Status;
  UINT32                         BlockSize;
  EFI_LBA                        LastBlock;
  UDF_EXTENT_AD                  *ExtentAd;
  UINT64                         StartingLsn;
  UINT64                         EndingLsn;
  VOID                           *Buffer;
  UDF_LOGICAL_VOLUME_DESCRIPTOR  *LogicalVolDesc;
  UDF_PARTITION_DESCRIPTOR       *PartitionDesc;
  UINT64                         GuardMainVdsStartLsn;
  UINT16                         PartitionNum;

  BlockSize             = BlockIo->Media->BlockSize;
  LastBlock             = BlockIo->Media->LastBlock;
  ExtentAd              = &AnchorPoint->MainVolumeDescriptorSequenceExtent;
  StartingLsn           = (UINT64)ExtentAd->ExtentLocation;
  EndingLsn             =
    StartingLsn + DivU64x32 ((UINT64)ExtentAd->ExtentLength, BlockSize);

  LogicalVolDesc        = NULL;
  PartitionDesc         = NULL;
  GuardMainVdsStartLsn  = StartingLsn;

  //
  // Allocate buffer for reading disk blocks
  //
  Buffer = AllocateZeroPool (BlockSize);
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = EFI_VOLUME_CORRUPTED;

  //
  // As per UDF 2.60 specification:
  //
  // There shall be exactly one prevailing Logical Volume Descriptor
  // recorded per Volume Set.
  //
  // Start Main Volume Descriptor Sequence and find Logical Volume Descriptor
  //
  while (StartingLsn <= EndingLsn) {
    //
    // Read disk block
    //
    Status = DiskIo->ReadDisk (
      DiskIo,
      BlockIo->Media->MediaId,
      MultU64x32 (StartingLsn, BlockSize),
      BlockSize,
      Buffer
      );
    if (EFI_ERROR (Status)) {
      goto Out_Free;
    }

    //
    // Check if read block is a Terminating Descriptor
    //
    if (IS_TD (Buffer)) {
      //
      // Stop Main Volume Descriptor Sequence
      //
      break;
    }

    //
    // Check if read block is a Logical Volume Descriptor
    //
    if (IS_LVD (Buffer)) {
      //
      // Ensure only one LVD (Logical Volume Descriptor) is handled
      //
      if (LogicalVolDesc != NULL) {
        Status = EFI_UNSUPPORTED;
        goto Out_Free;
      }

      //
      // As per UDF 2.60 specification:
      //
      // For the purpose of interchange, Partition Maps shall be limited to
      // Partition Map type 1, except type 2 maps.
      //
      // NOTE: Type 1 Partitions are the only supported in this implementation.
      //
      LogicalVolDesc = AllocateZeroPool (sizeof (*LogicalVolDesc));
      if (LogicalVolDesc == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        goto Out_Free;
      }

      //
      // Save Logical Volume Descriptor
      //
      CopyMem (LogicalVolDesc, Buffer, sizeof (*LogicalVolDesc));
    } else if (IS_PD (Buffer)) {
      //
      // Ensure only one PD (Partition Descriptor) is handled
      //
      if (PartitionDesc != NULL) {
        Status = EFI_UNSUPPORTED;
        goto Out_Free;
      }

      //
      // Found a Partition Descriptor.
      //
      // As per UDF 2.60 specification:
      //
      // A Partition Descriptor Access Type of read-only, rewritable,
      // overwritable, write-once and pseudo-overwritable shall be
      // supported. There shall be exactly one prevailing Partition
      // Descriptor recorded per volume, with one exception. For Volume
      // Sets that consist of single volume, the volume may contain 2 non-
      // overlapping Partitions with 2 prevailing Partition Descriptors only
      // if one has an Access Type of read-only and the other has an
      // Access Type of rewritable, overwritable, or write-once. The
      // Logical Volume for this volume would consist of the contents of
      // both partitions.
      //
      PartitionDesc = AllocateZeroPool (sizeof (*PartitionDesc));
      if (PartitionDesc == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
        goto Out_Free;
      }

      //
      // Save Partition Descriptor
      //
      CopyMem (PartitionDesc, Buffer, sizeof (*PartitionDesc));
    }
    //
    // Go to next disk block
    //
    StartingLsn++;
  }

  Status = EFI_VOLUME_CORRUPTED;

  //
  // Check if LVD and PD were found
  //
  if (LogicalVolDesc != NULL && PartitionDesc != NULL) {
    //
    // Get partition number from Partition map in LVD descriptor
    //
    Status = GetPartitionNumber (LogicalVolDesc, &PartitionNum);
    if (EFI_ERROR (Status)) {
      goto Out_Free;
    }

    //
    // Make sure we're handling expected Partition Descriptor
    //
    if (PartitionDesc->PartitionNumber != PartitionNum) {
      Status = EFI_VOLUME_CORRUPTED;
      goto Out_Free;
    }

    //
    // Cover the main VDS area so UdfDxe driver will also be able to get LVD and
    // PD descriptors out from the file system.
    //
    *MainVdsStartLsn = GuardMainVdsStartLsn;
    *LogicalVolEndLsn = *MainVdsStartLsn + (UINT64)ExtentAd->ExtentLength;

    //
    // Cover UDF partition area
    //
    *LogicalVolEndLsn +=
      ((UINT64)PartitionDesc->PartitionStartingLocation -
       *LogicalVolEndLsn) + PartitionDesc->PartitionLength - 1;
    //
    // Ensure to not attempt reading past end of device
    //
    if (*LogicalVolEndLsn > LastBlock) {
      Status = EFI_VOLUME_CORRUPTED;
    } else {
      Status = EFI_SUCCESS;
    }
  }

Out_Free:
  //
  // Free block read buffer
  //
  FreePool (Buffer);
  //
  // Free Logical Volume Descriptor
  //
  if (LogicalVolDesc != NULL) {
    FreePool (LogicalVolDesc);
  }
  //
  // Free Partition Descriptor
  //
  if (PartitionDesc != NULL) {
    FreePool (PartitionDesc);
  }

  return Status;
}

EFI_STATUS
FindUdfLogicalVolume (
  IN EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN EFI_DISK_IO_PROTOCOL   *DiskIo,
  OUT EFI_LBA               *StartingLBA,
  OUT EFI_LBA               *EndingLBA
  )
{
  EFI_STATUS Status;
  UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER  AnchorPoint;

  //
  // Find UDF volume identifiers
  //
  Status = FindUdfVolumeIdentifiers (BlockIo, DiskIo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Find Anchor Volume Descriptor Pointer
  //
  Status = FindAnchorVolumeDescriptorPointer (BlockIo, DiskIo, &AnchorPoint);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Find Logical Volume location
  //
  Status = FindLogicalVolumeLocation (
    BlockIo,
    DiskIo,
    &AnchorPoint,
    (UINT64 *)StartingLBA,
    (UINT64 *)EndingLBA
    );

  return Status;
}

/**
  Install child handles if the Handle supports UDF/ECMA-167 volume format.

  @param[in]  This        Calling context.
  @param[in]  Handle      Parent Handle.
  @param[in]  DiskIo      Parent DiskIo interface.
  @param[in]  DiskIo2     Parent DiskIo2 interface.
  @param[in]  BlockIo     Parent BlockIo interface.
  @param[in]  BlockIo2    Parent BlockIo2 interface.
  @param[in]  DevicePath  Parent Device Path


  @retval EFI_SUCCESS         Child handle(s) was added.
  @retval EFI_MEDIA_CHANGED   Media changed Detected.
  @retval other               no child handle was added.

**/
EFI_STATUS
PartitionInstallUdfChildHandles (
  IN  EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN  EFI_HANDLE                   Handle,
  IN  EFI_DISK_IO_PROTOCOL         *DiskIo,
  IN  EFI_DISK_IO2_PROTOCOL        *DiskIo2,
  IN  EFI_BLOCK_IO_PROTOCOL        *BlockIo,
  IN  EFI_BLOCK_IO2_PROTOCOL       *BlockIo2,
  IN  EFI_DEVICE_PATH_PROTOCOL     *DevicePath
  )
{
  UINT32                       RemainderByMediaBlockSize;
  EFI_STATUS                   Status;
  EFI_BLOCK_IO_MEDIA           *Media;
  EFI_DEVICE_PATH_PROTOCOL     *DevicePathNode;
  EFI_GUID                     *VendorDefinedGuid;
  EFI_GUID                     UdfDevPathGuid = EFI_UDF_DEVICE_PATH_GUID;
  EFI_PARTITION_INFO_PROTOCOL  PartitionInfo;
  EFI_LBA                      StartingLBA;
  EFI_LBA                      EndingLBA;

  Media = BlockIo->Media;

  //
  // Check if UDF logical block size is multiple of underlying device block size
  //
  DivU64x32Remainder (
    UDF_LOGICAL_SECTOR_SIZE,   // Dividend
    Media->BlockSize,          // Divisor
    &RemainderByMediaBlockSize // Remainder
    );
  if (RemainderByMediaBlockSize != 0) {
    return EFI_NOT_FOUND;
  }

  DevicePathNode = DevicePath;
  while (!IsDevicePathEnd (DevicePathNode)) {
    //
    // Do not allow checking for UDF file systems in CDROM "El Torito"
    // partitions, and skip duplicate installation of UDF file system child
    // nodes.
    //
    if (DevicePathType (DevicePathNode) == MEDIA_DEVICE_PATH) {
      if (DevicePathSubType (DevicePathNode) == MEDIA_CDROM_DP) {
        return EFI_NOT_FOUND;
      }
      if (DevicePathSubType (DevicePathNode) == MEDIA_VENDOR_DP) {
        VendorDefinedGuid = (EFI_GUID *)((UINTN)DevicePathNode +
                                         OFFSET_OF (VENDOR_DEVICE_PATH, Guid));
        if (CompareGuid (VendorDefinedGuid, &UdfDevPathGuid)) {
          return EFI_NOT_FOUND;
        }
      }
    }
    //
    // Try next device path node
    //
    DevicePathNode = NextDevicePathNode (DevicePathNode);
  }

  //
  // Find UDF logical volume on block device
  //
  Status = FindUdfLogicalVolume (BlockIo, DiskIo, &StartingLBA, &EndingLBA);
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  //
  // Create Partition Info protocol for UDF file system
  //
  ZeroMem (&PartitionInfo, sizeof (EFI_PARTITION_INFO_PROTOCOL));
  PartitionInfo.Revision = EFI_PARTITION_INFO_PROTOCOL_REVISION;
  PartitionInfo.Type = PARTITION_TYPE_OTHER;

  //
  // Install partition child handle for UDF file system
  //
  Status = PartitionInstallChildHandle (
    This,
    Handle,
    DiskIo,
    DiskIo2,
    BlockIo,
    BlockIo2,
    DevicePath,
    (EFI_DEVICE_PATH_PROTOCOL *)&gUdfDevicePath,
    &PartitionInfo,
    StartingLBA,
    EndingLBA,
    Media->BlockSize
    );
  if (!EFI_ERROR (Status)) {
    Status = EFI_NOT_FOUND;
  }

  return Status;
}
