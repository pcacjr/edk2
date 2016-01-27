#include <Uefi.h>

#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/PciLib.h>
#include <Library/BaseCryptLib.h>
#include <Library/MemoryAllocationLib.h>

#include <IndustryStandard/Pci22.h>
#include <IndustryStandard/Pci30.h>

#include <Protocol/PciIo.h>

VOID *mSha256Ctx = NULL;
UINT8 mOptionRomHashValue[SHA256_DIGEST_SIZE] = { 0, };

UINT8 *
GetLegacyOptionRomHashValue (
  IN   VOID                    *RomImage,
  IN   UINTN                   RomImageSize
  )
{
  UINTN Sha256CtxSize;
  BOOLEAN Status;

  if (mSha256Ctx == NULL) {
    Sha256CtxSize = Sha256GetContextSize ();

    mSha256Ctx = AllocatePool (Sha256CtxSize);
    ASSERT (mSha256Ctx != NULL);
    Status = Sha256Init (mSha256Ctx);
    ASSERT (Status == TRUE);
  }

  Status = Sha256Update (mSha256Ctx, RomImage, RomImageSize);
  ASSERT (Status == TRUE);
  Status = Sha256Final (mSha256Ctx, mOptionRomHashValue);
  ASSERT (Status == TRUE);

  return mOptionRomHashValue;
}

