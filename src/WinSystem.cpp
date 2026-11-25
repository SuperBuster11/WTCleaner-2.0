#include "WinSystem.hpp"

#include "SteamVdf.hpp"

#include <knownfolders.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace win {

namespace {

const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunValue = L"WT Cleaner"; // ta sama nazwa co w wersji Python

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) out = p;
    CoTaskMemFree(p);
    return out;
}

std::wstring regReadString(HKEY root, const wchar_t* subkey, const wchar_t* value,
                           REGSAM extra = 0) {
    HKEY key;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | extra, &key) != ERROR_SUCCESS) return L"";
    std::wstring out;
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(key, value, nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && size > 0) {
        std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
        if (RegQueryValueExW(key, value, nullptr, nullptr, (BYTE*)buf.data(), &size) ==
            ERROR_SUCCESS) {
            buf.resize(wcsnlen(buf.c_str(), buf.size()));
            out = buf;
        }
    }
    RegCloseKey(key);
    return out;
}

struct Candidate {
    fs::path path;
    bool devHint; // np. wpis w rejestrze "War Thunder Dev"
};

// Wpisy deinstalacji, których DisplayName zawiera "War Thunder".
void uninstallLocations(HKEY root, REGSAM view, std::vector<Candidate>& out) {
    const wchar_t* base = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    HKEY key;
    if (RegOpenKeyExW(root, base, 0, KEY_READ | view, &key) != ERROR_SUCCESS) return;
    wchar_t name[256];
    for (DWORD i = 0;; ++i) {
        DWORD len = 256;
        if (RegEnumKeyExW(key, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        std::wstring sub = std::wstring(base) + L"\\" + name;
        std::wstring display = lower(regReadString(root, sub.c_str(), L"DisplayName", view));
        if (display.find(L"war thunder") == std::wstring::npos) continue;
        std::wstring loc = regReadString(root, sub.c_str(), L"InstallLocation", view);
        if (!loc.empty()) out.push_back({loc, display.find(L"dev") != std::wstring::npos});
    }
    RegCloseKey(key);
}

void steamLocations(std::vector<Candidate>& out) {
    std::vector<fs::path> steamRoots;
    std::wstring sp = regReadString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (!sp.empty()) steamRoots.emplace_back(fs::path(sp).make_preferred());
    std::wstring ip = regReadString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath",
                                    KEY_WOW64_32KEY);
    if (!ip.empty()) steamRoots.emplace_back(ip);

    std::set<std::wstring> seen;
    for (const auto& root : steamRoots) {
        std::vector<fs::path> libraries{root};
        std::ifstream f(root / L"steamapps" / L"libraryfolders.vdf", std::ios::binary);
        if (f) {
            std::ostringstream ss;
            ss << f.rdbuf();
            for (const auto& p : steam::parseLibraryPaths(ss.str()))
                libraries.emplace_back(toWide(p));
        }
        for (const auto& lib : libraries) {
            if (!seen.insert(lower(lib.wstring())).second) continue;
            out.push_back({lib / L"steamapps" / L"common" / L"War Thunder", false});
        }
    }
}

} // namespace

// ------------------------------------------------------------ teksty

std::wstring toWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

std::string toUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::string errorMessage(DWORD code) {
    wchar_t* buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, code, 0, (LPWSTR)&buf, 0, nullptr);
    std::wstring msg = n ? std::wstring(buf, n) : L"";
    LocalFree(buf);
    while (!msg.empty() && (msg.back() == L'\n' || msg.back() == L'\r' || msg.back() == L'.'))
        msg.pop_back();
    if (msg.empty()) msg = L"Error " + std::to_wstring(code);
    return toUtf8(msg);
}

// ------------------------------------------------------------ ścieżki

std::wstring exePath() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (n < buf.size()) {
            buf.resize(n);
            return buf;
        }
        buf.resize(buf.size() * 2);
    }
}

fs::path exeDirectory() { return fs::path(exePath()).parent_path(); }

fs::path configFile() {
    std::wstring base = knownFolder(FOLDERID_LocalAppData);
    if (base.empty()) return exeDirectory() / L"config.json";
    return fs::path(base) / L"WTCleaner" / L"config.json";
}

std::string systemLanguage() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0) {
        std::string s = toUtf8(name);
        return s.substr(0, s.find('-'));
    }
    return "en";
}

// ------------------------------------------------------------ procesy

int terminateProcesses(const std::vector<std::wstring>& exeNames,
                       const std::function<void(const ProcessEvent&)>& onEvent,
                       const ProgressFn& onProgress) {
    std::set<std::wstring> wanted;
    for (const auto& n : exeNames) wanted.insert(lower(n));

    // Najpierw zbierz pasujące procesy, żeby znać liczbę do paska postępu.
    struct Target {
        std::wstring name;
        DWORD pid;
    };
    std::vector<Target> targets;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof pe;
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
            if (wanted.count(lower(pe.szExeFile))) targets.push_back({pe.szExeFile, pe.th32ProcessID});
        CloseHandle(snap);
    }

    int killed = 0;
    onProgress(0, targets.size());
    for (size_t i = 0; i < targets.size(); ++i) {
        const Target& t = targets[i];
        ProcessEvent ev{t.name, t.pid, false, ""};
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, t.pid);
        if (!h) h = OpenProcess(PROCESS_TERMINATE, FALSE, t.pid); // bez prawa czekania
        if (h && TerminateProcess(h, 1)) {
            ev.ok = true;
            ++killed;
            WaitForSingleObject(h, 5000); // TerminateProcess jest asynchroniczne
        } else {
            ev.error = errorMessage(GetLastError());
        }
        if (h) CloseHandle(h);
        onEvent(ev);
        onProgress(i + 1, targets.size());
    }
    return killed;
}

