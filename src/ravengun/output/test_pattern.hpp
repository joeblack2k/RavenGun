#pragma once
// Synthetische testpatronen voor de outputbackend (zonder Move). Posities zijn in aim-ruimte
// (genormaliseerd scherm, linksboven (0,0)); de transformatie gebeurt pas in GunOutput.
#include <string>
#include <vector>

namespace ravengun::output {

enum class PatternKind { Center, Corners, Grid, Sweep, Point };
const char* to_string(PatternKind k);
bool parse_pattern(const std::string& name, PatternKind& k);

struct PatternParams {
    PatternKind kind = PatternKind::Center;
    double dwell_s = 2.0;      // Corners/Grid: tijd per punt
    double duration_s = 12.0;  // Sweep: totale duur (drie benen: horizontaal, verticaal, cirkel)
    double margin = -1;        // < 0: standaard per patroon (corners 0, grid 0,1, sweep 0,05)
    double x = 0.5, y = 0.5;   // Point
    double rate_hz = 120;      // Sweep: stappen per seconde
};

struct PatternStep {
    double t_s = 0;  // tijdstip sinds de start
    double x = 0.5, y = 0.5;
    int index = 0;   // volgnummer van het punt of de stap
};

// Geeft de stappen terug (stijgende t_s). Leeg bij ongeldige parameters (err gevuld).
// Center en Point: één stap; Corners: 4 (met de klok mee vanaf linksboven); Grid: 9 rijsgewijs; Sweep: continu.
std::vector<PatternStep> generate_pattern(const PatternParams& p, std::string& err);

// Duur van het patroon in seconden (tijd van de laatste stap plus de dwell voor puntpatronen).
double pattern_length_s(const PatternParams& p, const std::vector<PatternStep>& steps);

}  // namespace ravengun::output
