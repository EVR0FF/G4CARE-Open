#ifndef ADAPTIVE_GRID_HH
#define ADAPTIVE_GRID_HH

//==============================================================================
// G4CARE
// @file    AdaptiveGrid.hh
// @brief   Adaptive octree grid with event-driven refinement and coarsening
// @details Implements an adaptive octree spatial grid (IGrid interface)
//   that dynamically splits and merges cells based on event statistics.
//   Supports three refinement criteria: event count, relative uncertainty
//   (variance-based), and absolute energy deposit. Merger cooldown
//   prevents oscillating split/merge cycles. MT-merge support via
//   MergeFrom() for worker→master data aggregation. Template parameter T
//   is the per-cell data type (must provide sum_wE, sum_w2E2, sum_wNIEL,
//   sum_w2NIEL2, sum_w, count, edep(), niel(), RelativeUncertainty()).
//   Exports to VTK unstructured grid with Dose_Gy, Flux_per_mm3,
//   Edep_MeV, and NIEL_MeV scalar fields.
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#include "IGrid.hh"
#include "G4ThreeVector.hh"
#include "G4VPhysicalVolume.hh"
#include "G4Navigator.hh"
#include "G4LogicalVolume.hh"
#include "G4Material.hh"
#include "G4SystemOfUnits.hh"
#include "G4ios.hh"

#include <vector>
#include <array>
#include <fstream>
#include <algorithm>
#include <limits>
#include <mutex>
#include <functional>

template <typename T>
class AdaptiveGrid : public IGrid {

private:
    struct OctreeNode {
        G4ThreeVector center;
        G4double halfSize;
        size_t depth;
        size_t minEventsToMerge;
        int eventCount;
        int eventsSinceSplit;  ///< Event counter since last split (for cooldown)
        T data;
        std::array<int, 8> children;
        bool isLeaf;           ///< true for leaf nodes
        size_t leafIndex;      ///< Position in leafIndices (valid only if isLeaf)

        OctreeNode() : halfSize(0), depth(0), eventCount(0),
                       eventsSinceSplit(0),
                       children{-1,-1,-1,-1,-1,-1,-1,-1},
                       isLeaf(true), leafIndex(0) {}
    };

    std::vector<OctreeNode> nodes;
    std::vector<size_t> leafIndices;

    size_t maxDepth;
    size_t minEventsToSplit;
    size_t minEventsToMerge;

    /// @brief Minimum events after split before another split is allowed
    size_t splitCooldown;

    /// @brief Minimum events after split before merging back is allowed
    size_t mergeCooldown;

    /// @brief Minimum edep for splitting a cell (0 = disabled)
    G4double minEdepForSplit;

    /// @brief Relative uncertainty threshold for variance-based splitting (1.0 = disabled)
    G4double maxRelUncertainty;

    /// @brief Emergency total node limit (0 = unlimited)
    size_t maxTotalNodes;

    G4ThreeVector globalMin;
    G4ThreeVector globalMax;
    G4double globalHalfSize;

    /// @brief Create a new octree node
    int CreateNode(const G4ThreeVector& center, G4double halfSize, size_t depth);

    /// @brief Split a leaf node into 8 children
    void SplitNode(size_t nodeIndex);

    /// @brief Attempt to merge a parent node's children back
    void TryMergeNode(size_t nodeIndex);

    /// @brief Determine which child octant contains a point
    int GetChildIdx(const G4ThreeVector& pos, const OctreeNode& node) const;

    /// @brief Rebuild the leaf index array from the node tree
    void UpdateLeafIndices();

public:
    /// @brief Constructor with refinement parameters
    /// @param maxDepth           Maximum octree depth
    /// @param minEventsToSplit   Minimum events before splitting a cell
    /// @param minEventsToMerge   Maximum total events in children to trigger merge
    /// @param splitCooldown      Events after split before re-splitting
    /// @param mergeCooldown      Events after split before merging back
    /// @param minEdepForSplit    Minimum edep for split (0 = disabled)
    /// @param maxRelUncertainty  Relative uncertainty threshold (1.0 = disabled)
    /// @param maxTotalNodes      Emergency node limit (0 = unlimited)
     AdaptiveGrid(size_t maxDepth = 10, size_t minEventsToSplit = 100, size_t minEventsToMerge = 20,
                  size_t splitCooldown = 10, size_t mergeCooldown = 50,
                  G4double minEdepForSplit = 0.0, G4double maxRelUncertainty = 1.0,
                  size_t maxTotalNodes = 0);
    ~AdaptiveGrid() override;

