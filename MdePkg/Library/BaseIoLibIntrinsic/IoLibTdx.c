/** @file
  TDX I/O Library routines.

  Copyright (c) 2020-2021, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/


#include "IoLibTdx.h"
#include <Include/IndustryStandard/Tdx.h>
#include <Library/TdxLib.h>

/**
  Reads an 8-bit I/O port.

  Reads the 8-bit I/O port specified by Port. The 8-bit read value is returned.
  This function must guarantee that all I/O read and write operations are
  serialized.

  If 8-bit I/O port operations are not supported, then ASSERT().

  @param  Port  The I/O port to read.

  @return The value read.

**/
UINT8
EFIAPI
TdIoRead8 (
  IN      UINTN                     Port
  )
{
  UINT64 Status;
  UINT64 Val;

  Status = TdVmCall (TDVMCALL_IO, 1, 0, Port, 0, &Val);
  if (Status != 0) {
    TdVmCall (TDVMCALL_HALT, 0, 0, 0, 0, 0);
  }
  return (UINT8)Val;
}

/**
  Reads a 16-bit I/O port.

  Reads the 16-bit I/O port specified by Port. The 16-bit read value is returned.
  This function must guarantee that all I/O read and write operations are
  serialized.

  If 16-bit I/O port operations are not supported, then ASSERT().
  If Port is not aligned on a 16-bit boundary, then ASSERT().

  @param  Port  The I/O port to read.

  @return The value read.

**/
UINT16
EFIAPI
TdIoRead16 (
  IN      UINTN                     Port
  )
{
  UINT64 Status;
  UINT64 Val;

  ASSERT ((Port & 1) == 0);

  Status = TdVmCall (TDVMCALL_IO, 2, 0, Port, 0, &Val);
  if (Status != 0) {
    TdVmCall (TDVMCALL_HALT, 0, 0, 0, 0, 0);
  }
  return (UINT16)Val;
}

/**
  Reads a 32-bit I/O port.

  Reads the 32-bit I/O port specified by Port. The 32-bit read value is returned.
  This function must guarantee that all I/O read and write operations are
  serialized.

  If 32-bit I/O port operations are not supported, then ASSERT().
  If Port is not aligned on a 32-bit boundary, then ASSERT().

  @param  Port  The I/O port to read.

  @return The value read.

**/
UINT32
EFIAPI
TdIoRead32 (
  IN      UINTN                     Port
  )
{
  UINT64 Status;
  UINT64 Val;

  ASSERT ((Port & 3) == 0);

  Status = TdVmCall (TDVMCALL_IO, 4, 0, Port, 0, &Val);
  if (Status != 0) {
    TdVmCall (TDVMCALL_HALT, 0, 0, 0, 0, 0);
  }
  return (UINT32)Val;
}

/**
  Writes an 8-bit I/O port.

  Writes the 8-bit I/O port specified by Port with the value specified by Value
  and returns Value. This function must guarantee that all I/O read and write
  operations are serialized.

  If 8-bit I/O port operations are not supported, then ASSERT().

  @param  Port  The I/O port to write.
  @param  Value The value to write to the I/O port.

  @return The value written the I/O port.

**/
UINT8
EFIAPI
TdIoWrite8 (
  IN      UINTN                     Port,
  IN      UINT8                     Value
  )
{
  UINT64 Status;
  UINT64 Val;

  Val = Value;
  Status = TdVmCall (TDVMCALL_IO, 1, 1, Port, Val, 0);
  if (Status != 0) {
    TdVmCall (TDVMCALL_HALT, 0, 0, 0, 0, 0);
  }
  return Value;;
}

/**
  Writes a 16-bit I/O port.

  Writes the 16-bit I/O port specified by Port with the value specified by Value
  and returns Value. This function must guarantee that all I/O read and write
  operations are serialized.

  If 16-bit I/O port operations are not supported, then ASSERT().
  If Port is not aligned on a 16-bit boundary, then ASSERT().

  @param  Port  The I/O port to write.
  @param  Value The value to write to the I/O port.

  @return The value written the I/O port.

**/
UINT16
EFIAPI
TdIoWrite16 (
  IN      UINTN                     Port,
  IN      UINT16                    Value
  )
{
  UINT64 Status;
  UINT64 Val;

  ASSERT ((Port & 1) == 0);
  Val = Value;
  Status = TdVmCall (TDVMCALL_IO, 2, 1, Port, Val, 0);
  if (Status != 0) {
    TdVmCall (TDVMCALL_HALT, 0, 0, 0, 0, 0);
  }
  return Value;;
}

