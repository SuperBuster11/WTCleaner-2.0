// Ustawienia zapisywane w %LOCALAPPDATA%\WTCleaner\config.json
// (ten sam plik co w wersji Python; klucze language/autostart/minimize_to_tray
// bez zmian, plus ścieżki folderów cache dla wersji zwykłej i deweloperskiej).
#pragma once

#include <filesystem>
#include <string>

struct CacheSlot {
    std::string path;      // pełna ścieżka do folderu "cache" (UTF-8); pusta = nieustawiona
    bool enabled = true;   // czy usuwać przy kliknięciu "Usuń folder cache"
};

struct Settings {
    std::string language;      // pusty = wykryj z systemu
    bool autostart = false;
    bool minimizeToTray = true;
    CacheSlot release;         // zwykły War Thunder
    CacheSlot dev;             // wersja deweloperska

    // Brak pliku lub uszkodzony JSON => wartości domyślne (jak w Pythonie).
    // Stary klucz "game_folder" (wersja C++ 2.0) jest zamieniany na <folder>\cache.
    static Settings load(const std::filesystem::path& file);

    // Zachowuje nieznane klucze z istniejącego pliku. Rzuca std::runtime_error.
    void save(const std::filesystem::path& file) const;
};