    // IGrid interface
    /// @brief Check all non-leaf nodes and attempt to merge their children
    void CheckAndMergeAll();

    void Initialize(const G4ThreeVector& minCorner, const G4ThreeVector& maxCorner) override;
    bool GetCellIndex(const G4ThreeVector& pos, size_t& index) const override;
    G4ThreeVector GetCellCenter(size_t index) const override;
    G4double GetCellSize(size_t index) const override;
    G4double GetCellVolume(size_t index) const override;
    size_t GetNumberOfCells() const override;

    /// @brief Get total number of nodes (leaves + internal)
    size_t GetTotalNodes() const { return nodes.size(); }

    /// @brief Get the minimum events threshold for merging
    size_t GetMinEventsToMerge() const { return minEventsToMerge; }

    std::string GetType() const override { return "AdaptiveOctree"; }

    void ExportToVTK(const std::string& filename, G4VPhysicalVolume* world,
                     const std::string& targetVolumeName = "") const override;

    /// @brief Get mutable data for a leaf cell by index
    T* GetData(size_t index);

    /// @brief Get const data for a leaf cell by index
    const T* GetData(size_t index) const;

    /// @brief Register an event at a given position (may trigger split)
    void AddEvent(const G4ThreeVector& pos);

    /// @brief Force-split a leaf cell by index
    void RefineCell(size_t index);

    /// @brief Variance-based refinement: split cells exceeding relative uncertainty
    void CheckAndRefineVariance();

    /// @brief Edep-based refinement: split cells exceeding energy deposit threshold
    void CheckAndRefineEdep();

    /// @brief Reset all cell data and event counters
    void ResetData();

    /// @brief Get the leaf index array
    const std::vector<size_t>& GetLeafIndices() const { return leafIndices; }

    /// @brief Get the node array (for debugging)
    const std::vector<OctreeNode>& GetNodes() const { return nodes; }

    /// @brief Merge data from a leaf cell (by center position) into this grid
    ///
    /// Used for MT-merging of worker data into the master grid.
    /// @param center  Cell center position
    /// @param srcData Data to merge
    void MergeLeafData(const G4ThreeVector& center, const T& srcData);

    /// @brief Merge all leaf data from another grid (MT worker→master merge)
    ///
    /// Replicates the other grid's structure via recursive splitting,
    /// then merges accumulated data into this grid.
    /// @param other Source grid to merge from
    void MergeFrom(const AdaptiveGrid<T>& other);

    // Parameter setters
    void SetSplitCooldown(size_t v) { splitCooldown = v; }
    void SetMergeCooldown(size_t v) { mergeCooldown = v; }
    void SetMinEdepForSplit(G4double v) { minEdepForSplit = v; }
    void SetMaxRelUncertainty(G4double v) { maxRelUncertainty = v; }
};

// ======================================================================
// Implementation
// ======================================================================

template <typename T>
AdaptiveGrid<T>::AdaptiveGrid(size_t maxD, size_t minEv, size_t minMerge,
                              size_t splitCool, size_t mergeCool,
                              G4double minEdep, G4double maxUnc,
                              size_t maxNodes)
    : maxDepth(maxD), minEventsToSplit(minEv), minEventsToMerge(minMerge),
      splitCooldown(splitCool), mergeCooldown(mergeCool),
      minEdepForSplit(minEdep), maxRelUncertainty(maxUnc),
      maxTotalNodes(maxNodes) {}


template <typename T>
AdaptiveGrid<T>::~AdaptiveGrid() {}

