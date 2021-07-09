;------------------------------------------------------------------------------
; @file
;   32-bit initialization for Tdx
;
; Copyright (c) 2021, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
;------------------------------------------------------------------------------

BITS 32

;
; Modified:  EBP
;
; @param[in]      EBX      [6:0] CPU supported GPA width
;                          [7:7] 5 level page table support
; @param[in]      ECX      [31:0] TDINITVP - Untrusted Configuration
; @param[in]      EDX      [31:0] VCPUID
; @param[in]      ESI      [31:0] VCPU_Index
;
Init32:
    ;
    ; Save EBX in EBP because EBX will be changed in ReloadFlat32
    ;
    mov     ebp, ebx

    OneTimeCall ReloadFlat32

    ;
    ; Init Tdx
    ;
    OneTimeCall InitTdx

    OneTimeCallRet Init32
