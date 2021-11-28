/** @file

  Copyright (c) 2021, Intel Corporation. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/
#ifndef TDX_STARTUP_LIB_H_
#define TDX_STARTUP_LIB_H_

#include <Library/BaseLib.h>
#include <Uefi/UefiBaseType.h>
#include <Uefi/UefiSpec.h>
#include <Pi/PiPeiCis.h>
#include <Library/DebugLib.h>
#include <Protocol/DebugSupport.h>
#include <IndustryStandard/Tpm20.h>

/**
 * @brief 
 * 
 * @param Context 
 * @return VOID 
 */
VOID
EFIAPI
TdxStartup(
  IN VOID     *Context
);

#endif
