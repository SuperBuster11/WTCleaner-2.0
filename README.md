# WT Cleaner (C++ / WinAPI)

Przepisana na C++ wersja WT Cleanera: zamyka procesy War Thunder (gra, launcher,
EAC/BattlEye) i usuwa folder `cache` gry. Czyste WinAPI, bez zewnętrznych bibliotek –
wynikiem jest jeden plik `WT Cleaner.exe` i folder `lang`.

## Co się zmieniło względem wersji Python

**Nowe funkcje**
- **Dwa pamiętane foldery cache: zwykły War Thunder i wersja deweloperska.** Każdy ma
  swoje pole wyboru (które usuwać), ścieżkę i przycisk **Zmień...**. „Usuń folder cache”
  usuwa cache wszystkich zaznaczonych wersji naraz (po jednym potwierdzeniu z listą ścieżek).
- **Wykrywanie automatyczne** (przycisk **Wykryj automatycznie**, a także przy pierwszym
  usuwaniu, gdy ścieżka nie jest ustawiona): biblioteki Steam (także na innych dyskach),
  wpisy w „Programy i funkcje”, `%LOCALAPPDATA%`, `Program Files`, `X:\Games`, `X:\Gry`.
  Instalacja jest uznawana za deweloperską, gdy jej nazwa w rejestrze lub nazwa folderu
  zawiera „dev” (np. `WarThunderDev`).
- **Ręczny wybór wymaga wskazania samego folderu `cache`.** Jeśli wybrany folder nie
  nazywa się `cache`, program to zgłasza i otwiera okno wyboru ponownie. Gdy obok
  folderu nie ma `aces.exe`/`launcher.exe`, program dodatkowo pyta, czy na pewno go użyć.
  (Folder `cache` musi istnieć, żeby dało się go wskazać – gra tworzy go przy uruchomieniu.)
- **Tłumaczenia w plikach** `lang/<kod>.json`. Nowy język = nowy plik (np. `fr.json`
  z polem `"language": "Français"`), bez kompilowania. Brakujące klucze biorą się
  z angielskiego, który jest też wbudowany w program na wypadek braku folderu `lang`.

- **Pasek postępu** dla zamykania procesów i usuwania cache. Przy usuwaniu program
  najpierw liczy pliki (pasek „przewija się”), potem pokazuje procent – łącznie dla obu
  folderów cache. Obie operacje działają w tle, więc okno nie zamarza.

**Drobne usprawnienia**
- Zamykanie procesów czeka (do 5 s na proces), aż faktycznie się zakończą, zanim zgłosi
  sukces – dzięki temu cache można usuwać od razu po zamknięciu gry.
- Jeśli jakiś plik w cache jest zablokowany, program usuwa resztę i zgłasza, który plik
  się nie dał.
- Okno potwierdzenia pokazuje pełną ścieżkę usuwanego folderu.
- Autostart dodaje parametr `--minimized`: program startuje do zasobnika tylko przy
  starcie systemu; uruchomiony ręcznie pokazuje okno (wcześniej zawsze się chował).
- Minimalizacja okna (gdy opcja „Minimalizuj do zasobnika” jest włączona) chowa je do zasobnika.
- Można uruchomić tylko jedną kopię programu – druga tylko pokazuje okno pierwszej.
- Komunikaty o zamykanych procesach są tłumaczone (wcześniej zawsze po angielsku).

**Naprawione błędy z wersji Python**
- `save_settings` było zdefiniowane dwa razy, druga wersja nie zapisywała
  `minimize_to_tray` – opcja nie była pamiętana.
- Okno potwierdzenia miało pytanie zamiast tytułu.
- Stan autostartu jest czytany z rejestru, a ścieżka w rejestrze odświeżana, gdy
  przeniesiesz program w inne miejsce.

Plik ustawień jest ten sam (`%LOCALAPPDATA%\WTCleaner\config.json`), więc stare
ustawienia (język, autostart) zostaną przejęte. Klucze folderów cache: `cache_folder`,
`dev_cache_folder` (+ `..._enabled`). Klucz `game_folder` z wersji 2.0 jest automatycznie
zamieniany na `<folder>\cache`.

## Budowanie

### Visual Studio 2022 (najprościej)
1. *Plik → Otwórz → Folder...* i wskaż ten folder (VS sam wykryje `CMakeLists.txt`).
2. Wybierz konfigurację **x64-Release** i *Kompiluj → Kompiluj wszystko*.
3. Gotowy program: `out\build\x64-Release\WT Cleaner.exe` (+ folder `lang`).

### Wiersz poleceń (Developer PowerShell for VS)
```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release   # testy
```

### MSYS2 / MinGW-w64
```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Program jest linkowany statycznie, więc nie wymaga VC++ Redistributable ani DLL-i MinGW.
Do dystrybucji skopiuj `WT Cleaner.exe` razem z folderem `lang`.

## Struktura

| Plik | Zawartość |
|---|---|
| `src/main.cpp` | Okno, kontrolki, zasobnik, logika przycisków |
| `src/WinSystem.*` | Procesy, rejestr (autostart), wykrywanie instalacji (zwykła/dev), wybór folderu, usuwanie |
| `src/I18n.*` | Wczytywanie tłumaczeń z `lang/*.json` |
| `src/Settings.*` | Odczyt/zapis `config.json` |
| `src/Json.*` | Mały parser JSON (bez zależności) |
| `src/SteamVdf.hpp` | Odczyt bibliotek Steam z `libraryfolders.vdf` |
| `lang/*.json` | Tłumaczenia (pl, en, cs, sk, de, ru, es, it) |
| `tests/core_tests.cpp` | Testy części przenośnej (JSON, tłumaczenia, ustawienia, VDF) |
| `tools/extract_translations.py` | Skrypt, który wyciągnął tłumaczenia ze starego `WT_Cleaner.py` |

## Licencja
AGPL-3.0, jak oryginał (`LICENSE.txt`).
