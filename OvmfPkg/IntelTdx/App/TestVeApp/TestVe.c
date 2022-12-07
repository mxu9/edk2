/** @file
  This is a test application that demonstrates how to use the C-style entry point
  for a shell application.

  Copyright (c) 2009 - 2015, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/ShellLib.h>
#include <Library/ShellCEntryLib.h>
#include <Library/PciLib.h>
#include <Protocol/PciIo.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "InternalMmio.h"

STATIC SHELL_PARAM_ITEM  mParamList[] = {
  { L"-r", TypeValue },
  { L"-c", TypeValue },
  { L"-i", TypeFlag  },
  { L"-u", TypeValue },
  { L"-a", TypeFlag  },
  { L"-?", TypeFlag  },
  { L"-h", TypeFlag  },
  { NULL,  TypeMax   },
};

STATIC BOOLEAN  mIsRdMsr       = FALSE;
STATIC BOOLEAN  mIsCpuid       = FALSE;
STATIC BOOLEAN  mIsMmio        = FALSE;
STATIC BOOLEAN  mIsUnsupported = FALSE;
STATIC BOOLEAN  mIsAll         = FALSE;

/**
   Display Application Header
 **/
STATIC
VOID
DisplayHeader (
  )
{
  CHAR16  build_date[20];
  CHAR16  build_time[20];

  AsciiStrToUnicodeStrS (__DATE__, build_date, 100);
  AsciiStrToUnicodeStrS (__TIME__, build_time, 100);

  Print (L"*********************************************\n");
  Print (L"Built: %s %s\n", build_date, build_time);
  Print (L"*********************************************\n");
}

STATIC
VOID
PrintUsage (
  VOID
  )
{
  Print (
    L"Test Application to inject VE and trigger #VE handler in TDVF\n"
    L"usage: TestVe [-r] [-c] [-i] [-u]\n"
    L"  -r    Inject Read Msr VE.\n"
    L"  -c    Inject CPUID VE.\n"
    L"  -i    Inject IO_Instruction VE.\n"
    L"  -u    Inject unsupported VE, INVD.\n"
    L"  -a    Inject All VE above one by one.\n"
    L"\n"
    );
  return;
}

#if 0
VOID
WalkThruPci (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       NoHandles;
  EFI_HANDLE  *Handles;
  UINT64      Attributes;
  UINTN       Idx;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciIoProtocolGuid,
                  NULL /* SearchKey */,
                  &NoHandles,
                  &Handles
                  );
  if (Status == EFI_NOT_FOUND) {
    //
    // No PCI devices were found on either of the root bridges. We're done.
    //
    DEBUG ((DEBUG_INFO, "No PCI devices were found on either of the root bridges. We're done.\n"));
    return;
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_WARN,
      "%a: LocateHandleBuffer(): %r\n",
      __FUNCTION__,
      Status
      ));
    return;
  }

  for (Idx = 0; Idx < NoHandles; ++Idx) {
    EFI_PCI_IO_PROTOCOL  *PciIo;

    Status = gBS->HandleProtocol (
                    Handles[Idx],
                    &gEfiPciIoProtocolGuid,
                    (VOID **)&PciIo
                    );
    ASSERT_EFI_ERROR (Status);

    Status = PciIo->Attributes (
                      PciIo,
                      EfiPciIoAttributeOperationGet,
                      0,
                      &Attributes
                      );
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_WARN,
        "%a: EfiPciIoAttributeOperationGet: %r\n",
        __FUNCTION__,
        Status
        ));
      continue;
    }

    //
    // Retrieve supported attributes
    //
    Status = PciIo->Attributes (
                      PciIo,
                      EfiPciIoAttributeOperationSupported,
                      0,
                      &Attributes
                      );
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_WARN,
        "%a: EfiPciIoAttributeOperationSupported: %r\n",
        __FUNCTION__,
        Status
        ));
      break;
    } else {
      DEBUG ((DEBUG_INFO, "%d: Attributes Supported=%llx\n", Idx, Attributes));
    }
  }

  //
  // Success
  //
  return;
}

