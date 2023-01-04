/** @file
  This module implements Tcg2 Protocol.

Copyright (c) 2015 - 2019, Intel Corporation. All rights reserved.<BR>
(C) Copyright 2016 Hewlett Packard Enterprise Development LP<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <IndustryStandard/TcpaAcpi.h>
#include <Guid/CcEventHob.h>
#include <Guid/HobList.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HashLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/Tpm2CommandLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/CcMeasurement.h>
#include <Protocol/MpService.h>

#include "Tcg2Dxe.h"

#define   CC_EVENT_LOG_AREA_COUNT_MAX  1
#define   CC_MR_INDEX_0_MRTD           0
#define   CC_MR_INDEX_1_RTMR0          1
#define   CC_MR_INDEX_2_RTMR1          2
#define   CC_MR_INDEX_3_RTMR2          3
#define   CC_MR_INDEX_INVALID          4

typedef TCG_EVENT_LOG_AREA_STRUCT CC_EVENT_LOG_AREA_STRUCT;

typedef struct _TDX_DXE_DATA {
  EFI_CC_BOOT_SERVICE_CAPABILITY    BsCap;
  CC_EVENT_LOG_AREA_STRUCT          EventLogAreaStruct[CC_EVENT_LOG_AREA_COUNT_MAX];
  BOOLEAN                           GetEventLogCalled[CC_EVENT_LOG_AREA_COUNT_MAX];
  CC_EVENT_LOG_AREA_STRUCT          FinalEventLogAreaStruct[CC_EVENT_LOG_AREA_COUNT_MAX];
  EFI_CC_FINAL_EVENTS_TABLE         *FinalEventsTable[CC_EVENT_LOG_AREA_COUNT_MAX];
} TDX_DXE_DATA;

TDX_DXE_DATA  mTdxDxeData = {
  {
    sizeof (EFI_CC_BOOT_SERVICE_CAPABILITY), // Size
    { 1, 0 },                                // StructureVersion
    { 1, 0 },                                // ProtocolVersion
    EFI_CC_BOOT_HASH_ALG_SHA384,             // HashAlgorithmBitmap
    EFI_CC_EVENT_LOG_FORMAT_TCG_2,           // SupportedEventLogs
    { 2, 0 }                                 // {CC_TYPE, CC_SUBTYPE}
  },
};

EFI_CC_EVENTLOG_ACPI_TABLE  mTdxEventlogAcpiTemplate = {
  {
    EFI_CC_EVENTLOG_ACPI_TABLE_SIGNATURE,
    sizeof (mTdxEventlogAcpiTemplate),
    EFI_CC_EVENTLOG_ACPI_TABLE_REVISION,
    //
    // Compiler initializes the remaining bytes to 0
    // These fields should be filled in production
    //
  },
  { EFI_CC_TYPE_TDX, 0 }, // CcType
  0,                      // rsvd
  0,                      // laml
  0,                      // lasa
};

/**

  This function initialize TD_EVENT_HDR for EV_NO_ACTION
  Event Type other than EFI Specification ID event. The behavior is defined
  by TCG PC Client PFP Spec. Section 9.3.4 EV_NO_ACTION Event Types

  @param[in, out]   NoActionEvent  Event Header of EV_NO_ACTION Event
  @param[in]        EventSize      Event Size of the EV_NO_ACTION Event

**/
VOID
TdInitNoActionEvent (
  IN OUT CC_EVENT_HDR  *NoActionEvent,
  IN UINT32            EventSize
  )
{
  UINT32         DigestListCount;
  TPMI_ALG_HASH  HashAlgId;
  UINT8          *DigestBuffer;

  DigestBuffer    = (UINT8 *)NoActionEvent->Digests.digests;
  DigestListCount = 0;

  NoActionEvent->MrIndex   = 0;
  NoActionEvent->EventType = EV_NO_ACTION;

  //
  // Set Hash count & hashAlg accordingly, while Digest.digests[n].digest to all 0
  //
  ZeroMem (&NoActionEvent->Digests, sizeof (NoActionEvent->Digests));

  if ((mTdxDxeData.BsCap.HashAlgorithmBitmap & EFI_CC_BOOT_HASH_ALG_SHA384) != 0) {
    HashAlgId = TPM_ALG_SHA384;
    CopyMem (DigestBuffer, &HashAlgId, sizeof (TPMI_ALG_HASH));
    DigestBuffer += sizeof (TPMI_ALG_HASH) + GetHashSizeFromAlgo (HashAlgId);
    DigestListCount++;
  }

  //
  // Set Digests Count
  //
  WriteUnaligned32 ((UINT32 *)&NoActionEvent->Digests.count, DigestListCount);

  //
  // Set Event Size
  //
  WriteUnaligned32 ((UINT32 *)DigestBuffer, EventSize);
}

