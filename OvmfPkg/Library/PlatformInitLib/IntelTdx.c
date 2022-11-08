/** @file
  Initialize Intel TDX support.

  Copyright (c) 2021, Intel Corporation. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Base.h>
#include <PiPei.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <IndustryStandard/Tdx.h>
#include <IndustryStandard/IntelTdx.h>
#include <IndustryStandard/QemuFwCfg.h>
#include <Library/QemuFwCfgLib.h>
#include <Library/PeiServicesLib.h>
#include <Library/TdxLib.h>
#include <Library/TdxMailboxLib.h>
#include <Library/SynchronizationLib.h>
#include <Pi/PrePiHob.h>
#include <WorkArea.h>
#include <ConfidentialComputingGuestAttr.h>

#define ALIGNED_2MB_MASK  0x1fffff
#define MEGABYTE_SHIFT    20

#define SET_ACCEPT_MEMORY_PHASE1_END_ADDRESS(addr) \
  (((TDX_WORK_AREA *)(UINTN)FixedPcdGet32 (PcdOvmfWorkAreaBase))->SecTdxWorkArea.AcceptMemoryPhase1EndAddress = addr)
#define ACCEPT_MEMORY_PHASE1_END_ADDRESS \
  (((TDX_WORK_AREA *)(UINTN)FixedPcdGet32 (PcdOvmfWorkAreaBase))->SecTdxWorkArea.AcceptMemoryPhase1EndAddress)

#define SET_ACCEPT_MEMORY_PHASE2_END_ADDRESS(addr) \
  (((TDX_WORK_AREA *)(UINTN)FixedPcdGet32 (PcdOvmfWorkAreaBase))->SecTdxWorkArea.AcceptMemoryPhase2EndAddress = addr)
#define ACCEPT_MEMORY_PHASE2_END_ADDRESS \
  (((TDX_WORK_AREA *)(UINTN)FixedPcdGet32 (PcdOvmfWorkAreaBase))->SecTdxWorkArea.AcceptMemoryPhase2EndAddress)

#define SET_ACCEPT_MEMORY_APS_STACK_ADDRESS(addr) \
  (((TDX_WORK_AREA *)(UINTN)FixedPcdGet32 (PcdOvmfWorkAreaBase))->SecTdxWorkArea.APsStackAddress = (UINT32)(UINTN)addr)
#define ACCEPT_MEMORY_APS_STACK_ADDRESS \
  (((TDX_WORK_AREA *)(UINTN)FixedPcdGet32 (PcdOvmfWorkAreaBase))->SecTdxWorkArea.APsStackAddress)

#define SET_ACCEPT_MEMORY_APS_STACK_SIZE(size) \
  (((TDX_WORK_AREA *)(UINTN)FixedPcdGet32 (PcdOvmfWorkAreaBase))->SecTdxWorkArea.APsStackSize = size)
#define ACCEPT_MEMORY_APS_STACK_SIZE \
  (((TDX_WORK_AREA *)(UINTN)FixedPcdGet32 (PcdOvmfWorkAreaBase))->SecTdxWorkArea.APsStackSize)

#define SET_ACCEPT_MEMORY_PHASE1_TSC_CNT(tsc) \
  (((TDX_WORK_AREA *)(UINTN)FixedPcdGet32 (PcdOvmfWorkAreaBase))->SecTdxWorkArea.AcceptMemoryPhase1TscCnt = tsc)
#define ACCEPT_MEMORY_PHASE1_TSC_CNT \
  (((TDX_WORK_AREA *)(UINTN)FixedPcdGet32 (PcdOvmfWorkAreaBase))->SecTdxWorkArea.AcceptMemoryPhase1TscCnt)

#define SET_ACCEPT_MEMORY_PHASE2_TSC_CNT(tsc) \
  (((TDX_WORK_AREA *)(UINTN)FixedPcdGet32 (PcdOvmfWorkAreaBase))->SecTdxWorkArea.AcceptMemoryPhase2TscCnt = tsc)
#define ACCEPT_MEMORY_PHASE2_TSC_CNT \
  (((TDX_WORK_AREA *)(UINTN)FixedPcdGet32 (PcdOvmfWorkAreaBase))->SecTdxWorkArea.AcceptMemoryPhase2TscCnt)

#define TSC_CNT_TO_MS(cnt,freq) (UINT32)DivU64x64Remainder(cnt * 1000, freq, NULL)
#define TSC_CNT_TO_US(cnt,freq) (UINT32)DivU64x64Remainder(cnt * 1000 * 1000, freq, NULL)

UINT64 CalcTscFreq(
  VOID
  )
{
  UINT32   RegEax;
  UINT32   RegEbx;
  UINT32   RegEcx;
  UINT64   TscFreq;

  AsmCpuid(0x15, &RegEax, &RegEbx, &RegEcx, NULL);

  if (RegEbx == 0 || RegEax == 0) {
    TscFreq = 0;
  } else {
    TscFreq = (UINT64)(UINTN)RegEcx * (UINT64)(UINTN)RegEbx / (UINT64)(UINTN)RegEax;
  }

  return TscFreq;
}

STATIC
UINT64
CalculateChunkSize (
  UINT64  MemoryRegionSize,
  UINT32  CpusNum
  )
{
  UINT64  ChunkSize;

  ChunkSize = ALIGN_VALUE (MemoryRegionSize / CpusNum, SIZE_2MB);

  return ChunkSize;
}

/**
  This function will be called to accept pages. Only BSP accepts pages.

  TDCALL(ACCEPT_PAGE) supports the accept page size of 4k and 2M. To
  simplify the implementation, the Memory to be accpeted is splitted
  into 3 parts:
  -----------------  <-- StartAddress1 (not 2M aligned)
  |  part 1       |      Length1 < 2M
  |---------------|  <-- StartAddress2 (2M aligned)
  |               |      Length2 = Integer multiples of 2M
  |  part 2       |
  |               |
  |---------------|  <-- StartAddress3
  |  part 3       |      Length3 < 2M
  |---------------|

  @param[in] PhysicalAddress   Start physical adress
  @param[in] PhysicalEnd       End physical address

  @retval    EFI_SUCCESS       Accept memory successfully
  @retval    Others            Other errors as indicated
**/
EFI_STATUS
EFIAPI
BspAcceptMemoryResourceRange (
  IN EFI_PHYSICAL_ADDRESS  PhysicalAddress,
  IN EFI_PHYSICAL_ADDRESS  PhysicalEnd
  )
{
  EFI_STATUS                  Status;
  UINT32                      AcceptPageSize;
  UINT64                      StartAddress1;
  UINT64                      StartAddress2;
  UINT64                      StartAddress3;
  UINT64                      TotalLength;
  UINT64                      Length1;
  UINT64                      Length2;
  UINT64                      Length3;
  UINT64                      Pages;
  volatile MP_WAKEUP_MAILBOX  *MailBox;

  AcceptPageSize = FixedPcdGet32 (PcdTdxAcceptPageSize);
  TotalLength    = PhysicalEnd - PhysicalAddress;
  StartAddress1  = 0;
  StartAddress2  = 0;
  StartAddress3  = 0;
  Length1        = 0;
  Length2        = 0;
  Length3        = 0;
  MailBox        = (volatile MP_WAKEUP_MAILBOX *)GetTdxMailBox ();

  if (TotalLength == 0) {
    return EFI_SUCCESS;
  }

  DEBUG ((DEBUG_INFO, "BspAccept: 0x%llx - 0x%llx\n", PhysicalAddress, TotalLength));

  if (ALIGN_VALUE (PhysicalAddress, SIZE_2MB) != PhysicalAddress) {
    StartAddress1 = PhysicalAddress;
    Length1       = ALIGN_VALUE (PhysicalAddress, SIZE_2MB) - PhysicalAddress;
    if (Length1 >= TotalLength) {
      Length1 = TotalLength;
    }

    PhysicalAddress += Length1;
    TotalLength     -= Length1;
  }

  if (TotalLength > SIZE_2MB) {
    StartAddress2    = PhysicalAddress;
    Length2          = TotalLength & ~(UINT64)ALIGNED_2MB_MASK;
    PhysicalAddress += Length2;
    TotalLength     -= Length2;
  }

  if (TotalLength) {
    StartAddress3 = PhysicalAddress;
    Length3       = TotalLength;
  }

  DEBUG ((DEBUG_INFO, "   Part1: 0x%llx - 0x%llx\n", StartAddress1, Length1));
  DEBUG ((DEBUG_INFO, "   Part2: 0x%llx - 0x%llx\n", StartAddress2, Length2));
  DEBUG ((DEBUG_INFO, "   Part3: 0x%llx - 0x%llx\n", StartAddress3, Length3));
  DEBUG ((DEBUG_INFO, "   Page : 0x%x\n", AcceptPageSize));

  Status = EFI_SUCCESS;
  if (Length1 > 0) {
    Pages  = Length1 / SIZE_4KB;
    Status = TdAcceptPages (StartAddress1, Pages, SIZE_4KB);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Length2 > 0) {
    Pages  = Length2 / AcceptPageSize;
    Status = TdAcceptPages (StartAddress2, Pages, AcceptPageSize);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if (Length3 > 0) {
    Pages  = Length3 / SIZE_4KB;
    Status = TdAcceptPages (StartAddress3, Pages, SIZE_4KB);
    ASSERT (!EFI_ERROR (Status));
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  MailBox->Tallies[0] += (UINT32)(UINTN)EFI_SIZE_TO_PAGES (Length1 + Length2 + Length3);

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
BspApAcceptMemoryResourceRange (
  UINT32                CpuIndex,
  UINT32                CpusNum,
  EFI_PHYSICAL_ADDRESS  PhysicalStart,
  EFI_PHYSICAL_ADDRESS  PhysicalEnd
  )
{
  UINT64                      Status;
  UINT64                      Pages;
  UINT64                      Stride;
  UINT64                      AcceptPageSize;
  UINT64                      AcceptChunkSize;
  EFI_PHYSICAL_ADDRESS        PhysicalAddress;
  volatile MP_WAKEUP_MAILBOX  *MailBox;

  // AcceptChunkSize = (UINT64)(UINTN)FixedPcdGet32 (PcdTdxAcceptMemoryChunkSize);
  AcceptChunkSize = CalculateChunkSize (PhysicalEnd - PhysicalStart, CpusNum);
  AcceptPageSize  = (UINT64)(UINTN)FixedPcdGet32 (PcdTdxAcceptPageSize);
  MailBox         = (volatile MP_WAKEUP_MAILBOX *)GetTdxMailBox ();

  Stride          = CpusNum * AcceptChunkSize;
  PhysicalAddress = PhysicalStart + AcceptChunkSize * CpuIndex;
  Status          = EFI_SUCCESS;

  while (!EFI_ERROR (Status) && PhysicalAddress < PhysicalEnd) {
    Pages  = MIN (AcceptChunkSize, PhysicalEnd - PhysicalAddress) / AcceptPageSize;
    Status = TdAcceptPages (PhysicalAddress, Pages, AcceptPageSize);
    ASSERT (!EFI_ERROR (Status));
    MailBox->Tallies[CpuIndex] += (UINT32)(UINTN)EFI_SIZE_TO_PAGES (Pages * AcceptPageSize);
    PhysicalAddress            += Stride;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ApAcceptMemoryResourceRange (
  UINT32                CpuIndex,
  EFI_PHYSICAL_ADDRESS  PhysicalStart,
  EFI_PHYSICAL_ADDRESS  PhysicalEnd
  )
{
  UINT64          Status;
  TD_RETURN_DATA  TdReturnData;

  Status = TdCall (TDCALL_TDINFO, 0, 0, 0, &TdReturnData);
  if (Status != TDX_EXIT_REASON_SUCCESS) {
    ASSERT (FALSE);
    return EFI_ABORTED;
  }

  if ((CpuIndex == 0) || (CpuIndex > TdReturnData.TdInfo.NumVcpus)) {
    ASSERT (FALSE);
    return EFI_ABORTED;
  }

  return BspApAcceptMemoryResourceRange (CpuIndex, TdReturnData.TdInfo.NumVcpus, PhysicalStart, PhysicalEnd);
}

EFI_STATUS
EFIAPI
MpAcceptMemoryResourceRange (
  IN EFI_PHYSICAL_ADDRESS  PhysicalStart,
  IN EFI_PHYSICAL_ADDRESS  PhysicalEnd
  )
{
  UINT64  AcceptPageSize;
  UINT64  TotalLength;
  UINT32  CpusNum;
  UINT64  AcceptChunkSize;
  UINT32  APsStackSize;
  VOID    *APsStackAddress;

  TotalLength = PhysicalEnd - PhysicalStart;

  if (TotalLength == 0) {
    return EFI_SUCCESS;
  }

  AcceptChunkSize = (UINT64)(UINTN)FixedPcdGet32 (PcdTdxAcceptMemoryChunkSize);
  AcceptPageSize  = (UINT64)(UINTN)FixedPcdGet32 (PcdTdxAcceptPageSize);

  DEBUG ((DEBUG_INFO, "MpAccept : 0x%llx - 0x%llx (0x%llx)\n", PhysicalStart, PhysicalEnd, TotalLength));
  DEBUG ((DEBUG_INFO, "    Page : 0x%x\n", AcceptPageSize));

  ASSERT (ALIGN_VALUE (PhysicalStart, SIZE_2MB) == PhysicalStart);

  if (TotalLength <= AcceptChunkSize) {
    return BspAcceptMemoryResourceRange (PhysicalStart, PhysicalEnd);
  }

  APsStackAddress = (VOID *)(UINTN)ACCEPT_MEMORY_APS_STACK_ADDRESS;
  APsStackSize    = ACCEPT_MEMORY_APS_STACK_SIZE;
  CpusNum         = GetCpusNum ();

  if (APsStackAddress == 0) {
    APsStackSize    = (CpusNum - 1) * SIZE_16KB;
    APsStackAddress = AllocatePages (EFI_SIZE_TO_PAGES (APsStackSize));
    ASSERT (APsStackAddress != NULL);
    SET_ACCEPT_MEMORY_APS_STACK_ADDRESS (APsStackAddress);
    SET_ACCEPT_MEMORY_APS_STACK_SIZE (APsStackSize);
  } else {
    ASSERT (APsStackSize != 0);
  }

  DEBUG ((DEBUG_INFO, "AP StackBaseAddress=%p, StackSize=%x\n", APsStackAddress, APsStackSize));

  DEBUG ((DEBUG_INFO, "BSP/APs accept memories ...\n"));
  RELEASE_DEBUG ((DEBUG_INFO, "  ChunkSize = %llx\n", CalculateChunkSize (PhysicalEnd - PhysicalStart, CpusNum)));

  MpSerializeStart ();

  MpSendWakeupCommand (
    MpProtectedModeWakeupCommandAcceptPages,
    (UINT64)(UINTN)ApAcceptMemoryResourceRange,
    PhysicalStart,
    PhysicalEnd,
    (UINT64)(UINTN)APsStackAddress,
    SIZE_16KB
    );

  //
  // Now BSP does its job.
  //
  BspApAcceptMemoryResourceRange (0, CpusNum, PhysicalStart, PhysicalEnd);

  MpSerializeEnd ();

  return EFI_SUCCESS;
}

/**
  Check the value whether in the valid list.

  @param[in] Value             A value
  @param[in] ValidList         A pointer to valid list
  @param[in] ValidListLength   Length of valid list

  @retval  TRUE   The value is in valid list.
  @retval  FALSE  The value is not in valid list.

**/
BOOLEAN
EFIAPI
IsInValidList (
  IN UINT32  Value,
  IN UINT32  *ValidList,
  IN UINT32  ValidListLength
  )
{
  UINT32  index;

  if (ValidList == NULL) {
    return FALSE;
  }

  for (index = 0; index < ValidListLength; index++) {
    if (ValidList[index] == Value) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  Check the integrity of VMM Hob List.

  @param[in] VmmHobList   A pointer to Hob List

  @retval  TRUE     The Hob List is valid.
  @retval  FALSE    The Hob List is invalid.

**/
BOOLEAN
EFIAPI
ValidateHobList (
  IN CONST VOID  *VmmHobList
  )
{
  EFI_PEI_HOB_POINTERS  Hob;
  UINT32                EFI_BOOT_MODE_LIST[] = {
    BOOT_WITH_FULL_CONFIGURATION,
    BOOT_WITH_MINIMAL_CONFIGURATION,
    BOOT_ASSUMING_NO_CONFIGURATION_CHANGES,
    BOOT_WITH_FULL_CONFIGURATION_PLUS_DIAGNOSTICS,
    BOOT_WITH_DEFAULT_SETTINGS,
    BOOT_ON_S4_RESUME,
    BOOT_ON_S5_RESUME,
    BOOT_WITH_MFG_MODE_SETTINGS,
    BOOT_ON_S2_RESUME,
    BOOT_ON_S3_RESUME,
    BOOT_ON_FLASH_UPDATE,
    BOOT_IN_RECOVERY_MODE
  };

  UINT32  EFI_RESOURCE_TYPE_LIST[] = {
    EFI_RESOURCE_SYSTEM_MEMORY,
    EFI_RESOURCE_MEMORY_MAPPED_IO,
    EFI_RESOURCE_IO,
    EFI_RESOURCE_FIRMWARE_DEVICE,
    EFI_RESOURCE_MEMORY_MAPPED_IO_PORT,
    EFI_RESOURCE_MEMORY_RESERVED,
    EFI_RESOURCE_IO_RESERVED,
    BZ3937_EFI_RESOURCE_MEMORY_UNACCEPTED
  };

  if (VmmHobList == NULL) {
    DEBUG ((DEBUG_ERROR, "HOB: HOB data pointer is NULL\n"));
    return FALSE;
  }

  Hob.Raw = (UINT8 *)VmmHobList;

  //
  // Parse the HOB list until end of list or matching type is found.
  //
  while (!END_OF_HOB_LIST (Hob)) {
    if (Hob.Header->Reserved != (UINT32)0) {
      DEBUG ((DEBUG_ERROR, "HOB: Hob header Reserved filed should be zero\n"));
      return FALSE;
    }

    if (Hob.Header->HobLength == 0) {
      DEBUG ((DEBUG_ERROR, "HOB: Hob header LEANGTH should not be zero\n"));
      return FALSE;
    }

    switch (Hob.Header->HobType) {
      case EFI_HOB_TYPE_HANDOFF:
        if (Hob.Header->HobLength != sizeof (EFI_HOB_HANDOFF_INFO_TABLE)) {
          DEBUG ((DEBUG_ERROR, "HOB: Hob length is not equal corresponding hob structure. Type: 0x%04x\n", EFI_HOB_TYPE_HANDOFF));
          return FALSE;
        }

        if (IsInValidList (Hob.HandoffInformationTable->BootMode, EFI_BOOT_MODE_LIST, ARRAY_SIZE (EFI_BOOT_MODE_LIST)) == FALSE) {
          DEBUG ((DEBUG_ERROR, "HOB: Unknow HandoffInformationTable BootMode type. Type: 0x%08x\n", Hob.HandoffInformationTable->BootMode));
          return FALSE;
        }

        if ((Hob.HandoffInformationTable->EfiFreeMemoryTop % 4096) != 0) {
          DEBUG ((DEBUG_ERROR, "HOB: HandoffInformationTable EfiFreeMemoryTop address must be 4-KB aligned to meet page restrictions of UEFI.\
                               Address: 0x%016lx\n", Hob.HandoffInformationTable->EfiFreeMemoryTop));
          return FALSE;
        }

        break;

      case EFI_HOB_TYPE_RESOURCE_DESCRIPTOR:
        if (Hob.Header->HobLength != sizeof (EFI_HOB_RESOURCE_DESCRIPTOR)) {
          DEBUG ((DEBUG_ERROR, "HOB: Hob length is not equal corresponding hob structure. Type: 0x%04x\n", EFI_HOB_TYPE_RESOURCE_DESCRIPTOR));
          return FALSE;
        }

        if (IsInValidList (Hob.ResourceDescriptor->ResourceType, EFI_RESOURCE_TYPE_LIST, ARRAY_SIZE (EFI_RESOURCE_TYPE_LIST)) == FALSE) {
          DEBUG ((DEBUG_ERROR, "HOB: Unknow ResourceDescriptor ResourceType type. Type: 0x%08x\n", Hob.ResourceDescriptor->ResourceType));
          return FALSE;
        }

        if ((Hob.ResourceDescriptor->ResourceAttribute & (~(EFI_RESOURCE_ATTRIBUTE_PRESENT |
                                                            EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
                                                            EFI_RESOURCE_ATTRIBUTE_TESTED |
                                                            EFI_RESOURCE_ATTRIBUTE_READ_PROTECTED |
                                                            EFI_RESOURCE_ATTRIBUTE_WRITE_PROTECTED |
                                                            EFI_RESOURCE_ATTRIBUTE_EXECUTION_PROTECTED |
                                                            EFI_RESOURCE_ATTRIBUTE_PERSISTENT |
                                                            EFI_RESOURCE_ATTRIBUTE_SINGLE_BIT_ECC |
                                                            EFI_RESOURCE_ATTRIBUTE_MULTIPLE_BIT_ECC |
                                                            EFI_RESOURCE_ATTRIBUTE_ECC_RESERVED_1 |
                                                            EFI_RESOURCE_ATTRIBUTE_ECC_RESERVED_2 |
                                                            EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE |
                                                            EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
                                                            EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
                                                            EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE |
                                                            EFI_RESOURCE_ATTRIBUTE_16_BIT_IO |
                                                            EFI_RESOURCE_ATTRIBUTE_32_BIT_IO |
                                                            EFI_RESOURCE_ATTRIBUTE_64_BIT_IO |
                                                            EFI_RESOURCE_ATTRIBUTE_UNCACHED_EXPORTED |
                                                            EFI_RESOURCE_ATTRIBUTE_READ_PROTECTABLE |
                                                            EFI_RESOURCE_ATTRIBUTE_WRITE_PROTECTABLE |
                                                            EFI_RESOURCE_ATTRIBUTE_EXECUTION_PROTECTABLE |
                                                            EFI_RESOURCE_ATTRIBUTE_PERSISTABLE |
                                                            EFI_RESOURCE_ATTRIBUTE_READ_ONLY_PROTECTED |
                                                            EFI_RESOURCE_ATTRIBUTE_READ_ONLY_PROTECTABLE |
                                                            EFI_RESOURCE_ATTRIBUTE_MORE_RELIABLE))) != 0)
        {
          DEBUG ((DEBUG_ERROR, "HOB: Unknow ResourceDescriptor ResourceAttribute type. Type: 0x%08x\n", Hob.ResourceDescriptor->ResourceAttribute));
          return FALSE;
        }

        break;

      // EFI_HOB_GUID_TYPE is variable length data, so skip check
      case EFI_HOB_TYPE_GUID_EXTENSION:
        break;

      case EFI_HOB_TYPE_FV:
        if (Hob.Header->HobLength != sizeof (EFI_HOB_FIRMWARE_VOLUME)) {
          DEBUG ((DEBUG_ERROR, "HOB: Hob length is not equal corresponding hob structure. Type: 0x%04x\n", EFI_HOB_TYPE_FV));
          return FALSE;
        }

        break;

      case EFI_HOB_TYPE_FV2:
        if (Hob.Header->HobLength != sizeof (EFI_HOB_FIRMWARE_VOLUME2)) {
          DEBUG ((DEBUG_ERROR, "HOB: Hob length is not equal corresponding hob structure. Type: 0x%04x\n", EFI_HOB_TYPE_FV2));
          return FALSE;
        }

        break;

      case EFI_HOB_TYPE_FV3:
        if (Hob.Header->HobLength != sizeof (EFI_HOB_FIRMWARE_VOLUME3)) {
          DEBUG ((DEBUG_ERROR, "HOB: Hob length is not equal corresponding hob structure. Type: 0x%04x\n", EFI_HOB_TYPE_FV3));
          return FALSE;
        }

        break;

      case EFI_HOB_TYPE_CPU:
        if (Hob.Header->HobLength != sizeof (EFI_HOB_CPU)) {
          DEBUG ((DEBUG_ERROR, "HOB: Hob length is not equal corresponding hob structure. Type: 0x%04x\n", EFI_HOB_TYPE_CPU));
          return FALSE;
        }

        for (UINT32 index = 0; index < 6; index++) {
          if (Hob.Cpu->Reserved[index] != 0) {
            DEBUG ((DEBUG_ERROR, "HOB: Cpu Reserved field will always be set to zero.\n"));
            return FALSE;
          }
        }

        break;

      default:
        DEBUG ((DEBUG_ERROR, "HOB: Hob type is not know. Type: 0x%04x\n", Hob.Header->HobType));
        return FALSE;
    }

    // Get next HOB
    Hob.Raw = (UINT8 *)(Hob.Raw + Hob.Header->HobLength);
  }

  return TRUE;
}

/**
  Processing the incoming HobList for the TDX

  Firmware must parse list, and accept the pages of memory before their can be
  use by the guest.

  @param[in] VmmHobList    The Hoblist pass the firmware

  @retval  EFI_SUCCESS     Process the HobList successfully
  @retval  Others          Other errors as indicated

**/
EFI_STATUS
EFIAPI
AcceptMemoryPhase1 (
  IN CONST VOID  *VmmHobList
  )
{
  EFI_STATUS            Status;
  EFI_PEI_HOB_POINTERS  Hob;
  EFI_PHYSICAL_ADDRESS  PhysicalEnd;
  EFI_PHYSICAL_ADDRESS  Phase1PhysicalEnd;
  UINT64                StartTsc;
  UINT64                EndTsc;

  Status = EFI_SUCCESS;
  ASSERT (VmmHobList != NULL);
  Hob.Raw = (UINT8 *)VmmHobList;

  Phase1PhysicalEnd = (PHYSICAL_ADDRESS)FixedPcdGet64 (PcdTdxAcceptMemoryPhase1EndAddress);
  if (Phase1PhysicalEnd == 0) {
    Phase1PhysicalEnd = BASE_4GB;
  }

  StartTsc = AsmReadTsc ();

  //
  // Parse the HOB list until end of list or matching type is found.
  //
  while (!END_OF_HOB_LIST (Hob)) {
    if (Hob.Header->HobType == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR) {
      DEBUG ((DEBUG_INFO, "\nResourceType: 0x%x\n", Hob.ResourceDescriptor->ResourceType));

      if (Hob.ResourceDescriptor->ResourceType == BZ3937_EFI_RESOURCE_MEMORY_UNACCEPTED) {
        DEBUG ((DEBUG_INFO, "ResourceAttribute: 0x%x\n", Hob.ResourceDescriptor->ResourceAttribute));
        DEBUG ((DEBUG_INFO, "PhysicalStart: 0x%llx\n", Hob.ResourceDescriptor->PhysicalStart));
        DEBUG ((DEBUG_INFO, "ResourceLength: 0x%llx\n", Hob.ResourceDescriptor->ResourceLength));
        DEBUG ((DEBUG_INFO, "Owner: %g\n\n", &Hob.ResourceDescriptor->Owner));

        PhysicalEnd = Hob.ResourceDescriptor->PhysicalStart + Hob.ResourceDescriptor->ResourceLength;

        if (Hob.ResourceDescriptor->PhysicalStart >= Phase1PhysicalEnd) {
          SET_ACCEPT_MEMORY_PHASE1_END_ADDRESS (Phase1PhysicalEnd);
          break;
        }

        if (PhysicalEnd >= Phase1PhysicalEnd) {
          PhysicalEnd = Phase1PhysicalEnd;
        }

        Status = BspAcceptMemoryResourceRange (
                   Hob.ResourceDescriptor->PhysicalStart,
                   PhysicalEnd
                   );
        if (EFI_ERROR (Status)) {
          break;
        }

        if (PhysicalEnd == Phase1PhysicalEnd) {
          // record the phase1 PhysicalEnd in workarea
          SET_ACCEPT_MEMORY_PHASE1_END_ADDRESS (PhysicalEnd);
          break;
        }
      }
    }

    Hob.Raw = GET_NEXT_HOB (Hob);
  }

  EndTsc = AsmReadTsc ();
  SET_ACCEPT_MEMORY_PHASE1_TSC_CNT (EndTsc - StartTsc);

  ASSERT (ACCEPT_MEMORY_PHASE1_END_ADDRESS != 0);

  return Status;
}

VOID
DumpMemoryAcceptInformation (
  VOID
  )
{
  volatile MP_WAKEUP_MAILBOX  *MailBox;
  UINT32                      Index;
  UINT64                      TotalPages;

  TotalPages = 0;
  MailBox    = (volatile MP_WAKEUP_MAILBOX *)GetTdxMailBox ();

  DEBUG ((
    DEBUG_INFO,
    "Phase1-End: %llx, Phase2-End: %llx\n",
    ACCEPT_MEMORY_PHASE1_END_ADDRESS,
    ACCEPT_MEMORY_PHASE2_END_ADDRESS
    ));

  DEBUG ((DEBUG_INFO, "BSP/APs Tallies:\n"));

  for (Index = 0; Index < GetCpusNum (); Index++) {
    TotalPages += MailBox->Tallies[Index];

    DEBUG ((DEBUG_INFO, "%8x", MailBox->Tallies[Index]));

    if ((Index+1) % 8 == 0) {
      DEBUG ((DEBUG_INFO, "\n"));
    }
  }

  DEBUG ((DEBUG_INFO, "\n"));

  DEBUG ((DEBUG_INFO, "Accept: %llx pages, %llx bytes\n", TotalPages, EFI_PAGES_TO_SIZE (TotalPages)));
}

EFI_STATUS
EFIAPI
AcceptMemoryPhase2 (
  IN CONST VOID  *VmmHobList
  )
{
  EFI_STATUS            Status;
  EFI_PEI_HOB_POINTERS  Hob;
  EFI_PHYSICAL_ADDRESS  PhysicalStart;
  EFI_PHYSICAL_ADDRESS  PhysicalEnd;
  EFI_PHYSICAL_ADDRESS  Phase1PhysicalEnd;
  EFI_PHYSICAL_ADDRESS  Phase2PhysicalEnd;
  UINT64                StartTsc;
  UINT64                EndTsc;

  DEBUG ((DEBUG_INFO, "AcceptMemoryPhase2...\n"));

  Status = EFI_SUCCESS;

  ASSERT (VmmHobList != NULL);
  Hob.Raw = (UINT8 *)VmmHobList;

  Phase1PhysicalEnd = ACCEPT_MEMORY_PHASE1_END_ADDRESS;
  Phase2PhysicalEnd = (PHYSICAL_ADDRESS)FixedPcdGet64 (PcdTdxAcceptMemoryPhase2EndAddress);
  if (Phase2PhysicalEnd == 0) {
    Phase2PhysicalEnd = MAX_UINT64;
  }

  StartTsc = AsmReadTsc ();

  //
  // Parse the HOB list until end of list or matching type is found.
  //
  while (!END_OF_HOB_LIST (Hob)) {
    if (Hob.Header->HobType == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR) {
      DEBUG ((DEBUG_INFO, "\nResourceType: 0x%x\n", Hob.ResourceDescriptor->ResourceType));

      if (Hob.ResourceDescriptor->ResourceType == BZ3937_EFI_RESOURCE_MEMORY_UNACCEPTED) {
        PhysicalStart = Hob.ResourceDescriptor->PhysicalStart;
        PhysicalEnd   = PhysicalStart + Hob.ResourceDescriptor->ResourceLength;

        if (PhysicalEnd <= Phase1PhysicalEnd) {
          // this memory region has been accepted. Skipped it.
          Hob.Raw = GET_NEXT_HOB (Hob);
          continue;
        }

        if (PhysicalStart >= Phase2PhysicalEnd) {
          // this memory region is not to be accepted. And we're done.
          break;
        }

        if (PhysicalStart >= Phase1PhysicalEnd) {
          // this memory region has not been acceted.
        } else if ((PhysicalStart < Phase1PhysicalEnd) && (PhysicalEnd > Phase1PhysicalEnd)) {
          // part of the memory region has been accepted.
          PhysicalStart = Phase1PhysicalEnd;
        }

        // then compare the PhysicalEnd with Phase2PhysicalEnd
        if (PhysicalEnd >= Phase2PhysicalEnd) {
          PhysicalEnd = Phase2PhysicalEnd;
        }

        DEBUG ((DEBUG_INFO, "ResourceAttribute: 0x%x\n", Hob.ResourceDescriptor->ResourceAttribute));
        DEBUG ((DEBUG_INFO, "PhysicalStart: 0x%llx\n", Hob.ResourceDescriptor->PhysicalStart));
        DEBUG ((DEBUG_INFO, "ResourceLength: 0x%llx\n", Hob.ResourceDescriptor->ResourceLength));
        DEBUG ((DEBUG_INFO, "Owner: %g\n\n", &Hob.ResourceDescriptor->Owner));

        // Now we're ready to accept memory [PhysicalStart, PhysicalEnd)
        Status = MpAcceptMemoryResourceRange (
                   PhysicalStart,
                   PhysicalEnd
                   );
        if (EFI_ERROR (Status)) {
          ASSERT (FALSE);
          break;
        }

        // record the PhysicalEnd in workarea as Phase2PhysicalEnd
        SET_ACCEPT_MEMORY_PHASE2_END_ADDRESS (PhysicalEnd);

        if (PhysicalEnd == Phase2PhysicalEnd) {
          break;
        }
      }
    }

    Hob.Raw = GET_NEXT_HOB (Hob);
  }

  EndTsc = AsmReadTsc ();
  SET_ACCEPT_MEMORY_PHASE2_TSC_CNT (EndTsc - StartTsc);

  if (ACCEPT_MEMORY_PHASE2_END_ADDRESS == 0) {
    SET_ACCEPT_MEMORY_PHASE2_END_ADDRESS (Phase1PhysicalEnd);
  }

  // print out the summary information of memory accept
  DumpMemoryAcceptInformation ();

  return Status;
}

/**
  In Tdx guest, some information need to be passed from host VMM to guest
  firmware. For example, the memory resource, etc. These information are
  prepared by host VMM and put in HobList which is described in TdxMetadata.

  Information in HobList is treated as external input. From the security
  perspective before it is consumed, it should be validated.

  @retval   EFI_SUCCESS   Successfully process the hoblist
  @retval   Others        Other error as indicated
**/
EFI_STATUS
EFIAPI
PlatformProcessTdxHobListPhase1 (
  VOID
  )
{
  EFI_STATUS      Status;
  VOID            *TdHob;
  TD_RETURN_DATA  TdReturnData;

  TdHob  = (VOID *)(UINTN)FixedPcdGet32 (PcdOvmfSecGhcbBase);
  Status = TdCall (TDCALL_TDINFO, 0, 0, 0, &TdReturnData);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((
    DEBUG_INFO,
    "Intel Tdx Started with (GPAW: %d, Cpus: %d)\n",
    TdReturnData.TdInfo.Gpaw,
    TdReturnData.TdInfo.NumVcpus
    ));

  //
  // Validate HobList
  //
  if (ValidateHobList (TdHob) == FALSE) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Process Hoblist to accept memory
  //
  Status = AcceptMemoryPhase1 (TdHob);

  return Status;
}

/**
 * Build ResourceDescriptorHob for the unaccepted memory region.
 * This memory region may be splitted into 2 parts because of lazy accept.
 *
 * @param Hob     Point to the EFI_HOB_RESOURCE_DESCRIPTOR
 * @return VOID
 */
VOID
BuildResourceDescriptorHobForUnacceptedMemory (
  IN EFI_HOB_RESOURCE_DESCRIPTOR  *Hob
  )
{
  EFI_PHYSICAL_ADDRESS         PhysicalStart;
  EFI_PHYSICAL_ADDRESS         PhysicalEnd;
  UINT64                       ResourceLength;
  EFI_RESOURCE_TYPE            ResourceType;
  EFI_RESOURCE_ATTRIBUTE_TYPE  ResourceAttribute;
  UINT64                       MaxAcceptedMemoryAddress;

  ASSERT (Hob->ResourceType == BZ3937_EFI_RESOURCE_MEMORY_UNACCEPTED);

  ResourceType      = BZ3937_EFI_RESOURCE_MEMORY_UNACCEPTED;
  ResourceAttribute = Hob->ResourceAttribute;
  PhysicalStart     = Hob->PhysicalStart;
  ResourceLength    = Hob->ResourceLength;
  PhysicalEnd       = PhysicalStart + ResourceLength;

  //
  // Accept memory is split into 2 phases:
  // Phase-2 accept a small size, phase-2 accept a bigger size.
  //
  MaxAcceptedMemoryAddress = ACCEPT_MEMORY_PHASE2_END_ADDRESS;

  if (PhysicalEnd <= MaxAcceptedMemoryAddress) {
    //
    // This memory region has been accepted.
    //
    ResourceType       = EFI_RESOURCE_SYSTEM_MEMORY;
    ResourceAttribute |= (EFI_RESOURCE_ATTRIBUTE_PRESENT | EFI_RESOURCE_ATTRIBUTE_INITIALIZED | EFI_RESOURCE_ATTRIBUTE_TESTED);
  } else if (PhysicalStart >= MaxAcceptedMemoryAddress) {
    //
    // This memory region hasn't been accepted.
    // So keep the ResourceType and ResourceAttribute unchange.
    //
  } else if ((PhysicalStart < MaxAcceptedMemoryAddress) && (PhysicalEnd > MaxAcceptedMemoryAddress)) {
    //
    // Left part of the memory region is accepted. The right part is unaccepted.
    //
    BuildResourceDescriptorHob (
      EFI_RESOURCE_SYSTEM_MEMORY,
      ResourceAttribute | (EFI_RESOURCE_ATTRIBUTE_PRESENT | EFI_RESOURCE_ATTRIBUTE_INITIALIZED | EFI_RESOURCE_ATTRIBUTE_TESTED),
      PhysicalStart,
      MaxAcceptedMemoryAddress - PhysicalStart
      );

    PhysicalStart  = MaxAcceptedMemoryAddress;
    ResourceLength = PhysicalEnd - MaxAcceptedMemoryAddress;
  }

  BuildResourceDescriptorHob (
    ResourceType,
    ResourceAttribute,
    PhysicalStart,
    ResourceLength
    );
}

/**
  Transfer the incoming HobList for the TD to the final HobList for Dxe.
  The Hobs transferred in this function are ResourceDescriptor hob and
  MemoryAllocation hob.

  @param[in] VmmHobList    The Hoblist pass the firmware

**/
VOID
EFIAPI
TransferTdxHobList (
  VOID
  )
{
  EFI_PEI_HOB_POINTERS         Hob;
  EFI_RESOURCE_TYPE            ResourceType;
  EFI_RESOURCE_ATTRIBUTE_TYPE  ResourceAttribute;

  //
  // PcdOvmfSecGhcbBase is used as the TD_HOB in Tdx guest.
  //
  Hob.Raw = (UINT8 *)(UINTN)FixedPcdGet32 (PcdOvmfSecGhcbBase);
  while (!END_OF_HOB_LIST (Hob)) {
    switch (Hob.Header->HobType) {
      case EFI_HOB_TYPE_RESOURCE_DESCRIPTOR:
        ResourceType      = Hob.ResourceDescriptor->ResourceType;
        ResourceAttribute = Hob.ResourceDescriptor->ResourceAttribute;

        if (ResourceType == BZ3937_EFI_RESOURCE_MEMORY_UNACCEPTED) {
          BuildResourceDescriptorHobForUnacceptedMemory (Hob.ResourceDescriptor);
        } else {
          BuildResourceDescriptorHob (
            ResourceType,
            ResourceAttribute,
            Hob.ResourceDescriptor->PhysicalStart,
            Hob.ResourceDescriptor->ResourceLength
            );
        }

        break;
      case EFI_HOB_TYPE_MEMORY_ALLOCATION:
        BuildMemoryAllocationHob (
          Hob.MemoryAllocation->AllocDescriptor.MemoryBaseAddress,
          Hob.MemoryAllocation->AllocDescriptor.MemoryLength,
          Hob.MemoryAllocation->AllocDescriptor.MemoryType
          );
        break;
    }

    Hob.Raw = GET_NEXT_HOB (Hob);
  }
}

VOID
DumpMemoryAcceptTime (
  VOID
  )
{
  UINT64 TscFreq;
  UINT64 Phase1TscCnt, Phase2TscCnt;

  TscFreq = CalcTscFreq ();
  RELEASE_DEBUG ((DEBUG_INFO, "TscFreq: %llu\n", TscFreq));

  Phase1TscCnt = ACCEPT_MEMORY_PHASE1_TSC_CNT;
  RELEASE_DEBUG ((DEBUG_INFO, "  Phase1 : physical_end_addr=0x%llx, time=%llu ms\n", ACCEPT_MEMORY_PHASE1_END_ADDRESS, TSC_CNT_TO_MS(Phase1TscCnt, TscFreq)));

  Phase2TscCnt = ACCEPT_MEMORY_PHASE2_TSC_CNT;
  RELEASE_DEBUG ((DEBUG_INFO, "  Phase2 : physical_end_addr=0x%llx, time=%llu ms\n", ACCEPT_MEMORY_PHASE2_END_ADDRESS, TSC_CNT_TO_MS(Phase2TscCnt, TscFreq)));
}

EFI_STATUS
EFIAPI
PlatformProcessTdxHobListPhase2 (
  VOID
  )
{
  AcceptMemoryPhase2 ((VOID *)(UINTN)FixedPcdGet32 (PcdOvmfSecGhcbBase));

  // dump the time used in phase1/phase2
  DumpMemoryAcceptTime ();

  TransferTdxHobList ();

  return EFI_SUCCESS;
}

/**
  In Tdx guest, the system memory is passed in TdHob by host VMM. So
  the major task of PlatformTdxPublishRamRegions is to walk thru the
  TdHob list and transfer the ResourceDescriptorHob and MemoryAllocationHob
  to the hobs in DXE phase.

  MemoryAllocationHob should also be created for Mailbox and Ovmf work area.
**/
VOID
EFIAPI
PlatformTdxPublishRamRegions (
  VOID
  )
{
  if (!TdIsEnabled ()) {
    return;
  }

  //
  // The memory region defined by PcdOvmfSecGhcbBackupBase is pre-allocated by
  // host VMM and used as the td mailbox at the beginning of system boot.
  //
  BuildMemoryAllocationHob (
    FixedPcdGet32 (PcdOvmfSecGhcbBackupBase),
    FixedPcdGet32 (PcdOvmfSecGhcbBackupSize),
    EfiACPIMemoryNVS
    );

  if (FixedPcdGet32 (PcdOvmfWorkAreaSize) != 0) {
    //
    // Reserve the work area.
    //
    // Since this memory range will be used by the Reset Vector on S3
    // resume, it must be reserved as ACPI NVS.
    //
    // If S3 is unsupported, then various drivers might still write to the
    // work area. We ought to prevent DXE from serving allocation requests
    // such that they would overlap the work area.
    //
    BuildMemoryAllocationHob (
      (EFI_PHYSICAL_ADDRESS)(UINTN)FixedPcdGet32 (PcdOvmfWorkAreaBase),
      (UINT64)(UINTN)FixedPcdGet32 (PcdOvmfWorkAreaSize),
      EfiBootServicesData
      );
  }
}
