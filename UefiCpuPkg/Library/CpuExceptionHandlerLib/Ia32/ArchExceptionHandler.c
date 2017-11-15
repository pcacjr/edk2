/** @file
  IA32 CPU Exception Handler functons.

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
  return (UINTN)IdtEntry->Bits.OffsetLow + (((UINTN)IdtEntry->Bits.OffsetHigh) << 16);
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
  ReservedVectors[ExceptionType].OldFlags      = SystemContext.SystemContextIa32->Eflags;
  ReservedVectors[ExceptionType].OldCs         = SystemContext.SystemContextIa32->Cs;
  ReservedVectors[ExceptionType].OldIp         = SystemContext.SystemContextIa32->Eip;
  ReservedVectors[ExceptionType].ExceptionData = SystemContext.SystemContextIa32->ExceptionData;
  //
  // Clear IF flag to avoid old IDT handler enable interrupt by IRET
  //
  Eflags.UintN = SystemContext.SystemContextIa32->Eflags;
  Eflags.Bits.IF = 0;
  SystemContext.SystemContextIa32->Eflags = Eflags.UintN;
  //
  // Modify the EIP in stack, then old IDT handler will return to the stub code
  //
  SystemContext.SystemContextIa32->Eip    = (UINTN) ReservedVectors[ExceptionType].HookAfterStubHeaderCode;
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
  SystemContext.SystemContextIa32->Eflags        = ReservedVectors[ExceptionType].OldFlags;
  SystemContext.SystemContextIa32->Cs            = ReservedVectors[ExceptionType].OldCs;
  SystemContext.SystemContextIa32->Eip           = ReservedVectors[ExceptionType].OldIp;
  SystemContext.SystemContextIa32->ExceptionData = ReservedVectors[ExceptionType].ExceptionData;
}

/**
  Display processor context.

  @param[in] ExceptionType  Exception type.
  @param[in] SystemContext  Processor context to be display.
**/
VOID
EFIAPI
DumpCpuContext (
  IN EFI_EXCEPTION_TYPE   ExceptionType,
  IN EFI_SYSTEM_CONTEXT   SystemContext
  )
{
  InternalPrintMessage (
    "!!!! IA32 Exception Type - %02x(%a)  CPU Apic ID - %08x !!!!\n",
    ExceptionType,
    GetExceptionNameStr (ExceptionType),
    GetApicId ()
    );
  if ((mErrorCodeFlag & (1 << ExceptionType)) != 0) {
    InternalPrintMessage (
      "ExceptionData - %08x",
      SystemContext.SystemContextIa32->ExceptionData
      );
    if (ExceptionType == EXCEPT_IA32_PAGE_FAULT) {
      InternalPrintMessage (
        "  I:%x R:%x U:%x W:%x P:%x PK:%x S:%x",
        (SystemContext.SystemContextIa32->ExceptionData & IA32_PF_EC_ID)   != 0,
        (SystemContext.SystemContextIa32->ExceptionData & IA32_PF_EC_RSVD) != 0,
        (SystemContext.SystemContextIa32->ExceptionData & IA32_PF_EC_US)   != 0,
        (SystemContext.SystemContextIa32->ExceptionData & IA32_PF_EC_WR)   != 0,
        (SystemContext.SystemContextIa32->ExceptionData & IA32_PF_EC_P)    != 0,
        (SystemContext.SystemContextIa32->ExceptionData & IA32_PF_EC_PK)   != 0,
        (SystemContext.SystemContextIa32->ExceptionData & IA32_PF_EC_SGX)  != 0
        );
    }
    InternalPrintMessage ("\n");
  }
  InternalPrintMessage (
    "EIP  - %08x, CS  - %08x, EFLAGS - %08x\n",
    SystemContext.SystemContextIa32->Eip,
    SystemContext.SystemContextIa32->Cs,
    SystemContext.SystemContextIa32->Eflags
    );
  InternalPrintMessage (
    "EAX  - %08x, ECX - %08x, EDX - %08x, EBX - %08x\n",
    SystemContext.SystemContextIa32->Eax,
    SystemContext.SystemContextIa32->Ecx,
    SystemContext.SystemContextIa32->Edx,
    SystemContext.SystemContextIa32->Ebx
    );
  InternalPrintMessage (
    "ESP  - %08x, EBP - %08x, ESI - %08x, EDI - %08x\n",
    SystemContext.SystemContextIa32->Esp,
    SystemContext.SystemContextIa32->Ebp,
    SystemContext.SystemContextIa32->Esi,
    SystemContext.SystemContextIa32->Edi
    );
  InternalPrintMessage (
    "DS   - %08x, ES  - %08x, FS  - %08x, GS  - %08x, SS - %08x\n",
    SystemContext.SystemContextIa32->Ds,
    SystemContext.SystemContextIa32->Es,
    SystemContext.SystemContextIa32->Fs,
    SystemContext.SystemContextIa32->Gs,
    SystemContext.SystemContextIa32->Ss
    );
  InternalPrintMessage (
    "CR0  - %08x, CR2 - %08x, CR3 - %08x, CR4 - %08x\n",
    SystemContext.SystemContextIa32->Cr0,
    SystemContext.SystemContextIa32->Cr2,
    SystemContext.SystemContextIa32->Cr3,
    SystemContext.SystemContextIa32->Cr4
    );
  InternalPrintMessage (
    "DR0  - %08x, DR1 - %08x, DR2 - %08x, DR3 - %08x\n",
    SystemContext.SystemContextIa32->Dr0,
    SystemContext.SystemContextIa32->Dr1,
    SystemContext.SystemContextIa32->Dr2,
    SystemContext.SystemContextIa32->Dr3
    );
  InternalPrintMessage (
    "DR6  - %08x, DR7 - %08x\n",
    SystemContext.SystemContextIa32->Dr6,
    SystemContext.SystemContextIa32->Dr7
    );
  InternalPrintMessage (
    "GDTR - %08x %08x, IDTR - %08x %08x\n",
    SystemContext.SystemContextIa32->Gdtr[0],
    SystemContext.SystemContextIa32->Gdtr[1],
    SystemContext.SystemContextIa32->Idtr[0],
    SystemContext.SystemContextIa32->Idtr[1]
    );
  InternalPrintMessage (
    "LDTR - %08x, TR - %08x\n",
    SystemContext.SystemContextIa32->Ldtr,
    SystemContext.SystemContextIa32->Tr
    );
  InternalPrintMessage (
    "FXSAVE_STATE - %08x\n",
    &SystemContext.SystemContextIa32->FxSaveState
    );
}

