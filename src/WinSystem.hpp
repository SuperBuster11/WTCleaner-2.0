// Funkcje zależne od Windows: procesy, rejestr, foldery, okna dialogowe.
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace win {

std::wstring toWide(const std::string& utf8);
std::string toUtf8(const std::wstring& wide);
std::string errorMessage(DWORD code); // komunikat systemowy (UTF-8)

std::filesystem::path exeDirectory();
std::wstring exePath();
std::filesystem::path configFile();   // %LOCALAPPDATA%\WTCleaner\config.json
std::string systemLanguage();         // np. "pl" (z "pl-PL")

// ---- procesy
struct ProcessEvent {
    std::wstring name;
    DWORD pid;
    bool ok;
    std::string error; // gdy !ok
};
using ProgressFn = std::function<void(size_t done, size_t total)>;

// Zamyka (TerminateProcess) wszystkie procesy o podanych nazwach exe
// (bez rozróżniania wielkości liter) i czeka chwilę, aż faktycznie się zakończą.
// Callbacki są wołane w wątku wywołującym. Zwraca liczbę zamkniętych.
int terminateProcesses(const std::vector<std::wstring>& exeNames,
                       const std::function<void(const ProcessEvent&)>& onEvent,
                       const ProgressFn& onProgress);

// ---- autostart (HKCU\...\Run)
bool isAutostartEnabled();
// Zapisuje "ścieżka.exe" --minimized. Zwraca pusty string lub opis błędu.
std::string setAutostart(bool enable);

// ---- folder gry
bool looksLikeGameFolder(const std::filesystem::path& dir);
// Czy folder nazywa się "cache" (bez rozróżniania wielkości liter).
bool isCacheFolderName(const std::filesystem::path& dir);

struct GameInstalls {
    std::filesystem::path release; // główny folder zwykłej wersji (pusty = nie znaleziono)
    std::filesystem::path dev;     // główny folder wersji deweloperskiej
};
// Szuka instalacji: Steam (wszystkie biblioteki), wpisy deinstalacji
// w rejestrze, domyślne lokalizacje launchera Gaijin. Instalacja jest uznawana
// za deweloperską, gdy jej nazwa w rejestrze lub nazwa folderu zawiera "dev".
GameInstalls detectGameInstalls();

// Okno wyboru folderu (IFileOpenDialog). Pusta ścieżka = anulowano.
std::filesystem::path pickFolder(HWND owner, const std::wstring& title,
                                 const std::filesystem::path& initial);

// Lista wszystkich plików i podfolderów (do liczenia postępu usuwania).
std::vector<std::filesystem::path> listTree(const std::filesystem::path& dir);

// Usuwa folder rekurencyjnie wg listy z listTree (zdejmuje atrybut
// "tylko do odczytu"). onDeleted(n) jest wołane po każdym usuniętym elemencie,
// n = liczba usuniętych do tej pory (łącznie z samym folderem na końcu).
// Gdy coś się nie da usunąć (np. plik w użyciu), usuwa resztę i zwraca opis
// pierwszego błędu; pusty string = sukces.
std::string deleteTree(const std::filesystem::path& dir,
                       const std::vector<std::filesystem::path>& entries,
                       const std::function<void(size_t)>& onDeleted);

} // namespace win