/**
  Add a new entry to the Event Log.

  @param[in] EventLogFormat  The type of the event log for which the information is requested.
  @param[in] NewEventHdr     Pointer to a TCG_PCR_EVENT_HDR/TCG_PCR_EVENT_EX data structure.
  @param[in] NewEventHdrSize New event header size.
  @param[in] NewEventData    Pointer to the new event data.
  @param[in] NewEventSize    New event data size.

  @retval EFI_SUCCESS           The new event log entry was added.
  @retval EFI_OUT_OF_RESOURCES  No enough memory to log the new event.

**/
EFI_STATUS
TdxDxeLogEvent (
  IN      EFI_CC_EVENT_LOG_FORMAT  EventLogFormat,
  IN      VOID                     *NewEventHdr,
  IN      UINT32                   NewEventHdrSize,
  IN      UINT8                    *NewEventData,
  IN      UINT32                   NewEventSize
  )
{
  EFI_STATUS                Status;
  UINTN                     Index;
  CC_EVENT_LOG_AREA_STRUCT  *EventLogAreaStruct;

  if (EventLogFormat != EFI_CC_EVENT_LOG_FORMAT_TCG_2) {
    ASSERT (FALSE);
    return EFI_INVALID_PARAMETER;
  }

  Index = 0;

  //
  // Record to normal event log
  //
  EventLogAreaStruct = &mTdxDxeData.EventLogAreaStruct[Index];

  if (EventLogAreaStruct->EventLogTruncated) {
    return EFI_VOLUME_FULL;
  }

  Status = TcgCommLogEvent (
             EventLogAreaStruct,
             NewEventHdr,
             NewEventHdrSize,
             NewEventData,
             NewEventSize
             );

  if (Status == EFI_OUT_OF_RESOURCES) {
    EventLogAreaStruct->EventLogTruncated = TRUE;
    return EFI_VOLUME_FULL;
  } else if (Status == EFI_SUCCESS) {
    EventLogAreaStruct->EventLogStarted = TRUE;
  }

  //
  // If GetEventLog is called, record to FinalEventsTable, too.
  //
  if (mTdxDxeData.GetEventLogCalled[Index]) {
    if (mTdxDxeData.FinalEventsTable[Index] == NULL) {
      //
      // no need for FinalEventsTable
      //
      return EFI_SUCCESS;
    }

    EventLogAreaStruct = &mTdxDxeData.FinalEventLogAreaStruct[Index];

    if (EventLogAreaStruct->EventLogTruncated) {
      return EFI_VOLUME_FULL;
    }

    Status = TcgCommLogEvent (
               EventLogAreaStruct,
               NewEventHdr,
               NewEventHdrSize,
               NewEventData,
               NewEventSize
               );
    if (Status == EFI_OUT_OF_RESOURCES) {
      EventLogAreaStruct->EventLogTruncated = TRUE;
      return EFI_VOLUME_FULL;
    } else if (Status == EFI_SUCCESS) {
      EventLogAreaStruct->EventLogStarted = TRUE;
      //
      // Increase the NumberOfEvents in FinalEventsTable
      //
      (mTdxDxeData.FinalEventsTable[Index])->NumberOfEvents++;
      DEBUG ((DEBUG_INFO, "FinalEventsTable->NumberOfEvents - 0x%x\n", (mTdxDxeData.FinalEventsTable[Index])->NumberOfEvents));
      DEBUG ((DEBUG_INFO, "  Size - 0x%x\n", (UINTN)EventLogAreaStruct->EventLogSize));
    }
  }

  return Status;
}

/**
  Add a new entry to the Event Log. The call chain is like below:
  TdxDxeLogHashEvent -> TdxDxeLogEvent -> TcgCommonLogEvent

  Before this function is called, the event information (including the digest)
  is ready.

  @param[in]     DigestList    A list of digest.
  @param[in,out] NewEventHdr   Pointer to a TD_EVENT_HDR data structure.
  @param[in]     NewEventData  Pointer to the new event data.

  @retval EFI_SUCCESS           The new event log entry was added.
  @retval EFI_OUT_OF_RESOURCES  No enough memory to log the new event.
**/
EFI_STATUS
TdxDxeLogHashEvent (
  IN      TPML_DIGEST_VALUES  *DigestList,
  IN OUT  CC_EVENT_HDR        *NewEventHdr,
  IN      UINT8               *NewEventData
  )
{
  EFI_STATUS               Status;
  EFI_TPL                  OldTpl;
  EFI_STATUS               RetStatus;
  CC_EVENT                 CcEvent;
  UINT8                    *DigestBuffer;
  UINT32                   *EventSizePtr;
  EFI_CC_EVENT_LOG_FORMAT  LogFormat;

  RetStatus = EFI_SUCCESS;
  LogFormat = EFI_CC_EVENT_LOG_FORMAT_TCG_2;

  ZeroMem (&CcEvent, sizeof (CcEvent));
  CcEvent.MrIndex   = NewEventHdr->MrIndex;
  CcEvent.EventType = NewEventHdr->EventType;
  DigestBuffer      = (UINT8 *)&CcEvent.Digests;
  EventSizePtr      = CopyDigestListToBuffer (DigestBuffer, DigestList, HASH_ALG_SHA384);
  CopyMem (EventSizePtr, &NewEventHdr->EventSize, sizeof (NewEventHdr->EventSize));

  //
  // Enter critical region
  //
  OldTpl = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  Status = TdxDxeLogEvent (
             LogFormat,
             &CcEvent,
             sizeof (CcEvent.MrIndex) + sizeof (CcEvent.EventType) + GetDigestListBinSize (DigestBuffer) + sizeof (CcEvent.EventSize),
             NewEventData,
             NewEventHdr->EventSize
             );
  if (Status != EFI_SUCCESS) {
    RetStatus = Status;
  }

  gBS->RestoreTPL (OldTpl);

  return RetStatus;
}

/**
  Do a hash operation on a data buffer, extend a specific RTMR with the hash result,
  and add an entry to the Event Log.

  @param[in]      Flags         Bitmap providing additional information.
  @param[in]      HashData      Physical address of the start of the data buffer
                                to be hashed, extended, and logged.
  @param[in]      HashDataLen   The length, in bytes, of the buffer referenced by HashData
  @param[in, out] NewEventHdr   Pointer to a TD_EVENT_HDR data structure.
  @param[in]      NewEventData  Pointer to the new event data.

  @retval EFI_SUCCESS           Operation completed successfully.
  @retval EFI_OUT_OF_RESOURCES  No enough memory to log the new event.
  @retval EFI_DEVICE_ERROR      The command was unsuccessful.

**/
EFI_STATUS
TdxDxeHashLogExtendEvent (
  IN      UINT64        Flags,
  IN      UINT8         *HashData,
  IN      UINT64        HashDataLen,
  IN OUT  CC_EVENT_HDR  *NewEventHdr,
  IN      UINT8         *NewEventData
  )
{
  EFI_STATUS          Status;
  TPML_DIGEST_VALUES  DigestList;
  CC_EVENT_HDR        NoActionEvent;

  if (NewEventHdr->EventType == EV_NO_ACTION) {
    //
    // Do not do RTMR extend for EV_NO_ACTION
    //
    Status = EFI_SUCCESS;
    TdInitNoActionEvent (&NoActionEvent, NewEventHdr->EventSize);
    if ((Flags & EFI_CC_FLAG_EXTEND_ONLY) == 0) {
      Status = TdxDxeLogHashEvent (&(NoActionEvent.Digests), NewEventHdr, NewEventData);
    }

    return Status;
  }

  //
  // According to UEFI Spec 2.10 Section 38.4.1 the mapping between MrIndex and Intel
  // TDX Measurement Register is:
  //    MrIndex 0   <--> MRTD
  //    MrIndex 1-3 <--> RTMR[0-2]
  // Only the RMTR registers can be extended in TDVF by HashAndExtend. So MrIndex will
  // decreased by 1 before it is sent to HashAndExtend.
  //
  Status = HashAndExtend (
             NewEventHdr->MrIndex - 1,
             HashData,
             (UINTN)HashDataLen,
             &DigestList
             );
  if (!EFI_ERROR (Status)) {
    if ((Flags & EFI_CC_FLAG_EXTEND_ONLY) == 0) {
      Status = TdxDxeLogHashEvent (&DigestList, NewEventHdr, NewEventData);
    }
  }

  return Status;
}