EFI_STATUS
EFIAPI
PciOptionRomTestEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS Status;
  UINTN NoHandles;
  EFI_HANDLE *HandleBuffer;
  UINTN Index;
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINTN Segment;
  UINTN Bus;
  UINTN Device;
  UINTN Function;
  PCI_TYPE00 PciConfigHdr;
  UINT16 VendorId;
  UINT16 DeviceId;
  EFI_PCI_ROM_HEADER RomHdr;
  PCI_3_0_DATA_STRUCTURE *PciDataStructure;
  UINT16 *DeviceIdList;
  BOOLEAN DeviceMatch;
  VOID *RomImage;
  UINTN RomImageSize;
  UINT8 *Hash;

  Status = gBS->LocateHandleBuffer (
    ByProtocol,
    &gEfiPciIoProtocolGuid,
    NULL,
    &NoHandles,
    &HandleBuffer);
  ASSERT_EFI_ERROR (Status);

  DEBUG ((DEBUG_ERROR, "PciORomTest: located %d PCI device(s)\n", NoHandles));

  for (Index = 0; Index < NoHandles; Index++) {
    Status = gBS->OpenProtocol (
      HandleBuffer[Index],
      &gEfiPciIoProtocolGuid,
      (VOID **)&PciIo,
      &gImageHandle,
      NULL,
      EFI_OPEN_PROTOCOL_GET_PROTOCOL
      );
    ASSERT_EFI_ERROR (Status);

    Status = PciIo->GetLocation (
      PciIo,
      &Segment,
      &Bus,
      &Device,
      &Function
      );
    ASSERT_EFI_ERROR (Status);

    DEBUG ((DEBUG_ERROR, "PciORomTest: PCI device %d:%d:%d\n", Bus, Device, Function));

    //
    // Access PCI configuration space for device identification. As per PCI local bus specification,
    // if the PCI device has a ROM image, the VendorId and DeviceId fields must match with the ones
    // in every PCI Data structure.
    //
    Status = PciIo->Pci.Read (
      PciIo,
      EfiPciIoWidthUint32,
      0,
      sizeof (PCI_TYPE00) / sizeof (UINT32),
      &PciConfigHdr
      );
    ASSERT_EFI_ERROR (Status);

    VendorId = PciConfigHdr.Hdr.VendorId;
    DeviceId = PciConfigHdr.Hdr.DeviceId;

    DEBUG ((DEBUG_ERROR, "PciORomTesT: VendorId %04X DeviceId %04X\n", VendorId, DeviceId));
    DEBUG ((DEBUG_ERROR, "PciORomTesT: ClassCode: %02X %02X %02X\n",
	    PciConfigHdr.Hdr.ClassCode[2], PciConfigHdr.Hdr.ClassCode[1],
	    PciConfigHdr.Hdr.ClassCode[3]));

    if ((PciConfigHdr.Hdr.ClassCode[2] == PCI_CLASS_OLD &&
	 PciConfigHdr.Hdr.ClassCode[1] == PCI_CLASS_OLD_VGA) ||
	(PciConfigHdr.Hdr.ClassCode[2] == PCI_CLASS_DISPLAY &&
	 PciConfigHdr.Hdr.ClassCode[1] == PCI_CLASS_DISPLAY_VGA)) {
      DEBUG ((DEBUG_ERROR, "PciORomTest: skip VGA device\n"));
      continue;
    }

    //
    // Check for presence of a PCI expansion ROM
    //
    if (PciIo->RomSize < sizeof (PCI_EXPANSION_ROM_HEADER)) {
      DEBUG ((DEBUG_ERROR, "PciORomTesT: PCI device has no expansion ROM\n"));
      continue;
    }

    DEBUG ((DEBUG_ERROR, "PciORomTesT: PCI expansion ROM mapped at 0x%08X (size %d)\n",
	    PciIo->RomImage, PciIo->RomSize));

    RomImage = NULL;
    RomImageSize = 0;
    RomHdr.Raw = PciIo->RomImage;
    //
    // As per UEFI specification, if a PCI expansion ROM contains legacy option ROM image, it
    // *must* be the first image.
    //
    while (RomHdr.Generic->Signature == PCI_EXPANSION_ROM_HEADER_SIGNATURE) {
      //
      // Check for valid expansion ROM header fields
      //
      if (RomHdr.Generic->PcirOffset == 0) {
	break;
      }
      if (PciIo->RomSize < RomHdr.Raw - (UINT8 *)PciIo->RomImage + RomHdr.Generic->PcirOffset + sizeof (PCI_DATA_STRUCTURE)) {
	break;
      }
      //
      // Find PCI Data Structure in the first 64KiB of the ROM image and check for valid fields
      //
      PciDataStructure = (PCI_3_0_DATA_STRUCTURE *)(RomHdr.Raw + RomHdr.Generic->PcirOffset);
      if (PciDataStructure->Signature != PCI_DATA_STRUCTURE_SIGNATURE) {
	DEBUG ((DEBUG_ERROR, "PciORomTesT: no valid PCI Data Structure siganture: 0x%08X\n",
		PciDataStructure->Signature));
	break;
      }

      //
      // Check for Intel X86, PC-AT compatible type
      //
      if (PciDataStructure->CodeType != PCI_CODE_TYPE_PCAT_IMAGE) {
	break;
      }

      //
      // Check for valid VendorId and DeviceId fields
      //
      DeviceMatch = FALSE;
      if (PciDataStructure->VendorId == VendorId) {
	if (PciDataStructure->DeviceId != DeviceId &&
	    PciDataStructure->Revision >= 3 && PciDataStructure->DeviceListOffset != 0) {
	  DeviceIdList = (UINT16 *)((UINT8 *)PciDataStructure + PciDataStructure->DeviceListOffset);
	  while  (*DeviceIdList != 0) {
	    if (*DeviceIdList == DeviceId) {
	      DeviceMatch = TRUE;
	    }
	  }
	} else {
	  DeviceMatch = TRUE;
	}
      }

      if (!DeviceMatch) {
	DEBUG ((DEBUG_ERROR, "PciORomTesT: invalid VendorId and DeviceId values\n"));
      } else {
	RomImage = (VOID *)RomHdr.Raw;
	//
	// Note: The ImageLength value is in units of 512 bytes
	//
	RomImageSize = 512 * PciDataStructure->ImageLength;
	DEBUG ((DEBUG_ERROR, "PciORomTest: found legacy option ROM image (addr 0x%08x size %d)\n",
		RomImage, RomImageSize));
      }

      if ((PciDataStructure->Indicator & BIT7) != 0) {
	//
	// If bit 7 of Indicator is set, then last image reached
	//
	DEBUG ((DEBUG_ERROR, "PciORomTest: end of ROM image(s) in this PCI device\n"));
	break;
      }

      //
      // Point to next ROM image.
      //
      RomHdr.Raw += 512 * PciDataStructure->ImageLength;
    }

    if (RomImage != NULL && RomImageSize > 0) {
      DEBUG ((DEBUG_ERROR, "PciORomTest: calculate hash of legacy option ROM image:\n"));
      Hash = GetLegacyOptionRomHashValue (RomImage, RomImageSize);
      for (Index = 0; Index < SHA256_DIGEST_SIZE; Index++) {
	DEBUG ((DEBUG_ERROR, "%02X ", Hash[Index]));
      }
      DEBUG ((DEBUG_ERROR, "\n"));
    }
  }

  if (mSha256Ctx != NULL) {
    FreePool (mSha256Ctx);
  }

  return EFI_SUCCESS;
}
