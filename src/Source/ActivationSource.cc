//==============================================================================
//
// G4CARE
//
// @file    ActivationSource.cc
// @brief   Data-driven activation source — generates ions from pre-loaded data tables.
//
// @details
//   Reads ion production data from a ROOT TTree (DATA block) and generates
//   primary ions (G4IonTable) per event.  Supports sequential and random
//   row selection modes, optional data looping, and per-thread data
//   partitioning for multi-threaded runs (Geant4 MT).
//
//   Column layout in data file (12+ columns):
//     [0..2] reserved, [3] Z, [4] A, [5] excitation energy,
//     [6] kinetic energy (MeV), [7] x, [8] y, [9] z (mm),
//     [10] time (ns), [11] weight.
//
//   Configuration keys read from SOURCE.<name>.*:
//     data, mode, reset_time, loop.
//
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
//
// @date    2026-07-15
// @version 0.9.0
//
// @copyright Copyright (c) 2026 G4CARE Developers
// @license SPDX-License-Identifier: Apache-2.0 License (see LICENSE)
//
//==============================================================================

#include "ActivationSource.hh"
#include "ConfigManager.hh"
#include "G4PrimaryVertex.hh"
#include "G4PrimaryParticle.hh"
#include "G4IonTable.hh"
#include "G4Threading.hh"
#include "G4SystemOfUnits.hh"
#include "Randomize.hh"
#include "G4ios.hh"
#include "G4RandomDirection.hh"
#include "BeamAnalysis.hh"
#include "UnifiedSource.hh"

size_t ActivationSource::fgTotalRows = 0;
std::string ActivationSource::fgFileName = "";
std::string ActivationSource::fgTreeName = "ion_production";


/// @brief Load global ion production data shared across all threads.
///
/// Opens the ROOT file specified in DATA.<alias>.file with the tree
/// from DATA.<alias>.tree (default "ion_production").  Counts the total
/// number of rows and stores the result in static members fgTotalRows,
/// fgFileName, fgTreeName for use by per-thread reader instances.
///
/// @param dataAlias Alias name referencing a DATA block subsection.
void ActivationSource::PrepareGlobalData(const std::string& dataAlias) {

    auto* cfg = ConfigManager::Instance();
    std::string dataPrefix = "DATA." + dataAlias;
    fgFileName = cfg->GetString(dataPrefix + ".file", "");
    fgTreeName = cfg->GetString(dataPrefix + ".tree", "ion_production");

    if (fgFileName.empty()) {
        G4cerr << "ActivationSource: No file specified for data alias '" << dataAlias << "'" << G4endl;
        return;
    }

    auto tmpReader = std::make_unique<RDFReader>();
    if (!tmpReader->LoadROOTFile({fgFileName}, fgTreeName)) {
        G4cerr << "ActivationSource: Failed to load file " << fgFileName << G4endl;
        return;
    }
    fgTotalRows = tmpReader->GetNumRows();

    G4cout << "ActivationSource: Global data prepared for alias '" << dataAlias
           << "': file=" << fgFileName << ", total rows=" << fgTotalRows
           << G4endl;
}

ActivationSource::ActivationSource(const std::string& sourceName)
    : fSourceName(sourceName)
{
    fName = sourceName;

    auto* cfg = ConfigManager::Instance();

    fDataAlias = cfg->GetString(fSourceName + ".data", "");
    if (fDataAlias.empty()) {
        G4cerr << "ActivationSource: No data alias provided for " << fSourceName << G4endl;
        return;
    }

    fMode      = cfg->GetString(fSourceName + ".mode", "sequential");
    fResetTime = cfg->GetBool(fSourceName + ".reset_time", true);
    fLoop      = cfg->GetBool(fSourceName + ".loop", true);
}

ActivationSource::~ActivationSource() = default;

