/** @file
  UDF/ECMA-167 Volume and File Structure Format Definition.

Copyright (c) 2014 Paulo Alcantara <pcacjr@zytor.com><BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#ifndef _UDF_H_
#define _UDF_H_


#define UDF_LOGICAL_SECTOR_SHIFT        11
#define UDF_LOGICAL_SECTOR_SIZE         ((UINT64)(1 << UDF_LOGICAL_SECTOR_SHIFT))
#define UDF_VRS_START_OFFSET            ((UINT64)(16 << UDF_LOGICAL_SECTOR_SHIFT))
#define UDF_STANDARD_IDENTIFIER_LENGTH  5
#define UDF_CDROM_VOLUME_IDENTIFIER     "CD001"

#define _GET_TAG_ID(_Pointer) \
  (((UDF_DESCRIPTOR_TAG *) (_Pointer))->TagIdentifier)

#define IS_PVD(_Pointer) \
  ((BOOLEAN)(_GET_TAG_ID (_Pointer) == 1))
#define IS_AVDP(_Pointer) \
  ((BOOLEAN)(_GET_TAG_ID (_Pointer) == 2))

typedef struct {
  UINT8 StandardIdentifier[UDF_STANDARD_IDENTIFIER_LENGTH];
} UDF_STANDARD_IDENTIFIER;

enum {
  BEA_IDENTIFIER,
  VSD_IDENTIFIER,
  TEA_IDENTIFIER,
  NR_STANDARD_IDENTIFIERS,
};

#pragma pack(1)

typedef struct {
  UINT16  TagIdentifier;
  UINT16  DescriptorVersion;
  UINT8   TagChecksum;
  UINT8   Reserved;
  UINT16  TagSerialNumber;
  UINT16  DescriptorCRC;
  UINT16  DescriptorCRCLength;
  UINT32  TagLocation;
} UDF_DESCRIPTOR_TAG;

typedef struct {
  UINT32  ExtentLength;
  UINT32  ExtentLocation;
} UDF_EXTENT_AD;

typedef struct {
  UINT8  StructureType;
  UINT8  StandardIdentifier[UDF_STANDARD_IDENTIFIER_LENGTH];
  UINT8  StructureVersion;
  UINT8  Reserved;
  UINT8  StructureData[2040];
} UDF_VOLUME_DESCRIPTOR;

typedef struct {
  UDF_DESCRIPTOR_TAG  DescriptorTag;
  UDF_EXTENT_AD       MainVolumeDescriptorSequenceExtent;
  UDF_EXTENT_AD       ReserveVolumeDescriptorSequenceExtent;
  UINT8               Reserved[480];
} UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER;

#pragma pack()

#endif // _UDF_H_
