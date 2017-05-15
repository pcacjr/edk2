/** @file
  Miscellaneous routines specific to Proxy for HttpDxe driver.

Copyright (c) 2017, Paulo Alcantara. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/
#ifndef __EFI_PROXY_SUPPORT_H__
#define __EFI_PROXY_SUPPORT_H__

EFI_STATUS
ParseProxyUrl (
  IN   CHAR8			*Url,
  OUT  CHAR8			**Scheme,
  OUT  CHAR8			**HostName,
  OUT  UINT16			*RemotePort
  );

#endif // __EFI_PROXY_SUPPORT_H__
