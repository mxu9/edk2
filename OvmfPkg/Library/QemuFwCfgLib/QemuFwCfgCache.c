/** @file

  QemuFwCfg cached feature related functions.

  Copyright (c) 224, Intel Corporation. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/QemuFwCfgLib.h>

#include "QemuFwCfgLibInternal.h"

/**
  Check if fw_cfg cache is ready.

  @retval    TRUE   Cache is ready
  @retval    FALSE  Cache is not ready
**/
BOOLEAN
QemuFwCfgCacheEnable (
  VOID
  )
{
  EFI_HOB_GUID_TYPE  *GuidHob;

  GuidHob = GetFirstGuidHob (&gOvmfFwCfgInfoHobGuid);
  if (GuidHob == NULL) {
    return FALSE;
  }

  UINT16  HobLength = GET_HOB_LENGTH (GuidHob);

  if (HobLength <= sizeof (EFI_HOB_GENERIC_HEADER) + sizeof (FW_CFG_SELECT_INFO)) {
    return FALSE;
  } else {
    return TRUE;
  }
}

/**
  Get the pointer to the cached fw_cfg item.

  @param[in] Item   The fw_cfg item to be retrieved.

  @retval    FW_CFG_CACHED_ITEM   Pointer to the cached fw_cfg item.
  @retval    NULL                The fw_cfg item is not cached.
**/
FW_CFG_CACHED_ITEM *
QemuFwCfgItemCached (
  IN FIRMWARE_CONFIG_ITEM  Item
  )
{
  UINT16             HobSize;
  UINT16             Offset;
  BOOLEAN            Cached;
  FW_CFG_CACHED_ITEM  *CachedItem;
  EFI_HOB_GUID_TYPE  *GuidHob;
  UINT32             CachedItemSize;

  if (!QemuFwCfgCacheEnable ()) {
    return FALSE;
  }

  GuidHob = GetFirstGuidHob (&gOvmfFwCfgInfoHobGuid);
  ASSERT (GuidHob != NULL);

  HobSize = GET_GUID_HOB_DATA_SIZE (GuidHob);
  Offset  = sizeof (EFI_HOB_GUID_TYPE) + sizeof (FW_CFG_SELECT_INFO);
  ASSERT (Offset < HobSize);

  CachedItem = QemuFwCfgCacheFirstItem();

  Cached = FALSE;
  while (Offset < HobSize) {
    if (CachedItem->FwCfgItem == Item) {
      Cached = TRUE;
      break;
    }

    CachedItemSize = CachedItem->DataSize + sizeof (FW_CFG_CACHED_ITEM);
    Offset += CachedItemSize;
    CachedItem = (FW_CFG_CACHED_ITEM *)((UINT8 *)CachedItem + CachedItemSize);
  }

  return Cached ? CachedItem : NULL;
}

/**
  Clear the FW_CFG_SELECT_INFO.
**/
VOID
QemuFwCfgCacheResetSelectInfo(
  VOID
  )
{
  FW_CFG_SELECT_INFO *SelectInfo;

  SelectInfo = QemuFwCfgCacheGetSelectInfo();
  if(SelectInfo != NULL) {
    SelectInfo->FwCfgItem = 0;
    SelectInfo->Offset = 0;
    SelectInfo->Reading = FALSE;
  }
}

/**
  Check if reading from FwCfgCache is ongoing.

  @retval   TRUE  Reading from FwCfgCache is ongoing.
  @retval   FALSE Reading from FwCfgCache is not ongoing.
**/
BOOLEAN
QemuFwCfgCacheReading (
  VOID
)
{
  BOOLEAN Reading;
  FW_CFG_SELECT_INFO *SelectInfo;

  Reading = FALSE;
  SelectInfo = QemuFwCfgCacheGetSelectInfo();
  if(SelectInfo != NULL) {
    Reading = SelectInfo->Reading;
  }

  return Reading;
}

BOOLEAN
QemuFwCfgCacheSelectItem (
  IN  FIRMWARE_CONFIG_ITEM  Item
  )
{
  FW_CFG_SELECT_INFO *SelectInfo;

  // Walk thru cached fw_items to see if Item is cached.
  if(QemuFwCfgItemCached(Item) == NULL) {
    return FALSE;
  }

  SelectInfo = QemuFwCfgCacheGetSelectInfo();
  ASSERT(SelectInfo);

  SelectInfo->FwCfgItem = Item;
  SelectInfo->Offset = 0;
  SelectInfo->Reading = TRUE;  

  return TRUE;
}

