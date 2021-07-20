/** @file
  Main SEC phase code. Handles initial TDX Hob List Processing

  Copyright (c) 2008, Intel Corporation. All rights reserved.<BR>
  (C) Copyright 2016 Hewlett Packard Enterprise Development LP<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiPei.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>

VOID
TdxPublishRamRegions (
  VOID
  )
{
}

VOID
IntelTdxInitialize (
  VOID
  )
{
}

VOID
AsmGetRelocationMap (
  OUT MP_RELOCATION_MAP    *AddressMap
  )
{
  *AddressMap = NULL;
}

