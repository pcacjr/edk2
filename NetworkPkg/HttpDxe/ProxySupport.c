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

#include "HttpDriver.h"

EFI_STATUS
ParseProxyUrl (
  IN   CHAR8			*Url,
  OUT  CHAR8			**Scheme,
  OUT  CHAR8			**HostName,
  OUT  UINT16			*RemotePort
  )
{
  EFI_STATUS            Status;
  VOID              	*UrlParser;
  UINTN                 Size;

  UrlParser = NULL;

  Status = HttpParseUrl (Url, (UINT32)AsciiStrLen (Url), FALSE, &UrlParser);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: failed to parse proxy URL: %r\n", __FUNCTION__,
            Status));
    return Status;
  }

  *Scheme = NULL;

  Status = HttpUrlGetScheme (Url, UrlParser, Scheme);
  if (EFI_ERROR (Status)) {
    if (Status != EFI_NOT_FOUND) {
      DEBUG ((EFI_D_ERROR, "%a: failed to parse proxy scheme: %r\n",
              __FUNCTION__, Status));
      goto Error;
    }

    Size = AsciiStrSize ("http");
    *Scheme = AllocatePool (Size);
    if (*Scheme == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      goto Error;
    }
    CopyMem (*Scheme, "http", Size);
  }

  DEBUG ((EFI_D_INFO, "%a: proxy scheme: %a\n", __FUNCTION__, *Scheme));

  *HostName = NULL;

  Status = HttpUrlGetHostName (Url, UrlParser, HostName);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: failed to parse proxy hostname: %r\n",
            __FUNCTION__, Status));
    FreePool (*Scheme);
    goto Error;
  }

  DEBUG ((EFI_D_INFO, "%a: proxy hostname: %a\n", __FUNCTION__, *HostName));

  Status = HttpUrlGetPort (Url, UrlParser, RemotePort);
  if (EFI_ERROR (Status)) {
    if (AsciiStrCmp (*Scheme, "https") == 0) {
      *RemotePort = HTTPS_DEFAULT_PORT;
    } else {
      *RemotePort = HTTP_DEFAULT_PORT;
    }

    Status = EFI_SUCCESS;
  }

  DEBUG ((EFI_D_INFO, "%a: proxy port: %d\n", __FUNCTION__, *RemotePort));

Error:
  if (UrlParser != NULL) {
    HttpUrlFreeParser (UrlParser);
  }

  return Status;
}