#endif

STATIC
EFI_STATUS
TestMmio (
  VOID
  )
{
  Print (L"MMIO instruction will cause #VE and TDVF should handle,expected to see MMIO #VE handler log in Debug TDVF\n");

  Print (L"MOV reg/memX, regX \n");
  TestMmioWrite_8889_1 ();
  TestMmioWrite_8889_2 ();
  TestMmioWrite_8889_4 ();
  TestMmioWrite_8889_8 ();

  Print (L"MOV reg/memX, immX \n");
  TestMmioWrite_C6C7_1 ();
  TestMmioWrite_C6C7_2 ();
  TestMmioWrite_C6C7_4 ();
  TestMmioWrite_C6C7_8 ();

  Print (L"MOV regX, reg/memX \n");
  TestMmioRead_8A8B_1 ();
  TestMmioRead_8A8B_2 ();
  TestMmioRead_8A8B_4 ();
  TestMmioRead_8A8B_8 ();

  // Print (L"MOVQ xmm1, reg/memX \n");
  // TestMmioRead_MOVQ1 ();
  // TestMmioRead_MOVQ2 ();

  Print (L"MOVZX regX, reg/memX \n");
  TestMmioRead_B6B7_1 ();
  TestMmioRead_B6B7_2 ();
  TestMmioRead_B6B7_3 ();
  TestMmioRead_B6B7_4 ();
  TestMmioRead_B6B7_5 ();

  Print (L"MOVSX regX, reg/memX \n");
  TestMmioRead_BEBF_1 ();
  TestMmioRead_BEBF_2 ();
  TestMmioRead_BEBF_3 ();
  TestMmioRead_BEBF_4 ();
  TestMmioRead_BEBF_5 ();
  // TestMmioRead_BEBF_6 ();
  // TestMmioRead_BEBF_7 ();
  // TestMmioRead_BEBF_8 ();


  return EFI_SUCCESS;
}

/**
  UEFI application entry point which has an interface similar to a
  standard C main function.

  The ShellCEntryLib library instance wrappers the actual UEFI application
  entry point and calls this ShellAppMain function.

  @param[in] Argc     The number of items in Argv.
  @param[in] Argv     Array of pointers to strings.

  @retval  0               The application exited normally.
  @retval  Other           An error occurred.

**/
INTN
EFIAPI
ShellAppMain (
  IN UINTN   Argc,
  IN CHAR16  **Argv
  )
{
  EFI_STATUS  Status;
  LIST_ENTRY  *ParamPackage;

  //
  // Display header
  //
  DisplayHeader ();

  Status = ShellCommandLineParse (mParamList, &ParamPackage, NULL, TRUE);

  if (EFI_ERROR (Status)) {
    Print (L"ERROR: Incorrect command line.\n");
    return Status;
  }

  if (ParamPackage == NULL) {
    mIsAll = TRUE;
  } else {
    if (ShellCommandLineGetFlag (ParamPackage, L"-?") ||
        ShellCommandLineGetFlag (ParamPackage, L"-h"))
    {
      PrintUsage ();
      return EFI_SUCCESS;
    }

    if (ShellCommandLineGetFlag (ParamPackage, L"-r")) {
      mIsRdMsr = TRUE;
    }

    if (ShellCommandLineGetFlag (ParamPackage, L"-c")) {
      mIsCpuid = TRUE;
    }

    if (ShellCommandLineGetFlag (ParamPackage, L"-i")) {
      mIsMmio = TRUE;
    }

    if (ShellCommandLineGetFlag (ParamPackage, L"-u")) {
      mIsUnsupported = TRUE;
    }

    if (ShellCommandLineGetFlag (ParamPackage, L"-a")) {
      mIsAll = TRUE;
    }
  }

  if (mIsAll) {
    // Do all
  } else {
    if (mIsMmio) {
      TestMmio ();
    }
  }

  return 0;
}
