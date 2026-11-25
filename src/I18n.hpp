// Tłumaczenia wczytywane z plików lang/<kod>.json obok programu.
// Angielski jest dodatkowo wbudowany w program jako zapas, więc aplikacja
// działa nawet bez folderu lang.
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

class I18n {
public:
    struct Language {
        std::string code; // np. "pl"
        std::string name; // np. "Polski" (pole "language" w pliku)
    };

    // Wczytuje wszystkie pliki *.json z folderu. Zwraca listę błędów
    // (np. uszkodzony plik), które warto pokazać w logu.
    std::vector<std::string> loadDirectory(const std::filesystem::path& dir);

    // Języki posortowane po kodzie (jak w wersji Python).
    std::vector<Language> languages() const;
    bool hasLanguage(const std::string& code) const { return tables_.count(code) > 0; }

    void setLanguage(const std::string& code);
    const std::string& language() const { return current_; }

    // Tekst w bieżącym języku -> po angielsku -> "[klucz]".
    std::string get(const std::string& key) const;

    // Jak get(), ale zamienia {nazwa} na podane wartości.
    std::string format(const std::string& key,
                       const std::map<std::string, std::string>& args) const;

    // Podstawia {nazwa} w dowolnym tekście (wydzielone do testów).
    static std::string substitute(std::string text,
                                  const std::map<std::string, std::string>& args);

    I18n();

private:
    using Table = std::map<std::string, std::string>;
    std::map<std::string, Table> tables_;
    std::string current_ = "en";
};