/**
  Get the first cached item.

  @retval    FW_CFG_CACHED_ITEM  The first cached item.
  @retval    NULL               There is no cached item.
**/
FW_CFG_CACHED_ITEM *
QemuFwCfgCacheFirstItem (
  VOID
)
{
  EFI_HOB_GUID_TYPE  *GuidHob;
  UINT16             HobSize;
  UINT16             Offset;

  if (!QemuFwCfgCacheEnable ()) {
    return NULL;
  }

  GuidHob = GetFirstGuidHob (&gOvmfFwCfgInfoHobGuid);
  ASSERT (GuidHob != NULL);

  HobSize = GET_GUID_HOB_DATA_SIZE (GuidHob);
  Offset  = sizeof (EFI_HOB_GUID_TYPE) + sizeof (FW_CFG_SELECT_INFO);
  ASSERT (Offset < HobSize);

  return (FW_CFG_CACHED_ITEM *)((UINT8 *)GuidHob + Offset);
}

/**
  Read the fw_cfg data from Cache.

  @param[in]  Size    Data size to be read
  @param[in]  Buffer  Pointer to the buffer to which data is written

  @retval  EFI_SUCCESS   - Successfully
  @retval  Others        - As the error code indicates
**/
EFI_STATUS
QemuFwCfgCacheReadBytes (
  IN     UINTN  Size,
  IN OUT VOID   *Buffer
  )
{
  FW_CFG_SELECT_INFO *SelectInfo;
  FW_CFG_CACHED_ITEM *CachedItem;
  UINT32            ReadSize;

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  SelectInfo = QemuFwCfgCacheGetSelectInfo();
  if(SelectInfo == NULL) {
    return RETURN_NOT_FOUND;
  }

  if(!SelectInfo->Reading) {
    return RETURN_NOT_READY;
  }

  CachedItem = QemuFwCfgItemCached(SelectInfo->FwCfgItem);
  if(CachedItem == NULL) {
    return RETURN_NOT_FOUND;
  }

  if(CachedItem->DataSize - SelectInfo->Offset > Size) {
    ReadSize = Size;
  } else {
    ReadSize = CachedItem->DataSize - SelectInfo->Offset;
  }

  CopyMem(Buffer, (UINT8 *)CachedItem + sizeof(FW_CFG_CACHED_ITEM) + SelectInfo->Offset, ReadSize);
  SelectInfo->Offset += ReadSize;

  DEBUG ((DEBUG_INFO, "%a: found in FwCfg Cache\n", __func__));
  return RETURN_SUCCESS;
}

RETURN_STATUS
QemuFwCfgItemInCacheList (
  IN   CONST CHAR8           *Name,
  OUT  FIRMWARE_CONFIG_ITEM  *Item,
  OUT  UINTN                 *Size
  )
{
  UINT16             HobSize;
  UINT16             Offset;
  FW_CFG_CACHED_ITEM  *CachedItem;
  EFI_HOB_GUID_TYPE  *GuidHob;
  UINT32             CachedItemSize;

  CachedItem = QemuFwCfgCacheFirstItem();
  if (CachedItem == NULL) {
    return RETURN_NOT_FOUND;
  }

  GuidHob = GetFirstGuidHob (&gOvmfFwCfgInfoHobGuid);
  ASSERT (GuidHob != NULL);

  HobSize = GET_GUID_HOB_DATA_SIZE (GuidHob);
  Offset  = sizeof (EFI_HOB_GUID_TYPE) + sizeof (FW_CFG_SELECT_INFO);
  ASSERT (Offset < HobSize);

  while (Offset < HobSize) {
    if(AsciiStrCmp (Name, CachedItem->FileName) == 0) {
      *Item = CachedItem->FwCfgItem;
      *Size = CachedItem->DataSize;
      return RETURN_SUCCESS;
    }

    CachedItemSize = CachedItem->DataSize + sizeof (FW_CFG_CACHED_ITEM);
    Offset += CachedItemSize;
    CachedItem = (FW_CFG_CACHED_ITEM *)((UINT8 *)CachedItem + CachedItemSize);
  }

  return RETURN_NOT_FOUND;
}
