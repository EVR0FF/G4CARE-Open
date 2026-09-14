//==============================================================================
// G4CARE
// @file    ChemSpeciesRegistry.hh
// @brief   Type alias for a TypedRegistry<std::string> used to register
//          chemical species (radicals, molecules) by name.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef CHEM_SPECIES_REGISTRY_HH
#define CHEM_SPECIES_REGISTRY_HH

#include "TypedRegistry.hh"
#include <string>

/// Chemical species (radical, molecule) — stored by name.
using ChemSpeciesRegistry = TypedRegistry<std::string>;

#endif
