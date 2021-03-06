/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 :miv */
/*
//
// BEGIN SONGBIRD GPL
//
// This file is part of the Songbird web player.
//
// Copyright(c) 2005-2008 POTI, Inc.
// http://songbirdnest.com
//
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the "GPL").
//
// Software distributed under the License is distributed
// on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either
// express or implied. See the GPL for the specific language
// governing rights and limitations.
//
// You should have received a copy of the GPL along with this
// program. If not, go to http://www.gnu.org/licenses/gpl.html
// or write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
// END SONGBIRD GPL
//
*/

/** 
 * \file  sbPrompterModule.cpp
 * \brief Songbird Prompter Module Component Factory and Main Entry Point.
 */

// Self imports.
#include "sbPrompter.h"

// Mozilla imports.
#include <mozilla/ModuleUtils.h>

// Construct the sbPrompter object and call its Init method.
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(sbPrompter, Init);
NS_DEFINE_NAMED_CID(SONGBIRD_PROMPTER_CID);

static const mozilla::Module::CIDEntry kPrompterCIDs[] = {
  { &kSONGBIRD_PROMPTER_CID, false, NULL, sbPrompterConstructor },
  { NULL }
};

static const mozilla::Module::ContractIDEntry kPrompterContracts[] = {
  { SONGBIRD_PROMPTER_CONTRACTID, &kSONGBIRD_PROMPTER_CID },
  { NULL }
};

static const mozilla::Module::CategoryEntry kPrompterCategories[] = {
  { NULL }
};

static const mozilla::Module kPrompterModule = {
  mozilla::Module::kVersion,
  kPrompterCIDs,
  kPrompterContracts,
  kPrompterCategories
};

NSMODULE_DEFN(sbPrompter) = &kPrompterModule;