/**
  Writes a 32-bit I/O port.

  Writes the 32-bit I/O port specified by Port with the value specified by Value
  and returns Value. This function must guarantee that all I/O read and write
  operations are serialized.

  If 32-bit I/O port operations are not supported, then ASSERT().
  If Port is not aligned on a 32-bit boundary, then ASSERT().

  @param  Port  The I/O port to write.
  @param  Value The value to write to the I/O port.

  @return The value written the I/O port.

**/
UINT32
EFIAPI
TdIoWrite32 (
  IN      UINTN                     Port,
  IN      UINT32                    Value
  )
{
  UINT64 Status;
  UINT64 Val;

  ASSERT ((Port & 3) == 0);
  Val = Value;
  Status = TdVmCall (TDVMCALL_IO, 4, 1, Port, Val, 0);
  if (Status != 0) {
    TdVmCall (TDVMCALL_HALT, 0, 0, 0, 0, 0);
  }
  return Value;;
}

/**
  Reads an 8-bit MMIO register.

  Reads the 8-bit MMIO register specified by Address. The 8-bit read value is
  returned. This function must guarantee that all MMIO read and write
  operations are serialized.

  If 8-bit MMIO register operations are not supported, then ASSERT().

  @param  Address The MMIO register to read.

  @return The value read.

**/
UINT8
EFIAPI
TdMmioRead8 (
  IN      UINTN                     Address
  )
{
  UINT64                             Value;
  UINT64                             Status;

  MemoryFence ();
  Status = TdVmCall (TDVMCALL_MMIO, 1, 0, Address, 0, &Value);
  if (Status != 0) {
    Value = *(volatile UINT64*)Address;
  }
  MemoryFence ();

  return (UINT8)Value;
}

/**
  Writes an 8-bit MMIO register.

  Writes the 8-bit MMIO register specified by Address with the value specified
  by Value and returns Value. This function must guarantee that all MMIO read
  and write operations are serialized.

  If 8-bit MMIO register operations are not supported, then ASSERT().

  @param  Address The MMIO register to write.
  @param  Value   The value to write to the MMIO register.

  @return Value.

**/
UINT8
EFIAPI
TdMmioWrite8 (
  IN      UINTN                     Address,
  IN      UINT8                     Val
  )
{
  UINT64                             Value;
  UINT64                             Status;

  MemoryFence ();
  Value = Val;
  Status = TdVmCall (TDVMCALL_MMIO, 1, 1, Address, Value, 0);
  if (Status != 0) {
    *(volatile UINT8*)Address = Val;
  }
  MemoryFence ();

  return Val;
}

/**
  Reads a 16-bit MMIO register.

  Reads the 16-bit MMIO register specified by Address. The 16-bit read value is
  returned. This function must guarantee that all MMIO read and write
  operations are serialized.

  If 16-bit MMIO register operations are not supported, then ASSERT().
  If Address is not aligned on a 16-bit boundary, then ASSERT().

  @param  Address The MMIO register to read.

  @return The value read.

**/
UINT16
EFIAPI
TdMmioRead16 (
  IN      UINTN                     Address
  )
{
  UINT64                             Value;
  UINT64                             Status;

  MemoryFence ();
  Status = TdVmCall (TDVMCALL_MMIO, 2, 0, Address, 0, &Value);
  if (Status != 0) {
    Value = *(volatile UINT64*)Address;
  }
  MemoryFence ();

  return (UINT16)Value;
}

