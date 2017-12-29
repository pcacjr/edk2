/** @file
  CPU Exception Handler Library common functions.

  Copyright (c) 2012 - 2016, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "CpuExceptionCommon.h"

#include <Register/Msr.h>
#include <Library/DebugLib.h>

//
// Error code flag indicating whether or not an error code will be
// pushed on the stack if an exception occurs.
//
// 1 means an error code will be pushed, otherwise 0
//
CONST UINT32 mErrorCodeFlag = 0x00027d00;

//
// Define the maximum message length
//
#define MAX_DEBUG_MESSAGE_LENGTH  0x100

CONST CHAR8 mExceptionReservedStr[] = "Reserved";
CONST CHAR8 *mExceptionNameStr[] = {
  "#DE - Divide Error",
  "#DB - Debug",
  "NMI Interrupt",
  "#BP - Breakpoint",
  "#OF - Overflow",
  "#BR - BOUND Range Exceeded",
  "#UD - Invalid Opcode",
  "#NM - Device Not Available",
  "#DF - Double Fault",
  "Coprocessor Segment Overrun",
  "#TS - Invalid TSS",
  "#NP - Segment Not Present",
  "#SS - Stack Fault Fault",
  "#GP - General Protection",
  "#PF - Page-Fault",
  "Reserved",
  "#MF - x87 FPU Floating-Point Error",
  "#AC - Alignment Check",
  "#MC - Machine-Check",
  "#XM - SIMD floating-point",
  "#VE - Virtualization"
};

#define EXCEPTION_KNOWN_NAME_NUM  (sizeof (mExceptionNameStr) / sizeof (CHAR8 *))

//
// Unknown PDB file name
//
GLOBAL_REMOVE_IF_UNREFERENCED CONST CHAR8 *mUnknownPdbFileName = "????";

/**
  Get ASCII format string exception name by exception type.

  @param ExceptionType  Exception type.

  @return  ASCII format string exception name.
**/
CONST CHAR8 *
GetExceptionNameStr (
  IN EFI_EXCEPTION_TYPE          ExceptionType
  )
{
  if ((UINTN) ExceptionType < EXCEPTION_KNOWN_NAME_NUM) {
    return mExceptionNameStr[ExceptionType];
  } else {
    return mExceptionReservedStr;
  }
}

/**
  Prints a message to the serial port.

  @param  Format      Format string for the message to print.
  @param  ...         Variable argument list whose contents are accessed
                      based on the format string specified by Format.

**/
VOID
EFIAPI
InternalPrintMessage (
  IN  CONST CHAR8  *Format,
  ...
  )
{
  CHAR8    Buffer[MAX_DEBUG_MESSAGE_LENGTH];
  VA_LIST  Marker;

  //
  // Convert the message to an ASCII String
  //
  VA_START (Marker, Format);
  AsciiVSPrint (Buffer, sizeof (Buffer), Format, Marker);
  VA_END (Marker);

  //
  // Send the print string to a Serial Port
  //
  SerialPortWrite ((UINT8 *)Buffer, AsciiStrLen (Buffer));
}

/**
  Read and save reserved vector information

  @param[in]  VectorInfo        Pointer to reserved vector list.
  @param[out] ReservedVector    Pointer to reserved vector data buffer.
  @param[in]  VectorCount       Vector number to be updated.

  @return EFI_SUCCESS           Read and save vector info successfully.
  @retval EFI_INVALID_PARAMETER VectorInfo includes the invalid content if VectorInfo is not NULL.

**/
EFI_STATUS
ReadAndVerifyVectorInfo (
  IN  EFI_VECTOR_HANDOFF_INFO       *VectorInfo,
  OUT RESERVED_VECTORS_DATA         *ReservedVector,
  IN  UINTN                         VectorCount
  )
{
  while (VectorInfo->Attribute != EFI_VECTOR_HANDOFF_LAST_ENTRY) {
    if (VectorInfo->Attribute > EFI_VECTOR_HANDOFF_HOOK_AFTER) {
      //
      // If vector attrubute is invalid
      //
      return EFI_INVALID_PARAMETER;
    }
    if (VectorInfo->VectorNumber < VectorCount) {
      ReservedVector[VectorInfo->VectorNumber].Attribute = VectorInfo->Attribute;
    }
    VectorInfo ++;
  }
  return EFI_SUCCESS;
}

