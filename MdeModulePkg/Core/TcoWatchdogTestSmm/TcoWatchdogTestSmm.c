#include <Uefi.h>

#include <Library/PcdLib.h>
#include <Library/IoLib.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/SmmServicesTableLib.h>

#include <IndustryStandard/Q35MchIch9.h>

STATIC
EFI_STATUS
EFIAPI
TimeoutHandler(IN EFI_HANDLE DispatchHandle,
               IN CONST VOID *Context,
               IN OUT VOID *CommBuffer OPTIONAL,
               IN OUT UINTN *CommBufferSize OPTIONAL)
{
   UINT16 TcoBase = (PcdGet16(PcdAcpiPmBaseAddress) & ~3) + 0x60;
   UINT16 Value;

   Value = IoRead16(TcoBase + 0x04);
   if (Value & BIT3)
   {
      DEBUG((DEBUG_ERROR, "%a: TCO WDT timeout occurred\n", __func__));
      Value &= ~BIT3;
      IoWrite16(TcoBase + 0x04, Value); // clear out timeout status
   }
   return EFI_SUCCESS;
}

/**
  Entry Point for TcoWatchdogTestSmm driver.

  @param ImageHandle     Image handle of this driver.
  @param SystemTable     a Pointer to the EFI System Table.

  @retval  EFI_SUCEESS  Operation completed successfully.
  @return  Others       Some error occurs when installing TcoWatchdogTestSmm driver.

**/
EFI_STATUS
EFIAPI
TcoWatchdogTestSmmEntryPoint(IN EFI_HANDLE ImageHandle,
                             IN EFI_SYSTEM_TABLE *SystemTable)
{
   EFI_STATUS Status;
   EFI_HANDLE Handle = NULL;

   DEBUG((DEBUG_ERROR, "%a: in\n", __func__));

   Status = gSmst->SmiHandlerRegister(TimeoutHandler, NULL, &Handle);
   ASSERT_EFI_ERROR(Status);

   DEBUG((DEBUG_ERROR, "%a: out: %r\n", __func__, Status));

   return Status;
}
