//==============================================================================
// G4CARE
// @file    GridField.hh
// @brief   3D grid-based electromagnetic field that loads E and/or B field
//          maps from CSV files and interpolates using trilinear or nearest-
//          neighbor methods.
// @details Supports both regular (uniform-grid) and scattered-node CSV formats.
//   Implements FieldBase with separate E and B grids, each with its own
//   scaling factor for unit conversion.
//
//   Configuration keys read: none (constructed programmatically).
// @author  I. I. Everstov
// @author  V. F. Myshkin (Scientific Supervisor)
// @date    2026-07-15  @version 0.9.0
// @copyright Copyright (c) 2026 G4CARE Developers  @
// @license SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef GRID_FIELD_HH
#define GRID_FIELD_HH

#include "FieldBase.hh"
#include "G4ThreeVector.hh"
#include <string>
#include <vector>

/// @brief Grid-based E/B field loaded from CSV with interpolation.
class GridField : public FieldBase {
public:
    /// Constructor: name, E-file, B-file, interpolation method, E scale, B scale.
    GridField(const std::string& name, 
              const std::string& eFilename, 
              const std::string& bFilename,
              const std::string& interp = "trilinear",
              double eScale = 1.0,
              double bScale = 1.0);
    
    ~GridField() override;

    void GetFieldValue(const G4double point[4], G4double* field) const override;
    FieldBase* Clone() const override;

private:
    struct Node {
        double x, y, z;
        double vx, vy, vz;
    };

    /// Electric field data
    std::vector<Node> fENodes;
    bool fERegular;
    int fENx, fENy, fENz;
    double fEX0, fEY0, fEZ0;
    double fEDx, fEDy, fEDz;
    std::vector<double> fERegularData;
    double fEScale; ///< Unit scale factor for E

    /// Magnetic field data
    std::vector<Node> fBNodes;
    bool fBRegular;
    int fBNx, fBNy, fBNz;
    double fBX0, fBY0, fBZ0;
    double fBDx, fBDy, fBDz;
    std::vector<double> fBRegularData;
    double fBScale; ///< Unit scale factor for B

    std::string fInterpMethod;

    void LoadCSV(const std::string& filename, std::vector<Node>& nodes, 
                 bool& isRegular, int& nx, int& ny, int& nz,
                 double& x0, double& y0, double& z0,
                 double& dx, double& dy, double& dz,
                 std::vector<double>& regularData);
    
    void Interpolate(const std::vector<Node>& nodes, bool isRegular,
                     int nx, int ny, int nz,
                     double x0, double y0, double z0,
                     double dx, double dy, double dz,
                     const std::vector<double>& regularData,
                     double x, double y, double z, double* result) const;
};

#endif