template <typename T>
void AdaptiveGrid<T>::Initialize(const G4ThreeVector& minC, const G4ThreeVector& maxC) {
    globalMin = minC;
    globalMax = maxC;
    G4ThreeVector center = (minC + maxC) * 0.5;
    G4ThreeVector extent = maxC - minC;
    globalHalfSize = std::max({extent.x(), extent.y(), extent.z()}) * 0.5;

    nodes.clear();
    leafIndices.clear();

    int rootIdx = CreateNode(center, globalHalfSize, 0);
    nodes[rootIdx].isLeaf = true;
    nodes[rootIdx].leafIndex = 0;
    leafIndices.push_back(rootIdx);
}

template <typename T>
int AdaptiveGrid<T>::CreateNode(const G4ThreeVector& center, G4double halfSize, size_t depth) {
    OctreeNode node;
    node.center = center;
    node.halfSize = halfSize;
    node.depth = depth;
    node.isLeaf = true;   // initially a leaf
    nodes.push_back(node);
    return nodes.size() - 1;
}

template <typename T>
void AdaptiveGrid<T>::SplitNode(size_t nodeIndex) {
    if (nodeIndex >= nodes.size()) return;
    if (!nodes[nodeIndex].isLeaf) return;
    if (nodes[nodeIndex].depth >= maxDepth) return;

    // Emergency node limit check
    if (maxTotalNodes > 0 && nodes.size() + 8 > maxTotalNodes) {
        static int warnCount = 0;
        if (warnCount++ < 5) {
            G4cout << "AdaptiveGrid: Max total nodes reached (" << maxTotalNodes
                   << "). Stopping refinement." << G4endl;
        }
        return;
    }

    // Copy data BEFORE calling CreateNode, since push_back reallocates the vector.
    // Do not hold a reference to the vector element — it gets invalidated by reallocation.
    G4ThreeVector center = nodes[nodeIndex].center;
    G4double halfSize = nodes[nodeIndex].halfSize;
    size_t depth = nodes[nodeIndex].depth;

    nodes[nodeIndex].isLeaf = false;   // no longer a leaf
    nodes[nodeIndex].eventsSinceSplit = 0;  // reset cooldown on split

    G4double newHalf = halfSize * 0.5;
    size_t newDepth = depth + 1;

    for (int i = 0; i < 8; ++i) {
        G4ThreeVector offset(
            (i & 1) ? newHalf : -newHalf,
            (i & 2) ? newHalf : -newHalf,
            (i & 4) ? newHalf : -newHalf
        );
        int childIdx = CreateNode(center + offset, newHalf, newDepth);
        nodes[childIdx].eventsSinceSplit = 0;  // new node — fresh cooldown
        nodes[nodeIndex].children[i] = childIdx;  // access by index, not reference
    }

    // Rebuild leafIndices completely (could be optimized, kept simple)
    UpdateLeafIndices();
}

template <typename T>
void AdaptiveGrid<T>::TryMergeNode(size_t nodeIndex) {
    if (nodeIndex >= nodes.size()) return;
    OctreeNode& parent = nodes[nodeIndex];

    if (parent.isLeaf) return;

    // Merge cooldown check: all children must have accumulated enough events after split
    if (mergeCooldown > 0) {
        for (int i = 0; i < 8; ++i) {
            int cIdx = parent.children[i];
            if (cIdx >= 0 && cIdx < static_cast<int>(nodes.size())) {
                if (nodes[cIdx].eventsSinceSplit < static_cast<int>(mergeCooldown)) {
                    return;  // not yet time to merge
                }
            }
        }
    }

    std::vector<int> childIndices;
    childIndices.reserve(8);
    int totalEvents = 0;
    bool allLeaves = true;

    for (int i = 0; i < 8; ++i) {
        int cIdx = parent.children[i];
        if (cIdx == -1 || cIdx >= static_cast<int>(nodes.size())) {
            allLeaves = false; break;
        }
        if (!nodes[cIdx].isLeaf) {
            allLeaves = false; break;
        }
        childIndices.push_back(cIdx);
        totalEvents += nodes[cIdx].eventCount;
    }

    if (allLeaves && totalEvents < static_cast<int>(minEventsToMerge)) {
        parent.data = T();
        for (int idx : childIndices) {
            parent.data.sum_wE     += nodes[idx].data.sum_wE;
            parent.data.sum_w2E2   += nodes[idx].data.sum_w2E2;
            parent.data.sum_wNIEL   += nodes[idx].data.sum_wNIEL;
            parent.data.sum_w2NIEL2 += nodes[idx].data.sum_w2NIEL2;
            parent.data.sum_w      += nodes[idx].data.sum_w;
            parent.data.count      += nodes[idx].data.count;
        }
        parent.eventCount = totalEvents;
        parent.eventsSinceSplit = 0;

        for (int i = 0; i < 8; ++i) parent.children[i] = -1;
        parent.isLeaf = true;

        UpdateLeafIndices();
    }
}

