/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 :miv */
/*
 *=BEGIN SONGBIRD GPL
 *
 * This file is part of the Songbird web player.
 *
 * Copyright(c) 2005-2010 POTI, Inc.
 * http://www.songbirdnest.com
 *
 * This file may be licensed under the terms of of the
 * GNU General Public License Version 2 (the ``GPL'').
 *
 * Software distributed under the License is distributed
 * on an ``AS IS'' basis, WITHOUT WARRANTY OF ANY KIND, either
 * express or implied. See the GPL for the specific language
 * governing rights and limitations.
 *
 * You should have received a copy of the GPL along with this
 * program. If not, go to http://www.gnu.org/licenses/gpl.html
 * or write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *=END SONGBIRD GPL
 */

//------------------------------------------------------------------------------
//
// Windows GUID instantiations.
//
//   Some Windows GUIDs (e.g., CLSID_VdsLoader) are not provided in any
// libraries from Microsoft and must be instantiated by client applications.
// This file instantiates these GUIDs for Songbird.
//
//------------------------------------------------------------------------------

/**
 * \file  WindowsGUIDs.cpp
 * \brief Windows GUID Instantiation Source.
 */

#ifdef NG_CROSS_COMP
# include <windows.h>
typedef enum  { 
  VDS_IA_UNKNOWN  = 0,
  VDS_IA_FCFS     = 1,
  VDS_IA_FCPH     = 2,
  VDS_IA_FCPH3    = 3,
  VDS_IA_MAC      = 4,
  VDS_IA_SCSI     = 5
} VDS_INTERCONNECT_ADDRESS_TYPE;

typedef enum {
  VDSBusTypeUnknown = 0x00,
  VDSBusTypeScsi = 0x01,
  VDSBusTypeAtapi = 0x02,
  VDSBusTypeAta = 0x03,
  VDSBusType1394 = 0x04,
  VDSBusTypeSsa = 0x05,
  VDSBusTypeFibre = 0x06,
  VDSBusTypeUsb = 0x07,
  VDSBusTypeRAID = 0x08,
  VDSBusTypeiScsi = 0x09,
  VDSBusTypeMaxReserved = 0x7F
}VDS_STORAGE_BUS_TYPE;

typedef enum {
  VDSStorageIdCodeSetReserved = 0,
  VDSStorageIdCodeSetBinary = 1,
  VDSStorageIdCodeSetAscii = 2
} VDS_STORAGE_IDENTIFIER_CODE_SET;

typedef enum {
  VDSStorageIdTypeVendorSpecific = 0,
  VDSStorageIdTypeVendorId = 1,
  VDSStorageIdTypeEUI64 = 2,
  VDSStorageIdTypeFCPHName = 3,
  VDSStorageIdTypeSCSINameString = 8
} VDS_STORAGE_IDENTIFIER_TYPE;

typedef struct _VDS_STORAGE_IDENTIFIER {
  VDS_STORAGE_IDENTIFIER_CODE_SET m_CodeSet;
  VDS_STORAGE_IDENTIFIER_TYPE m_Type;
  ULONG m_cbIdentifier;
  BYTE* m_rgbIdentifier;
} VDS_STORAGE_IDENTIFIER;

typedef struct _VDS_STORAGE_DEVICE_ID_DESCRIPTOR {
  ULONG                  m_version;
  ULONG                  m_cIdentifiers;
  VDS_STORAGE_IDENTIFIER *m_rgIdentifiers;
} VDS_STORAGE_DEVICE_ID_DESCRIPTOR;
#endif

// Import defs for instatiating GUIDs.
#include <initguid.h>

// Instantiate GUIDs.
#include <vds.h>
