#ifndef ACTIVATIONSOURCE_HH
#define ACTIVATIONSOURCE_HH

//==============================================================================
// G4CARE
// @file    ActivationSource.hh
// @brief   Particle source reading primary data from ROOT RDF files
// @details Reads primary particle data (position, direction, energy,
//   particle type, time) from ROOT RDataFrame sources. Supports
//   multi-threaded operation by splitting rows across threads.
//   Global data (file name, tree name, total rows) is shared via
//   static members and prepared once by PrepareGlobalData().
//   Each thread opens its own RDFReader on its assigned row range.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "Source.hh"
#include "RDFReader.hh"
#include <memory>
#include <string>

class ActivationSource : public Source {
public:
    /// @brief Constructor
    /// @param sourceName Name identifier for this source
    ActivationSource(const std::string& sourceName);

    /// @brief Destructor
    virtual ~ActivationSource();

    /// @brief Generate primary particles for the given event
    ///
    /// Reads the next row from the thread's RDFReader and creates
    /// a primary vertex with the corresponding particle.
    /// @param event Pointer to the G4Event to fill
    virtual void GeneratePrimaries(G4Event* event) override;

    /// @brief Prepare global data shared across all threads
    ///
    /// Must be called once from the master thread before worker
    /// threads start. Reads file name, tree name, and total row count
    /// from ConfigManager using the given data alias.
    /// @param dataAlias Configuration key prefix for the RDF data source
    static void PrepareGlobalData(const std::string& dataAlias);

private:
    /// @brief Ensure per-thread RDFReader is initialized
    void EnsureThreadData();

    /// @brief Total number of rows in the RDF source (static, shared)
    static size_t fgTotalRows;

    /// @brief ROOT file name (static, shared)
    static std::string fgFileName;

    /// @brief TTree name in the ROOT file (static, shared)
    static std::string fgTreeName;

    /// @brief Number of worker threads (static, shared)
    static int fgNumThreads;

    /// @brief Name identifier for this source instance
    std::string fSourceName;

    /// @brief Configuration key prefix for data lookup
    std::string fDataAlias;

    /// @brief Operation mode string from configuration
    std::string fMode;

    /// @brief If true, reset event time to zero
    bool fResetTime;

    /// @brief If true, loop over rows when exhausted
    bool fLoop;

    /// @brief Per-thread data: RDF reader and row range
    struct ThreadData {
        std::unique_ptr<RDFReader> reader;  ///< Thread-local RDF reader
        size_t currentRow;                   ///< Current row index
        size_t startRow;                     ///< First row assigned to this thread
        size_t endRow;                       ///< Last row assigned to this thread
        bool valid;                          ///< Whether the reader is valid
    };
    std::unique_ptr<ThreadData> fThreadData;
};

#endif