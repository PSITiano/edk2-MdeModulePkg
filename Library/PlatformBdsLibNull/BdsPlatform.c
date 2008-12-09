/** @file
  This file include all platform action which can be customized by IBV/OEM.

Copyright (c) 2004 - 2008, Intel Corporation. <BR>
All rights reserved. This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "BdsPlatform.h"

//
// BDS Platform Functions
//
/**
  Platform Bds init. Incude the platform firmware vendor, revision
  and so crc check.

  @param  PrivateData             The EFI_BDS_ARCH_PROTOCOL_INSTANCE instance

**/
VOID
EFIAPI
PlatformBdsInit (
  IN EFI_BDS_ARCH_PROTOCOL_INSTANCE  *PrivateData
  )
{
  return;
}

/**
  Connect the predefined platform default console device. Always try to find
  and enable the vga device if have.

  @param PlatformConsole          Predfined platform default console device array.

  @retval EFI_SUCCESS             Success connect at least one ConIn and ConOut
                                  device, there must have one ConOut device is
                                  active vga device.
  @return Return the status of BdsLibConnectAllDefaultConsoles ()

**/
EFI_STATUS
PlatformBdsConnectConsole (
  IN BDS_CONSOLE_CONNECT_ENTRY   *PlatformConsole
  )
{
  return EFI_SUCCESS;
}

/**
  Connect with predeined platform connect sequence,
  the OEM/IBV can customize with their own connect sequence.
**/
VOID
PlatformBdsConnectSequence (
  VOID
  )
{
  return;
}

/**
  Load the predefined driver option, OEM/IBV can customize this
  to load their own drivers

  @param BdsDriverLists  - The header of the driver option link list.

**/
VOID
PlatformBdsGetDriverOption (
  IN OUT LIST_ENTRY              *BdsDriverLists
  )
{
  return;
}

/**
  Perform the platform diagnostic, such like test memory. OEM/IBV also
  can customize this fuction to support specific platform diagnostic.

  @param MemoryTestLevel  The memory test intensive level
  @param QuietBoot        Indicate if need to enable the quiet boot

**/
VOID
PlatformBdsDiagnostics (
  IN EXTENDMEM_COVERAGE_LEVEL    MemoryTestLevel,
  IN BOOLEAN                     QuietBoot
  )
{
  return;
}

/**
  The function will excute with as the platform policy, current policy
  is driven by boot mode. IBV/OEM can customize this code for their specific
  policy action.

  @param  PrivateData             The EFI_BDS_ARCH_PROTOCOL_INSTANCE instance
  @param  DriverOptionList        The header of the driver option link list
  @param  BootOptionList          The header of the boot option link list

**/
VOID
EFIAPI
PlatformBdsPolicyBehavior (
  IN EFI_BDS_ARCH_PROTOCOL_INSTANCE  *PrivateData,
  IN LIST_ENTRY                      *DriverOptionList,
  IN LIST_ENTRY                      *BootOptionList
  )
{
  return ;
}

/**
  Hook point after a boot attempt succeeds. We don't expect a boot option to
  return, so the UEFI 2.0 specification defines that you will default to an
  interactive mode and stop processing the BootOrder list in this case. This
  is alos a platform implementation and can be customized by IBV/OEM.

  @param  Option                  Pointer to Boot Option that succeeded to boot.

**/
VOID
EFIAPI
PlatformBdsBootSuccess (
  IN  BDS_COMMON_OPTION *Option
  )
{
  return;
}

/**
  Hook point after a boot attempt fails.

  @param  Option                  Pointer to Boot Option that failed to boot.
  @param  Status                  Status returned from failed boot.
  @param  ExitData                Exit data returned from failed boot.
  @param  ExitDataSize            Exit data size returned from failed boot.

**/
VOID
EFIAPI
PlatformBdsBootFail (
  IN  BDS_COMMON_OPTION  *Option,
  IN  EFI_STATUS         Status,
  IN  CHAR16             *ExitData,
  IN  UINTN              ExitDataSize
  )
{
  return;
}

/**
  This function is remained for IBV/OEM to do some platform action,
  if there no console device can be connected.

  @return EFI_SUCCESS      Direct return success now.

**/
EFI_STATUS
PlatformBdsNoConsoleAction (
  VOID
  )
{
  return EFI_SUCCESS;
}

/**
  This function locks platform flash that is not allowed to be updated during normal boot path.
  The flash layout is platform specific.

  @retval EFI_SUCCESS             The non-updatable flash areas.
**/
EFI_STATUS
EFIAPI
PlatformBdsLockNonUpdatableFlash (
  VOID
  )
{
  return EFI_SUCCESS;
}
