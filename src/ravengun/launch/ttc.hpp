#pragma once
// TrueType-collecties (.ttc) als losse font. Wine's GdipPrivateAddMemoryFont neemt geen collectie aan (load_ttf_name_id leest
// alleen een kop 0x00010000 of OTTO): het aantal families blijft 0 en Silent Scope: Bone Eater stopt met "No font loaded or no
// font families data in font file" (gemeten 2026-10-17). Een face uit de collectie als eigen .ttf laadt wel. [Launch] ViewTtcAsTtf
// (direct.cpp) zet zo'n face in de view in de plaats van de link naar de collectie.
#include <string>

namespace ravengun::launch {

// Begint `data` met de kop van een collectie ('ttcf')?
bool is_ttc(const std::string& data);

// Face `face` van de collectie `ttc` als losse font in `out`: de tabelmap van die face met eigen offsets, de tabellen er
// direct achter (elk op 4 bytes uitgelijnd). false + err (Nederlands) bij een kapotte of te korte collectie of een face die er
// niet is.
bool ttc_face(const std::string& ttc, unsigned face, std::string& out, std::string& err);

}  // namespace ravengun::launch