/**
  Get absolute path and file name of PDB file in PE/COFF image.

  @param[in]  ImageBase            Base address of PE/COFF image.
  @param[out] PdbAbsoluteFilePath  Absolute path of PDB file.
  @param[out] PdbFileName          File name of PDB file.
**/
VOID
GetPdbFileName (
  IN  UINTN    ImageBase,
  OUT CHAR8    **PdbAbsoluteFilePath,
  OUT CHAR8    **PdbFileName
  )
{
  VOID   *PdbPointer;
  CHAR8  *Str;

  //
  // Get PDB file name from PE/COFF image
  //
  PdbPointer = PeCoffLoaderGetPdbPointer ((VOID *)ImageBase);
  if (PdbPointer == NULL) {
    //
    // No PDB file name found. Set it to an unknown file name.
    //
    *PdbFileName = (CHAR8 *)mUnknownPdbFileName;
    if (PdbAbsoluteFilePath != NULL) {
      *PdbAbsoluteFilePath = NULL;
    }
  } else {
    //
    // Get file name portion out of PDB file in PE/COFF image
    //
    Str = (CHAR8 *)((UINTN)PdbPointer +
                    AsciiStrLen ((CHAR8 *)PdbPointer) - sizeof *Str);
    for (; *Str != '/' && *Str != '\\'; Str--) {
      ;
    }

    //
    // Set PDB file name (also skip trailing path separator: '/' or '\\')
    //
    *PdbFileName = Str + 1;

    if (PdbAbsoluteFilePath != NULL) {
      //
      // Set absolute file path of PDB file
      //
      *PdbAbsoluteFilePath = PdbPointer;
    }
  }
}

/**
  Check if a linear address is valid by walking the page tables in 4-level
  paging mode.

  @param[in]  Cr3             CR3 control register.
  @param[in]  MaxPhyAddrBits  MAXPHYADDRBITS bits.
  @param[in]  LinearAddress   Linear address to be checked.
**/
STATIC
BOOLEAN
Do4LevelPagingModeCheck (
  IN UINTN            Cr3,
  IN UINT8            MaxPhyAddrBits,
  IN UINTN            LinearAddress
  )
{
  UINT64 PhysicalAddress;
  UINT64 *Pml4TableEntry;
  UINT64 *PageDirPtrTableEntry;
  UINT64 *PageDirEntry;
  UINT64 *PageTableEntry;

  //
  // In 4-level paging mode, linear addresses are 48 bits wide
  //
  if ((UINT64)LinearAddress > (1ULL << 48) - 1) {
    return FALSE;
  }

  //
  // Calculate physical address of PML4E
  //
  PhysicalAddress = (UINT64)Cr3 & (((1ULL << MaxPhyAddrBits) - 1) << 12);
  PhysicalAddress |= (((UINT64)LinearAddress >> 39) & 0x1FF) << 3;

  ASSERT ((PhysicalAddress & (sizeof (*Pml4TableEntry) - 1)) == 0);

  Pml4TableEntry = (UINT64 *)(UINTN)PhysicalAddress;

  //
  // Check if a PDPTE is present
  //
  if ((*Pml4TableEntry & BIT0) == 0) {
    return FALSE;
  }

  //
  // Calculate physical address of PDPTE
  //
  PhysicalAddress = *Pml4TableEntry & (((1ULL << MaxPhyAddrBits) - 1) << 12);
  PhysicalAddress |= (((UINT64)LinearAddress >> 30) & 0x1FF) << 3;

  ASSERT ((PhysicalAddress & (sizeof (*PageDirPtrTableEntry) - 1)) == 0);

  PageDirPtrTableEntry = (UINT64 *)(UINTN)PhysicalAddress;

  //
  // Check whether a PDPTE or 1GiB page entry is present
  //
  if ((*PageDirPtrTableEntry & BIT0) == 0) {
    return FALSE;
  }

  //
  // Check if PDPTE maps an 1GiB page
  //
  if ((*PageDirPtrTableEntry & BIT7) != 0) {
    return TRUE;
  }

  //
  // Calculate physical address of PDE
  //
  PhysicalAddress = *PageDirPtrTableEntry & (((1ULL << MaxPhyAddrBits) - 1) <<
                                             12);
  PhysicalAddress |= (((UINT64)LinearAddress >> 21) & 0x1FF) << 3;

  ASSERT ((PhysicalAddress & (sizeof (*PageDirEntry) - 1)) == 0);

  PageDirEntry = (UINT64 *)(UINTN)PhysicalAddress;

  //
  // Check whether a PDE or a 2MiB page entry is present
  //
  if ((*PageDirEntry & BIT0) == 0) {
    return FALSE;
  }

  //
  // Check if PDE maps a 2MiB page
  //
  if ((*PageDirEntry & BIT7) != 0) {
    return TRUE;
  }

  //
  // Calculate physical address of PTE
  //
  PhysicalAddress = *PageDirEntry & (((1ULL << MaxPhyAddrBits) - 1) << 12);
  PhysicalAddress |= (((UINT64)LinearAddress >> 12) & 0x1FF) << 3;

  ASSERT ((PhysicalAddress & (sizeof (*PageTableEntry) - 1)) == 0);

  PageTableEntry = (UINT64 *)(UINTN)PhysicalAddress;

  //
  // Check if PTE maps a 4KiB page
  //
  if ((*PageTableEntry & BIT0) == 0) {
    return FALSE;
  }

  return TRUE;
}

