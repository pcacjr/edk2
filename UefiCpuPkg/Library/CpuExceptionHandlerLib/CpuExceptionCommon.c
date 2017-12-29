/** @file
  CPU Exception Handler Library common functions.

  Copyright (c) 2012 - 2017, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "CpuExceptionCommon.h"

#include <Register/Msr.h>

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

//
// IA32 virtual memory bit definitions
//
#define IA32_PG_P   BIT0
#define IA32_PG_PS  BIT7

//
// IA32 control register bit definitions
//
#define IA32_CR0_PG   BIT31
#define IA32_CR4_PAE  BIT5
#define IA32_CR0_PE   BIT0

//
// IA32 CPUID 01h EDX bit definitions
//
#define IA32_CPUID1_EDX_PAE BIT6

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
  @param[in]  MaxPhyAddrBits  MAXPHYADDR bits.
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
  UINT64  PhysicalAddressMask;
  UINTN   IndexMask;
  UINTN   Index;
  UINT64  *Pml4Table;
  UINT64  *TableEntry;
  UINT64  *PageDirPtrTable;
  UINT64  *PageDirTable;
  UINT64  *PageTable;

  //
  // In 4-level paging mode, linear addresses are 48 bits wide
  //
  if ((UINT64)LinearAddress > 0xFFFFFFFFFFFFULL) {
    return FALSE;
  }

  //
  // Calculate physical address mask (bits M-1:12)
  //
  PhysicalAddressMask = (LShiftU64 (1, MaxPhyAddrBits) - 1) & ~0xFFF;
  //
  // 9 bits for masking page table indexes out of linear addresses
  //
  IndexMask = 0x1FF;

  //
  // Calculate physical address of PML4 table and index of PML4E
  //
  Pml4Table = (UINT64 *)(UINTN)((UINT64)Cr3 & PhysicalAddressMask);
  Index = (UINTN)(RShiftU64 ((UINT64)LinearAddress, 39) & IndexMask);

  //
  // Select PML4E
  //
  TableEntry = &Pml4Table[Index];

  //
  // Check if a PDPTE is present
  //
  if ((*TableEntry & IA32_PG_P) == 0) {
    return FALSE;
  }

  //
  // Calculate physical address of page-directory-pointer table and index of
  // PDPTE.
  //
  PageDirPtrTable = (UINT64 *)(UINTN)(*TableEntry & PhysicalAddressMask);
  Index = (UINTN)(RShiftU64 ((UINT64)LinearAddress, 30) & IndexMask);

  //
  // Select PDPTE
  //
  TableEntry = &PageDirPtrTable[Index];

  //
  // Check whether a PDPTE or 1GiB page entry is present
  //
  if ((*TableEntry & IA32_PG_P) == 0) {
    return FALSE;
  }

  //
  // Check if PDPTE maps an 1GiB page
  //
  if ((*TableEntry & IA32_PG_PS) != 0) {
    return TRUE;
  }

  //
  // Calculate physical address of page directory table and index of PDE
  //
  PageDirTable = (UINT64 *)(UINTN)(*TableEntry & PhysicalAddressMask);
  Index = (UINTN)(RShiftU64 ((UINT64)LinearAddress, 21) & IndexMask);

  //
  // Select PDE
  //
  TableEntry = &PageDirTable[Index];

  //
  // Check whether a PDE or a 2MiB page entry is present
  //
  if ((*TableEntry & IA32_PG_P) == 0) {
    return FALSE;
  }

  //
  // Check if PDE maps a 2MiB page
  //
  if ((*TableEntry & IA32_PG_PS) != 0) {
    return TRUE;
  }

  //
  // Calculate physical address of page table and index of PTE
  //
  PageTable = (UINT64 *)(UINTN)(*TableEntry & PhysicalAddressMask);
  Index = (UINTN)(RShiftU64 ((UINT64)LinearAddress, 12) & IndexMask);

  //
  // Select PTE
  //
  TableEntry = &PageTable[Index];

  //
  // Check if PTE maps a 4KiB page
  //
  if ((*TableEntry & IA32_PG_P) == 0) {
    return FALSE;
  }

  return TRUE;
}

/**
  Check if a linear address is valid by walking the page tables in 32-bit paging
  mode.

  NOTE: Current UEFI implementations do not support IA32 non-PAE paging mode.

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
  InternalPrintMessage ("!!!! Unsupported IA32 non-PAE paging mode !!!!\n");
  return FALSE;
}

/**
  Check if a linear address is valid by walking the page tables in PAE paging
  mode.

  @param[in]  Cr3             CR3 control register.
  @param[in]  MaxPhyAddrBits  MAXPHYADDR bits.
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
  UINT64  *PageDirPtrTable;
  UINTN   Index;
  UINT64  *PageDirTable;
  UINT64  PhysicalAddressMask;
  UINTN   IndexMask;
  UINT64  *TableEntry;
  UINT64  *PageTable;

  //
  // In 32-bit PAE paging mode, linear addresses are 32 bits wide
  //
  if (LinearAddress > 0xFFFFFFFF) {
    return FALSE;
  }

  //
  // Calculate physical address of page-directory-pointer table and index of
  // PDPTE register.
  //
  PageDirPtrTable = (UINT64 *)(UINTN)(Cr3 & ~0x1F);
  Index = (UINTN)((UINT32)LinearAddress >> 30);

  //
  // Select PDPTE register
  //
  TableEntry = &PageDirPtrTable[Index];

  //
  // Check if PDE is present
  //
  if ((*TableEntry & IA32_PG_P) == 0) {
    return FALSE;
  }

  //
  // Calculate physical address mask (bits M-1:12)
  //
  PhysicalAddressMask = (LShiftU64 (1, MaxPhyAddrBits) - 1) & ~0xFFF;
  //
  // 9 bits for masking page table indexes out of linear addresses
  //
  IndexMask = 0x1FF;

  //
  // Calculate physical address of page directory table and index of PDE
  //
  PageDirTable = (UINT64 *)(UINTN)(*TableEntry & PhysicalAddressMask);
  Index = (UINTN)(RShiftU64 ((UINT64)LinearAddress, 21) & IndexMask);

  //
  // Select PDE
  //
  TableEntry = &PageDirTable[Index];

  //
  // Check whether a PTE or a 2MiB page is present
  //
  if ((*TableEntry & IA32_PG_P) == 0) {
    return FALSE;
  }

  //
  // Check if PDE maps a 2MiB page
  //
  if ((*TableEntry & IA32_PG_PS) != 0) {
    return TRUE;
  }

  //
  // Calculate physical address of page table and index of PTE
  //
  PageTable = (UINT64 *)(UINTN)(*TableEntry & PhysicalAddressMask);
  Index = (UINTN)(RShiftU64 ((UINT64)LinearAddress, 12) & IndexMask);

  //
  // Select PTE
  //
  TableEntry = &PageTable[Index];

  //
  // Check if PTE maps a 4KiB page
  //
  if ((*TableEntry & IA32_PG_P) == 0) {
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
  if ((Cr0 & IA32_CR0_PG) == 0) {
    //
    // If CR4.PAE bit is set, then the linear (or physical) address supports
    // only up to 36 bits.
    //
    if ((UINT64)LinearAddress > 0xFFFFFFFFFULL ||
        ((Cr4 & IA32_CR4_PAE) == 0 && LinearAddress > 0xFFFFFFFF)) {
      return FALSE;
    }

    return TRUE;
  }

  //
  // Paging can be enabled only if CR0.PE bit is set
  //
  if ((Cr0 & IA32_CR0_PE) == 0) {
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
    if ((Edx & IA32_CPUID1_EDX_PAE) != 0) {
      MaxPhyAddrBits = 36;
    } else {
      MaxPhyAddrBits = 32;
    }
  }

  //
  // Check if CR4.PAE bit is not set
  //
  if ((Cr4 & IA32_CR4_PAE) == 0) {
    //
    // Check if linear address is valid in 32-bit paging mode
    //
    AddressValid = Do32BitPagingModeCheck (Cr3, Cr4, LinearAddress);
  } else {
    //
    // In either PAE or 4-level paging mode, physical addresses can hold only
    // up to 52 bits.
    //
    if (MaxPhyAddrBits > 52) {
      return FALSE;
    }

    //
    // Read IA32_EFER MSR register
    //
    Msr.Uint64 = AsmReadMsr64 (MSR_IA32_EFER);

    //
    // Check if IA32_EFER.LME bit is not set (e.g. PAE paging mode)
    //
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
