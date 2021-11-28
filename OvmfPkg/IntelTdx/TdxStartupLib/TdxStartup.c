/** @file

  Copyright (c) 2021, Intel Corporation. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiPei.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Protocol/DebugSupport.h>
#include <Library/TdxLib.h>
#include <IndustryStandard/Tdx.h>
#include <Library/TdxMailboxLib.h>
#include <Library/TdxPlatformLib.h>
#include <Library/PrePiLib.h>
#include <Library/TdxStartupLib.h>
#include "TdxStartupInternal.h"

#define GET_GPAW_INIT_STATE(INFO)  ((UINT8) ((INFO) & 0x3f))

EFI_STATUS
EFIAPI
RelocateMailbox (
  EFI_HOB_PLATFORM_INFO       *PlatformInfoHob
  )
{
  VOID                        *Address;
  VOID                        *ApLoopFunc;
  UINT32                      RelocationPages;
  MP_RELOCATION_MAP           RelocationMap;
  MP_WAKEUP_MAILBOX           *RelocatedMailBox;

  Address     = NULL;
  ApLoopFunc  = NULL;

  //
  // Get information needed to setup aps running in their
  // run loop in allocated acpi reserved memory
  // Add another page for mailbox
  //
  AsmGetRelocationMap (&RelocationMap);
  RelocationPages  = EFI_SIZE_TO_PAGES ((UINT32)RelocationMap.RelocateApLoopFuncSize) + 1;

  Address = AllocatePagesWithMemoryType (EfiACPIMemoryNVS, RelocationPages);
  if (Address == NULL) {
    ASSERT (FALSE);
    return EFI_OUT_OF_RESOURCES;
  }

  ApLoopFunc = (VOID *) ((UINTN) Address + EFI_PAGE_SIZE);

  CopyMem (
    ApLoopFunc,
    RelocationMap.RelocateApLoopFuncAddress,
    RelocationMap.RelocateApLoopFuncSize
    );

  DEBUG ((DEBUG_INFO, "Ap Relocation: mailbox %p, loop %p\n",
    Address, ApLoopFunc));

  //
  // Initialize mailbox
  //
  RelocatedMailBox = (MP_WAKEUP_MAILBOX *)Address;
  RelocatedMailBox->Command = MpProtectedModeWakeupCommandNoop;
  RelocatedMailBox->ApicId = MP_CPU_PROTECTED_MODE_MAILBOX_APICID_INVALID;
  RelocatedMailBox->WakeUpVector = 0;

  PlatformInfoHob->RelocatedMailBox = (UINT64)RelocatedMailBox;

  //
  // Wakup APs and have been move to the finalized run loop
  // They will spin until guest OS wakes them
  //
  MpSerializeStart ();

  MpSendWakeupCommand (
    MpProtectedModeWakeupCommandWakeup,
    (UINT64)ApLoopFunc,
    (UINT64)RelocatedMailBox,
    0,
    0,
    0);

  return EFI_SUCCESS;
}

/**
 * @brief
 *
 * @param Context
 * @return VOID
 */
VOID
EFIAPI
TdxStartup(
  IN VOID                           *Context
  )
{
  EFI_SEC_PEI_HAND_OFF        *SecCoreData;
  EFI_FIRMWARE_VOLUME_HEADER  *BootFv;
  EFI_STATUS                  Status;
  EFI_HOB_PLATFORM_INFO       PlatformInfoHob;
  UINT32                      DxeCodeBase;
  UINT32                      DxeCodeSize;
  TD_RETURN_DATA              TdReturnData;
  UINT8                       *PlatformInfoPtr;
  VOID                        *VmmHobList;
  BOOLEAN                     CfgSysStateDefault;
  BOOLEAN                     CfgNxStackDefault;

  Status      = EFI_SUCCESS;
  BootFv      = NULL;
  SecCoreData = (EFI_SEC_PEI_HAND_OFF *)Context;
  VmmHobList  = (VOID *) (UINTN) FixedPcdGet32 (PcdOvmfSecGhcbBase);

  Status = TdCall (TDCALL_TDINFO, 0, 0, 0, &TdReturnData);
  ASSERT (Status == EFI_SUCCESS);

  DEBUG ((EFI_D_INFO,
    "Tdx started with(Hob: 0x%x, Gpaw: 0x%x, Cpus: %d)\n",
    (UINT32)(UINTN)VmmHobList,
    GET_GPAW_INIT_STATE (TdReturnData.TdInfo.Gpaw),
    TdReturnData.TdInfo.NumVcpus
  ));

  ZeroMem (&PlatformInfoHob, sizeof (PlatformInfoHob));

  //
  // Tranfer the Hoblist to the final Hoblist for DXe
  //
  TransferHobList (VmmHobList);

  //
  // Initialize Platform
  //
  TdxPlatformInitialize (&PlatformInfoHob, &CfgSysStateDefault, &CfgNxStackDefault);

  //
  // Relocate mailbox
  //
  RelocateMailbox (&PlatformInfoHob);

   //
  // TDVF must not use any CpuHob from input HobList.
  // It must create its own using GPWA from VMM and 0 for SizeOfIoSpace
  //
  BuildCpuHob (GET_GPAW_INIT_STATE (TdReturnData.TdInfo.Gpaw), 16);

  //
  // SecFV
  //
  BootFv = (EFI_FIRMWARE_VOLUME_HEADER *)SecCoreData->BootFirmwareVolumeBase;
  BuildFvHob ((UINTN)BootFv, BootFv->FvLength);

  //
  // DxeFV
  //
  DxeCodeBase = PcdGet32 (PcdBfvBase);
  DxeCodeSize = PcdGet32 (PcdBfvRawDataSize) - (UINT32)BootFv->FvLength;
  BuildFvHob (DxeCodeBase, DxeCodeSize);

  DEBUG ((DEBUG_INFO, "SecFv : %p, 0x%x\n", BootFv, BootFv->FvLength));
  DEBUG ((DEBUG_INFO, "DxeFv : %x, 0x%x\n", DxeCodeBase, DxeCodeSize));

  PlatformInfoPtr = (UINT8*)BuildGuidDataHob (&gUefiOvmfPkgTdxPlatformGuid, &PlatformInfoHob, sizeof (EFI_HOB_PLATFORM_INFO));

  BuildStackHob ((UINTN)SecCoreData->StackBase, SecCoreData->StackSize <<=1 );

  BuildResourceDescriptorHob (
    EFI_RESOURCE_SYSTEM_MEMORY,
    EFI_RESOURCE_ATTRIBUTE_PRESENT |
    EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
    EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
    EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_TESTED,
    (UINT64)SecCoreData->TemporaryRamBase,
    (UINT64)SecCoreData->TemporaryRamSize);

  BuildMemoryAllocationHob (
    FixedPcdGet32 (PcdOvmfSecGhcbBackupBase),
    EFI_PAGE_SIZE,
    EfiACPIMemoryNVS
    );

  //
  // Load the DXE Core and transfer control to it.
  // DXE FV is the 1st FvInstance. (base 0)
  //
  Status = DxeLoadCore (1);

  //
  // Never arrive here.
  //
  ASSERT (FALSE);
  CpuDeadLoop ();

}

