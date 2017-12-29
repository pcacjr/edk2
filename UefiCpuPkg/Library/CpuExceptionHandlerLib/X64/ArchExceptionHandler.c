/** @file
  x64 CPU Exception Handler.

  Copyright (c) 2012 - 2017, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "CpuExceptionCommon.h"

/**
  Return address map of exception handler template so that C code can generate
  exception tables.

  @param IdtEntry          Pointer to IDT entry to be updated.
  @param InterruptHandler  IDT handler value.
**/
VOID
ArchUpdateIdtEntry (
  IN IA32_IDT_GATE_DESCRIPTOR        *IdtEntry,
  IN UINTN                           InterruptHandler
  )
{
  IdtEntry->Bits.OffsetLow   = (UINT16)(UINTN)InterruptHandler;
  IdtEntry->Bits.OffsetHigh  = (UINT16)((UINTN)InterruptHandler >> 16);
  IdtEntry->Bits.OffsetUpper = (UINT32)((UINTN)InterruptHandler >> 32);
  IdtEntry->Bits.GateType    = IA32_IDT_GATE_TYPE_INTERRUPT_32;
}

/**
  Read IDT handler value from IDT entry.

  @param IdtEntry          Pointer to IDT entry to be read.

**/
UINTN
ArchGetIdtHandler (
  IN IA32_IDT_GATE_DESCRIPTOR        *IdtEntry
  )
{
  return IdtEntry->Bits.OffsetLow + (((UINTN) IdtEntry->Bits.OffsetHigh)  << 16) +
                                    (((UINTN) IdtEntry->Bits.OffsetUpper) << 32);
}

/**
  Save CPU exception context when handling EFI_VECTOR_HANDOFF_HOOK_AFTER case.

  @param[in] ExceptionType        Exception type.
  @param[in] SystemContext        Pointer to EFI_SYSTEM_CONTEXT.
  @param[in] ExceptionHandlerData Pointer to exception handler data.
**/
VOID
ArchSaveExceptionContext (
  IN UINTN                        ExceptionType,
  IN EFI_SYSTEM_CONTEXT           SystemContext,
  IN EXCEPTION_HANDLER_DATA       *ExceptionHandlerData
  )
{
  IA32_EFLAGS32           Eflags;
  RESERVED_VECTORS_DATA   *ReservedVectors;

  ReservedVectors = ExceptionHandlerData->ReservedVectors;
  //
  // Save Exception context in global variable
  //
  ReservedVectors[ExceptionType].OldSs         = SystemContext.SystemContextX64->Ss;
  ReservedVectors[ExceptionType].OldSp         = SystemContext.SystemContextX64->Rsp;
  ReservedVectors[ExceptionType].OldFlags      = SystemContext.SystemContextX64->Rflags;
  ReservedVectors[ExceptionType].OldCs         = SystemContext.SystemContextX64->Cs;
  ReservedVectors[ExceptionType].OldIp         = SystemContext.SystemContextX64->Rip;
  ReservedVectors[ExceptionType].ExceptionData = SystemContext.SystemContextX64->ExceptionData;
  //
  // Clear IF flag to avoid old IDT handler enable interrupt by IRET
  //
  Eflags.UintN = SystemContext.SystemContextX64->Rflags;
  Eflags.Bits.IF = 0;
  SystemContext.SystemContextX64->Rflags = Eflags.UintN;
  //
  // Modify the EIP in stack, then old IDT handler will return to the stub code
  //
  SystemContext.SystemContextX64->Rip = (UINTN) ReservedVectors[ExceptionType].HookAfterStubHeaderCode;
}

