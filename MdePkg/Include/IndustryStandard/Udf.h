/** @file
  OSTA Universal Disk Format (UDF) definitions.

  Copyright (C) 2014-2017 Paulo Alcantara <pcacjr@zytor.com>

  This program and the accompanying materials are licensed and made available
  under the terms and conditions of the BSD License which accompanies this
  distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS, WITHOUT
  WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/

#ifndef __UDF_H__
#define __UDF_H__

#define UDF_BEA_IDENTIFIER   "BEA01"
#define UDF_NSR2_IDENTIFIER  "NSR02"
#define UDF_NSR3_IDENTIFIER  "NSR03"
#define UDF_TEA_IDENTIFIER   "TEA01"

#define UDF_LOGICAL_SECTOR_SHIFT  11
#define UDF_LOGICAL_SECTOR_SIZE   ((UINT64)(1ULL << UDF_LOGICAL_SECTOR_SHIFT))
#define UDF_VRS_START_OFFSET      ((UINT64)(16ULL << UDF_LOGICAL_SECTOR_SHIFT))

#define _GET_TAG_ID(_Pointer) \
  (((UDF_DESCRIPTOR_TAG *)(_Pointer))->TagIdentifier)

#define IS_PD(_Pointer) \
  ((BOOLEAN)(_GET_TAG_ID (_Pointer) == 5))
#define IS_LVD(_Pointer) \
  ((BOOLEAN)(_GET_TAG_ID (_Pointer) == 6))
#define IS_TD(_Pointer) \
  ((BOOLEAN)(_GET_TAG_ID (_Pointer) == 8))

#define IS_AVDP(_Pointer) \
  ((BOOLEAN)(_GET_TAG_ID (_Pointer) == 2))

#define LV_UDF_REVISION(_Lv) \
  *(UINT16 *)(UINTN)(_Lv)->DomainIdentifier.IdentifierSuffix

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
  UDF_DESCRIPTOR_TAG  DescriptorTag;
  UDF_EXTENT_AD       MainVolumeDescriptorSequenceExtent;
  UDF_EXTENT_AD       ReserveVolumeDescriptorSequenceExtent;
  UINT8               Reserved[480];
} UDF_ANCHOR_VOLUME_DESCRIPTOR_POINTER;

typedef struct {
  UINT8           CharacterSetType;
  UINT8           CharacterSetInfo[63];
} UDF_CHAR_SPEC;

typedef struct {
  UINT8           Flags;
  UINT8           Identifier[23];
  UINT8           IdentifierSuffix[8];
} UDF_ENTITY_ID;

typedef struct {
  UINT32        LogicalBlockNumber;
  UINT16        PartitionReferenceNumber;
} UDF_LB_ADDR;

typedef struct {
  UINT32                           ExtentLength;
  UDF_LB_ADDR                      ExtentLocation;
  UINT8                            ImplementationUse[6];
} UDF_LONG_ALLOCATION_DESCRIPTOR;

typedef struct {
  UDF_DESCRIPTOR_TAG              DescriptorTag;
  UINT32                          VolumeDescriptorSequenceNumber;
  UDF_CHAR_SPEC                   DescriptorCharacterSet;
  UINT8                           LogicalVolumeIdentifier[128];
  UINT32                          LogicalBlockSize;
  UDF_ENTITY_ID                   DomainIdentifier;
  UDF_LONG_ALLOCATION_DESCRIPTOR  LogicalVolumeContentsUse;
  UINT32                          MapTableLength;
  UINT32                          NumberOfPartitionMaps;
  UDF_ENTITY_ID                   ImplementationIdentifier;
  UINT8                           ImplementationUse[128];
  UDF_EXTENT_AD                   IntegritySequenceExtent;
  UINT8                           PartitionMaps[6];
} UDF_LOGICAL_VOLUME_DESCRIPTOR;

typedef struct {
  UDF_DESCRIPTOR_TAG         DescriptorTag;
  UINT32                     VolumeDescriptorSequenceNumber;
  UINT16                     PartitionFlags;
  UINT16                     PartitionNumber;
  UDF_ENTITY_ID              PartitionContents;
  UINT8                      PartitionContentsUse[128];
  UINT32                     AccessType;
  UINT32                     PartitionStartingLocation;
  UINT32                     PartitionLength;
  UDF_ENTITY_ID              ImplementationIdentifier;
  UINT8                      ImplementationUse[128];
  UINT8                      Reserved[156];
} UDF_PARTITION_DESCRIPTOR;

#pragma pack()

#endif
