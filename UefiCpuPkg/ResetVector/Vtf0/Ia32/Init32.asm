;------------------------------------------------------------------------------
; @file
;   32-bit initialization code.
; Copyright (c) 2021, Intel Corporation. All rights reserved.<BR>
; SPDX-License-Identifier: BSD-2-Clause-Patent
;
;------------------------------------------------------------------------------

BITS 32

Init32:
    nop
    OneTimeCallRet Init32