// ------------------------------------------------------------ autostart

bool isAutostartEnabled() {
    return !regReadString(HKEY_CURRENT_USER, kRunKey, kRunValue).empty();
}

std::string setAutostart(bool enable) {
    HKEY key;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                              &key, nullptr);
    if (rc != ERROR_SUCCESS) return errorMessage(rc);
    if (enable) {
        std::wstring cmd = L"\"" + exePath() + L"\" --minimized";
        rc = RegSetValueExW(key, kRunValue, 0, REG_SZ, (const BYTE*)cmd.c_str(),
                            (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, kRunValue);
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return rc == ERROR_SUCCESS ? "" : errorMessage(rc);
}

// ------------------------------------------------------------ folder gry

bool looksLikeGameFolder(const fs::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / L"launcher.exe", ec) ||
           fs::is_regular_file(dir / L"win64" / L"aces.exe", ec) ||
           fs::is_regular_file(dir / L"aces.exe", ec);
}

bool isCacheFolderName(const fs::path& dir) {
    fs::path p = dir;
    if (!p.has_filename()) p = p.parent_path(); // "X:\\...\\cache\\"
    return lower(p.filename().wstring()) == L"cache";
}

GameInstalls detectGameInstalls() {
    std::vector<Candidate> candidates;
    steamLocations(candidates);
    uninstallLocations(HKEY_CURRENT_USER, 0, candidates);
    uninstallLocations(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, candidates);
    uninstallLocations(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, candidates);

    // Typowe nazwy folderów – zwykła wersja i deweloperska.
    const wchar_t* names[] = {L"WarThunder", L"War Thunder", L"WarThunderDev",
                              L"War Thunder Dev", L"War Thunder (Dev)"};
    std::vector<fs::path> bases;
    std::wstring local = knownFolder(FOLDERID_LocalAppData);
    if (!local.empty()) bases.emplace_back(local);
    for (const KNOWNFOLDERID& id : {FOLDERID_ProgramFiles, FOLDERID_ProgramFilesX86}) {
        std::wstring pf = knownFolder(id);
        if (!pf.empty()) bases.emplace_back(pf);
    }
    // Popularne ręczne lokalizacje na każdym dysku stałym.
    DWORD drives = GetLogicalDrives();
    for (wchar_t d = L'C'; d <= L'Z'; ++d) {
        if (!(drives & (1u << (d - L'A')))) continue;
        std::wstring root = std::wstring(1, d) + L":\\";
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) continue;
        bases.emplace_back(root);
        bases.push_back(fs::path(root) / L"Games");
        bases.push_back(fs::path(root) / L"Gry");
    }
    for (const auto& b : bases)
        for (const wchar_t* n : names) candidates.push_back({b / n, false});

    GameInstalls found;
    for (const auto& c : candidates) {
        if (!looksLikeGameFolder(c.path)) continue;
        bool isDev = c.devHint || lower(c.path.filename().wstring()).find(L"dev") != std::wstring::npos;
        fs::path& slot = isDev ? found.dev : found.release;
        fs::path& other = isDev ? found.release : found.dev;
        std::error_code ec;
        if (!slot.empty() || (!other.empty() && fs::equivalent(other, c.path, ec))) continue;
        slot = c.path;
    }
    return found;
}

fs::path pickFolder(HWND owner, const std::wstring& title, const fs::path& initial) {
    fs::path result;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(title.c_str());
    std::error_code ec;
    if (!initial.empty() && fs::is_directory(initial, ec)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
            dlg->SetFolder(item);
            item->Release();
        }
    }
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                result = p;
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

std::vector<fs::path> listTree(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        out.push_back(it->path());
    return out;
}

std::string deleteTree(const fs::path& dir, const std::vector<fs::path>& entries,
                       const std::function<void(size_t)>& onDeleted) {
    std::string firstError;
    size_t done = 0;
    auto removeOne = [&](const fs::path& p) {
        DWORD attr = GetFileAttributesW(p.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY))
            SetFileAttributesW(p.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
        std::error_code ec;
        fs::remove(p, ec); // plik, pusty folder albo dowiązanie (bez wchodzenia w cel)
        if (ec && firstError.empty()) {
            std::string msg = ec.category() == std::system_category()
                                  ? errorMessage((DWORD)ec.value())
                                  : ec.message();
            firstError = msg + " (" + toUtf8(p.wstring()) + ")";
        }
        onDeleted(++done);
    };
    // listTree zwraca rodzica przed dziećmi, więc od końca usuwamy najpierw
    // zawartość, a potem (już puste) foldery.
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) removeOne(*it);
    removeOne(dir);

    // Gdyby w trakcie pojawiły się nowe pliki – dokończ standardowo.
    std::error_code ec;
    if (firstError.empty() && fs::exists(dir, ec)) {
        fs::remove_all(dir, ec);
        if (ec) firstError = ec.message();
    }
    return firstError;
}

} // namespace win
