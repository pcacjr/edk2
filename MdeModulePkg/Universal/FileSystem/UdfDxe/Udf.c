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

UDF_DEVICE_PATH gUdfDriverDevicePath = {
  {
    { MEDIA_DEVICE_PATH, MEDIA_VENDOR_DP, { sizeof (VENDOR_DEVICE_PATH), 0 } },
    EFI_UDF_DEVICE_PATH_GUID
  },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {
                              sizeof (EFI_DEVICE_PATH_PROTOCOL), 0
                              }
  }
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
  EFI_TPL                         OldTpl;
  EFI_STATUS                      Status;
  EFI_BLOCK_IO_PROTOCOL           *BlockIo;
  EFI_DISK_IO_PROTOCOL            *DiskIo;
  BOOLEAN                         IsUdfVolume;
  PRIVATE_UDF_SIMPLE_FS_DATA      *PrivFsData;

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

  //
  // Check if media contains a valid UDF volume
  //
  Status = IsSupportedUdfVolume (
                       BlockIo,
		       DiskIo,
		       &IsUdfVolume
                       );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  if (!IsUdfVolume) {
    Status = EFI_UNSUPPORTED;
    goto Exit;
  }

  PrivFsData = AllocateZeroPool (sizeof (PRIVATE_UDF_SIMPLE_FS_DATA));
  if (!PrivFsData) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  //
  // Create new child handle
  //
  PrivFsData->Signature   = PRIVATE_UDF_SIMPLE_FS_DATA_SIGNATURE;
  PrivFsData->BlockIo     = BlockIo;
  PrivFsData->DiskIo      = DiskIo;

  PrivFsData->SimpleFs.Revision   = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION;
  PrivFsData->SimpleFs.OpenVolume = UdfOpenVolume;

  PrivFsData->Handle = NULL;

  //
  // Install new child handle
  //
  Status = gBS->InstallMultipleProtocolInterfaces (
                                     &PrivFsData->Handle,
                                     &gEfiSimpleFileSystemProtocolGuid,
                                     &PrivFsData->SimpleFs,
                                     &gEfiDevicePathProtocolGuid,
                                     &gUdfDriverDevicePath,
                                     NULL
                                     );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

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