/**
  Check if a linear address is valid by walking the page tables in 32-bit paging
  mode.

  @param[in]  Cr3             CR3 control register.
  @param[in]  Cr4             CR4 control register.
  @param[in]  LinearAddress   Linear address to be checked.
**/
STATIC
BOOLEAN
Do32BitPagingModeCheck (
  IN UINTN            Cr3,
  IN UINTN            Cr4,
  IN UINTN            LinearAddress
  )
{
  UINT64 PhysicalAddress;
  UINT32 *PageDirEntry;
  UINT32 *PageTableEntry;

  if (LinearAddress > MAX_UINT32) {
    return FALSE;
  }

  //
  // Calculate physical address of PDE
  //
  PhysicalAddress = (UINT32)Cr3 & (((1ULL << 20) - 1) << 12);
  PhysicalAddress |= (((UINT32)LinearAddress >> 22) & 0x3FF) << 2;

  ASSERT ((PhysicalAddress & (sizeof (*PageDirEntry) - 1)) == 0);

  PageDirEntry = (UINT32 *)(UINTN)PhysicalAddress;

  //
  // Check whether a PTE or a 4MiB page is present
  //
  if ((*PageDirEntry & BIT0) == 0) {
    return FALSE;
  }

  //
  // Check if PDE maps a 4MiB page
  //
  if ((Cr4 & BIT4) != 0 && (*PageDirEntry & BIT7) != 0) {
    return TRUE;
  }

  //
  // Calculate physical address of PTE
  //
  PhysicalAddress = *PageDirEntry & (((1ULL << 20) - 1) << 12);
  PhysicalAddress |= (((UINT32)LinearAddress >> 12) & 0x3FF) << 2;

  ASSERT ((PhysicalAddress & (sizeof (*PageTableEntry) - 1)) == 0);

  PageTableEntry = (UINT32 *)(UINTN)PhysicalAddress;

  //
  // Check if PTE maps a 4KiB page
  //
  if ((*PageTableEntry & BIT0) == 0) {
    return FALSE;
  }

  return TRUE;
}

/**
  Check if a linear address is valid by walking the page tables in PAE paging
  mode.

  @param[in]  Cr3             CR3 control register.
  @param[in]  MaxPhyAddrBits  MAXPHYADDRBITS bits.
  @param[in]  LinearAddress   Linear address to be checked.
**/
STATIC
BOOLEAN
DoPAEPagingModeCheck (
  IN UINTN            Cr3,
  IN UINT8            MaxPhyAddrBits,
  IN UINTN            LinearAddress
  )
{
  UINT64 PhysicalAddress;
  UINT64 *PageDirPtrTableEntry;
  UINT64 *PageDirEntry;
  UINT64 *PageTableEntry;

  if (LinearAddress > MAX_UINT32) {
    return FALSE;
  }

  //
  // Calculate physical address of PDPTE
  //
  PhysicalAddress = (UINT32)Cr3 >> 5;

  //
  // Select PDPTE register
  //
  PhysicalAddress +=
    ((UINT32)LinearAddress >> 30) * sizeof (*PageDirPtrTableEntry);

  PageDirPtrTableEntry = (UINT64 *)(UINTN)PhysicalAddress;

  //
  // Check if PDE is present
  //
  if ((*PageDirPtrTableEntry & BIT0) == 0) {
    return FALSE;
  }

  PhysicalAddress = *PageDirPtrTableEntry & (((1ULL << MaxPhyAddrBits) - 1) <<
                                             12);
  PhysicalAddress |= ((LinearAddress >> 21) & 0x1FF) << 3;
  ASSERT ((PhysicalAddress & (sizeof (*PageDirEntry) - 1)) == 0);

  PageDirEntry = (UINT64 *)(UINTN)PhysicalAddress;

  //
  // Check whether a PTE or a 2MiB page is present
  //
  if ((*PageDirEntry & BIT0) == 0) {
    return FALSE;
  }

  //
  // Check if PDE maps a 2MiB page
  //
  if ((*PageDirEntry & BIT7) != 0) {
    return TRUE;
  }

  //
  // Calculate physical address of PTE
  //
  PhysicalAddress = *PageDirEntry & (((1ULL << MaxPhyAddrBits) - 1) << 12);
  PhysicalAddress |= ((LinearAddress >> 12) & 0x1FF) << 3;
  ASSERT ((PhysicalAddress & (sizeof (*PageTableEntry) - 1)) == 0);

  PageTableEntry = (UINT64 *)(UINTN)PhysicalAddress;

  //
  // Check if PTE maps a 4KiB page
  //
  if ((*PageTableEntry & BIT0) == 0) {
    return FALSE;
  }

  return TRUE;
}

