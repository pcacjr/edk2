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
// UDF filesystem driver's Global Variables.
//
EFI_DRIVER_BINDING_PROTOCOL gUdfDriverBinding = {
  UdfDriverBindingSupported,
  UdfDriverBindingStart,
  UdfDriverBindingStop,
  0x0000000bul,
  NULL,
  NULL
};

/**
  Test to see if this driver supports ControllerHandle. Any ControllerHandle
  than contains a BlockIo and DiskIo protocol or a BlockIo2 protocol can be
  supported.

  @param[in]  This                Protocol instance pointer.
  @param[in]  ControllerHandle    Handle of device to test.
  @param[in]  RemainingDevicePath Optional parameter use to pick a specific child
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
  )
{
  EFI_STATUS                      Status;
  EFI_DISK_IO_PROTOCOL            *DiskIo;

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiDiskIoProtocolGuid,
                  (VOID **) &DiskIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (Status == EFI_ALREADY_STARTED) {
    Status = EFI_SUCCESS;
    goto Exit;
  }

  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  gBS->CloseProtocol (
         ControllerHandle,
         &gEfiDiskIoProtocolGuid,
         This->DriverBindingHandle,
         ControllerHandle
         );

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_TEST_PROTOCOL
                  );

Exit:
  return Status;
}

/**
  Start this driver on ControllerHandle by opening a Block IO or a Block IO2
  or both, and Disk IO protocol, reading Device Path, and creating a child
  handle with a Disk IO and device path protocol.

  @param[in]  This                 Protocol instance pointer.
  @param[in]  ControllerHandle     Handle of device to bind driver to
  @param[in]  RemainingDevicePath  Optional parameter use to pick a specific child
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
  )
{
  EFI_TPL                                OldTpl;
  EFI_STATUS                             Status;
  EFI_BLOCK_IO_PROTOCOL                  *BlockIo;
  EFI_DISK_IO_PROTOCOL                   *DiskIo;
  UINT32                                 BlockSize;
  UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER   *AnchorPoint;
  UDF_PARTITION_DESCRIPTOR               *PartitionDesc;
  UDF_LOGICAL_VOLUME_DESCRIPTOR          *LogicalVolDesc;
  UDF_FILE_SET_DESCRIPTOR                *FileSetDesc;
  UDF_FILE_ENTRY                         *RootFileEntry;
  UDF_FILE_IDENTIFIER_DESCRIPTOR         *RootFileIdentifierDesc;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiBlockIoProtocolGuid,
                  (VOID **) &BlockIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiDiskIoProtocolGuid,
                  (VOID **) &DiskIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status) && Status != EFI_ALREADY_STARTED) {
    gBS->CloseProtocol (
          ControllerHandle,
          &gEfiDevicePathProtocolGuid,
          This->DriverBindingHandle,
          ControllerHandle
          );
    goto Exit;
  }

#if 0
  Print (
    L"UdfDriverStart: Number of LBAs:   %d (0x%08x)\n",
    BlockIo->Media->LastBlock + 1,
    BlockIo->Media->LastBlock + 1
    );
#endif

  if ((BlockIo->Media->LogicalPartition == TRUE) ||
      (BlockIo->Media->LastBlock + 1 != 0x5ED4D0)) {
    Status = EFI_UNSUPPORTED;
    goto Exit;
  }

  //
  // Logical block size for DVD ready-only discs
  //
  BlockSize = 2048;

  Print (L"UdfDriverStart: Defaulting to logical block size of 2048\n");

  Status = FindRootDirectory (
                          BlockIo,
                          DiskIo,
			  BlockSize,
			  &AnchorPoint,
			  &PartitionDesc,
			  &LogicalVolDesc,
			  &FileSetDesc,
			  &RootFileEntry,
			  &RootFileIdentifierDesc
                          );
  if (EFI_ERROR (Status)) {
    Print (L"UdfDriverStart: Failed to find Root Directory (%r)\n", Status);
    goto Exit;
  }

  Print (L"UdfDriverStart: Root Directory found\n");

  Print (L"\n");

  Status = ListDirectoryFids (
                          BlockIo,
			  DiskIo,
			  BlockSize,
			  AnchorPoint,
			  PartitionDesc,
			  LogicalVolDesc,
			  FileSetDesc,
			  RootFileEntry,
			  RootFileIdentifierDesc
                          );
  if (EFI_ERROR (Status)) {
    Print (L"UdfDriverStart: Failed to list directory FIDs (%r)\n", Status);
    goto Exit;
  }

  Print (L"\n");

  Print (L"UdfDriverStart: Done (%r)\n", EFI_SUCCESS);

  //
  // FIXME: Leaking too much memory. Free all of them before exiting!
  //
Exit:
  gBS->RestoreTPL (OldTpl);
  return Status;
}

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
  )
{
  //
  // Close the bus driver
  //
  gBS->CloseProtocol (
        ControllerHandle,
        &gEfiDiskIoProtocolGuid,
        This->DriverBindingHandle,
        ControllerHandle
        );

  Print (L"UdfDriverStop: Stopped (%r)\n", EFI_SUCCESS);

  return EFI_SUCCESS;
}

/**
  The user Entry Point for UDF filesystem driver. The user code starts with
  this function.

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.
  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
InitializeUdf (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
{
  EFI_STATUS              Status;

  Status = EfiLibInstallDriverBindingComponentName2 (
             ImageHandle,
             SystemTable,
             &gUdfDriverBinding,
             ImageHandle,
             &gUdfComponentName,
             &gUdfComponentName2
             );
  ASSERT_EFI_ERROR (Status);

  return Status;
}
