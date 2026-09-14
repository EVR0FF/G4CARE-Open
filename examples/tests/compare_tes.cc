// compare_tes.cc — Сравнение G4CARE primary ntuple с официальным TES_Tuple
//
// Официальный TES_Tuple: 1000 entries (первый шаг primary протона в детекторе)
// Сравниваемые метрики: x, y, z, theta, phi, kinetic_energy, init_kinetic_energy, step_energy_dep
//
// g++ -o compare_tes compare_tes.cc $(root-config --cflags --libs)
// ./compare_tes official_output.root test_xray_tes.root

#include "TFile.h"
#include "TTree.h"
#include "TBranch.h"
#include <iostream>
#include <iomanip>
#include <cmath>

void stats(TTree* t, const char* col, Long64_t n) {
    double sum=0, sum2=0, v; Long64_t cnt=0;
    TBranch* b = t->GetBranch(col);
    if (!b) { std::cout << "  [skip] no branch '" << col << "'\n"; return; }
    b->SetAddress(&v);
    for (Long64_t i=0; i<n; ++i) { b->GetEntry(i); sum+=v; sum2+=v*v; ++cnt; }
    double mean = cnt>0 ? sum/cnt : 0;
    double rms  = cnt>0 ? sqrt(sum2/cnt - mean*mean) : 0;
    std::cout << "  " << std::setw(28) << col << ": mean=" << std::setw(14) << mean
              << "  rms=" << std::setw(14) << rms << "  entries=" << cnt << "\n";
}

int main(int argc, char** argv) {
    if (argc < 3) { std::cerr << "Usage: compare_tes <official.root> <g4care.root>\n"; return 1; }
    TFile* f1 = TFile::Open(argv[1]);
    TFile* f2 = TFile::Open(argv[2]);
    if (!f1 || !f2) { std::cerr << "Cannot open files\n"; return 1; }

    TTree* t1 = (TTree*)f1->Get("TES_Tuple");
    TTree* t2 = (TTree*)f2->Get("primary");
    if (!t1 || !t2) { std::cerr << "Trees not found (TES_Tuple / primary)\n"; return 1; }

    Long64_t n1 = t1->GetEntries(), n2 = t2->GetEntries();
    std::cout << "\n============================================================\n";
    std::cout << "=== xray_TESdetector comparison ===\n";
    std::cout << "Official TES_Tuple entries: " << n1 << " (per-event first step)\n";
    std::cout << "G4CARE   primary   entries: " << n2 << " (per-event emission)\n\n";

    // ── Сравниваемые метрики ──
    const char* cols[] = {"x", "y", "z", "theta", "phi", "kinetic_energy", "init_kinetic_energy", "step_energy_dep"};
    const char* g4cols[] = {"x_mm", "y_mm", "z_mm", "theta", "phi", "kinetic_energy_MeV", "pre_kinetic_energy_MeV", nullptr};

    std::cout << "--- Official TES_Tuple ---\n";
    for (const char* c : cols) stats(t1, c, n1);

    std::cout << "\n--- G4CARE primary ---\n";
    for (const char* c : g4cols) {
        if (c) stats(t2, c, n2);
        else std::cout << "  [skip] step_energy_dep — not in primary NTuple\n";
    }

    // EDEP totals
    double s1=0, v;
    TBranch* b1_edep = t1->GetBranch("step_energy_dep");
    if (b1_edep) { b1_edep->SetAddress(&v); for (Long64_t i=0;i<n1;++i){b1_edep->GetEntry(i);s1+=v;} }

    std::cout << "\n============================================================\n";
    std::cout << "Official total EDEP: " << s1 << " MeV (" << s1*1000 << " keV)\n";
    std::cout << "============================================================\n";

    f1->Close(); f2->Close();
    return 0;
}