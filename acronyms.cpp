#include "acronyms.h"

#include <algorithm>
#include <cctype>

void load_default_acronyms() {
    // No presets. The list starts empty and the user fills it in.
    acronyms.clear();
}

std::string expand_acronyms(const std::string& text) {
    if (acronyms.empty() || text.empty()) return text;

    std::string out;
    out.reserve(text.size() + 32);

    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = (unsigned char)text[i];

        // Only try to match at the start of a word
        bool at_word_start = (i == 0) ||
            !(isalnum((unsigned char)text[i - 1]) || text[i - 1] == '_');
        if (!at_word_start || !isalpha(c)) {
            out += text[i];
            i++;
            continue;
        }

        // Read the whole word
        size_t start = i;
        while (i < text.size() && (isalnum((unsigned char)text[i]) || text[i] == '_')) i++;
        std::string word = text.substr(start, i - start);

        std::string lower_word = to_lower_copy(word);

        // A blocked word is never expanded, even if an expansion with the
        // same spelling exists.
        for (const auto& ac : acronyms) {
            if (!ac.blocked || ac.acronym.empty()) continue;
            if (acronym_case_sensitive ? (ac.acronym == word)
                                       : (to_lower_copy(ac.acronym) == lower_word)) {
                out += word;
                word.clear();
                break;
            }
        }
        if (word.empty()) continue;

        const Acronym* match = nullptr;
        for (const auto& ac : acronyms) {
            if (!ac.enabled || ac.blocked) continue;
            if (ac.acronym.empty() || ac.expansion.empty()) continue;

            bool hit = acronym_case_sensitive ? (ac.acronym == word)
                                              : (to_lower_copy(ac.acronym) == lower_word);
            if (hit) { match = &ac; break; }
        }

        if (!match) {
            out += word;
            continue;
        }

        // Keep the capitalisation style of the original word
        std::string expansion = match->expansion;
        bool all_upper = true, all_lower = true;
        for (char ch : word) {
            if (isalpha((unsigned char)ch)) {
                if (isupper((unsigned char)ch)) all_lower = false;
                else all_upper = false;
            }
        }
        if (!expansion.empty()) {
            if (all_upper) expansion[0] = (char)toupper((unsigned char)expansion[0]);
            else if (all_lower) expansion[0] = (char)tolower((unsigned char)expansion[0]);
        }

        out += expansion;
    }

    return out;
}
