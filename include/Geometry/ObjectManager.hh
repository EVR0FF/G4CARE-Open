#ifndef OBJECT_MANAGER_HH
#define OBJECT_MANAGER_HH

//==============================================================================
//
// G4CARE
//
// @file    ObjectManager.hh
// @brief   Detector object definitions, creation and placement.
//
// @details
//   Reads GEOMETRY.OBJECTS configuration via ConfigManager to construct
//   G4LogicalVolume and G4PVPlacement objects. Supports shapes: box,
//   sphere, cylinder, tube, cone, torus, and para. Each object can have
//   configurable dimensions, materials, sensitivity flags, digitization
//   parameters, production cuts, and isotope composition.
//
//   Configuration keys read from GEOMETRY.OBJECTS.<id>.*:
//     name, shape, material, DIMENSIONS, POSITION, size, pos.x/y/z,
//     isSensitive, DIGITIZE.*, cuts.*, max_step_size,
//     record_ion_production, isotopes, isotope, mother,
//     REPLICA.axis, REPLICA.count, REPLICA.step, REPLICA.offset,
//     CHILDREN.<id>.* (nested objects).
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

#include "G4VSolid.hh"
#include "G4LogicalVolume.hh"
#include "G4VPhysicalVolume.hh"
#include "G4ThreeVector.hh"
#include "DetectorRegistry.hh"

#include <unordered_map>  
#include <vector>
#include <map>
#include <string>

/// @brief Descriptor for parametric replication (REPLICA in YAML)
struct ReplicaDesc {
    std::string axis = "";   ///< Replication axis: "x", "y", or "z"
    int count = 0;           ///< Number of replicas
    double step = 0.0;       ///< Step size between replicas (mm)
    double offset = 0.0;     ///< Offset of first replica from origin (mm)
};

struct ObjDesc {
    std::string name;
    std::string shape;
    std::string material;
    std::string mother;
    std::string gdmlFile;  ///< GDML file for geometry+material (shape/material/DIMENSIONS ignored if set)
    std::vector<std::string> rawSizes;
    G4ThreeVector pos;
    bool isSensitive = false;
    std::string isotope;
    std::vector<std::pair<std::string, double>> isotopes;
    
    G4VSolid* solid = nullptr;
    G4LogicalVolume* logicalVolume = nullptr;
    G4VPhysicalVolume* physicalVolume = nullptr;
    std::map<G4String, G4double> fCuts;
    G4double fMaxStepSize; 
    bool fRecordIonProduction;
    DetectorProperties digitizeProps;
    ReplicaDesc replica;                    ///< REPLICA configuration
    std::vector<ObjDesc> children;          ///< CHILDREN objects placed inside each replica
    ObjDesc() : pos(0,0,0), fMaxStepSize(0.0), fRecordIonProduction(false) {}
};

class ObjectManager {
public:
    ObjectManager();
    ~ObjectManager();
    
    void CreateObjects();
    void PlaceObjects(G4LogicalVolume* motherLV);
    
    const std::vector<ObjDesc>& GetObjects() const { return fObjects; }
    const ObjDesc* FindObject(const std::string& name) const;
    ObjDesc* FindObject(const std::string& name);
    bool HasObject(const std::string& name) const;
    
private:
    void CreateSolid(ObjDesc& obj);
    void PlaceReplica(ObjDesc& parent, G4LogicalVolume* motherLV, int& copyNo);
    
    std::vector<ObjDesc> fObjects;
    std::unordered_map<std::string, size_t> fIndexMap;  
};

#endif