template <typename T>
void AdaptiveGrid<T>::UpdateLeafIndices() {
    leafIndices.clear();
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].isLeaf) {
            nodes[i].leafIndex = leafIndices.size();
            leafIndices.push_back(i);
        } else {
            nodes[i].leafIndex = std::numeric_limits<size_t>::max();
        }
    }
}

template <typename T>
int AdaptiveGrid<T>::GetChildIdx(const G4ThreeVector& pos, const OctreeNode& node) const {
    int idx = 0;
    if (pos.x() > node.center.x()) idx |= 1;
    if (pos.y() > node.center.y()) idx |= 2;
    if (pos.z() > node.center.z()) idx |= 4;
    return idx;
}

template <typename T>
bool AdaptiveGrid<T>::GetCellIndex(const G4ThreeVector& pos, size_t& index) const {
    if (nodes.empty()) return false;

    int currentIdx = 0;
    while (true) {
        const OctreeNode& node = nodes[currentIdx];

        if (std::abs(pos.x() - node.center.x()) > node.halfSize ||
            std::abs(pos.y() - node.center.y()) > node.halfSize ||
            std::abs(pos.z() - node.center.z()) > node.halfSize) {
            return false;
        }

        if (node.isLeaf) {
            index = node.leafIndex;   // fast access
            return true;
        }

        int childIdx = GetChildIdx(pos, node);
        currentIdx = node.children[childIdx];
    }
}

template <typename T>
T* AdaptiveGrid<T>::GetData(size_t index) {
    if (index >= leafIndices.size()) return nullptr;
    return &nodes[leafIndices[index]].data;
}

template <typename T>
const T* AdaptiveGrid<T>::GetData(size_t index) const {
    if (index >= leafIndices.size()) return nullptr;
    return &nodes[leafIndices[index]].data;
}

template <typename T>
G4ThreeVector AdaptiveGrid<T>::GetCellCenter(size_t index) const {
    if (index >= leafIndices.size()) return G4ThreeVector();
    return nodes[leafIndices[index]].center;
}

template <typename T>
G4double AdaptiveGrid<T>::GetCellSize(size_t index) const {
    if (index >= leafIndices.size()) return 0.0;
    return nodes[leafIndices[index]].halfSize * 2.0;
}

template <typename T>
G4double AdaptiveGrid<T>::GetCellVolume(size_t index) const {
    if (index >= leafIndices.size()) return 0.0;
    G4double side = nodes[leafIndices[index]].halfSize * 2.0;
    return side * side * side;
}

template <typename T>
size_t AdaptiveGrid<T>::GetNumberOfCells() const {
    return leafIndices.size();
}

template <typename T>
void AdaptiveGrid<T>::RefineCell(size_t index) {
    if (index >= leafIndices.size()) return;
    SplitNode(leafIndices[index]);
}

template <typename T>
void AdaptiveGrid<T>::AddEvent(const G4ThreeVector& pos) {
    size_t idx;
    if (!GetCellIndex(pos, idx)) return;
    if (idx >= leafIndices.size()) return;

    size_t nodeIdx = leafIndices[idx];
    // Do not hold a reference — SplitNode may reallocate the vector
    nodes[nodeIdx].eventCount++;
    nodes[nodeIdx].eventsSinceSplit++;

    // Split cooldown check: wait for minimum events after previous split
    int events = nodes[nodeIdx].eventsSinceSplit;
    bool cooldownPassed = (splitCooldown == 0) || (events >= static_cast<int>(splitCooldown));

    if (cooldownPassed && nodes[nodeIdx].eventCount >= static_cast<int>(minEventsToSplit)) {
        SplitNode(nodeIdx);
    }
}

