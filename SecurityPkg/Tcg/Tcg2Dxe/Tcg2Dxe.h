/** @file
  This module implements Tcg2 Protocol.

Copyright (c) 2015 - 2019, Intel Corporation. All rights reserved.<BR>
(C) Copyright 2016 Hewlett Packard Enterprise Development LP<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef TCG2_DXE_H_
#define TCG2_DXE_H_

#include <PiDxe.h>
#include <Protocol/MpService.h>
#include <Protocol/Tcg2Protocol.h>

#define TCG_EVENT_LOG_AREA_COUNT_MAX  2

typedef struct {
  EFI_TCG2_EVENT_LOG_FORMAT    EventLogFormat;
  EFI_PHYSICAL_ADDRESS         Lasa;
  UINT64                       Laml;
  UINTN                        EventLogSize;
  UINT8                        *LastEvent;
  BOOLEAN                      EventLogStarted;
  BOOLEAN                      EventLogTruncated;
  UINTN                        Next800155EventOffset;
} TCG_EVENT_LOG_AREA_STRUCT;

typedef
EFI_STATUS
(*TD_TCG_MEASURE_ACTION)(
  IN UINT32  Index,
  IN CHAR8   *String
  );

typedef
EFI_STATUS
(*TD_TCG_MEASURE_SEPARATORS)(
  VOID
  );

typedef
EFI_STATUS
(*TD_TCG_MEASURE_HANDOFF_TABLES)(
  VOID
  );

typedef
EFI_STATUS
(*TD_TCG_MEASURE_VARIABLE)(
  IN      UINT32         PCRIndex,
  IN      TCG_EVENTTYPE  EventType,
  IN      CHAR16         *VarName,
  IN      EFI_GUID       *VendorGuid,
  IN      VOID           *VarData,
  IN      UINTN          VarSize
  );

typedef
EFI_STATUS
(*TD_TCG_MEASURE_LAUNCHOFF_FIRMWARE_DEBUGGER)(
  VOID  
  );

UINT32
GetDigestListBinSize (
  VOID  *DigestListBin
  );

EFI_STATUS
TcgCommLogEvent (
  IN OUT  TCG_EVENT_LOG_AREA_STRUCT  *EventLogAreaStruct,
  IN      VOID                       *NewEventHdr,
  IN      UINT32                     NewEventHdrSize,
  IN      UINT8                      *NewEventData,
  IN      UINT32                     NewEventSize
  );

/**
  This function get size of TCG_EfiSpecIDEventStruct.

  @param[in]  TcgEfiSpecIdEventStruct     A pointer to TCG_EfiSpecIDEventStruct.
**/
UINTN
GetTcgEfiSpecIdEventStructSize (
  IN TCG_EfiSpecIDEventStruct  *TcgEfiSpecIdEventStruct
  );

/**
  Measure PE image into TPM log based on the authenticode image hashing in
  PE/COFF Specification 8.0 Appendix A.

  Caution: This function may receive untrusted input.
  PE/COFF image is external input, so this function will validate its data structure
  within this image buffer before use.

  Notes: PE/COFF image is checked by BasePeCoffLib PeCoffLoaderGetImageInfo().

  @param[in]  PCRIndex       TPM PCR index
  @param[in]  ImageAddress   Start address of image buffer.
  @param[in]  ImageSize      Image size
  @param[out] DigestList     Digest list of this image.

  @retval EFI_SUCCESS            Successfully measure image.
  @retval EFI_OUT_OF_RESOURCES   No enough resource to measure image.
  @retval other error value
**/
EFI_STATUS
MeasurePeImageAndExtend (
  IN  UINT32                PCRIndex,
  IN  EFI_PHYSICAL_ADDRESS  ImageAddress,
  IN  UINTN                 ImageSize,
  OUT TPML_DIGEST_VALUES    *DigestList
  );

/**
  This function dump event log.

  @param[in]  EventLogFormat     The type of the event log for which the information is requested.
  @param[in]  EventLogLocation   A pointer to the memory address of the event log.
  @param[in]  EventLogLastEntry  If the Event Log contains more than one entry, this is a pointer to the
                                 address of the start of the last entry in the event log in memory.
  @param[in]  FinalEventsTable   A pointer to the memory address of the final event table.
**/
VOID
DumpEventLog (
  IN EFI_TCG2_EVENT_LOG_FORMAT    EventLogFormat,
  IN EFI_PHYSICAL_ADDRESS         EventLogLocation,
  IN EFI_PHYSICAL_ADDRESS         EventLogLastEntry,
  IN EFI_TCG2_FINAL_EVENTS_TABLE  *FinalEventsTable
  );

EFI_STATUS
GetProcessorsCpuLocation (
  OUT  EFI_CPU_PHYSICAL_LOCATION  **LocationBuf,
  OUT  UINTN                      *Num
  );

/**
  Measure and log launch of FirmwareDebugger, and extend the measurement result into a specific PCR.

  @retval EFI_SUCCESS           Operation completed successfully.
  @retval EFI_OUT_OF_RESOURCES  Out of memory.
  @retval EFI_DEVICE_ERROR      The operation was unsuccessful.

**/
EFI_STATUS
TdMeasureLaunchOfFirmwareDebugger (
  VOID
  );

/**
  Measure and log EFI handoff tables, and extend the measurement result into PCR[1].

  @retval EFI_SUCCESS         Operation completed successfully.
  @retval EFI_DEVICE_ERROR    The operation was unsuccessful.

**/
EFI_STATUS
TdMeasureHandoffTables (
  VOID
  );

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
  );

/**
  Measure and log an action string, and extend the measurement result into PCR[PCRIndex].

  @param[in] PCRIndex         PCRIndex to extend
  @param[in] String           A specific string that indicates an Action event.

  @retval EFI_SUCCESS         Operation completed successfully.
  @retval EFI_DEVICE_ERROR    The operation was unsuccessful.

**/
EFI_STATUS
TdMeasureAction (
  IN      UINT32  Index,
  IN      CHAR8   *String
  );

/**
  Measure and log Separator event, and extend the measurement result into a specific PCR.

  @param[in] PCRIndex         PCR index.

  @retval EFI_SUCCESS         Operation completed successfully.
  @retval EFI_DEVICE_ERROR    The operation was unsuccessful.

**/
EFI_STATUS
TdMeasureSeparatorEvents (
  VOID
  );

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
  );


EFI_STATUS
Tcg2DxeInitializeTdx (
  VOID
  );

EFI_STATUS
TdSetupEventLog (
  VOID
  );

EFI_STATUS
TdSyncEvent (
  VOID
  );

EFI_STATUS
TdInstallCcMeasurement (
  VOID
  );

#endif
