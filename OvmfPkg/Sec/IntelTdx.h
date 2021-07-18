#ifndef _INTEL_TDX__H_
#define _INTEL_TDX__H_

#include <PiPei.h>
#include <Library/BaseLib.h>
#include <Uefi/UefiSpec.h>
#include <Uefi/UefiBaseType.h>
#include <IndustryStandard/UefiTcgPlatform.h>

/*
  TODO Add description
*/
EFI_STATUS
EFIAPI
TdxStartup (
  IN EFI_SEC_PEI_HAND_OFF             *SecCoreData,
  IN EFI_FIRMWARE_VOLUME_HEADER       *BootFv,
  IN VOID                             *TopOfCurrentStack
  );
#endif
