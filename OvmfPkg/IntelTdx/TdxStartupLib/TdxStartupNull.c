#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/TdxStartupLib.h>

VOID
EFIAPI
TdxStartup(
  IN VOID                           * Context
  )
{
  ASSERT (FALSE);
  CpuDeadLoop ();
}

