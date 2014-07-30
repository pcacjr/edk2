/** @file
  UDF filesystem driver.

Copyright (c) 2014 Paulo Alcantara <pcacjr@zytor.com><BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "Udf.h"

STATIC
CHAR16 *
TrimString (
  CHAR16       *String
  )
{
  CHAR16       *TempString;

  for ( ; (*String) && (*String == L' '); String++) {
    ;
  }

  TempString = String + StrLen (String) - 1;
  while ((TempString >= String) && (*TempString == L' ')) {
    TempString--;
  }

  *(TempString + 1) = L'\0';

  return String;
}

STATIC
CHAR16 *
ExcludeTrailingBackslashes (
  CHAR16                       *String
  )
{
  CHAR16                       *TempString;

  switch (*(String + 1)) {
    case L'\\':
      break;
    case L'\0':
    default:
      String++;
      goto Exit;
  }

  TempString = String;
  while ((*TempString) && (*TempString == L'\\')) {
    TempString++;
  }

  if (TempString - 1 > String) {
    StrnCpy (String + 1, TempString, StrLen (TempString) + 1);
  }

  String++;

Exit:
  return String;
}

CHAR16 *
MangleFileName (
  CHAR16           *FileName
  )
{
  CHAR16           *FileNameSavedPointer;
  CHAR16           *TempFileName;
  UINTN            BackSlashesNo;

  if ((!FileName) || ((FileName) && (!*FileName))) {
    FileName = NULL;
    goto Exit;
  }

  FileName = TrimString (FileName);
  if (!*FileName) {
    goto Exit;
  }

  if ((StrLen (FileName) > 1) && (FileName[StrLen (FileName) - 1] == L'\\')) {
    FileName[StrLen (FileName) - 1] = L'\0';
  }

  FileNameSavedPointer = FileName;

  if (FileName[0] == L'.') {
    if (FileName[1] == L'.') {
      if (!FileName[2]) {
	goto Exit;
      } else {
	FileName += 2;
      }
    } else if (!FileName[1]) {
      goto Exit;
    }
  }

  while (*FileName) {
    if (*FileName == L'\\') {
      FileName = ExcludeTrailingBackslashes (FileName);
    } else if (*FileName == L'.') {
      switch (*(FileName + 1)) {
	case L'\0':
	  *FileName = L'\0';
	  break;
	case L'\\':
	  TempFileName = FileName + 1;
	  TempFileName = ExcludeTrailingBackslashes (TempFileName);
	  StrnCpy (FileName, TempFileName, StrLen (TempFileName) + 1);
	  break;
	case '.':
	  if ((*(FileName - 1) != L'\\') && ((*(FileName + 2) != L'\\') ||
					     (*(FileName + 2) != L'\0'))) {
	    FileName++;
	    continue;
	  }

	  BackSlashesNo = 0;
	  TempFileName = FileName - 1;
	  while (TempFileName >= FileNameSavedPointer) {
	    if (*TempFileName == L'\\') {
	      if (++BackSlashesNo == 2) {
		break;
	      }
	    }

	    TempFileName--;
	  }

	  TempFileName++;

	  if ((*TempFileName == L'.') && (*(TempFileName + 1) == L'.')) {
	    FileName += 2;
	  } else {
	    if (*(FileName + 2)) {
	      StrnCpy (TempFileName, FileName + 3, StrLen (FileName + 3) + 1);
	      if (*(TempFileName - 1) == L'\\') {
		FileName = TempFileName;
		ExcludeTrailingBackslashes (TempFileName - 1);
		TempFileName = FileName;
	      }
	    } else {
	      *TempFileName = L'\0';
	    }

	    FileName = TempFileName;
	  }

	  break;
	default:
	  FileName++;
      }
    } else {
      FileName++;
    }
  }

  FileName = FileNameSavedPointer;
  if ((StrLen (FileName) > 1) && (FileName [StrLen (FileName) - 1] == L'\\')) {
    FileName [StrLen (FileName) - 1] = L'\0';
  }

Exit:
  return FileName;
}
