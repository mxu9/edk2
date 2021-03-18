#ifndef __APIC_LIB_INTERNAL_H__
#define __APIC_LIB_INTERNAL_H__

BOOLEAN mUseTdvmcall = FALSE;
BOOLEAN mMsrAccessInitialized = FALSE;
BOOLEAN mTdGuest = FALSE;

BOOLEAN
EFIAPI
InitializeMsrAccess(VOID);

#endif