// ---------------------- VTK Export ----------------------
template <typename T>
void AdaptiveGrid<T>::ExportToVTK(const std::string& filename, G4VPhysicalVolume* world,
                                  const std::string& targetVolumeName) const {
    if (!world || leafIndices.empty()) {
        G4cerr << "AdaptiveGrid: Cannot export VTK without valid World volume or data." << G4endl;
        return;
    }

    std::ofstream file(filename);
    if (!file.is_open()) {
        G4cerr << "AdaptiveGrid: Cannot open file " << filename << " for writing." << G4endl;
        return;
    }

    auto* navigator = new G4Navigator();
    navigator->SetWorldVolume(world);

    // Filter leaves: if targetVolumeName is set, keep only cells whose center
    // falls inside the specified physical volume
    std::vector<size_t> filteredLeaves;
    if (!targetVolumeName.empty()) {
        filteredLeaves.reserve(leafIndices.size());
        for (size_t leafIdx : leafIndices) {
            if (leafIdx >= nodes.size()) continue;
            G4VPhysicalVolume* vol = navigator->LocateGlobalPointAndSetup(nodes[leafIdx].center);
            if (vol && vol->GetLogicalVolume()->GetName() == targetVolumeName + "_LV") {
                filteredLeaves.push_back(leafIdx);
            }
        }
        G4cout << "AdaptiveGrid: VTK export filtered " << leafIndices.size()
               << " → " << filteredLeaves.size() << " cells (targetVolumeName='"
               << targetVolumeName << "')" << G4endl;
    } else {
        filteredLeaves = leafIndices;
    }

    size_t numCells = filteredLeaves.size();
    if (numCells == 0) {
        G4cerr << "AdaptiveGrid: No cells to export after filtering." << G4endl;
        file.close();
        delete navigator;
        return;
    }

    G4cout << "AdaptiveGrid: Exporting " << numCells
           << " cells to VTK file: " << filename << G4endl;

    file << "# vtk DataFile Version 3.0\n";
    file << "Adaptive Scoring Grid\n";
    file << "ASCII\n";
    file << "DATASET UNSTRUCTURED_GRID\n";

    size_t numPoints = numCells * 8;
    file << "POINTS " << numPoints << " double\n";

    std::vector<G4double> doseData(numCells);
    std::vector<G4double> fluxData(numCells);
    std::vector<G4double> edepData(numCells);
    std::vector<G4double> nielData(numCells);

    for (size_t i = 0; i < numCells; ++i) {
        size_t nodeIdx = filteredLeaves[i];
        const OctreeNode& node = nodes[nodeIdx];
        G4double halfSize = node.halfSize;
        G4ThreeVector center = node.center;

        G4Material* mat = nullptr;
        G4double density_g_cm3 = 1.0;
        G4VPhysicalVolume* vol = navigator->LocateGlobalPointAndSetup(center);
        if (vol) {
            mat = vol->GetLogicalVolume()->GetMaterial();
            if (mat) {
                density_g_cm3 = mat->GetDensity() / (g/cm3);
            }
        }

        G4double density_kg_m3 = density_g_cm3 * 1000.0;

        G4double edep_MeV = node.data.edep();
        int count = node.data.count;
        G4double volume_mm3 = (2.0 * halfSize) * (2.0 * halfSize) * (2.0 * halfSize);

        G4double flux = (volume_mm3 > 0) ? static_cast<G4double>(count) / volume_mm3 : 0.0;
        G4double edep_J = edep_MeV * 1.60218e-13;
        G4double volume_m3 = volume_mm3 * 1e-9;
        G4double mass_kg = volume_m3 * density_kg_m3;
        G4double dose_Gy = (mass_kg > 0) ? edep_J / mass_kg : 0.0;

        doseData[i] = dose_Gy;
        fluxData[i] = flux;
        edepData[i] = edep_MeV;
        nielData[i] = node.data.niel();  // NIEL in MeV

        G4double offsets[8][3] = {
            {-1, -1, -1}, { 1, -1, -1}, { 1,  1, -1}, {-1,  1, -1},
            {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}
        };

        for (int v = 0; v < 8; ++v) {
            file << center.x() + offsets[v][0] * halfSize << " "
                 << center.y() + offsets[v][1] * halfSize << " "
                 << center.z() + offsets[v][2] * halfSize << "\n";
        }
    }

    file << "CELLS " << numCells << " " << (numCells * 9) << "\n";
    for (size_t i = 0; i < numCells; ++i) {
        size_t baseIdx = i * 8;
        file << "8 "
             << baseIdx << " " << baseIdx+1 << " " << baseIdx+2 << " " << baseIdx+3 << " "
             << baseIdx+4 << " " << baseIdx+5 << " " << baseIdx+6 << " " << baseIdx+7 << "\n";
    }

    file << "CELL_TYPES " << numCells << "\n";
    for (size_t i = 0; i < numCells; ++i) {
        file << "12\n";
    }

    file << "CELL_DATA " << numCells << "\n";
    file << "SCALARS Dose_Gy double 1\nLOOKUP_TABLE default\n";
    for (auto val : doseData) file << val << "\n";
    file << "SCALARS Flux_per_mm3 double 1\nLOOKUP_TABLE default\n";
    for (auto val : fluxData) file << val << "\n";
    file << "SCALARS Edep_MeV double 1\nLOOKUP_TABLE default\n";
    for (auto val : edepData) file << val << "\n";
    file << "SCALARS NIEL_MeV double 1\nLOOKUP_TABLE default\n";
    for (auto val : nielData) file << val << "\n";

    file.close();
    delete navigator;
    G4cout << "AdaptiveGrid: VTK export completed. Filtered " << numCells << " cells." << G4endl;
}