/// @brief Initialise or verify the per-thread data reader and row range.
///
/// Called on first use in each worker thread (or master).  Divides the
/// global ion table (fgTotalRows) evenly among threads and opens a
/// dedicated RDFReader for the thread's row range.  Sets the ThreadData
/// valid flag only after a successful ROOT file load.
void ActivationSource::EnsureThreadData() {
    if (fThreadData && fThreadData->valid) return;

    if (fgTotalRows == 0) {
        G4cerr << "ActivationSource: Global data not initialized" << G4endl;
        return;
    }

    if (!fThreadData) {
        fThreadData = std::make_unique<ThreadData>();
    }

    int threadId = G4Threading::G4GetThreadId();
    if (threadId < 0) threadId = 0;

    int numThreads = G4Threading::GetNumberOfRunningWorkerThreads();
    if (numThreads <= 0) numThreads = 1;

    size_t rowsPerThread = fgTotalRows / numThreads;
    fThreadData->startRow = threadId * rowsPerThread;
    fThreadData->endRow = (threadId == numThreads - 1) ? fgTotalRows : fThreadData->startRow + rowsPerThread;
    fThreadData->currentRow = fThreadData->startRow;

    fThreadData->reader = std::make_unique<RDFReader>();
    if (!fThreadData->reader->LoadROOTFile({fgFileName}, fgTreeName)) {
        G4cerr << "ActivationSource: Failed to load file " << fgFileName << " in thread " << threadId << G4endl;
        fThreadData->valid = false;
        return;
    }

    fThreadData->valid = true;
    G4cout << "ActivationSource: Thread " << threadId << " assigned rows ["
           << fThreadData->startRow << ", " << fThreadData->endRow << ")" << G4endl;
}

/// @brief Generate one primary ion vertex from the ion production data table.
///
/// Selects a row index depending on fMode:
/// - "sequential" — increment fThreadData->currentRow each event, optionally
///   loop back to startRow when the range is exhausted (fLoop controls this).
/// - "random" — uniform random draw within the thread's [startRow, endRow).
///
/// Column mapping (12+ column table expected):
///   col[3]=Z, col[4]=A, col[5]=excitation, col[6]=kinetic energy (MeV),
///   col[7]=x, col[8]=y, col[9]=z (mm), col[10]=time (ns), col[11]=weight.
///
/// Creates a G4IonTable ion, sets kinetic energy and isotropic direction
/// (if energy > 0), attaches a G4PrimaryVertex and registers it with
/// BeamAnalysis for the "primary" NTuple.
///
/// @param event Current Geant4 event to receive the generated ion.
void ActivationSource::GeneratePrimaries(G4Event* event) {
    if (fgTotalRows == 0) return;
    if (fDataAlias.empty()) return;

    EnsureThreadData();
    if (!fThreadData || !fThreadData->valid) return;

    size_t rowIndex;
    if (fMode == "random") {
        size_t range = fThreadData->endRow - fThreadData->startRow;
        if (range == 0) return;
        rowIndex = fThreadData->startRow + static_cast<size_t>(G4UniformRand() * range);
    } else {
        rowIndex = fThreadData->currentRow++;
        if (rowIndex >= fThreadData->endRow) {
            if (fLoop) {
                fThreadData->currentRow = fThreadData->startRow;
                rowIndex = fThreadData->currentRow++;
            } else {
                return;
            }
        }
    }

    auto rowDataOpt = fThreadData->reader->GetRow(rowIndex);
    if (!rowDataOpt) {
        G4cerr << "ActivationSource: Failed to get row " << rowIndex << G4endl;
        return;
    }
    const auto& row = rowDataOpt.value();

    const int idxZ = 3;
    const int idxA = 4;
    const int idxExc = 5;
    const int idxEnergy = 6;
    const int idxX = 7;
    const int idxY = 8;
    const int idxZpos = 9;
    const int idxTime = 10;
    const int idxWeight = 11;

    if (row.size() < 12) {
        G4cerr << "ActivationSource: row has only " << row.size() << " columns, expected at least 12" << G4endl;
        return;
    }

    int Z = static_cast<int>(std::round(row[idxZ]));
    int A = static_cast<int>(std::round(row[idxA]));
    double excitation = row[idxExc];
    double energy = row[idxEnergy] * CLHEP::MeV;
    double x = row[idxX] * CLHEP::mm;
    double y = row[idxY] * CLHEP::mm;
    double z = row[idxZpos] * CLHEP::mm;
    double time = fResetTime ? 0.0 : row[idxTime] * CLHEP::ns;
    double weight = row[idxWeight];

    G4ParticleDefinition* ion = G4IonTable::GetIonTable()->GetIon(Z, A, excitation);
    if (!ion) {
        G4cerr << "ActivationSource: Failed to get ion Z=" << Z << " A=" << A << G4endl;
        return;
    }

    G4PrimaryVertex* vertex = new G4PrimaryVertex(x, y, z, time);
    G4PrimaryParticle* particle = new G4PrimaryParticle(ion);
    particle->SetKineticEnergy(energy);
    if (energy > 0) {
        particle->SetMomentumDirection(G4RandomDirection());
    }
    vertex->SetPrimary(particle);
    vertex->SetWeight(weight);

    BeamAnalysis::Instance()->Fill("primary", UnifiedSource(vertex, particle));

    event->AddPrimaryVertex(vertex);
}