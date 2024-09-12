/** @file
  Initialize TPM2 device and measure FVs before handing off control to DXE.

Copyright (c) 2015 - 2021, Intel Corporation. All rights reserved.<BR>
Copyright (c) 2017, Microsoft Corporation.  All rights reserved. <BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiPei.h>
#include <Ppi/CcMeasurement.h>
#include <Library/DebugLib.h>
#include <Library/PeiServicesLib.h>

BOOLEAN              mImageInMemory = FALSE;
EFI_PEI_FILE_HANDLE  mFileHandle;

/**
  Do a hash operation on a data buffer, extend a specific TPM PCR with the hash result,
  and build a GUIDed HOB recording the event which will be passed to the DXE phase and
  added into the Event Log.

  @param[in]      This          Indicates the calling context
  @param[in]      Flags         Bitmap providing additional information.
  @param[in]      HashData      If BIT0 of Flags is 0, it is physical address of the
                                start of the data buffer to be hashed, extended, and logged.
                                If BIT0 of Flags is 1, it is physical address of the
                                start of the pre-hash data buffter to be extended, and logged.
                                The pre-hash data format is TPML_DIGEST_VALUES.
  @param[in]      HashDataLen   The length, in bytes, of the buffer referenced by HashData.
  @param[in]      NewEventHdr   Pointer to a TCG_PCR_EVENT_HDR data structure.
  @param[in]      NewEventData  Pointer to the new event data.

  @retval EFI_SUCCESS           Operation completed successfully.
  @retval EFI_OUT_OF_RESOURCES  No enough memory to log the new event.
  @retval EFI_DEVICE_ERROR      The command was unsuccessful.

**/
EFI_STATUS
EFIAPI
TdHashLogExtendEvent (
  IN      EDKII_CC_PPI      *This,
  IN      UINT64             Flags,
  IN      UINT8              *HashData,
  IN      UINTN              HashDataLen,
  IN      CC_EVENT_HDR       *NewEventHdr,
  IN      UINT8              *NewEventData
  )
{
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
TdMapPcrToMrIndex (
  IN  EDKII_CC_PPI  *This,
  IN  UINT32        PCRIndex,
  OUT UINT32        *MrIndex
  )
{
  return EFI_SUCCESS;
}

EDKII_CC_PPI  mEdkiiCcPpi = {
  TdHashLogExtendEvent,
  TdMapPcrToMrIndex
};

EFI_PEI_PPI_DESCRIPTOR  mCcPpiList = {
  EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST,
  &gEdkiiCcPpiGuid,
  &mEdkiiCcPpi
};

/**
  Do measurement after memory is ready.

  @param[in]      PeiServices   Describes the list of possible PEI Services.

  @retval EFI_SUCCESS           Operation completed successfully.
  @retval EFI_OUT_OF_RESOURCES  No enough memory to log the new event.
  @retval EFI_DEVICE_ERROR      The command was unsuccessful.

**/
EFI_STATUS
PeimEntryMP (
  IN      EFI_PEI_SERVICES  **PeiServices
  )
{
  EFI_STATUS  Status;

  //
  // install Tcg Services
  //
  Status = PeiServicesInstallPpi (&mCcPpiList);
  ASSERT_EFI_ERROR (Status);

  return Status;
}

/**
  Entry point of this module.

  @param[in] FileHandle   Handle of the file being invoked.
  @param[in] PeiServices  Describes the list of possible PEI Services.

  @return Status.

**/
EFI_STATUS
EFIAPI
PeimEntryMA (
  IN       EFI_PEI_FILE_HANDLE  FileHandle,
  IN CONST EFI_PEI_SERVICES     **PeiServices
  )
{
  EFI_STATUS     Status;

  if(!TdIsEnabled ()) {
    DEBUG ((DEBUG_INFO, "TD is not enabled.\n"));
    return EFI_UNSUPPORTED;
  }

  Status = (**PeiServices).RegisterForShadow (FileHandle);
  if (Status == EFI_ALREADY_STARTED) {
    mImageInMemory = TRUE;
    mFileHandle    = FileHandle;
  } else if (Status == EFI_NOT_FOUND) {
    ASSERT_EFI_ERROR (Status);
  }


  if (mImageInMemory) {
    Status = PeimEntryMP ((EFI_PEI_SERVICES **)PeiServices);
  }

  return Status;
}