template <typename T>
void AdaptiveGrid<T>::ResetData() {
    for (size_t idx : leafIndices) {
        if (idx < nodes.size()) {
            nodes[idx].data = T();
            nodes[idx].eventCount = 0;
            nodes[idx].eventsSinceSplit = 0;
        }
    }
    G4cout << "AdaptiveGrid: Data reset. Cells: " << leafIndices.size() << G4endl;
}

template <typename T>
void AdaptiveGrid<T>::CheckAndMergeAll() {
    for (int i = nodes.size() - 1; i >= 0; --i) {
        if (!nodes[i].isLeaf) {
            TryMergeNode(i);
        }
    }
}

// -----------------------------------------------------------------
//  Variance-based refinement (relative uncertainty)
// -----------------------------------------------------------------
template <typename T>
void AdaptiveGrid<T>::CheckAndRefineVariance() {
    if (maxRelUncertainty >= 1.0) return;

    const auto leaves = leafIndices;
    std::vector<size_t> toRefine;
    toRefine.reserve(leaves.size());

    for (size_t leafIdx : leaves) {
        if (leafIdx >= nodes.size()) continue;

        size_t depth = nodes[leafIdx].depth;
        int cooldown = nodes[leafIdx].eventsSinceSplit;
        int count = nodes[leafIdx].data.count;
        G4double unc = nodes[leafIdx].data.RelativeUncertainty();

        if (depth >= maxDepth) continue;
        if (splitCooldown > 0 && cooldown < static_cast<int>(splitCooldown)) continue;

        if (unc > maxRelUncertainty && count >= static_cast<int>(minEventsToSplit)) {
            toRefine.push_back(leafIdx);
        }
    }

    if (!toRefine.empty()) {
        G4cout << "AdaptiveGrid: Variance-based refinement of "
               << toRefine.size() << " cells." << G4endl;
        for (size_t idx : toRefine) {
            SplitNode(idx);
        }
    }
}