/**
  Check if a linear address is valid.

  @param[in]  Cr0            CR0 control register.
  @param[in]  Cr3            CR3 control register.
  @param[in]  Cr4            CR4 control register.
  @param[in]  LinearAddress  Linear address to be checked.
**/
BOOLEAN
IsLinearAddressValid (
  IN  UINTN              Cr0,
  IN  UINTN              Cr3,
  IN  UINTN              Cr4,
  IN  UINTN              LinearAddress
  )
{
  UINT32                  Eax;
  UINT32                  Edx;
  UINT8                   MaxPhyAddrBits;
  MSR_IA32_EFER_REGISTER  Msr;
  BOOLEAN                 AddressValid;

  //
  // Check for valid input parameters
  //
  if (Cr0 == 0 || Cr4 == 0 || LinearAddress == 0) {
    return FALSE;
  }

  //
  // Check if paging is disabled
  //
  if ((Cr0 & BIT31) == 0) {
    //
    // If CR4.PAE bit is set, then the linear (or physical) address supports
    // only up to 36 bits.
    //
    if (((Cr4 & BIT5) != 0 && (UINT64)LinearAddress > 0xFFFFFFFFFULL) ||
        LinearAddress > 0xFFFFFFFF) {
      return FALSE;
    }

    return TRUE;
  }

  //
  // Paging can be enabled only if CR0.PE bit is set
  //
  if ((Cr0 & BIT0) == 0) {
    return FALSE;
  }

  //
  // CR3 register cannot be zero if paging is enabled
  //
  if (Cr3 == 0) {
    return FALSE;
  }

  //
  // Get MAXPHYADDR bits
  //
  AsmCpuid (0x80000000, &Eax, NULL, NULL, NULL);
  if (Eax >= 0x80000008) {
    AsmCpuid (0x80000008, &Eax, NULL, NULL, NULL);
    MaxPhyAddrBits = (UINT8)Eax;
  } else {
    AsmCpuid (1, NULL, NULL, NULL, &Edx);
    if ((Edx & BIT6) != 0) {
      MaxPhyAddrBits = 36;
    } else {
      MaxPhyAddrBits = 32;
    }
  }

  ASSERT (MaxPhyAddrBits > 0);

  AddressValid = FALSE;

  //
  // check if CR4.PAE bit is not set
  //
  if ((Cr4 & BIT5) == 0) {
    //
    // Check if linear address is valid in 32-bit paging mode
    //
    AddressValid = Do32BitPagingModeCheck (Cr3, Cr4, LinearAddress);
  } else {
    if (MaxPhyAddrBits > 52) {
      return FALSE;
    }

    Msr.Uint64 = AsmReadMsr64 (MSR_IA32_EFER);

    if (Msr.Bits.LME == 0) {
      //
      // Check if linear address is valid in PAE paging mode
      //
      AddressValid = DoPAEPagingModeCheck (Cr3, MaxPhyAddrBits, LinearAddress);
    } else {
      //
      // Check if linear address is valid in 4-level paging mode
      //
      AddressValid = Do4LevelPagingModeCheck (Cr3, MaxPhyAddrBits,
                                              LinearAddress);
    }
  }

  return AddressValid;
}
