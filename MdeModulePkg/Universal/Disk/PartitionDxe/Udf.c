/** @file
  Decode an UDF/ECMA-167 formatted medium

Copyright (c) 2014 Paulo Alcantara <pcacjr@zytor.com><BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/


#include "Partition.h"


EFI_GUID gUdfVolumeSignatureGuid = {
  0xC5BD4D42, 0x1A76, 0x4996,
  { 0x89, 0x56, 0x73, 0xCD, 0xA3, 0x26, 0xCD, 0x0A }
};

//
// The UDF/ECMA-167 file system driver only supports UDF revision 2.00 or
// higher.
//
// Note the "NSR03" identifier.
//
UDF_STANDARD_IDENTIFIER gUdfStandardIdentifiers[NR_STANDARD_IDENTIFIERS] = {
  { { 'B', 'E', 'A', '0', '1' } },
  { { 'N', 'S', 'R', '0', '3' } },
  { { 'T', 'E', 'A', '0', '1' } },
};

typedef struct {
  VENDOR_DEVICE_PATH        DevicePath;
  EFI_DEVICE_PATH_PROTOCOL  End;
} UDF_DEVICE_PATH;

//
// C5BD4D42-1A76-4996-8956-73CDA326CD0A
//
#define EFI_UDF_DEVICE_PATH_GUID \
  { 0xC5BD4D42, 0x1A76, 0x4996, \
    { 0x89, 0x56, 0x73, 0xCD, 0xA3, 0x26, 0xCD, 0x0A } \
  }

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

  BlockSize  = BlockIo->Media->BlockSize;
  EndLBA     = BlockIo->Media->LastBlock;

  //
  // Look for an AVDP at LBA 256.
  //
  Status = DiskIo->ReadDisk (
                          DiskIo,
                          BlockIo->Media->MediaId,
                          MultU64x32 (0x100ULL, BlockSize),
                          sizeof (UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER),
                          (VOID *)AnchorPoint
                          );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (IS_AVDP (AnchorPoint)) {
    return EFI_SUCCESS;
  }

  //
  // Look for an AVDP at last LBA - 256.
  //
  Status = DiskIo->ReadDisk (
                          DiskIo,
                          BlockIo->Media->MediaId,
                          MultU64x32 (EndLBA - 0x100ULL, BlockSize),
                          sizeof (UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER),
                          (VOID *)AnchorPoint
                          );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (IS_AVDP (AnchorPoint)) {
    return EFI_SUCCESS;
  }

  //
  // Look for an AVDP at last LBA.
  //
  Status = DiskIo->ReadDisk (
                          DiskIo,
                          BlockIo->Media->MediaId,
                          MultU64x32 (EndLBA, BlockSize),
                          sizeof (UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER),
                          (VOID *)AnchorPoint
                          );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (IS_AVDP (AnchorPoint)) {
    return EFI_SUCCESS;
  }

  //
  // There is no AVDP on this medium.
  //
  return EFI_VOLUME_CORRUPTED;
}

EFI_STATUS
SupportUdfFileSystem (
  IN EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN EFI_DISK_IO_PROTOCOL   *DiskIo
  )
{
  EFI_STATUS                            Status;
  UINT64                                Offset;
  UINT64                                EndDiskOffset;
  UDF_VOLUME_DESCRIPTOR                 VolDescriptor;
  UDF_VOLUME_DESCRIPTOR                 TerminatingVolDescriptor;
  UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER  AnchorPoint;

  ZeroMem ((VOID *)&TerminatingVolDescriptor, sizeof (UDF_VOLUME_DESCRIPTOR));

  //
  // Start Volume Recognition Sequence.
  //
  EndDiskOffset = (UINT64)BlockIo->Media->LastBlock * BlockIo->Media->BlockSize;

  for (Offset = UDF_VRS_START_OFFSET; Offset < EndDiskOffset;
       Offset += UDF_LOGICAL_SECTOR_SIZE) {
    Status = DiskIo->ReadDisk (
                            DiskIo,
                            BlockIo->Media->MediaId,
                            Offset,
                            sizeof (UDF_VOLUME_DESCRIPTOR),
                            (VOID *)&VolDescriptor
                            );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (!CompareMem (
	  (VOID *)&VolDescriptor.StandardIdentifier,
	  (VOID *)&gUdfStandardIdentifiers[BEA_IDENTIFIER],
	  UDF_STANDARD_IDENTIFIER_LENGTH
	  )
      ) {
      break;
    }

    if (CompareMem (
	  (VOID *)&VolDescriptor.StandardIdentifier,
	  (VOID *)UDF_CDROM_VOLUME_IDENTIFIER,
	  UDF_STANDARD_IDENTIFIER_LENGTH
	  ) ||
	!CompareMem (
	  (VOID *)&VolDescriptor,
	  (VOID *)&TerminatingVolDescriptor,
	  sizeof (UDF_VOLUME_DESCRIPTOR)
	  )
      ) {
      return EFI_UNSUPPORTED;
    }
  }

  //
  // Look for "NSR03" identifier in the Extended Area
  //
  Offset += UDF_LOGICAL_SECTOR_SIZE;
  if (Offset >= EndDiskOffset) {
    return EFI_UNSUPPORTED;
  }

  Status = DiskIo->ReadDisk (
                          DiskIo,
                          BlockIo->Media->MediaId,
                          Offset,
                          sizeof (UDF_VOLUME_DESCRIPTOR),
                          (VOID *)&VolDescriptor
                          );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (CompareMem (
	(VOID *)&VolDescriptor.StandardIdentifier,
	(VOID *)&gUdfStandardIdentifiers[VSD_IDENTIFIER],
	UDF_STANDARD_IDENTIFIER_LENGTH
	)
    ) {
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
                          sizeof (UDF_VOLUME_DESCRIPTOR),
                          (VOID *)&VolDescriptor
                          );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (CompareMem (
	(VOID *)&VolDescriptor.StandardIdentifier,
	(VOID *)&gUdfStandardIdentifiers[TEA_IDENTIFIER],
	UDF_STANDARD_IDENTIFIER_LENGTH
	)
    ) {
    return EFI_UNSUPPORTED;
  }

  Status = FindAnchorVolumeDescriptorPointer (BlockIo, DiskIo, &AnchorPoint);
  if (EFI_ERROR (Status)) {
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
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
  EFI_STATUS                Status;
  EFI_DEVICE_PATH_PROTOCOL  *LastDevicePathNode;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePathNode;
  EFI_GUID                  *VendorDevGuid;
  EFI_GUID                  UdfDevGuid = EFI_UDF_DEVICE_PATH_GUID;

  if (!FeaturePcdGet (PcdUdfFileSystemSupport)) {
    return EFI_NOT_FOUND;
  }

  Status = SupportUdfFileSystem (BlockIo, DiskIo);
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  LastDevicePathNode = NULL;
  DevicePathNode = DevicePath;
  while (!IsDevicePathEnd (DevicePathNode)) {
    LastDevicePathNode  = DevicePathNode;
    DevicePathNode      = NextDevicePathNode (DevicePathNode);
  }
  if (LastDevicePathNode) {
    VendorDevGuid = (EFI_GUID *)((UINT8 *)LastDevicePathNode + OFFSET_OF (VENDOR_DEVICE_PATH, Guid));
    if (DevicePathSubType (LastDevicePathNode) == MEDIA_VENDOR_DP &&
	CompareGuid (VendorDevGuid, &UdfDevGuid)) {
      return EFI_NOT_FOUND;
    }
  }

  Status = PartitionInstallChildHandle (
                                    This,
                                    Handle,
                                    DiskIo,
                                    DiskIo2,
                                    BlockIo,
                                    BlockIo2,
                                    DevicePath,
                                    (EFI_DEVICE_PATH_PROTOCOL *)&gUdfDevicePath,
                                    0,
                                    BlockIo->Media->LastBlock,
                                    BlockIo->Media->BlockSize,
                                    FALSE
                                    );
  if (!EFI_ERROR (Status)) {
    Status = EFI_NOT_FOUND;
  }

  return Status;
}
