/** @file
  GCC inline implementation of BaseSynchronizationLib processor specific functions.

  Copyright (c) 2006 - 2018, Intel Corporation. All rights reserved.<BR>
  Portions copyright (c) 2008 - 2009, Apple Inc. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/



/**
  Performs an atomic increment of an 32-bit unsigned integer.

  Performs an atomic increment of the 32-bit unsigned integer specified by
  Value and returns the incremented value. The increment operation must be
  performed using MP safe mechanisms.

  @param  Value A pointer to the 32-bit value to increment.

  @return The incremented value.

**/
UINT32
EFIAPI
TdInternalSyncIncrement (
  IN      volatile UINT32    *Value
  )
{
  UINT32  Result;

  __asm__ __volatile__ (
    "movl    $1, %%eax  \n\t"
    "lock               \n\t"
    "xadd    %%eax, %1  \n\t"
    "inc     %%eax      \n\t"
    : "=&a" (Result),         // %0
      "+m" (*Value)           // %1
    :                         // no inputs that aren't also outputs
    : "memory",
      "cc"
    );

  return Result;
}

/**
  Performs an atomic decrement of an 32-bit unsigned integer.

  Performs an atomic decrement of the 32-bit unsigned integer specified by
  Value and returns the decremented value. The decrement operation must be
  performed using MP safe mechanisms.

  @param  Value A pointer to the 32-bit value to decrement.

  @return The decremented value.

**/
UINT32
EFIAPI
TdInternalSyncDecrement (
  IN      volatile UINT32       *Value
  )
{
   UINT32  Result;

  __asm__ __volatile__ (
    "movl    $-1, %%eax  \n\t"
    "lock                \n\t"
    "xadd    %%eax, %1   \n\t"
    "dec     %%eax       \n\t"
    : "=&a" (Result),          // %0
      "+m" (*Value)            // %1
    :                          // no inputs that aren't also outputs
    : "memory",
      "cc"
    );

  return Result;
}