/**
  Restore CPU exception context when handling EFI_VECTOR_HANDOFF_HOOK_AFTER case.

  @param[in] ExceptionType        Exception type.
  @param[in] SystemContext        Pointer to EFI_SYSTEM_CONTEXT.
  @param[in] ExceptionHandlerData Pointer to exception handler data.
**/
VOID
ArchRestoreExceptionContext (
  IN UINTN                        ExceptionType,
  IN EFI_SYSTEM_CONTEXT           SystemContext,
  IN EXCEPTION_HANDLER_DATA       *ExceptionHandlerData
  )
{
  RESERVED_VECTORS_DATA   *ReservedVectors;

  ReservedVectors = ExceptionHandlerData->ReservedVectors;
  SystemContext.SystemContextX64->Ss            = ReservedVectors[ExceptionType].OldSs;
  SystemContext.SystemContextX64->Rsp           = ReservedVectors[ExceptionType].OldSp;
  SystemContext.SystemContextX64->Rflags        = ReservedVectors[ExceptionType].OldFlags;
  SystemContext.SystemContextX64->Cs            = ReservedVectors[ExceptionType].OldCs;
  SystemContext.SystemContextX64->Rip           = ReservedVectors[ExceptionType].OldIp;
  SystemContext.SystemContextX64->ExceptionData = ReservedVectors[ExceptionType].ExceptionData;
}