// -----------------------------------------------------------------
//  Energy-deposit–based refinement
// -----------------------------------------------------------------
template <typename T>
void AdaptiveGrid<T>::CheckAndRefineEdep() {
    if (minEdepForSplit <= 0.0) return;

    const auto leaves = leafIndices;
    std::vector<size_t> toRefine;
    toRefine.reserve(leaves.size());

    for (size_t leafIdx : leaves) {
        if (leafIdx >= nodes.size()) continue;

        size_t depth = nodes[leafIdx].depth;
        int cooldown = nodes[leafIdx].eventsSinceSplit;
        G4double edep = nodes[leafIdx].data.edep();
        int count = nodes[leafIdx].data.count;

        if (depth >= maxDepth) continue;
        if (splitCooldown > 0 && cooldown < static_cast<int>(splitCooldown)) continue;

        if (edep >= minEdepForSplit && count >= static_cast<int>(minEventsToSplit)) {
            toRefine.push_back(leafIdx);
        }
    }

    if (!toRefine.empty()) {
        G4cout << "AdaptiveGrid: Edep-based refinement of "
               << toRefine.size() << " cells." << G4endl;
        for (size_t idx : toRefine) {
            SplitNode(idx);
        }
    }
}

// -----------------------------------------------------------------
//  Data merge from another grid (MT worker→master merge)
// -----------------------------------------------------------------
template <typename T>
void AdaptiveGrid<T>::MergeLeafData(const G4ThreeVector& center, const T& srcData) {
    size_t idx;
    if (GetCellIndex(center, idx)) {
        T* dst = GetData(idx);
        if (dst) {
            // Accumulate weighted statistics (correct for MT weights)
            dst->sum_wE     += srcData.sum_wE;
            dst->sum_w2E2   += srcData.sum_w2E2;
            dst->sum_wNIEL   += srcData.sum_wNIEL;
            dst->sum_w2NIEL2 += srcData.sum_w2NIEL2;
            dst->sum_w      += srcData.sum_w;
            dst->count      += srcData.count;
        }
    } else {
        static int warnCount = 0;
        if (warnCount++ < 3) {
            G4cout << "AdaptiveGrid::MergeLeafData: cell not found for merge at ("
                   << center.x()/mm << "," << center.y()/mm << "," << center.z()/mm
                   << ") mm — skipping." << G4endl;
        }
    }
}

template <typename T>
void AdaptiveGrid<T>::MergeFrom(const AdaptiveGrid<T>& other) {
    G4cout << "AdaptiveGrid::MergeFrom: merging " << other.leafIndices.size()
           << " leaves into " << leafIndices.size() << " cells." << G4endl;

    // Recursively replicate the other grid's octree structure into this grid.
    // Walk from root: if src node is split but dst is not, split dst.
    std::function<void(size_t, size_t)> replicateStructure;
    replicateStructure = [&](size_t srcIdx, size_t dstIdx) {
        if (srcIdx >= other.nodes.size() || dstIdx >= nodes.size()) return;
        const OctreeNode& srcNode = other.nodes[srcIdx];

        if (srcNode.isLeaf) return;  // Leaf — nothing to split

        // src is split → dst must also be split
        if (nodes[dstIdx].isLeaf) {
            SplitNode(dstIdx);
            if (nodes[dstIdx].isLeaf) return;  // SplitNode failed (depth/limit)
        }

        for (int i = 0; i < 8; ++i) {
            int srcChild = srcNode.children[i];
            if (srcChild < 0 || srcChild >= static_cast<int>(other.nodes.size())) continue;
            int dstChild = nodes[dstIdx].children[i];
            if (dstChild < 0) continue;
            replicateStructure(static_cast<size_t>(srcChild),
                               static_cast<size_t>(dstChild));
        }
    };

    // Replicate structure from root
    replicateStructure(0, 0);

    // Update leafIndices after SplitNode calls
    UpdateLeafIndices();

    G4cout << "AdaptiveGrid::MergeFrom: after structure replication: "
           << leafIndices.size() << " leaves." << G4endl;

    // Merge data from all leaves
    for (size_t leafIdx : other.leafIndices) {
        if (leafIdx >= other.nodes.size()) continue;
        const OctreeNode& srcNode = other.nodes[leafIdx];
        if (srcNode.data.sum_wE == 0.0 && srcNode.data.count == 0) continue;
        MergeLeafData(srcNode.center, srcNode.data);
    }
}

#endif // ADAPTIVE_GRID_HH