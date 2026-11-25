// Wyciąga ścieżki bibliotek Steam z pliku steamapps/libraryfolders.vdf.
// Przenośne (bez WinAPI), żeby dało się to przetestować.
#pragma once

#include <string>
#include <vector>

namespace steam {

// Zwraca wartości kluczy "path" (UTF-8, z odkodowanym "\\" -> "\").
// Obsługuje też stary format, gdzie biblioteki to "1" "D:\\SteamLibrary".
inline std::vector<std::string> parseLibraryPaths(const std::string& vdf) {
    std::vector<std::string> paths;
    std::vector<std::string> tokens; // tokeny w bieżącej linii
    size_t i = 0;
    auto flushLine = [&] {
        if (tokens.size() == 2) {
            const std::string& key = tokens[0];
            bool numericKey = !key.empty() &&
                              key.find_first_not_of("0123456789") == std::string::npos;
            bool looksLikePath = tokens[1].size() > 2 && tokens[1][1] == ':';
            if (key == "path" || (numericKey && looksLikePath)) paths.push_back(tokens[1]);
        }
        tokens.clear();
    };
    while (i < vdf.size()) {
        char c = vdf[i];
        if (c == '\n') {
            flushLine();
            ++i;
        } else if (c == '"') {
            std::string tok;
            ++i;
            while (i < vdf.size() && vdf[i] != '"') {
                if (vdf[i] == '\\' && i + 1 < vdf.size()) {
                    ++i; // \\ -> \, \" -> "
                }
                tok += vdf[i++];
            }
            ++i; // zamykający cudzysłów
            tokens.push_back(tok);
        } else {
            ++i;
        }
    }
    flushLine();
    return paths;
}

} // namespace steam
