#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/PcdLib.h>
#include <Library/IoLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>

#include <IndustryStandard/Q35MchIch9.h>

STATIC
VOID
StartTcoWdt(UINT16 TcoIoBase)
{
   UINT16 Value;

   Value = IoRead16(TcoIoBase + 0x08);
   Value &= ~BIT11;
   IoWrite16(TcoIoBase + 0x08, Value);
}

STATIC
VOID
StopTcoWdt(UINT16 TcoIoBase)
{
   UINT16 Value;

   Value = IoRead16(TcoIoBase + 0x08);
   Value |= BIT11;
   IoWrite16(TcoIoBase + 0x08, Value);
}

STATIC
VOID
ClearTcoWdtStatus(UINT16 TcoIoBase)
{
   IoWrite16(TcoIoBase + 0x04, 0x0008);
   IoWrite16(TcoIoBase + 0x04, 0x0002);
   IoWrite16(TcoIoBase + 0x06, 0x0004);
}

STATIC
VOID
LoadTcoWdt(UINT16 TcoIoBase)
{
   IoWrite16(TcoIoBase + 0x00, 4);
}

STATIC
VOID
DisableResetOnSecondTimeout(VOID)
{
   UINT32 Value;

   Value = MmioRead32(ICH9_ROOT_COMPLEX_BASE + 0x3410);
   Value |= BIT5;
   MmioWrite32(ICH9_ROOT_COMPLEX_BASE + 0x3410, Value);
}

STATIC
VOID
SetTcoWdtTimeout(UINT16 TcoIoBase, UINT16 Timeout)
{
   IoWrite16(TcoIoBase + 0x12, (Timeout * 10) / 6);
}

EFI_STATUS
EFIAPI
TcoWatchdogTestEntryPoint(IN EFI_HANDLE ImageHandle,
                          IN EFI_SYSTEM_TABLE *SystemTable)
{
   BOOLEAN OldIntFlagState;
   UINT16 PmBase = PcdGet16(PcdAcpiPmBaseAddress) & ~3;
   UINT16 TcoIoBase = PmBase + 0x60;
   UINT32 Value32;

   gBS->SetWatchdogTimer(0, 0, 0xFFFF, NULL);

   OldIntFlagState = SaveAndDisableInterrupts();

   //
   // Enable SMI generation upon WDT timeouts
   //
   Value32 = IoRead32(PmBase + 0x30);
   Value32 |= BIT13;
   IoWrite32(PmBase + 0x30, Value32);

   StopTcoWdt(TcoIoBase);
   ClearTcoWdtStatus(TcoIoBase);
   DisableResetOnSecondTimeout();
   SetTcoWdtTimeout(TcoIoBase, 8);
   LoadTcoWdt(TcoIoBase);
   StartTcoWdt(TcoIoBase);

   SetInterruptState(OldIntFlagState);

   return EFI_SUCCESS;
}
