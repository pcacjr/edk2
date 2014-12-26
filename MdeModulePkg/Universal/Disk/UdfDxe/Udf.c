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
  0x0000000BUL,
  NULL,
  NULL
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

EFI_SIMPLE_FILE_SYSTEM_PROTOCOL gUdfSimpleFsOps = {
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION,
  UdfOpenVolume
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
  EFI_STATUS                       Status;
  EFI_DISK_IO_PROTOCOL             *DiskIo;
  EFI_BLOCK_IO_PROTOCOL            *BlockIo;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *SimpleFs;

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiDiskIoProtocolGuid,
                  (VOID **)&DiskIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiBlockIoProtocolGuid,
                  (VOID **)&BlockIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    goto Error_Open_BlockIo;
  }

  Status = SupportUdfFileSystem (BlockIo, DiskIo);
  if (EFI_ERROR (Status)) {
    goto Error_No_Udf_Volume;
  }

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID **)&SimpleFs,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (!EFI_ERROR (Status)) {
    gBS->CloseProtocol (
           ControllerHandle,
           &gEfiSimpleFileSystemProtocolGuid,
           This->DriverBindingHandle,
           ControllerHandle
           );
    Status = EFI_ALREADY_STARTED;
  } else {
    Status = EFI_SUCCESS;
  }

  if (EFI_ERROR (Status)) {
Error_No_Udf_Volume:
Error_Open_BlockIo:
    gBS->CloseProtocol (
           ControllerHandle,
           &gEfiDiskIoProtocolGuid,
           This->DriverBindingHandle,
           ControllerHandle
           );
    gBS->CloseProtocol (
           ControllerHandle,
           &gEfiBlockIoProtocolGuid,
           This->DriverBindingHandle,
           ControllerHandle
           );
  }

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
  EFI_TPL                     OldTpl;
  EFI_STATUS                  Status;
  EFI_BLOCK_IO_PROTOCOL       *BlockIo;
  EFI_DISK_IO_PROTOCOL        *DiskIo;
  PRIVATE_UDF_SIMPLE_FS_DATA  *PrivFsData;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiBlockIoProtocolGuid,
                  (VOID **)&BlockIo,
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
                  (VOID **)&DiskIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  //
  // Check if media contains a valid UDF volume
  //
  Status = SupportUdfFileSystem (BlockIo, DiskIo);
  if (EFI_ERROR (Status)) {
    goto Exit;
  }

  PrivFsData =
    (PRIVATE_UDF_SIMPLE_FS_DATA *)AllocateZeroPool (
                                             sizeof (PRIVATE_UDF_SIMPLE_FS_DATA)
                                             );
  if (!PrivFsData) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  //
  // Create new child handle
  //
  PrivFsData->Signature = PRIVATE_UDF_SIMPLE_FS_DATA_SIGNATURE;
  PrivFsData->BlockIo   = BlockIo;
  PrivFsData->DiskIo    = DiskIo;

  CopyMem (
    (VOID *)&PrivFsData->SimpleFs,
    (VOID *)&gUdfSimpleFsOps,
    sizeof (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL)
    );

  PrivFsData->DevicePath = DuplicateDevicePath (
                                     (EFI_DEVICE_PATH_PROTOCOL *)&gUdfDevicePath
                                     );

  //
  // Install new child handle
  //
  Status = gBS->InstallMultipleProtocolInterfaces (
                                     &PrivFsData->Handle,
                                     &gEfiSimpleFileSystemProtocolGuid,
                                     &PrivFsData->SimpleFs,
                                     &gEfiDevicePathProtocolGuid,
                                     PrivFsData->DevicePath,
                                     NULL,
                                     NULL
                                     );

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
  UINTN Index;
  PRIVATE_UDF_SIMPLE_FS_DATA        *PrivFsData;
  EFI_STATUS                        Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL   *SimpleFs;
  EFI_DISK_IO_PROTOCOL              *DiskIo;
  EFI_BLOCK_IO_PROTOCOL              *BlockIo;
  BOOLEAN                           Done;

  Status = EFI_SUCCESS;

  if (!NumberOfChildren) {
    gBS->CloseProtocol (
           ControllerHandle,
           &gEfiDiskIoProtocolGuid,
           This->DriverBindingHandle,
           ControllerHandle
           );
    goto Exit;
  }

  Done = TRUE;

  for (Index = 0; Index < NumberOfChildren; Index++) {
    gBS->OpenProtocol (
           ChildHandleBuffer[Index],
           &gEfiSimpleFileSystemProtocolGuid,
           (VOID **)&SimpleFs,
           This->DriverBindingHandle,
           ControllerHandle,
           EFI_OPEN_PROTOCOL_GET_PROTOCOL
           );

    PrivFsData = PRIVATE_UDF_SIMPLE_FS_DATA_FROM_THIS (This);

    gBS->CloseProtocol (
           ControllerHandle,
           &gEfiDiskIoProtocolGuid,
           This->DriverBindingHandle,
           ControllerHandle
           );
    gBS->CloseProtocol (
           ControllerHandle,
           &gEfiBlockIoProtocolGuid,
           This->DriverBindingHandle,
           ControllerHandle
           );

    Status = gBS->UninstallMultipleProtocolInterfaces (
                                           ChildHandleBuffer[Index],
                                           &gEfiSimpleFileSystemProtocolGuid,
                                           &PrivFsData->SimpleFs,
                                           &gEfiDevicePathProtocolGuid,
                                           PrivFsData->DevicePath,
                                           NULL
                                           );
    if (EFI_ERROR (Status)) {
      gBS->OpenProtocol (
             ControllerHandle,
             &gEfiDiskIoProtocolGuid,
             (VOID **)&DiskIo,
             This->DriverBindingHandle,
             ChildHandleBuffer[Index],
             EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
             );
      gBS->OpenProtocol (
             ControllerHandle,
             &gEfiBlockIoProtocolGuid,
             (VOID **)&BlockIo,
             This->DriverBindingHandle,
             ChildHandleBuffer[Index],
             EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
             );
    } else {
      FreePool ((VOID *)PrivFsData->DevicePath);

      if (PrivFsData->OpenFiles) {
        CleanupVolumeInformation (&PrivFsData->Volume);
      }

      FreePool ((VOID *)PrivFsData);
    }

    if (EFI_ERROR (Status)) {
      Done = FALSE;
    }
  }

  if (!Done) {
    Status = EFI_DEVICE_ERROR;
  }

Exit:
  return Status;
}

/**
  The user Entry Point for UDF file system driver. The user code starts with
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
