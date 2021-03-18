#ifndef __TDX_STARTUP_LIB_H__
#define __TDX_STARTUP_LIB_H__

#include <Library/BaseLib.h>
#include <Uefi/UefiBaseType.h>
#include <Uefi/UefiSpec.h>
#include <Library/DebugLib.h>
#include <Protocol/DebugSupport.h>

typedef
VOID
(EFIAPI * fProcessLibraryConstructorList)(
  IN VOID   *ImageHandle,
  IN VOID   *SystemTable
  );


VOID
EFIAPI
TdxStartup(
  IN VOID                           * Context,
  IN VOID                           * VmmHobList,
  IN UINTN                          Info,
  IN fProcessLibraryConstructorList Function
);

#endif