/**
  Dump stack trace.

  @param[in]  SystemContext      Pointer to EFI_SYSTEM_CONTEXT.
  @param[out] UnwondStacksCount  Count of unwond stack frames.
**/
STATIC
VOID
DumpStackTrace (
  IN  EFI_SYSTEM_CONTEXT   SystemContext,
  OUT INTN                 *UnwondStacksCount
  )
{
  UINT32  Eip;
  UINT32  Ebp;
  UINTN   ImageBase;
  CHAR8   *PdbFileName;

  //
  // Set current EIP address
  //
  Eip = SystemContext.SystemContextIa32->Eip;

  //
  // Set current frame pointer address
  //
  Ebp = SystemContext.SystemContextIa32->Ebp;

  //
  // Check for proper frame pointer alignment
  //
  if (((UINTN)Ebp & (CPU_STACK_ALIGNMENT - 1)) != 0) {
    InternalPrintMessage ("!!!! Unaligned frame pointer. !!!!\n");
    return;
  }

  //
  // Get initial PE/COFF image base address from current EIP
  //
  ImageBase = PeCoffSearchImageBase (Eip);
  if (ImageBase == 0) {
    InternalPrintMessage ("!!!! Could not find backtrace information. !!!!");
    return;
  }

  //
  // Get PDB file name from initial PE/COFF image
  //
  GetPdbFileName (ImageBase, NULL, &PdbFileName);

  //
  // Initialize count of unwond stacks
  //
  *UnwondStacksCount = 1;

  //
  // Print out back trace
  //
  InternalPrintMessage ("\nCall trace:\n");

  for (;;) {
    //
    // Print stack frame in the following format:
    //
    // # <EIP> @ <ImageBase>+<RelOffset> (EBP) in [<ModuleName> | ????]
    //
    InternalPrintMessage (
      "%d 0x%08x @ 0x%08x+0x%x (0x%08x) in %a\n",
      *UnwondStacksCount - 1,
      Eip,
      ImageBase,
      Eip - ImageBase - 1,
      Ebp,
      PdbFileName
      );

    //
    // Set EIP with return address from current stack frame
    //
    Eip = *(UINT32 *)((UINTN)Ebp + 4);

    //
    // If EIP is zero, then stop unwinding the stack
    //
    if (Eip == 0) {
      break;
    }

    //
    // Check if EIP is within another PE/COFF image base address
    //
    if (Eip < ImageBase) {
      //
      // Search for the respective PE/COFF image based on EIP
      //
      ImageBase = PeCoffSearchImageBase (Eip);
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
    }

    //
    // Unwind the stack
    //
    Ebp = *(UINT32 *)(UINTN)Ebp;

    //
    // Increment count of unwond stacks
    //
    (*UnwondStacksCount)++;
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
  UINT32      Eip;
  UINT32      Ebp;
  UINTN       ImageBase;
  VOID        *EntryPoint;
  CHAR8       *PdbAbsoluteFilePath;
  CHAR8       *PdbFileName;

  //
  // Set current EIP address
  //
  Eip = SystemContext.SystemContextIa32->Eip;

  //
  // Set current frame pointer address
  //
  Ebp = SystemContext.SystemContextIa32->Ebp;

  //
  // Get initial PE/COFF image base address from current EIP
  //
  ImageBase = PeCoffSearchImageBase (Eip);
  if (ImageBase == 0) {
    InternalPrintMessage ("!!!! Could not find image module names. !!!!");
    return;
  }

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
      "\n%a (ImageBase=0x%08x, EntryPoint=0x%08x):\n",
      PdbFileName,
      ImageBase,
      (UINTN)EntryPoint
      );
    InternalPrintMessage ("%a\n", PdbAbsoluteFilePath);
  }

  //
  // Walk through call stack and find next module names
  //
  for (;;) {
    //
    // Set EIP with return address from current stack frame
    //
    Eip = *(UINT32 *)((UINTN)Ebp + 4);

    //
    // Check if EIP is within another PE/COFF image base address
    //
    if (Eip < ImageBase) {
      //
      // Search for the respective PE/COFF image based on Eip
      //
      ImageBase = PeCoffSearchImageBase (Eip);
      if (ImageBase == 0) {
        //
        // Stop stack trace
        //
        break;
      }

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
          "%a (ImageBase=0x%08x, EntryPoint=0x%08x):\n",
          PdbFileName,
          ImageBase,
          (UINTN)EntryPoint
          );
        InternalPrintMessage ("%a\n", PdbAbsoluteFilePath);
      }
    }

    //
    // Unwind the stack
    //
    Ebp = *(UINT32 *)(UINTN)Ebp;
  }
}