/**
  Writes a 16-bit MMIO register.

  Writes the 16-bit MMIO register specified by Address with the value specified
  by Value and returns Value. This function must guarantee that all MMIO read
  and write operations are serialized.

  If 16-bit MMIO register operations are not supported, then ASSERT().
  If Address is not aligned on a 16-bit boundary, then ASSERT().

  @param  Address The MMIO register to write.
  @param  Value   The value to write to the MMIO register.

  @return Value.

**/
UINT16
EFIAPI
TdMmioWrite16 (
  IN      UINTN                     Address,
  IN      UINT16                    Val
  )
{
  UINT64                             Value;
  UINT64                             Status;

  ASSERT ((Address & 1) == 0);

  MemoryFence ();
  Value = Val;
  Status = TdVmCall (TDVMCALL_MMIO, 2, 1, Address, Value, 0);
  if (Status != 0) {
    *(volatile UINT16*)Address = Val;
  }
  MemoryFence ();

  return Val;
}

/**
  Reads a 32-bit MMIO register.

  Reads the 32-bit MMIO register specified by Address. The 32-bit read value is
  returned. This function must guarantee that all MMIO read and write
  operations are serialized.

  If 32-bit MMIO register operations are not supported, then ASSERT().
  If Address is not aligned on a 32-bit boundary, then ASSERT().

  @param  Address The MMIO register to read.

  @return The value read.

**/
UINT32
EFIAPI
TdMmioRead32 (
  IN      UINTN                     Address
  )
{
  UINT64                             Value;
  UINT64                             Status;

  MemoryFence ();
  Status = TdVmCall (TDVMCALL_MMIO, 4, 0, Address, 0, &Value);
  if (Status != 0) {
    Value = *(volatile UINT64*)Address;
  }
  MemoryFence ();

  return (UINT32)Value;
}

/**
  Writes a 32-bit MMIO register.

  Writes the 32-bit MMIO register specified by Address with the value specified
  by Value and returns Value. This function must guarantee that all MMIO read
  and write operations are serialized.

  If 32-bit MMIO register operations are not supported, then ASSERT().
  If Address is not aligned on a 32-bit boundary, then ASSERT().

  @param  Address The MMIO register to write.
  @param  Value   The value to write to the MMIO register.

  @return Value.

**/
UINT32
EFIAPI
TdMmioWrite32 (
  IN      UINTN                     Address,
  IN      UINT32                    Val
  )
{
  UINT64                             Value;
  UINT64                             Status;

  ASSERT ((Address & 3) == 0);

  MemoryFence ();
  Value = Val;
  Status = TdVmCall (TDVMCALL_MMIO, 4, 1, Address, Value, 0);
  if (Status != 0) {
    *(volatile UINT32*)Address = Val;
  }
  MemoryFence ();

  return Val;
}

/**
  Reads a 64-bit MMIO register.

  Reads the 64-bit MMIO register specified by Address. The 64-bit read value is
  returned. This function must guarantee that all MMIO read and write
  operations are serialized.

  If 64-bit MMIO register operations are not supported, then ASSERT().
  If Address is not aligned on a 64-bit boundary, then ASSERT().

  @param  Address The MMIO register to read.

  @return The value read.

**/
UINT64
EFIAPI
TdMmioRead64 (
  IN      UINTN                     Address
  )
{
  UINT64                             Value;
  UINT64                             Status;

  MemoryFence ();
  Status = TdVmCall (TDVMCALL_MMIO, 8, 0, Address, 0, &Value);
  if (Status != 0) {
    Value = *(volatile UINT64*)Address;
  }
  MemoryFence ();

  return Value;
}

/**
  Writes a 64-bit MMIO register.

  Writes the 64-bit MMIO register specified by Address with the value specified
  by Value and returns Value. This function must guarantee that all MMIO read
  and write operations are serialized.

  If 64-bit MMIO register operations are not supported, then ASSERT().
  If Address is not aligned on a 64-bit boundary, then ASSERT().

  @param  Address The MMIO register to write.
  @param  Value   The value to write to the MMIO register.

**/
UINT64
EFIAPI
TdMmioWrite64 (
  IN      UINTN                     Address,
  IN      UINT64                    Value
  )
{
  UINT64                             Status;

  ASSERT ((Address & 7) == 0);

  MemoryFence ();
  Status = TdVmCall (TDVMCALL_MMIO, 4, 1, Address, Value, 0);
  if (Status != 0) {
    *(volatile UINT64*)Address = Value;
  }
  MemoryFence ();
  return Value;
}


