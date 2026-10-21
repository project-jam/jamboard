#pragma once
// ============================================================
// Acronym engine
//
// Expands common text abbreviations before the text is spoken, so
// "btw" is read out as "by the way" while "nvm" is left alone.
//
// Words can be grouped, renamed and removed from the settings panel.
// ============================================================

#include "data_models.h"

#include <string>
#include <vector>
#include <cctype>

// Fills the acronym list with the defaults the first time the app runs.
void load_default_acronyms();

// Expands every known acronym in the text.
// Matching is case insensitive unless acronym_case_sensitive is set.
// The capitalisation of the original word is kept: "BTW" becomes
// "By the way" and "btw" becomes "by the way".
std::string expand_acronyms(const std::string& text);

// Lower case copy of a string, used for lookups.
inline std::string to_lower_copy(const std::string& s) {
    std::string out = s;
    for (auto& c : out) c = (char)tolower((unsigned char)c);
    return out;
}
