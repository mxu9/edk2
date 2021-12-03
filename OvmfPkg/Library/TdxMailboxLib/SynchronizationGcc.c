/** @file
  Implementation of synchronization functions.

  Copyright (c) 2006 - 2018, Intel Corporation. All rights reserved.<BR>
  Portions copyright (c) 2008 - 2009, Apple Inc. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "TdxMailboxInternal.h"

/**
  Performs an atomic increment of an 32-bit unsigned integer.

  Performs an atomic increment of the 32-bit unsigned integer specified by
  Value and returns the incremented value. The increment operation must be
  performed using MP safe mechanisms.

  If Value is NULL, then ASSERT().

  @param  Value A pointer to the 32-bit value to increment.

  @return The incremented value.

**/
UINT32
EFIAPI
TdInterlockedIncrement (
  IN      volatile UINT32           *Value
  )
{
  ASSERT (Value != NULL);
  return TdInternalSyncIncrement (Value);
}

/**
  Performs an atomic decrement of an 32-bit unsigned integer.

  Performs an atomic decrement of the 32-bit unsigned integer specified by
  Value and returns the decremented value. The decrement operation must be
  performed using MP safe mechanisms.

  If Value is NULL, then ASSERT().

  @param  Value A pointer to the 32-bit value to decrement.

  @return The decremented value.

**/
UINT32
EFIAPI
TdInterlockedDecrement (
  IN      volatile UINT32           *Value
  )
{
  ASSERT (Value != NULL);
  return TdInternalSyncDecrement (Value);
}