/**
  Setup separate stack for given exceptions.

  @param[in] StackSwitchData      Pointer to data required for setuping up
                                  stack switch.

  @retval EFI_SUCCESS             The exceptions have been successfully
                                  initialized with new stack.
  @retval EFI_INVALID_PARAMETER   StackSwitchData contains invalid content.

**/
EFI_STATUS
ArchSetupExcpetionStack (
  IN CPU_EXCEPTION_INIT_DATA          *StackSwitchData
  )
{
  IA32_DESCRIPTOR                   Gdtr;
  IA32_DESCRIPTOR                   Idtr;
  IA32_IDT_GATE_DESCRIPTOR          *IdtTable;
  IA32_TSS_DESCRIPTOR               *TssDesc;
  IA32_TASK_STATE_SEGMENT           *Tss;
  UINTN                             StackTop;
  UINTN                             Index;
  UINTN                             Vector;
  UINTN                             TssBase;
  UINTN                             GdtSize;

  if (StackSwitchData == NULL ||
      StackSwitchData->Ia32.Revision != CPU_EXCEPTION_INIT_DATA_REV ||
      StackSwitchData->X64.KnownGoodStackTop == 0 ||
      StackSwitchData->X64.KnownGoodStackSize == 0 ||
      StackSwitchData->X64.StackSwitchExceptions == NULL ||
      StackSwitchData->X64.StackSwitchExceptionNumber == 0 ||
      StackSwitchData->X64.StackSwitchExceptionNumber > CPU_EXCEPTION_NUM ||
      StackSwitchData->X64.GdtTable == NULL ||
      StackSwitchData->X64.IdtTable == NULL ||
      StackSwitchData->X64.ExceptionTssDesc == NULL ||
      StackSwitchData->X64.ExceptionTss == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // The caller is responsible for that the GDT table, no matter the existing
  // one or newly allocated, has enough space to hold descriptors for exception
  // task-state segments.
  //
  if (((UINTN)StackSwitchData->X64.GdtTable & (IA32_GDT_ALIGNMENT - 1)) != 0) {
    return EFI_INVALID_PARAMETER;
  }

  if ((UINTN)StackSwitchData->X64.ExceptionTssDesc < (UINTN)(StackSwitchData->X64.GdtTable)) {
    return EFI_INVALID_PARAMETER;
  }

  if (((UINTN)StackSwitchData->X64.ExceptionTssDesc + StackSwitchData->X64.ExceptionTssDescSize) >
      ((UINTN)(StackSwitchData->X64.GdtTable) + StackSwitchData->X64.GdtTableSize)) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // One task gate descriptor and one task-state segment are needed.
  //
  if (StackSwitchData->X64.ExceptionTssDescSize < sizeof (IA32_TSS_DESCRIPTOR)) {
    return EFI_INVALID_PARAMETER;
  }
  if (StackSwitchData->X64.ExceptionTssSize < sizeof (IA32_TASK_STATE_SEGMENT)) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Interrupt stack table supports only 7 vectors.
  //
  TssDesc = StackSwitchData->X64.ExceptionTssDesc;
  Tss     = StackSwitchData->X64.ExceptionTss;
  if (StackSwitchData->X64.StackSwitchExceptionNumber > ARRAY_SIZE (Tss->IST)) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Initialize new GDT table and/or IDT table, if any
  //
  AsmReadIdtr (&Idtr);
  AsmReadGdtr (&Gdtr);

  GdtSize = (UINTN)TssDesc + sizeof (IA32_TSS_DESCRIPTOR) -
            (UINTN)(StackSwitchData->X64.GdtTable);
  if ((UINTN)StackSwitchData->X64.GdtTable != Gdtr.Base) {
    CopyMem (StackSwitchData->X64.GdtTable, (VOID *)Gdtr.Base, Gdtr.Limit + 1);
    Gdtr.Base = (UINTN)StackSwitchData->X64.GdtTable;
    Gdtr.Limit = (UINT16)GdtSize - 1;
  }

  if ((UINTN)StackSwitchData->X64.IdtTable != Idtr.Base) {
    Idtr.Base = (UINTN)StackSwitchData->X64.IdtTable;
  }
  if (StackSwitchData->X64.IdtTableSize > 0) {
    Idtr.Limit = (UINT16)(StackSwitchData->X64.IdtTableSize - 1);
  }

  //
  // Fixup current task descriptor. Task-state segment for current task will
  // be filled by processor during task switching.
  //
  TssBase = (UINTN)Tss;

  TssDesc->Bits.LimitLow   = sizeof(IA32_TASK_STATE_SEGMENT) - 1;
  TssDesc->Bits.BaseLow    = (UINT16)TssBase;
  TssDesc->Bits.BaseMidl   = (UINT8)(TssBase >> 16);
  TssDesc->Bits.Type       = IA32_GDT_TYPE_TSS;
  TssDesc->Bits.P          = 1;
  TssDesc->Bits.LimitHigh  = 0;
  TssDesc->Bits.BaseMidh   = (UINT8)(TssBase >> 24);
  TssDesc->Bits.BaseHigh   = (UINT32)(TssBase >> 32);

  //
  // Fixup exception task descriptor and task-state segment
  //
  StackTop = StackSwitchData->X64.KnownGoodStackTop - CPU_STACK_ALIGNMENT;
  StackTop = (UINTN)ALIGN_POINTER (StackTop, CPU_STACK_ALIGNMENT);
  IdtTable = StackSwitchData->X64.IdtTable;
  for (Index = 0; Index < StackSwitchData->X64.StackSwitchExceptionNumber; ++Index) {
    //
    // Fixup IST
    //
    Tss->IST[Index] = StackTop;
    StackTop -= StackSwitchData->X64.KnownGoodStackSize;

    //
    // Set the IST field to enable corresponding IST
    //
    Vector = StackSwitchData->X64.StackSwitchExceptions[Index];
    if (Vector >= CPU_EXCEPTION_NUM ||
        Vector >= (Idtr.Limit + 1) / sizeof (IA32_IDT_GATE_DESCRIPTOR)) {
      continue;
    }
    IdtTable[Vector].Bits.Reserved_0 = (UINT8)(Index + 1);
  }

  //
  // Publish GDT
  //
  AsmWriteGdtr (&Gdtr);

  //
  // Load current task
  //
  AsmWriteTr ((UINT16)((UINTN)StackSwitchData->X64.ExceptionTssDesc - Gdtr.Base));

  //
  // Publish IDT
  //
  AsmWriteIdtr (&Idtr);

  return EFI_SUCCESS;
}

/**
  Display CPU information.

  @param ExceptionType  Exception type.
  @param SystemContext  Pointer to EFI_SYSTEM_CONTEXT.
**/
VOID
EFIAPI
DumpCpuContext (
  IN EFI_EXCEPTION_TYPE   ExceptionType,
  IN EFI_SYSTEM_CONTEXT   SystemContext
  )
{
  InternalPrintMessage (
    "!!!! X64 Exception Type - %02x(%a)  CPU Apic ID - %08x !!!!\n",
    ExceptionType,
    GetExceptionNameStr (ExceptionType),
    GetApicId ()
    );
  if ((mErrorCodeFlag & (1 << ExceptionType)) != 0) {
    InternalPrintMessage (
      "ExceptionData - %016lx",
      SystemContext.SystemContextX64->ExceptionData
      );
    if (ExceptionType == EXCEPT_IA32_PAGE_FAULT) {
      InternalPrintMessage (
        "  I:%x R:%x U:%x W:%x P:%x PK:%x S:%x",
        (SystemContext.SystemContextX64->ExceptionData & IA32_PF_EC_ID)   != 0,
        (SystemContext.SystemContextX64->ExceptionData & IA32_PF_EC_RSVD) != 0,
        (SystemContext.SystemContextX64->ExceptionData & IA32_PF_EC_US)   != 0,
        (SystemContext.SystemContextX64->ExceptionData & IA32_PF_EC_WR)   != 0,
        (SystemContext.SystemContextX64->ExceptionData & IA32_PF_EC_P)    != 0,
        (SystemContext.SystemContextX64->ExceptionData & IA32_PF_EC_PK)   != 0,
        (SystemContext.SystemContextX64->ExceptionData & IA32_PF_EC_SGX)  != 0
        );
    }
    InternalPrintMessage ("\n");
  }
  InternalPrintMessage (
    "RIP  - %016lx, CS  - %016lx, RFLAGS - %016lx\n",
    SystemContext.SystemContextX64->Rip,
    SystemContext.SystemContextX64->Cs,
    SystemContext.SystemContextX64->Rflags
    );
  InternalPrintMessage (
    "RAX  - %016lx, RCX - %016lx, RDX - %016lx\n",
    SystemContext.SystemContextX64->Rax,
    SystemContext.SystemContextX64->Rcx,
    SystemContext.SystemContextX64->Rdx
    );
  InternalPrintMessage (
    "RBX  - %016lx, RSP - %016lx, RBP - %016lx\n",
    SystemContext.SystemContextX64->Rbx,
    SystemContext.SystemContextX64->Rsp,
    SystemContext.SystemContextX64->Rbp
    );
  InternalPrintMessage (
    "RSI  - %016lx, RDI - %016lx\n",
    SystemContext.SystemContextX64->Rsi,
    SystemContext.SystemContextX64->Rdi
    );
  InternalPrintMessage (
    "R8   - %016lx, R9  - %016lx, R10 - %016lx\n",
    SystemContext.SystemContextX64->R8,
    SystemContext.SystemContextX64->R9,
    SystemContext.SystemContextX64->R10
    );
  InternalPrintMessage (
    "R11  - %016lx, R12 - %016lx, R13 - %016lx\n",
    SystemContext.SystemContextX64->R11,
    SystemContext.SystemContextX64->R12,
    SystemContext.SystemContextX64->R13
    );
  InternalPrintMessage (
    "R14  - %016lx, R15 - %016lx\n",
    SystemContext.SystemContextX64->R14,
    SystemContext.SystemContextX64->R15
    );
  InternalPrintMessage (
    "DS   - %016lx, ES  - %016lx, FS  - %016lx\n",
    SystemContext.SystemContextX64->Ds,
    SystemContext.SystemContextX64->Es,
    SystemContext.SystemContextX64->Fs
    );
  InternalPrintMessage (
    "GS   - %016lx, SS  - %016lx\n",
    SystemContext.SystemContextX64->Gs,
    SystemContext.SystemContextX64->Ss
    );
  InternalPrintMessage (
    "CR0  - %016lx, CR2 - %016lx, CR3 - %016lx\n",
    SystemContext.SystemContextX64->Cr0,
    SystemContext.SystemContextX64->Cr2,
    SystemContext.SystemContextX64->Cr3
    );
  InternalPrintMessage (
    "CR4  - %016lx, CR8 - %016lx\n",
    SystemContext.SystemContextX64->Cr4,
    SystemContext.SystemContextX64->Cr8
    );
  InternalPrintMessage (
    "DR0  - %016lx, DR1 - %016lx, DR2 - %016lx\n",
    SystemContext.SystemContextX64->Dr0,
    SystemContext.SystemContextX64->Dr1,
    SystemContext.SystemContextX64->Dr2
    );
  InternalPrintMessage (
    "DR3  - %016lx, DR6 - %016lx, DR7 - %016lx\n",
    SystemContext.SystemContextX64->Dr3,
    SystemContext.SystemContextX64->Dr6,
    SystemContext.SystemContextX64->Dr7
    );
  InternalPrintMessage (
    "GDTR - %016lx %016lx, LDTR - %016lx\n",
    SystemContext.SystemContextX64->Gdtr[0],
    SystemContext.SystemContextX64->Gdtr[1],
    SystemContext.SystemContextX64->Ldtr
    );
  InternalPrintMessage (
    "IDTR - %016lx %016lx,   TR - %016lx\n",
    SystemContext.SystemContextX64->Idtr[0],
    SystemContext.SystemContextX64->Idtr[1],
    SystemContext.SystemContextX64->Tr
    );
  InternalPrintMessage (
    "FXSAVE_STATE - %016lx\n",
    &SystemContext.SystemContextX64->FxSaveState
    );
}

/**
  Dump stack contents.

  @param[in]  SystemContext       Pointer to EFI_SYSTEM_CONTEXT.
  @param[in]  UnwoundStacksCount  Count of unwound stack frames.
**/
STATIC
VOID
DumpStackContents (
  IN  EFI_SYSTEM_CONTEXT  SystemContext,
  IN  INTN                UnwoundStacksCount
  )
{
  UINT64  CurrentRsp;
  UINTN   Cr0;
  UINTN   Cr3;
  UINTN   Cr4;

  //
  // Get current stack pointer
  //
  CurrentRsp = SystemContext.SystemContextX64->Rsp;

  //
  // Check for proper stack pointer alignment
  //
  if (((UINTN)CurrentRsp & (CPU_STACK_ALIGNMENT - 1)) != 0) {
    InternalPrintMessage ("!!!! Unaligned stack pointer. !!!!\n");
    return;
  }

  //
  // Get system control registers
  //
  Cr0 = SystemContext.SystemContextX64->Cr0;
  Cr3 = SystemContext.SystemContextX64->Cr3;
  Cr4 = SystemContext.SystemContextX64->Cr4;

  //
  // Dump out stack contents
  //
  InternalPrintMessage ("\nStack dump:\n");
  while (UnwoundStacksCount-- > 0) {
    //
    // Check for a valid stack pointer address
    //
    if (!IsLinearAddressValid (Cr0, Cr3, Cr4, (UINTN)CurrentRsp) ||
        !IsLinearAddressValid (Cr0, Cr3, Cr4, (UINTN)CurrentRsp + 8)) {
      InternalPrintMessage ("%a: attempted to dereference an invalid stack "
                            "pointer at 0x%016lx\n", __FUNCTION__, CurrentRsp);
      break;
    }

    InternalPrintMessage (
      "0x%016lx: %016lx %016lx\n",
      CurrentRsp,
      *(UINT64 *)CurrentRsp,
      *(UINT64 *)((UINTN)CurrentRsp + 8)
      );

    //
    // Point to next stack
    //
    CurrentRsp += CPU_STACK_ALIGNMENT;
  }
}

/**
  Dump all image module names from call stack.

  @param[in]  SystemContext  Pointer to EFI_SYSTEM_CONTEXT.
**/
STATIC
VOID
DumpImageModuleNames (
  IN EFI_SYSTEM_CONTEXT   SystemContext
  )
{
  EFI_STATUS  Status;
  UINT64      Rip;
  UINTN       ImageBase;
  VOID        *EntryPoint;
  CHAR8       *PdbAbsoluteFilePath;
  CHAR8       *PdbFileName;
  UINT64      Rbp;
  UINTN       LastImageBase;
  UINTN       Cr0;
  UINTN       Cr3;
  UINTN       Cr4;

  //
  // Set current RIP address
  //
  Rip = SystemContext.SystemContextX64->Rip;

  //
  // Set current frame pointer address
  //
  Rbp = SystemContext.SystemContextX64->Rbp;

  //
  // Check for proper frame pointer alignment
  //
  if (((UINTN)Rbp & (CPU_STACK_ALIGNMENT - 1)) != 0) {
    InternalPrintMessage ("!!!! Unaligned frame pointer. !!!!\n");
    return;
  }

  //
  // Get initial PE/COFF image base address from current RIP
  //
  ImageBase = PeCoffSearchImageBase (Rip);
  if (ImageBase == 0) {
    InternalPrintMessage ("!!!! Could not find image module names. !!!!");
    return;
  }

  //
  // Set last PE/COFF image base address
  //
  LastImageBase = ImageBase;

  //
  // Get initial PE/COFF image's entry point
  //
  Status = PeCoffLoaderGetEntryPoint ((VOID *)ImageBase, &EntryPoint);
  if (EFI_ERROR (Status)) {
    EntryPoint = NULL;
  }

  //
  // Get file name and absolute path of initial PDB file
  //
  GetPdbFileName (ImageBase, &PdbAbsoluteFilePath, &PdbFileName);

  //
  // Print out initial image module name (if any)
  //
  if (PdbAbsoluteFilePath != NULL) {
    InternalPrintMessage (
      "\n%a (ImageBase=0x%016lx, EntryPoint=0x%016lx):\n",
      PdbFileName,
      ImageBase,
      (UINTN)EntryPoint
      );
    InternalPrintMessage ("%a\n", PdbAbsoluteFilePath);
  }

  //
  // Get system control registers
  //
  Cr0 = SystemContext.SystemContextX64->Cr0;
  Cr3 = SystemContext.SystemContextX64->Cr3;
  Cr4 = SystemContext.SystemContextX64->Cr4;

  //
  // Walk through call stack and find next module names
  //
  for (;;) {
    //
    // Check for a valid frame pointer
    //
    if (!IsLinearAddressValid (Cr0, Cr3, Cr4, (UINTN)Rbp + 8) ||
        !IsLinearAddressValid (Cr0, Cr3, Cr4, (UINTN)Rbp)) {
      InternalPrintMessage ("%a: attempted to dereference an invalid frame "
                            "pointer at 0x%016lx\n", __FUNCTION__, Rbp);
      break;
    }

    //
    // Set RIP with return address from current stack frame
    //
    Rip = *(UINT64 *)((UINTN)Rbp + 8);

    //
    // If RIP is zero, then stop unwinding the stack
    //
    if (Rip == 0) {
      break;
    }

    //
    // Search for the respective PE/COFF image based on RIP
    //
    ImageBase = PeCoffSearchImageBase (Rip);
    if (ImageBase == 0) {
      //
      // Stop stack trace
      //
      break;
    }

    //
    // If RIP points to another PE/COFF image, then find its respective PDB file
    // name.
    //
    if (LastImageBase != ImageBase) {
      //
      // Get PE/COFF image's entry point
      //
      Status = PeCoffLoaderGetEntryPoint ((VOID *)ImageBase, &EntryPoint);
      if (EFI_ERROR (Status)) {
        EntryPoint = NULL;
      }

      //
      // Get file name and absolute path of PDB file
      //
      GetPdbFileName (ImageBase, &PdbAbsoluteFilePath, &PdbFileName);

      //
      // Print out image module name (if any)
      //
      if (PdbAbsoluteFilePath != NULL) {
        InternalPrintMessage (
          "%a (ImageBase=0x%016lx, EntryPoint=0x%016lx):\n",
          PdbFileName,
          ImageBase,
          (UINTN)EntryPoint
          );
        InternalPrintMessage ("%a\n", PdbAbsoluteFilePath);
      }

      //
      // Save last PE/COFF image base address
      //
      LastImageBase = ImageBase;
    }

    //
    // Unwind the stack
    //
    Rbp = *(UINT64 *)(UINTN)Rbp;
  }
}

/**
  Dump stack trace.

  @param[in]  SystemContext      Pointer to EFI_SYSTEM_CONTEXT.
  @param[out] UnwoundStacksCount  Count of unwound stack frames.
**/
STATIC
VOID
DumpStackTrace (
  IN  EFI_SYSTEM_CONTEXT   SystemContext,
  OUT INTN                 *UnwoundStacksCount
  )
{
  UINT64  Rip;
  UINT64  Rbp;
  UINTN   ImageBase;
  CHAR8   *PdbFileName;
  UINTN   Cr0;
  UINTN   Cr3;
  UINTN   Cr4;

  //
  // Set current RIP address
  //
  Rip = SystemContext.SystemContextX64->Rip;

  //
  // Set current frame pointer address
  //
  Rbp = SystemContext.SystemContextX64->Rbp;

  //
  // Get initial PE/COFF image base address from current RIP
  //
  ImageBase = PeCoffSearchImageBase (Rip);
  if (ImageBase == 0) {
    InternalPrintMessage ("!!!! Could not find backtrace information. !!!!");
    return;
  }

  //
  // Get PDB file name from initial PE/COFF image
  //
  GetPdbFileName (ImageBase, NULL, &PdbFileName);

  //
  // Initialize count of unwound stacks
  //
  *UnwoundStacksCount = 1;

  //
  // Get system control registers
  //
  Cr0 = SystemContext.SystemContextX64->Cr0;
  Cr3 = SystemContext.SystemContextX64->Cr3;
  Cr4 = SystemContext.SystemContextX64->Cr4;

  //
  // Print out back trace
  //
  InternalPrintMessage ("\nCall trace:\n");

  for (;;) {
    //
    // Check for valid frame pointer
    //
    if (!IsLinearAddressValid (Cr0, Cr3, Cr4, (UINTN)Rbp + 8) ||
        !IsLinearAddressValid (Cr0, Cr3, Cr4, (UINTN)Rbp)) {
      InternalPrintMessage ("%a: attempted to dereference an invalid frame "
                            "pointer at 0x%016lx\n", __FUNCTION__, Rbp);
      break;
    }

    //
    // Print stack frame in the following format:
    //
    // # <RIP> @ <ImageBase>+<RelOffset> (RBP) in [<ModuleName> | ????]
    //
    InternalPrintMessage (
      "%d 0x%016lx @ 0x%016lx+0x%x (0x%016lx) in %a\n",
      *UnwoundStacksCount - 1,
      Rip,
      ImageBase,
      Rip - ImageBase,
      Rbp,
      PdbFileName
      );

    //
    // Set RIP with return address from current stack frame
    //
    Rip = *(UINT64 *)((UINTN)Rbp + 8);

    //
    // If RIP is zero, then stop unwinding the stack
    //
    if (Rip == 0) {
      break;
    }

    //
    // Search for the respective PE/COFF image based on RIP
    //
    ImageBase = PeCoffSearchImageBase (Rip);
    if (ImageBase == 0) {
      //
      // Stop stack trace
      //
      break;
    }

    //
    // Get PDB file name
    //
    GetPdbFileName (ImageBase, NULL, &PdbFileName);

    //
    // Unwind the stack
    //
    Rbp = *(UINT64 *)(UINTN)Rbp;

    //
    // Increment count of unwound stacks
    //
    (*UnwoundStacksCount)++;
  }
}

/**
  Display CPU information.

  @param ExceptionType  Exception type.
  @param SystemContext  Pointer to EFI_SYSTEM_CONTEXT.
**/
VOID
DumpImageAndCpuContent (
  IN EFI_EXCEPTION_TYPE   ExceptionType,
  IN EFI_SYSTEM_CONTEXT   SystemContext
  )
{
  INTN UnwoundStacksCount;

  //
  // Dump CPU context
  //
  DumpCpuContext (ExceptionType, SystemContext);

  //
  // Dump stack trace
  //
  DumpStackTrace (SystemContext, &UnwoundStacksCount);

  //
  // Dump image module names
  //
  DumpImageModuleNames (SystemContext);

  //
  // Dump stack contents
  //
  DumpStackContents (SystemContext, UnwoundStacksCount);
}
