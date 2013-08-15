/* vim: set sw=2 :miv */
/*
 * BEGIN NIGHTINGALE GPL
 * 
 * This file is part of the Nightingale Media Player.
 *
 * Copyright(c) 2013
 * http://getnightingale.com
 * 
 * This file may be licensed under the terms of of the
 * GNU General Public License Version 2 (the "GPL").
 * 
 * Software distributed under the License is distributed 
 * on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either 
 * express or implied. See the GPL for the specific language 
 * governing rights and limitations.
 *
 * You should have received a copy of the GPL along with this 
 * program. If not, go to http://www.gnu.org/licenses/gpl.html
 * or write to the Free Software Foundation, Inc., 
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 * 
 * END NIGHTINGALE GPL
 */

#include <nsCOMPtr.h>
#include <nsServiceManagerUtils.h>
#include <nsICategoryManager.h>
#include <mozilla/ModuleUtils.h>

#include "sbMockDeviceFirmwareHandler.h"

#define SB_MOCKDEVICEFIRMWAREHADLER_CID                                       \
{ 0x5b100d2b, 0x486a, 0x4f3a,                                                 \
  { 0x9b, 0x60, 0x14, 0x64, 0xb, 0xcb, 0xa6, 0x8e } }
#define SB_MOCKDEVICEFIRMWAREHADLER_CLASSNAME                                 \
  "Songbird Device Firmware Tester - Mock Device Firmware Handler"
#define SB_MOCKDEVICEFIRMWAREHADLER_CONTRACTID                                \
  "@songbirdnest.com/Songbird/Device/Firmware/Handler/MockDevice;1"

NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(sbMockDeviceFirmwareHandler, Init);

NS_DEFINE_NAMED_CID(SB_MOCKDEVICEFIRMWAREHADLER_CID);

static const mozilla::Module::CIDEntry kMockDeviceFirmwareHandlerCIDs[] = {
  { &kSB_MOCKDEVICEFIRMWAREHADLER_CID, false, NULL, sbMockDeviceFirmwareHandlerConstructor },
  { NULL }
};

static const mozilla::Module::ContractIDEntry kMockDeviceFirmwareHandlerContracts[] = {
  { SB_MOCKDEVICEFIRMWAREHADLER_CONTRACTID, &kSB_MOCKDEVICEFIRMWAREHADLER_CID },
  { NULL }
};

static const mozilla::Module::CategoryEntry kMockDeviceFirmwareHandlerCategories[] = {
  { NULL }
};

static const mozilla::Module kMockDeviceFirmwareHandlerModule = {
  mozilla::Module::kVersion,
  kMockDeviceFirmwareHandlerCIDs,
  kMockDeviceFirmwareHandlerContracts,
  kMockDeviceFirmwareHandlerCategories
};

NSMODULE_DEFN(sbDeviceFirmwareTesterComponents) = &kMockDeviceFirmwareHandlerModule;

