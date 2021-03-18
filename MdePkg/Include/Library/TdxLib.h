/** @file
  TdxLib definitions

  Copyright (c) 2020 - 2021, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef _TDX_LIB_H_
#define _TDX_LIB_H_

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Uefi/UefiBaseType.h>
#include <Protocol/DebugSupport.h>

/**
  This function accepts a pending private page, and initialize the page to
  all-0 using the TD ephemeral private key.

  @param[in]  StartAddress     Guest physical address of the private page
                               to accept.
  @param[in]  NumberOfPages    Number of the pages to be accepted.
  @param[in]  PageSize         GPA page size. Only accept 1G/2M/4K size.

  @return EFI_SUCCESS
**/
EFI_STATUS
EFIAPI
TdAcceptPages (
  IN UINT64  StartAddress,
  IN UINT64  NumberOfPages,
  IN UINT64  PageSize
  );

/**
  This function extends one of the RTMR measurement register
  in TDCS with the provided extension data in memory.
  RTMR extending supports SHA384 which length is 48 bytes.

  @param[in]  Data      Point to the data to be extended
  @param[in]  DataLen   Length of the data. Must be 48
  @param[in]  Index     RTMR index

  @return EFI_SUCCESS
  @return EFI_INVALID_PARAMETER
  @return EFI_DEVICE_ERROR

**/
EFI_STATUS
EFIAPI
TdExtendRtmr (
  IN  UINT32  *Data,
  IN  UINT32  DataLen,
  IN  UINT8   Index
  );

/**
  The TDCALL instruction causes a VM exit to the Intel TDX module.  It is
  used to call guest-side Intel TDX functions, either local or a TD exit
  to the host VMM, as selected by Leaf.
  Leaf functions are described at <https://software.intel.com/content/
  www/us/en/develop/articles/intel-trust-domain-extensions.html>

  @param[in]      Leaf        Leaf number of TDCALL instruction
  @param[in]      Arg1        Arg1
  @param[in]      Arg2        Arg2
  @param[in]      Arg3        Arg3
  @param[in,out]  Results  Returned result of the Leaf function

  @return EFI_SUCCESS
  @return Other           See individual leaf functions
**/
EFI_STATUS
EFIAPI
TdCall (
  IN UINT64           Leaf,
  IN UINT64           Arg1,
  IN UINT64           Arg2,
  IN UINT64           Arg3,
  IN OUT VOID         *Results
  );

/**
  TDVMALL is a leaf function 0 for TDCALL. It helps invoke services from the
  host VMM to pass/receive information.

  @param[in]     Leaf        Number of sub-functions
  @param[in]     Arg1        Arg1
  @param[in]     Arg2        Arg2
  @param[in]     Arg3        Arg3
  @param[in]     Arg4        Arg4
  @param[in,out] Results     Returned result of the sub-function

  @return EFI_SUCCESS
  @return Other           See individual sub-functions

**/
EFI_STATUS
EFIAPI
TdVmCall (
  IN UINT64          Leaf,
  IN UINT64          Arg1,
  IN UINT64          Arg2,
  IN UINT64          Arg3,
  IN UINT64          Arg4,
  IN OUT VOID        *Results
  );

/**
  This function enable the TD guest to request the VMM to emulate CPUID
  operation, especially for non-architectural, CPUID leaves.

  @param[in]  Eax        Main leaf of the CPUID
  @param[in]  Ecx        Sub-leaf of the CPUID
  @param[out] Results    Returned result of CPUID operation

  @return EFI_SUCCESS
**/
EFI_STATUS
EFIAPI
TdVmCallCpuid (
  IN UINT64         Eax,
  IN UINT64         Ecx,
  OUT VOID          *Results
  );

#endif