/**
  The EFI_CC_MEASUREMENT_PROTOCOL HashLogExtendEvent function call provides callers with
  an opportunity to extend and optionally log events without requiring
  knowledge of actual TPM commands.
  The extend operation will occur even if this function cannot create an event
  log entry (e.g. due to the event log being full).

  @param[in]  This               Indicates the calling context
  @param[in]  Flags              Bitmap providing additional information.
  @param[in]  DataToHash         Physical address of the start of the data buffer to be hashed.
  @param[in]  DataToHashLen      The length in bytes of the buffer referenced by DataToHash.
  @param[in]  Event              Pointer to data buffer containing information about the event.

  @retval EFI_SUCCESS            Operation completed successfully.
  @retval EFI_DEVICE_ERROR       The command was unsuccessful.
  @retval EFI_VOLUME_FULL        The extend operation occurred, but the event could not be written to one or more event logs.
  @retval EFI_INVALID_PARAMETER  One or more of the parameters are incorrect.
  @retval EFI_UNSUPPORTED        The PE/COFF image type is not supported.
**/
EFI_STATUS
EFIAPI
TdHashLogExtendEvent (
  IN EFI_CC_MEASUREMENT_PROTOCOL  *This,
  IN UINT64                       Flags,
  IN EFI_PHYSICAL_ADDRESS         DataToHash,
  IN UINT64                       DataToHashLen,
  IN EFI_CC_EVENT                 *CcEvent
  )
{
  EFI_STATUS          Status;
  CC_EVENT_HDR        NewEventHdr;
  TPML_DIGEST_VALUES  DigestList;

  DEBUG ((DEBUG_VERBOSE, "TdHashLogExtendEvent ...\n"));

  if ((This == NULL) || (CcEvent == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Do not check hash data size for EV_NO_ACTION event.
  //
  if ((CcEvent->Header.EventType != EV_NO_ACTION) && (DataToHash == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if (CcEvent->Size < CcEvent->Header.HeaderSize + sizeof (UINT32)) {
    return EFI_INVALID_PARAMETER;
  }

  if (CcEvent->Header.MrIndex == CC_MR_INDEX_0_MRTD) {
    DEBUG ((DEBUG_ERROR, "%a: MRTD cannot be extended in TDVF.\n", __FUNCTION__));
    return EFI_INVALID_PARAMETER;
  }

  if (CcEvent->Header.MrIndex >= CC_MR_INDEX_INVALID) {
    DEBUG ((DEBUG_ERROR, "%a: MrIndex is invalid. (%d)\n", __FUNCTION__, CcEvent->Header.MrIndex));
    return EFI_INVALID_PARAMETER;
  }

  NewEventHdr.MrIndex   = CcEvent->Header.MrIndex;
  NewEventHdr.EventType = CcEvent->Header.EventType;
  NewEventHdr.EventSize = CcEvent->Size - sizeof (UINT32) - CcEvent->Header.HeaderSize;
  if ((Flags & EFI_CC_FLAG_PE_COFF_IMAGE) != 0) {
    //
    // According to UEFI Spec 2.10 Section 38.4.1 the mapping between MrIndex and Intel
    // TDX Measurement Register is:
    //    MrIndex 0   <--> MRTD
    //    MrIndex 1-3 <--> RTMR[0-2]
    // Only the RMTR registers can be extended in TDVF by HashAndExtend. So MrIndex will
    // decreased by 1 before it is sent to MeasurePeImageAndExtend.
    //
    Status = MeasurePeImageAndExtend (
               NewEventHdr.MrIndex - 1,
               DataToHash,
               (UINTN)DataToHashLen,
               &DigestList
               );
    if (!EFI_ERROR (Status)) {
      if ((Flags & EFI_CC_FLAG_EXTEND_ONLY) == 0) {
        Status = TdxDxeLogHashEvent (&DigestList, &NewEventHdr, CcEvent->Event);
      }
    }
  } else {
    Status = TdxDxeHashLogExtendEvent (
               Flags,
               (UINT8 *)(UINTN)DataToHash,
               DataToHashLen,
               &NewEventHdr,
               CcEvent->Event
               );
  }

  DEBUG ((DEBUG_VERBOSE, "%a - %r\n", __FUNCTION__, Status));
  return Status;
}

/**
  The EFI_CC_MEASUREMENT_PROTOCOL Get Event Log function call allows a caller to
  retrieve the address of a given event log and its last entry.

  @param[in]  This               Indicates the calling context
  @param[in]  EventLogFormat     The type of the event log for which the information is requested.
  @param[out] EventLogLocation   A pointer to the memory address of the event log.
  @param[out] EventLogLastEntry  If the Event Log contains more than one entry, this is a pointer to the
                                 address of the start of the last entry in the event log in memory.
  @param[out] EventLogTruncated  If the Event Log is missing at least one entry because an event would
                                 have exceeded the area allocated for events, this value is set to TRUE.
                                 Otherwise, the value will be FALSE and the Event Log will be complete.

  @retval EFI_SUCCESS            Operation completed successfully.
  @retval EFI_INVALID_PARAMETER  One or more of the parameters are incorrect
                                 (e.g. asking for an event log whose format is not supported).
**/
EFI_STATUS
EFIAPI
TdGetEventLog (
  IN EFI_CC_MEASUREMENT_PROTOCOL  *This,
  IN EFI_CC_EVENT_LOG_FORMAT      EventLogFormat,
  OUT EFI_PHYSICAL_ADDRESS        *EventLogLocation,
  OUT EFI_PHYSICAL_ADDRESS        *EventLogLastEntry,
  OUT BOOLEAN                     *EventLogTruncated
  )
{
  UINTN  Index = 0;

  DEBUG ((DEBUG_INFO, "TdGetEventLog ... (0x%x)\n", EventLogFormat));
  ASSERT (EventLogFormat == EFI_CC_EVENT_LOG_FORMAT_TCG_2);

  if (EventLogLocation != NULL) {
    *EventLogLocation = mTdxDxeData.EventLogAreaStruct[Index].Lasa;
    DEBUG ((DEBUG_INFO, "TdGetEventLog (EventLogLocation - %x)\n", *EventLogLocation));
  }

  if (EventLogLastEntry != NULL) {
    if (!mTdxDxeData.EventLogAreaStruct[Index].EventLogStarted) {
      *EventLogLastEntry = (EFI_PHYSICAL_ADDRESS)(UINTN)0;
    } else {
      *EventLogLastEntry = (EFI_PHYSICAL_ADDRESS)(UINTN)mTdxDxeData.EventLogAreaStruct[Index].LastEvent;
    }

    DEBUG ((DEBUG_INFO, "TdGetEventLog (EventLogLastEntry - %x)\n", *EventLogLastEntry));
  }

  if (EventLogTruncated != NULL) {
    *EventLogTruncated = mTdxDxeData.EventLogAreaStruct[Index].EventLogTruncated;
    DEBUG ((DEBUG_INFO, "TdGetEventLog (EventLogTruncated - %x)\n", *EventLogTruncated));
  }

  DEBUG ((DEBUG_INFO, "TdGetEventLog - %r\n", EFI_SUCCESS));

  // Dump Event Log for debug purpose
  if ((EventLogLocation != NULL) && (EventLogLastEntry != NULL)) {
    DumpEventLog (EventLogFormat, *EventLogLocation, *EventLogLastEntry, (EFI_TCG2_FINAL_EVENTS_TABLE *)mTdxDxeData.FinalEventsTable[Index]);
  }

  //
  // All events generated after the invocation of EFI_TCG2_GET_EVENT_LOG SHALL be stored
  // in an instance of an EFI_CONFIGURATION_TABLE named by the VendorGuid of EFI_TCG2_FINAL_EVENTS_TABLE_GUID.
  //
  mTdxDxeData.GetEventLogCalled[Index] = TRUE;

  return EFI_SUCCESS;
}

/**
  The EFI_CC_MEASUREMENT_PROTOCOL GetCapability function call provides protocol
  capability information and state information.

  @param[in]      This               Indicates the calling context
  @param[in, out] ProtocolCapability The caller allocates memory for a EFI_CC_BOOT_SERVICE_CAPABILITY
                                     structure and sets the size field to the size of the structure allocated.
                                     The callee fills in the fields with the EFI protocol capability information
                                     and the current EFI TCG2 state information up to the number of fields which
                                     fit within the size of the structure passed in.

  @retval EFI_SUCCESS            Operation completed successfully.
  @retval EFI_DEVICE_ERROR       The command was unsuccessful.
                                 The ProtocolCapability variable will not be populated.
  @retval EFI_INVALID_PARAMETER  One or more of the parameters are incorrect.
                                 The ProtocolCapability variable will not be populated.
  @retval EFI_BUFFER_TOO_SMALL   The ProtocolCapability variable is too small to hold the full response.
                                 It will be partially populated (required Size field will be set).
**/
EFI_STATUS
EFIAPI
TdGetCapability (
  IN EFI_CC_MEASUREMENT_PROTOCOL         *This,
  IN OUT EFI_CC_BOOT_SERVICE_CAPABILITY  *ProtocolCapability
  )
{
  DEBUG ((DEBUG_VERBOSE, "TdGetCapability\n"));

  if ((This == NULL) || (ProtocolCapability == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (!TdIsEnabled ()) {
    return EFI_UNSUPPORTED;
  }

  CopyMem (ProtocolCapability, &mTdxDxeData.BsCap, sizeof (EFI_CC_BOOT_SERVICE_CAPABILITY));

  return EFI_SUCCESS;
}

/**
  According to UEFI Spec 2.10 Section 38.4.1:
    The following table shows the TPM PCR index mapping and CC event log measurement
  register index interpretation for Intel TDX, where MRTD means Trust Domain Measurement
   Register and RTMR means Runtime Measurement Register

    // TPM PCR Index | CC Measurement Register Index | TDX-measurement register
    //  ------------------------------------------------------------------------
    // 0             |   0                           |   MRTD
    // 1, 7          |   1                           |   RTMR[0]
    // 2~6           |   2                           |   RTMR[1]
    // 8~15          |   3                           |   RTMR[2]

  @param[in] PCRIndex Index of the TPM PCR

  @retval    UINT32               Index of the CC Event Log Measurement Register Index
  @retval    CC_MR_INDEX_INVALID  Invalid MR Index
**/
UINT32
EFIAPI
MapPcrToMrIndex (
  IN  UINT32  PCRIndex
  )
{
  UINT32  MrIndex;

  if (PCRIndex > 15) {
    ASSERT (FALSE);
    return CC_MR_INDEX_INVALID;
  }

  MrIndex = 0;
  if (PCRIndex == 0) {
    MrIndex = CC_MR_INDEX_0_MRTD;
  } else if ((PCRIndex == 1) || (PCRIndex == 7)) {
    MrIndex = CC_MR_INDEX_1_RTMR0;
  } else if ((PCRIndex >= 2) && (PCRIndex <= 6)) {
    MrIndex = CC_MR_INDEX_2_RTMR1;
  } else if ((PCRIndex >= 8) && (PCRIndex <= 15)) {
    MrIndex = CC_MR_INDEX_3_RTMR2;
  }

  return MrIndex;
}

EFI_STATUS
EFIAPI
TdMapPcrToMrIndex (
  IN  EFI_CC_MEASUREMENT_PROTOCOL  *This,
  IN  UINT32                       PCRIndex,
  OUT UINT32                       *MrIndex
  )
{
  if (MrIndex == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *MrIndex = MapPcrToMrIndex (PCRIndex);

  return *MrIndex == CC_MR_INDEX_INVALID ? EFI_INVALID_PARAMETER : EFI_SUCCESS;
}

EFI_CC_MEASUREMENT_PROTOCOL  mTdProtocol = {
  TdGetCapability,
  TdGetEventLog,
  TdHashLogExtendEvent,
  TdMapPcrToMrIndex,
};

EFI_STATUS
Tcg2DxeInitializeTdx (
  VOID
  )
{
  if (!TdIsEnabled ()) {
    return EFI_UNSUPPORTED;
  }

  //
  // Fill information
  //
  mTdxDxeData.BsCap.Size                   = sizeof (EFI_CC_BOOT_SERVICE_CAPABILITY);
  mTdxDxeData.BsCap.ProtocolVersion.Major  = 1;
  mTdxDxeData.BsCap.ProtocolVersion.Minor  = 0;
  mTdxDxeData.BsCap.StructureVersion.Major = 1;
  mTdxDxeData.BsCap.StructureVersion.Minor = 0;

  //
  // Get supported PCR and current Active PCRs
  // For TD gueset HA384 is supported.
  //
  mTdxDxeData.BsCap.HashAlgorithmBitmap = HASH_ALG_SHA384;

  // TD guest only supports EFI_TCG2_EVENT_LOG_FORMAT_TCG_2
  mTdxDxeData.BsCap.SupportedEventLogs = EFI_CC_EVENT_LOG_FORMAT_TCG_2;

  return EFI_SUCCESS;
}

EFI_STATUS
TdSyncEvent (
  VOID
  )
{
  EFI_STATUS               Status;
  EFI_PEI_HOB_POINTERS     GuidHob;
  VOID                     *CcEvent;
  VOID                     *DigestListBin;
  UINT32                   DigestListBinSize;
  UINT8                    *Event;
  UINT32                   EventSize;
  EFI_CC_EVENT_LOG_FORMAT  LogFormat;

  DEBUG ((DEBUG_INFO, "Td SyncEvent from SEC/PEI\n"));

  Status       = EFI_SUCCESS;
  LogFormat    = EFI_CC_EVENT_LOG_FORMAT_TCG_2;
  GuidHob.Guid = GetFirstGuidHob (&gCcEventEntryHobGuid);

  while (!EFI_ERROR (Status) && GuidHob.Guid != NULL) {
    CcEvent = AllocateCopyPool (GET_GUID_HOB_DATA_SIZE (GuidHob.Guid), GET_GUID_HOB_DATA (GuidHob.Guid));
    if (CcEvent == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    GuidHob.Guid = GET_NEXT_HOB (GuidHob);
    GuidHob.Guid = GetNextGuidHob (&gCcEventEntryHobGuid, GuidHob.Guid);

    DigestListBin     = (UINT8 *)CcEvent + sizeof (UINT32) + sizeof (TCG_EVENTTYPE);
    DigestListBinSize = GetDigestListBinSize (DigestListBin);

    //
    // Event size.
    //
    EventSize = *(UINT32 *)((UINT8 *)DigestListBin + DigestListBinSize);
    Event     = (UINT8 *)DigestListBin + DigestListBinSize + sizeof (UINT32);

    //
    // Log the event
    //
    Status = TdxDxeLogEvent (
               LogFormat,
               CcEvent,
               sizeof (UINT32) + sizeof (TCG_EVENTTYPE) + DigestListBinSize + sizeof (UINT32),
               Event,
               EventSize
               );

    FreePool (CcEvent);
  }

  return Status;
}

#define TD_HASH_COUNT  1
#define TEMP_BUF_LEN   (sizeof(TCG_EfiSpecIDEventStruct) +  sizeof(UINT32) \
                     + (TD_HASH_COUNT * sizeof(TCG_EfiSpecIdEventAlgorithmSize)) + sizeof(UINT8))

/**
  Initialize the TD Event Log and log events passed from the PEI phase.

  @retval EFI_SUCCESS           Operation completed successfully.
  @retval EFI_OUT_OF_RESOURCES  Out of memory.

**/
EFI_STATUS
TdSetupEventLog (
  VOID
  )
{
  EFI_STATUS                       Status;
  EFI_PHYSICAL_ADDRESS             Lasa;
  UINTN                            Index;
  TCG_EfiSpecIDEventStruct         *TcgEfiSpecIdEventStruct;
  UINT8                            TempBuf[TEMP_BUF_LEN];
  TCG_PCR_EVENT_HDR                SpecIdEvent;
  TCG_EfiSpecIdEventAlgorithmSize  *DigestSize;
  TCG_EfiSpecIdEventAlgorithmSize  *TempDigestSize;
  UINT8                            *VendorInfoSize;
  UINT32                           NumberOfAlgorithms;
  EFI_CC_EVENT_LOG_FORMAT          LogFormat;
  EFI_PEI_HOB_POINTERS             GuidHob;
  CC_EVENT_HDR                     NoActionEvent;

  Status = EFI_SUCCESS;
  DEBUG ((DEBUG_INFO, "TdSetupEventLog\n"));

  Index     = 0;
  LogFormat = EFI_CC_EVENT_LOG_FORMAT_TCG_2;

  //
  // 1. Create Log Area
  //
  mTdxDxeData.EventLogAreaStruct[Index].EventLogFormat = LogFormat;

  // allocate pages for TD Event log
  Status = gBS->AllocatePages (
                  AllocateAnyPages,
                  EfiACPIMemoryNVS,
                  EFI_SIZE_TO_PAGES (PcdGet32 (PcdTcgLogAreaMinLen)),
                  &Lasa
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mTdxDxeData.EventLogAreaStruct[Index].Lasa                  = Lasa;
  mTdxDxeData.EventLogAreaStruct[Index].Laml                  = PcdGet32 (PcdTcgLogAreaMinLen);
  mTdxDxeData.EventLogAreaStruct[Index].Next800155EventOffset = 0;

  //
  // Report TD event log address and length, so that they can be reported in
  // TD ACPI table. Ignore the return status, because those fields are optional.
  //
  PcdSet32S (PcdCcEventlogAcpiTableLaml, (UINT32)mTdxDxeData.EventLogAreaStruct[Index].Laml);
  PcdSet64S (PcdCcEventlogAcpiTableLasa, mTdxDxeData.EventLogAreaStruct[Index].Lasa);

  //
  // To initialize them as 0xFF is recommended
  // because the OS can know the last entry for that.
  //
  SetMem ((VOID *)(UINTN)Lasa, PcdGet32 (PcdTcgLogAreaMinLen), 0xFF);

  //
  // Create first entry for Log Header Entry Data
  //

  //
  // TcgEfiSpecIdEventStruct
  //
  TcgEfiSpecIdEventStruct = (TCG_EfiSpecIDEventStruct *)TempBuf;
  CopyMem (TcgEfiSpecIdEventStruct->signature, TCG_EfiSpecIDEventStruct_SIGNATURE_03, sizeof (TcgEfiSpecIdEventStruct->signature));

  TcgEfiSpecIdEventStruct->platformClass = PcdGet8 (PcdTpmPlatformClass);

  TcgEfiSpecIdEventStruct->specVersionMajor = TCG_EfiSpecIDEventStruct_SPEC_VERSION_MAJOR_TPM2;
  TcgEfiSpecIdEventStruct->specVersionMinor = TCG_EfiSpecIDEventStruct_SPEC_VERSION_MINOR_TPM2;
  TcgEfiSpecIdEventStruct->specErrata       = TCG_EfiSpecIDEventStruct_SPEC_ERRATA_TPM2;
  TcgEfiSpecIdEventStruct->uintnSize        = sizeof (UINTN)/sizeof (UINT32);
  NumberOfAlgorithms                        = 0;
  DigestSize                                = (TCG_EfiSpecIdEventAlgorithmSize *)((UINT8 *)TcgEfiSpecIdEventStruct
                                                                                  + sizeof (*TcgEfiSpecIdEventStruct)
                                                                                  + sizeof (NumberOfAlgorithms));

  TempDigestSize              = DigestSize;
  TempDigestSize             += NumberOfAlgorithms;
  TempDigestSize->algorithmId = TPM_ALG_SHA384;
  TempDigestSize->digestSize  = SHA384_DIGEST_SIZE;
  NumberOfAlgorithms++;

  CopyMem (TcgEfiSpecIdEventStruct + 1, &NumberOfAlgorithms, sizeof (NumberOfAlgorithms));
  TempDigestSize  = DigestSize;
  TempDigestSize += NumberOfAlgorithms;
  VendorInfoSize  = (UINT8 *)TempDigestSize;
  *VendorInfoSize = 0;

  SpecIdEvent.PCRIndex  = 1;
  SpecIdEvent.EventType = EV_NO_ACTION;
  ZeroMem (&SpecIdEvent.Digest, sizeof (SpecIdEvent.Digest));
  SpecIdEvent.EventSize = (UINT32)GetTcgEfiSpecIdEventStructSize (TcgEfiSpecIdEventStruct);

  //
  // TD Event log re-use the spec of TCG2 Event log.
  // Log TcgEfiSpecIdEventStruct as the first Event. Event format is TCG_PCR_EVENT.
  //   TCG EFI Protocol Spec. Section 5.3 Event Log Header
  //   TCG PC Client PFP spec. Section 9.2 Measurement Event Entries and Log
  //
  Status = TdxDxeLogEvent (
             LogFormat,
             &SpecIdEvent,
             sizeof (SpecIdEvent),
             (UINT8 *)TcgEfiSpecIdEventStruct,
             SpecIdEvent.EventSize
             );
  //
  // record the offset at the end of 800-155 event.
  // the future 800-155 event can be inserted here.
  //
  mTdxDxeData.EventLogAreaStruct[Index].Next800155EventOffset = mTdxDxeData.EventLogAreaStruct[Index].EventLogSize;

  //
  // Tcg800155PlatformIdEvent. Event format is TCG_PCR_EVENT2
  //
  GuidHob.Guid = GetFirstGuidHob (&gTcg800155PlatformIdEventHobGuid);
  while (GuidHob.Guid != NULL) {
    TdInitNoActionEvent (&NoActionEvent, GET_GUID_HOB_DATA_SIZE (GuidHob.Guid));

    Status = TdxDxeLogEvent (
               LogFormat,
               &NoActionEvent,
               sizeof (NoActionEvent.MrIndex) + sizeof (NoActionEvent.EventType) + GetDigestListBinSize (&NoActionEvent.Digests) + sizeof (NoActionEvent.EventSize),
               GET_GUID_HOB_DATA (GuidHob.Guid),
               GET_GUID_HOB_DATA_SIZE (GuidHob.Guid)
               );

    GuidHob.Guid = GET_NEXT_HOB (GuidHob);
    GuidHob.Guid = GetNextGuidHob (&gTcg800155PlatformIdEventHobGuid, GuidHob.Guid);
  }

  //
  // 2. Create Final Log Area
  //
  Status = gBS->AllocatePages (
                  AllocateAnyPages,
                  EfiACPIMemoryNVS,
                  EFI_SIZE_TO_PAGES (PcdGet32 (PcdTcg2FinalLogAreaLen)),
                  &Lasa
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  SetMem ((VOID *)(UINTN)Lasa, PcdGet32 (PcdTcg2FinalLogAreaLen), 0xFF);

  //
  // Initialize
  //
  mTdxDxeData.FinalEventsTable[Index]                   = (VOID *)(UINTN)Lasa;
  (mTdxDxeData.FinalEventsTable[Index])->Version        = EFI_TCG2_FINAL_EVENTS_TABLE_VERSION;
  (mTdxDxeData.FinalEventsTable[Index])->NumberOfEvents = 0;

  mTdxDxeData.FinalEventLogAreaStruct[Index].EventLogFormat        = LogFormat;
  mTdxDxeData.FinalEventLogAreaStruct[Index].Lasa                  = Lasa + sizeof (EFI_CC_FINAL_EVENTS_TABLE);
  mTdxDxeData.FinalEventLogAreaStruct[Index].Laml                  = PcdGet32 (PcdTcg2FinalLogAreaLen) - sizeof (EFI_CC_FINAL_EVENTS_TABLE);
  mTdxDxeData.FinalEventLogAreaStruct[Index].EventLogSize          = 0;
  mTdxDxeData.FinalEventLogAreaStruct[Index].LastEvent             = (VOID *)(UINTN)mTdxDxeData.FinalEventLogAreaStruct[Index].Lasa;
  mTdxDxeData.FinalEventLogAreaStruct[Index].EventLogStarted       = FALSE;
  mTdxDxeData.FinalEventLogAreaStruct[Index].EventLogTruncated     = FALSE;
  mTdxDxeData.FinalEventLogAreaStruct[Index].Next800155EventOffset = 0;

  //
  // Install to configuration table for EFI_CC_EVENT_LOG_FORMAT_TCG_2
  //
  Status = gBS->InstallConfigurationTable (&gEfiCcFinalEventsTableGuid, (VOID *)mTdxDxeData.FinalEventsTable[Index]);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return Status;
}

/**
  Measure and log Separator event, and extend the measurement result into a specific PCR.

  @param[in] PCRIndex         PCR index.

  @retval EFI_SUCCESS         Operation completed successfully.
  @retval EFI_DEVICE_ERROR    The operation was unsuccessful.

**/
EFI_STATUS
TdMeasureSeparatorEvents (
  VOID
  )
{
  CC_EVENT_HDR  CcEvent;
  UINT32        EventData;
  UINT32        MrIndex;
  EFI_STATUS    Status;

  if (!TdIsEnabled ()) {
    return EFI_UNSUPPORTED;
  }

  EventData = 0;

  for (MrIndex = 1; MrIndex < 4; MrIndex++) {
    DEBUG ((DEBUG_INFO, "%a MrIndex - %x\n", __FUNCTION__, MrIndex));
    CcEvent.MrIndex   = MrIndex;
    CcEvent.EventType = EV_SEPARATOR;
    CcEvent.EventSize = (UINT32)sizeof (EventData);

    Status = TdxDxeHashLogExtendEvent (
               0,
               (UINT8 *)&EventData,
               sizeof (EventData),
               &CcEvent,
               (UINT8 *)&EventData
               );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a Measured. Error!\n"));
    }
  }

  return EFI_SUCCESS;
}

/**
  Measure and log EFI handoff tables, and extend the measurement result into PCR[1].

  @retval EFI_SUCCESS         Operation completed successfully.
  @retval EFI_DEVICE_ERROR    The operation was unsuccessful.

**/
EFI_STATUS
TdMeasureHandoffTables (
  VOID
  )
{
  EFI_STATUS                  Status;
  CC_EVENT_HDR                CcEvent;
  EFI_HANDOFF_TABLE_POINTERS  HandoffTables;
  UINTN                       ProcessorNum;
  EFI_CPU_PHYSICAL_LOCATION   *ProcessorLocBuf;

  ProcessorLocBuf = NULL;
  Status          = EFI_SUCCESS;

  if (!TdIsEnabled ()) {
    return EFI_UNSUPPORTED;
  }

  if (PcdGet8 (PcdTpmPlatformClass) == TCG_PLATFORM_TYPE_SERVER) {
    //
    // Tcg Server spec.
    // Measure each processor EFI_CPU_PHYSICAL_LOCATION with EV_TABLE_OF_DEVICES to PCR[1]
    //
    Status = GetProcessorsCpuLocation (&ProcessorLocBuf, &ProcessorNum);

    if (!EFI_ERROR (Status)) {
      CcEvent.MrIndex   = MapPcrToMrIndex (1);
      CcEvent.EventType = EV_TABLE_OF_DEVICES;
      CcEvent.EventSize = sizeof (HandoffTables);

      HandoffTables.NumberOfTables            = 1;
      HandoffTables.TableEntry[0].VendorGuid  = gEfiMpServiceProtocolGuid;
      HandoffTables.TableEntry[0].VendorTable = ProcessorLocBuf;

      Status = TdxDxeHashLogExtendEvent (
                 0,
                 (UINT8 *)(UINTN)ProcessorLocBuf,
                 sizeof (EFI_CPU_PHYSICAL_LOCATION) * ProcessorNum,
                 &CcEvent,
                 (UINT8 *)&HandoffTables
                 );

      FreePool (ProcessorLocBuf);
    }
  }

  return Status;
}

/**
  Measure and log an action string, and extend the measurement result into PCR[PCRIndex].

  @param[in] PCRIndex         PCRIndex to extend
  @param[in] String           A specific string that indicates an Action event.

  @retval EFI_SUCCESS         Operation completed successfully.
  @retval EFI_DEVICE_ERROR    The operation was unsuccessful.

**/
EFI_STATUS
TdMeasureAction (
  IN      TPM_PCRINDEX  PCRIndex,
  IN      CHAR8         *String
  )
{
  CC_EVENT_HDR  CcEvent;
  UINT32        MrIndex;

  if (!TdIsEnabled ()) {
    return EFI_UNSUPPORTED;
  }

  MrIndex = MapPcrToMrIndex (PCRIndex);
  if (MrIndex == CC_MR_INDEX_INVALID) {
    ASSERT (FALSE);
    return EFI_INVALID_PARAMETER;
  }

  CcEvent.MrIndex   = MrIndex;
  CcEvent.EventType = EV_EFI_ACTION;
  CcEvent.EventSize = (UINT32)AsciiStrLen (String);
  return TdxDxeHashLogExtendEvent (
           0,
           (UINT8 *)String,
           CcEvent.EventSize,
           &CcEvent,
           (UINT8 *)String
           );
}

/**
  Measure and log an EFI variable, and extend the measurement result into a specific PCR.

  @param[in]  PCRIndex          PCR Index.
  @param[in]  EventType         Event type.
  @param[in]  VarName           A Null-terminated string that is the name of the vendor's variable.
  @param[in]  VendorGuid        A unique identifier for the vendor.
  @param[in]  VarData           The content of the variable data.
  @param[in]  VarSize           The size of the variable data.

  @retval EFI_SUCCESS           Operation completed successfully.
  @retval EFI_OUT_OF_RESOURCES  Out of memory.
  @retval EFI_DEVICE_ERROR      The operation was unsuccessful.

**/
EFI_STATUS
TdMeasureVariable (
  IN      UINT32         PCRIndex,
  IN      TCG_EVENTTYPE  EventType,
  IN      CHAR16         *VarName,
  IN      EFI_GUID       *VendorGuid,
  IN      VOID           *VarData,
  IN      UINTN          VarSize
  )
{
  EFI_STATUS          Status;
  CC_EVENT_HDR        CcEvent;
  UINTN               VarNameLength;
  UEFI_VARIABLE_DATA  *VarLog;
  UINT32              EventSize;
  UINT32              MrIndex;

  if (!TdIsEnabled ()) {
    return EFI_UNSUPPORTED;
  }

  MrIndex = MapPcrToMrIndex (PCRIndex);
  if (MrIndex == CC_MR_INDEX_INVALID) {
    return EFI_INVALID_PARAMETER;
  }

  DEBUG ((DEBUG_INFO, "Tcg2Dxe: TdMeasureVariable (Pcr - %x, MrIndex - %x, EventType - %x, ", (UINTN)PCRIndex, (UINTN)MrIndex, (UINTN)EventType));
  DEBUG ((DEBUG_INFO, "VariableName - %s, VendorGuid - %g)\n", VarName, VendorGuid));

  VarNameLength = StrLen (VarName);
  EventSize     = (UINT32)(sizeof (*VarLog) + VarNameLength * sizeof (*VarName) + VarSize
                           - sizeof (VarLog->UnicodeName) - sizeof (VarLog->VariableData));

  VarLog = (UEFI_VARIABLE_DATA *)AllocatePool (EventSize);
  if (VarLog == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  VarLog->VariableName       = *VendorGuid;
  VarLog->UnicodeNameLength  = VarNameLength;
  VarLog->VariableDataLength = VarSize;
  CopyMem (
    VarLog->UnicodeName,
    VarName,
    VarNameLength * sizeof (*VarName)
    );
  if ((VarSize != 0) && (VarData != NULL)) {
    CopyMem (
      (CHAR16 *)VarLog->UnicodeName + VarNameLength,
      VarData,
      VarSize
      );
  }

  CcEvent.MrIndex   = MrIndex;
  CcEvent.EventType = EventType;
  CcEvent.EventSize = EventSize;

  if (EventType == EV_EFI_VARIABLE_DRIVER_CONFIG) {
    //
    // Digest is the event data (UEFI_VARIABLE_DATA)
    //
    Status = TdxDxeHashLogExtendEvent (
               0,
               (UINT8 *)VarLog,
               CcEvent.EventSize,
               &CcEvent,
               (UINT8 *)VarLog
               );
  } else {
    ASSERT (VarData != NULL);
    Status = TdxDxeHashLogExtendEvent (
               0,
               (UINT8 *)VarData,
               VarSize,
               &CcEvent,
               (UINT8 *)VarLog
               );
  }

  FreePool (VarLog);
  return Status;
}

/**
  Measure and log launch of FirmwareDebugger, and extend the measurement result into a specific PCR.

  @retval EFI_SUCCESS           Operation completed successfully.
  @retval EFI_OUT_OF_RESOURCES  Out of memory.
  @retval EFI_DEVICE_ERROR      The operation was unsuccessful.

**/
EFI_STATUS
TdMeasureLaunchOfFirmwareDebugger (
  VOID
  )
{
  CC_EVENT_HDR  CcEvent;

  if (!TdIsEnabled ()) {
    return EFI_UNSUPPORTED;
  }

  CcEvent.MrIndex   = MapPcrToMrIndex (7);
  CcEvent.EventType = EV_EFI_ACTION;
  CcEvent.EventSize = sizeof (FIRMWARE_DEBUGGER_EVENT_STRING) - 1;
  return TdxDxeHashLogExtendEvent (
           0,
           (UINT8 *)FIRMWARE_DEBUGGER_EVENT_STRING,
           sizeof (FIRMWARE_DEBUGGER_EVENT_STRING) - 1,
           &CcEvent,
           (UINT8 *)FIRMWARE_DEBUGGER_EVENT_STRING
           );
}

/**
  Install TDVF ACPI Table when ACPI Table Protocol is available.

  @param[in]  Event     Event whose notification function is being invoked
  @param[in]  Context   Pointer to the notification function's context
**/
VOID
EFIAPI
TdInstallAcpiTable (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINTN                    TableKey;
  EFI_STATUS               Status;
  EFI_ACPI_TABLE_PROTOCOL  *AcpiTable;
  UINT64                   OemTableId;

  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID **)&AcpiTable);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: AcpiTableProtocol is not installed. %r\n", __FUNCTION__, Status));
    return;
  }

  mTdxEventlogAcpiTemplate.Laml = (UINT64)PcdGet32 (PcdCcEventlogAcpiTableLaml);
  mTdxEventlogAcpiTemplate.Lasa = PcdGet64 (PcdCcEventlogAcpiTableLasa);
  CopyMem (mTdxEventlogAcpiTemplate.Header.OemId, PcdGetPtr (PcdAcpiDefaultOemId), sizeof (mTdxEventlogAcpiTemplate.Header.OemId));
  OemTableId = PcdGet64 (PcdAcpiDefaultOemTableId);
  CopyMem (&mTdxEventlogAcpiTemplate.Header.OemTableId, &OemTableId, sizeof (UINT64));
  mTdxEventlogAcpiTemplate.Header.OemRevision     = PcdGet32 (PcdAcpiDefaultOemRevision);
  mTdxEventlogAcpiTemplate.Header.CreatorId       = PcdGet32 (PcdAcpiDefaultCreatorId);
  mTdxEventlogAcpiTemplate.Header.CreatorRevision = PcdGet32 (PcdAcpiDefaultCreatorRevision);

  //
  // Construct ACPI Table
  Status = AcpiTable->InstallAcpiTable (
                        AcpiTable,
                        &mTdxEventlogAcpiTemplate,
                        mTdxEventlogAcpiTemplate.Header.Length,
                        &TableKey
                        );
  ASSERT_EFI_ERROR (Status);

  DEBUG ((DEBUG_INFO, "%a: TDEL ACPI Table installed with %r.\n", __FUNCTION__, Status));
}

EFI_STATUS
TdInstallCcMeasurement (
  VOID
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiCcMeasurementProtocolGuid,
                  &mTdProtocol,
                  NULL
                  );
  DEBUG ((DEBUG_INFO, "%a: Installed with %r\n", __FUNCTION__, Status));
  return Status;
}