/**
  Dump stack contents.

  @param[in]  CurrentEsp         Current stack pointer address.
  @param[in]  UnwondStacksCount  Count of unwond stack frames.
**/
STATIC
VOID
DumpStackContents (
  IN UINT32  CurrentEsp,
  IN INTN    UnwondStacksCount
  )
{
  //
  // Check for proper stack alignment
  //
  if (((UINTN)CurrentEsp & (CPU_STACK_ALIGNMENT - 1)) != 0) {
    InternalPrintMessage ("!!!! Unaligned stack pointer. !!!!\n");
    return;
  }

  //
  // Dump out stack contents
  //
  InternalPrintMessage ("\nStack dump:\n");
  while (UnwondStacksCount-- > 0) {
    InternalPrintMessage (
      "0x%08x: %08x %08x\n",
      CurrentEsp,
      *(UINT32 *)CurrentEsp,
      *(UINT32 *)((UINTN)CurrentEsp + 4)
      );

    //
    // Point to next stack
    //
    CurrentEsp += CPU_STACK_ALIGNMENT;
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
  INTN UnwondStacksCount;

  //
  // Dump CPU context
  //
  DumpCpuContext (ExceptionType, SystemContext);

  //
  // Dump stack trace
  //
  DumpStackTrace (SystemContext, &UnwondStacksCount);

  //
  // Dump image module names
  //
  DumpImageModuleNames (SystemContext);

  //
  // Dump stack contents
  //
  DumpStackContents (SystemContext.SystemContextIa32->Esp, UnwondStacksCount);
}
