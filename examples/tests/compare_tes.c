// compare_tes.c — Compare G4CARE output with official xray_TESdetector
// Build: g++ -o compare_tes compare_tes.c $(root-config --cflags --libs)
// Run: ./compare_tes official_output.root test_xray_tes.root
#include "TFile.h"
#include "TTree.h"
#include "TTreeReader.h"
#include "TTreeReaderArray.h"
#include "TTreeReaderValue.h"
#include <iostream>
#include <iomanip>
#include <cmath>

void compare(const char* officialFile, const char* g4careFile) {
    TFile* f1 = TFile::Open(officialFile);
    TFile* f2 = TFile::Open(g4careFile);
    if (!f1 || !f2) { std::cerr << "Cannot open files\n"; return; }

    TTree* t1 = (TTree*)f1->Get("TES_Tuple");
    TTree* t2 = (TTree*)f2->Get("g4care");
    if (!t1 || !t2) { std::cerr << "Trees not found\n"; return; }

    Long64_t n1 = t1->GetEntries(), n2 = t2->GetEntries();
    std::cout << "=== xray_TESdetector comparison ===\n";
    std::cout << "Official TES_Tuple entries: " << n1 << "\n";
    std::cout << "G4CARE   g4care     entries: " << n2 << "\n\n";

    // ── Compare column sets ──
    std::cout << "Official columns: ";
    for (auto* leaf : *t1->GetListOfLeaves()) std::cout << leaf->GetName() << " ";
    std::cout << "\nG4CARE   columns: ";
    for (auto* leaf : *t2->GetListOfLeaves()) std::cout << leaf->GetName() << " ";
    std::cout << "\n\n";

    // ── Common statistics ──
    auto stats = [](TTree* t, const char* col, Long64_t n) {
        double sum=0, sum2=0, v; Long64_t cnt=0;
        t->SetBranchStatus("*",0); t->SetBranchStatus(col,1);
        TBranch* b = t->GetBranch(col); if (!b) return;
        b->SetAddress(&v);
        for (Long64_t i=0; i<n; ++i) { b->GetEntry(i); sum+=v; sum2+=v*v; ++cnt; }
        double mean = cnt>0 ? sum/cnt : 0;
        double rms  = cnt>0 ? sqrt(sum2/cnt - mean*mean) : 0;
        std::cout << "  " << std::setw(20) << col << ": mean=" << std::setw(12) << mean
                  << "  rms=" << std::setw(12) << rms << "  entries=" << cnt << "\n";
    };

    std::cout << "--- Official (TES_Tuple) ---\n";
    stats(t1, "x", n1);
    stats(t1, "y", n1);
    stats(t1, "z", n1);
    stats(t1, "kinetic_energy", n1);
    stats(t1, "step_energy_dep", n1);

    std::cout << "\n--- G4CARE (g4care) ---\n";
    stats(t2, "x", n2);
    stats(t2, "y", n2);
    stats(t2, "z", n2);
    stats(t2, "kinetic_energy", n2);
    stats(t2, "edep", n2);

    // ── EDEP comparison ──
    double sumEdep1=0, sumEdep2=0, v1, v2;
    t1->SetBranchStatus("*",0); t1->SetBranchStatus("step_energy_dep",1);
    t2->SetBranchStatus("*",0); t2->SetBranchStatus("edep",1);
    t1->GetBranch("step_energy_dep")->SetAddress(&v1);
    t2->GetBranch("edep")->SetAddress(&v2);
    for (Long64_t i=0; i<n1; ++i) { t1->GetEntry(i); sumEdep1+=v1; }
    for (Long64_t i=0; i<n2; ++i) { t2->GetEntry(i); sumEdep2+=v2; }
    std::cout << "\n--- EDEP totals ---\n";
    std::cout << "Official sum(step_energy_dep): " << sumEdep1/CLHEP::keV << " keV\n";
    std::cout << "G4CARE   sum(edep):           " << sumEdep2/CLHEP::keV << " keV\n";

    f1->Close(); f2->Close();
}

int main(int argc, char** argv) {
    if (argc<3) { std::cerr << "Usage: compare_tes <official.root> <g4care.root>\n"; return 1; }
    compare(argv[1], argv[2]);
    return 0;
}