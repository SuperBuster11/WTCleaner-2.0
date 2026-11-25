// WT Cleaner – wersja C++ / WinAPI.
// Zamyka procesy War Thunder i usuwa folder "cache" gry.

#include "WinSystem.hpp" // musi być pierwszy (UNICODE, windows.h)

#include "I18n.hpp"
#include "Settings.hpp"
#include "resource.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <map>
#include <thread>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "advapi32.lib")
#endif

namespace fs = std::filesystem;
using win::toWide;

namespace {

const wchar_t* kWindowClass = L"WTCleanerMainWindow";
const wchar_t* kMutexName = L"Local\\WTCleanerSingleInstance";

// Znaczki w logu jak w wersji Python (zapisane jako UTF-8 przez escape).
const char* kOk = "\xE2\x9C\x94 ";   // ✔
const char* kWarn = "\xE2\x9A\xA0 "; // ⚠

const std::vector<std::wstring> kGameProcesses = {
    L"launcher.exe",         L"aces.exe",          L"aces-min-cpu.exe",
    L"cefprocess.exe",       L"gjagent.exe",       L"aces_be.exe",
    L"beac_wt_mlauncher.exe", L"beservice.exe",    L"beservice_x64.exe",
    L"eac_wt_mlauncher.exe", L"easyanticheat.exe", L"easyanticheat_setup.exe",
    L"eac_launcher.exe",
};

enum : UINT {
    WM_APP_TRAY = WM_APP + 1,   // zdarzenia ikony w zasobniku
    WM_APP_SHOW,                // druga instancja prosi o pokazanie okna
    WM_APP_DELETE_DONE,         // wątek usuwania skończył pracę
    WM_APP_PROGRESS,            // wParam: postęp 0..1000 lub -1 (liczenie), lParam: Phase
    WM_APP_PROC_EVENT,          // lParam: win::ProcessEvent* (zamknięty proces)
    WM_APP_KILL_DONE,           // wParam: liczba zamkniętych procesów
};

enum Phase : LPARAM { PHASE_KILL = 0, PHASE_DELETE = 1 };

// Wysyła postęp z wątku roboczego do okna – tylko gdy zmieni się o 0,1%,
// żeby nie zalać kolejki komunikatów przy tysiącach plików.
struct ProgressPoster {
    HWND target;
    Phase phase;
    int last = -2;
    void operator()(size_t done, size_t total) {
        int permille = total ? (int)(done * 1000 / total) : 1000;
        if (permille == last) return;
        last = permille;
        PostMessageW(target, WM_APP_PROGRESS, (WPARAM)permille, phase);
    }
    void counting() { PostMessageW(target, WM_APP_PROGRESS, (WPARAM)(INT_PTR)-1, phase); }
};

enum ControlId : int {
    ID_LANG_GROUP = 100,
    ID_LANG_COMBO,
    ID_CACHE_GROUP,
    ID_REL_CHECK,
    ID_REL_LABEL,
    ID_REL_CHANGE,
    ID_DEV_CHECK,
    ID_DEV_LABEL,
    ID_DEV_CHANGE,
    ID_DETECT,
    ID_ACTION_GROUP,
    ID_KILL,
    ID_DELETE_CACHE,
    ID_AUTOSTART,
    ID_TRAY_OPTION,
    ID_INFO,
    ID_PROGRESS,
    ID_LOG,
    ID_CLEAR_LOG,
    ID_CLOSE,
    ID_MENU_SHOW = 200,
    ID_MENU_EXIT,
};

// Dwie pamiętane lokalizacje folderu cache.
enum class Slot { Release, Dev };
const Slot kSlots[] = {Slot::Release, Slot::Dev};

struct SlotIds {
    int check, label, change;
    const char* nameKey; // klucz tłumaczenia z nazwą wersji
};

SlotIds slotIds(Slot s) {
    return s == Slot::Release ? SlotIds{ID_REL_CHECK, ID_REL_LABEL, ID_REL_CHANGE, "slot_release"}
                              : SlotIds{ID_DEV_CHECK, ID_DEV_LABEL, ID_DEV_CHANGE, "slot_dev"};
}

struct DeleteItem {
    Slot slot;
    fs::path path;
    std::string error; // pusty = sukces
};
struct DeleteResult {
    std::vector<DeleteItem> items;
};

class App {
public:
    bool create(HINSTANCE inst, bool startMinimized);
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    // budowa UI
    void createControls();
    HWND makeControl(const wchar_t* cls, DWORD style, int id, DWORD exStyle = 0);
    void layout();
    void retranslate();
    void updateFolderLabels();
    int S(int v) const { return MulDiv(v, dpi_, 96); }

    // akcje
    void onLanguageChanged();
    void onAutostartToggled();
    void closeGameProcesses();
    void deleteCache();
    void onDeleteDone(DeleteResult* r);
    fs::path chooseCacheFolder(Slot slot);
    void detectCacheFolders(bool onlyMissing);
    void setCacheFolder(Slot slot, const fs::path& p);
    CacheSlot& cacheSlot(Slot s) { return s == Slot::Release ? settings_.release : settings_.dev; }
    fs::path cachePath(Slot s) { return fs::path(toWide(cacheSlot(s).path)); }
    std::string slotName(Slot s) const { return i18n_.get(slotIds(s).nameKey); }
    void setBusy(bool busy);
    void onProgress(int permille, Phase phase);
    void setProgressMarquee(bool on);
    void onKillDone(int killed);

    // zasobnik / zamykanie
    void handleClose();
    void hideToTray();
    void restoreFromTray();
    void addTrayIcon();
    void removeTrayIcon();
    void showTrayMenu();

    // pomocnicze
    void log(const std::string& utf8);
    void report(const std::string& utf8); // etykieta informacyjna + log
    void saveSettings();
    bool isChecked(int id) const;
    void setChecked(int id, bool on);
    std::wstring tr(const std::string& key) const { return toWide(i18n_.get(key)); }
    int messageBox(const std::string& text, UINT flags);

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    HICON iconSmall_ = nullptr;
    int dpi_ = 96;
    UINT wmTaskbarCreated_ = 0;
    bool trayVisible_ = false;
    bool busy_ = false;

    fs::path configPath_;
    Settings settings_;
    I18n i18n_;
    std::vector<I18n::Language> languages_;
    std::vector<std::string> startupMessages_;
};

// ====================================================================== start

bool App::create(HINSTANCE inst, bool startMinimized) {
    inst_ = inst;

    configPath_ = win::configFile();
    settings_ = Settings::load(configPath_);

    for (const auto& err : i18n_.loadDirectory(win::exeDirectory() / L"lang"))
        startupMessages_.push_back(std::string(kWarn) + err);
    languages_ = i18n_.languages();

    // Język: zapisany -> systemowy -> angielski (jak w wersji Python).
    std::string lang = settings_.language;
    if (!i18n_.hasLanguage(lang)) lang = win::systemLanguage();
    if (!i18n_.hasLanguage(lang)) lang = "en";
    i18n_.setLanguage(lang);

    // Stan autostartu bierzemy z rejestru (prawda), a jeśli jest włączony,
    // odświeżamy ścieżkę – na wypadek gdyby program został przeniesiony.
    settings_.autostart = win::isAutostartEnabled();
    if (settings_.autostart) win::setAutostart(true);

    HDC screen = GetDC(nullptr);
    dpi_ = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);

    iconSmall_ = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = iconSmall_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) return false;

    wmTaskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");

    // Rozmiar okna: obszar klienta 420x712 (przeskalowany do DPI), na środku ekranu.
    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rc{0, 0, S(420), S(712)};
    AdjustWindowRectEx(&rc, style, FALSE, 0);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + ((work.right - work.left) - w) / 2;
    int y = work.top + ((work.bottom - work.top) - h) / 2;

    hwnd_ = CreateWindowExW(0, kWindowClass, L"WT Cleaner", style, x, y, w, h, nullptr, nullptr,
                            inst, this);
    if (!hwnd_) return false;

    saveSettings(); // utrwal wykryty język / stan autostartu
    for (const auto& m : startupMessages_) log(m);

    if (startMinimized && settings_.minimizeToTray) {
        hideToTray();
    } else {
        ShowWindow(hwnd_, startMinimized ? SW_SHOWMINNOACTIVE : SW_SHOW);
        UpdateWindow(hwnd_);
    }
    return true;
}

LRESULT CALLBACK App::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* self;
    if (msg == WM_NCCREATE) {
        self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

// ====================================================================== UI

HWND App::makeControl(const wchar_t* cls, DWORD style, int id, DWORD exStyle) {
    HWND h = CreateWindowExW(exStyle, cls, L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, hwnd_,
                             (HMENU)(INT_PTR)id, inst_, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)font_, FALSE);
    return h;
}

void App::createControls() {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    font_ = CreateFontIndirectW(&ncm.lfMessageFont);

    makeControl(L"BUTTON", BS_GROUPBOX, ID_LANG_GROUP);
    HWND combo = makeControl(L"COMBOBOX", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_LANG_COMBO);

    makeControl(L"BUTTON", BS_GROUPBOX, ID_CACHE_GROUP);
    for (Slot slot : kSlots) {
        SlotIds ids = slotIds(slot);
        makeControl(L"BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, ids.check);
        makeControl(L"STATIC", SS_LEFT | SS_PATHELLIPSIS | SS_NOPREFIX, ids.label);
        makeControl(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, ids.change);
        setChecked(ids.check, cacheSlot(slot).enabled);
    }
    makeControl(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, ID_DETECT);

    makeControl(L"BUTTON", BS_GROUPBOX, ID_ACTION_GROUP);
    makeControl(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, ID_KILL);
    makeControl(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, ID_DELETE_CACHE);

    makeControl(L"BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, ID_AUTOSTART);
    makeControl(L"BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, ID_TRAY_OPTION);
    makeControl(L"STATIC", SS_LEFT | SS_NOPREFIX, ID_INFO);
    HWND progress = makeControl(PROGRESS_CLASSW, PBS_SMOOTH, ID_PROGRESS);
    SendMessageW(progress, PBM_SETRANGE32, 0, 1000);

    HWND logEdit = makeControl(
        L"EDIT", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, ID_LOG,
        WS_EX_CLIENTEDGE);
    SendMessageW(logEdit, EM_SETLIMITTEXT, 0, 0); // bez limitu 32 KB

    makeControl(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, ID_CLEAR_LOG);
    makeControl(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, ID_CLOSE);

    for (size_t i = 0; i < languages_.size(); ++i) {
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)toWide(languages_[i].name).c_str());
        if (languages_[i].code == i18n_.language()) SendMessageW(combo, CB_SETCURSEL, i, 0);
    }

    setChecked(ID_AUTOSTART, settings_.autostart);
    setChecked(ID_TRAY_OPTION, settings_.minimizeToTray);
    retranslate();
    layout();
}

void App::layout() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int cw = rc.right, ch = rc.bottom;
    const int m = S(10), pad = S(10), innerW = cw - 2 * m - 2 * pad;
    auto move = [&](int id, int x, int y, int w, int h) {
        MoveWindow(GetDlgItem(hwnd_, id), x, y, w, h, FALSE);
    };

    int y = m;
    move(ID_LANG_GROUP, m, y, cw - 2 * m, S(54));
    move(ID_LANG_COMBO, m + pad, y + S(20), innerW, S(200));
    y += S(54) + S(8);

    // Każda wersja gry: [x] nazwa ........ [Zmień...]
    //                       ścieżka do folderu cache
    move(ID_CACHE_GROUP, m, y, cw - 2 * m, S(158));
    int row = y + S(20);
    const int changeW = S(100);
    for (Slot slot : kSlots) {
        SlotIds ids = slotIds(slot);
        move(ids.check, m + pad, row + S(2), innerW - changeW - S(6), S(20));
        move(ids.change, m + pad + innerW - changeW, row, changeW, S(24));
        move(ids.label, m + pad + S(18), row + S(27), innerW - S(18), S(18));
        row += S(50);
    }
    move(ID_DETECT, m + pad, y + S(120), S(170), S(28));
    y += S(158) + S(8);

    move(ID_ACTION_GROUP, m, y, cw - 2 * m, S(98));
    move(ID_KILL, m + pad, y + S(22), innerW, S(30));
    move(ID_DELETE_CACHE, m + pad, y + S(58), innerW, S(30));
    y += S(98) + S(8);

    move(ID_AUTOSTART, m + S(2), y, cw - 2 * m, S(20));
    y += S(22);
    move(ID_TRAY_OPTION, m + S(2), y, cw - 2 * m, S(20));
    y += S(26);
    move(ID_INFO, m, y, cw - 2 * m, S(34));
    y += S(36);
    move(ID_PROGRESS, m, y, cw - 2 * m, S(14));
    y += S(22);

    const int btnH = S(28), btnW = S(120);
    const int by = ch - m - btnH;
    move(ID_LOG, m, y, cw - 2 * m, std::max(S(40), by - S(8) - y));
    move(ID_CLEAR_LOG, cw / 2 - S(5) - btnW, by, btnW, btnH);
    move(ID_CLOSE, cw / 2 + S(5), by, btnW, btnH);

    InvalidateRect(hwnd_, nullptr, TRUE);
}

void App::retranslate() {
    auto setText = [&](int id, const char* key) { SetDlgItemTextW(hwnd_, id, tr(key).c_str()); };
    SetWindowTextW(hwnd_, tr("app_title").c_str());
    setText(ID_LANG_GROUP, "select_language");
    setText(ID_CACHE_GROUP, "cache_group");
    for (Slot slot : kSlots) {
        SlotIds ids = slotIds(slot);
        setText(ids.check, ids.nameKey);
        setText(ids.change, "change_folder");
    }
    setText(ID_DETECT, "detect_folder");
    setText(ID_ACTION_GROUP, "action_choice");
    setText(ID_KILL, "close_processes_option");
    setText(ID_DELETE_CACHE, "delete_cache_option");
    setText(ID_AUTOSTART, "autostart_option");
    setText(ID_TRAY_OPTION, "minimize_to_tray_option");
    setText(ID_CLEAR_LOG, "clear_log");
    setText(ID_CLOSE, "close");
    updateFolderLabels();
    if (trayVisible_) addTrayIcon(); // odśwież podpowiedź
}

void App::updateFolderLabels() {
    for (Slot slot : kSlots) {
        const std::string& path = cacheSlot(slot).path;
        std::wstring text = path.empty() ? tr("folder_not_set") : toWide(path);
        SetDlgItemTextW(hwnd_, slotIds(slot).label, text.c_str());
    }
}

// ====================================================================== komunikaty

LRESULT App::handle(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == wmTaskbarCreated_ && wmTaskbarCreated_ != 0) {
        // Explorer się zrestartował – ikona zniknęła, dodaj ją ponownie.
        if (trayVisible_) {
            trayVisible_ = false;
            addTrayIcon();
        }
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        createControls();
        return 0;

    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) {
            if (settings_.minimizeToTray) hideToTray();
        } else {
            layout();
        }
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        RECT rc{0, 0, S(380), S(652)};
        AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);
        mmi->ptMinTrackSize = {rc.right - rc.left, rc.bottom - rc.top};
        return 0;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wp), code = HIWORD(wp);
        switch (id) {
        case ID_LANG_COMBO:
            if (code == CBN_SELCHANGE) onLanguageChanged();
            break;
        case ID_REL_CHANGE:
            chooseCacheFolder(Slot::Release);
            break;
        case ID_DEV_CHANGE:
            chooseCacheFolder(Slot::Dev);
            break;
        case ID_REL_CHECK:
        case ID_DEV_CHECK: {
            Slot slot = id == ID_REL_CHECK ? Slot::Release : Slot::Dev;
            cacheSlot(slot).enabled = isChecked(id);
            saveSettings();
            break;
        }
        case ID_DETECT:
            detectCacheFolders(false);
            break;
        case ID_KILL:
            closeGameProcesses();
            break;
        case ID_DELETE_CACHE:
            deleteCache();
            break;
        case ID_AUTOSTART:
            onAutostartToggled();
            break;
        case ID_TRAY_OPTION:
            settings_.minimizeToTray = isChecked(ID_TRAY_OPTION);
            saveSettings();
            break;
        case ID_CLEAR_LOG:
            SetDlgItemTextW(hwnd_, ID_LOG, L"");
            break;
        case ID_CLOSE:
            handleClose();
            break;
        case ID_MENU_SHOW:
            restoreFromTray();
            break;
        case ID_MENU_EXIT:
            DestroyWindow(hwnd_);
            break;
        }
        return 0;
    }

    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            restoreFromTray();
            break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            showTrayMenu();
            break;
        }
        return 0;

    case WM_APP_SHOW:
        restoreFromTray();
        return 0;

    case WM_APP_DELETE_DONE:
        onDeleteDone(reinterpret_cast<DeleteResult*>(lp));
        return 0;

    case WM_APP_PROGRESS:
        onProgress((int)(INT_PTR)wp, (Phase)lp);
        return 0;

    case WM_APP_PROC_EVENT: {
        auto* ev = reinterpret_cast<win::ProcessEvent*>(lp);
        std::map<std::string, std::string> args{{"name", win::toUtf8(ev->name)},
                                                {"pid", std::to_string(ev->pid)}};
        if (ev->ok) {
            log(kOk + i18n_.format("process_terminated", args));
        } else {
            args["error"] = ev->error;
            log(kWarn + i18n_.format("process_terminate_failed", args));
        }
        delete ev;
        return 0;
    }

    case WM_APP_KILL_DONE:
        onKillDone((int)wp);
        return 0;

    case WM_CLOSE:
        handleClose();
        return 0;

    case WM_DESTROY:
        removeTrayIcon();
        if (font_) DeleteObject(font_);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// ====================================================================== akcje

void App::onLanguageChanged() {
    int sel = (int)SendDlgItemMessageW(hwnd_, ID_LANG_COMBO, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)languages_.size()) return;
    i18n_.setLanguage(languages_[sel].code);
    retranslate();
    saveSettings();
}

void App::onAutostartToggled() {
    bool enable = isChecked(ID_AUTOSTART);
    std::string err = win::setAutostart(enable);
    if (!err.empty()) {
        setChecked(ID_AUTOSTART, !enable);
        messageBox(i18n_.get("autostart_error") + "\n\n" + err, MB_OK | MB_ICONERROR);
        return;
    }
    settings_.autostart = enable;
    log(i18n_.get(enable ? "autostart_enabled" : "autostart_disabled"));
    saveSettings();
}

void App::closeGameProcesses() {
    if (busy_) return;
    report(i18n_.get("closing_processes"));
    setBusy(true);
    SendDlgItemMessageW(hwnd_, ID_PROGRESS, PBM_SETPOS, 0, 0);

    // W tle: czekanie na zakończenie procesów (do 5 s każdy) nie blokuje okna.
    HWND target = hwnd_;
    std::thread([target] {
        ProgressPoster progress{target, PHASE_KILL};
        int killed = win::terminateProcesses(
            kGameProcesses,
            [target](const win::ProcessEvent& ev) {
                auto* copy = new win::ProcessEvent(ev);
                if (!PostMessageW(target, WM_APP_PROC_EVENT, 0, (LPARAM)copy)) delete copy;
            },
            [&progress](size_t done, size_t total) { progress(done, total); });
        PostMessageW(target, WM_APP_KILL_DONE, (WPARAM)killed, 0);
    }).detach();
}

void App::onKillDone(int killed) {
    setBusy(false);
    SendDlgItemMessageW(hwnd_, ID_PROGRESS, PBM_SETPOS, 1000, 0);
    if (killed > 0)
        report(i18n_.format("processes_closed", {{"count", std::to_string(killed)}}));
    else
        report(i18n_.get("no_processes_found"));
}

void App::setProgressMarquee(bool on) {
    HWND bar = GetDlgItem(hwnd_, ID_PROGRESS);
    LONG_PTR style = GetWindowLongPtrW(bar, GWL_STYLE);
    bool isOn = (style & PBS_MARQUEE) != 0;
    if (on == isOn) return;
    SetWindowLongPtrW(bar, GWL_STYLE, on ? (style | PBS_MARQUEE) : (style & ~PBS_MARQUEE));
    SendMessageW(bar, PBM_SETMARQUEE, on ? TRUE : FALSE, 30);
    if (!on) SendMessageW(bar, PBM_SETRANGE32, 0, 1000);
}

void App::onProgress(int permille, Phase phase) {
    if (permille < 0) { // jeszcze nie wiadomo, ile jest pracy
        setProgressMarquee(true);
        if (phase == PHASE_DELETE) SetDlgItemTextW(hwnd_, ID_INFO, tr("counting_files").c_str());
        return;
    }
    setProgressMarquee(false);
    SendDlgItemMessageW(hwnd_, ID_PROGRESS, PBM_SETPOS, permille, 0);
    if (phase == PHASE_DELETE) {
        std::string text =
            i18n_.format("deleting_progress", {{"percent", std::to_string(permille / 10)}});
        SetDlgItemTextW(hwnd_, ID_INFO, toWide(text).c_str());
    }
}

void App::setCacheFolder(Slot slot, const fs::path& p) {
    cacheSlot(slot).path = win::toUtf8(p.wstring());
    updateFolderLabels();
    saveSettings();
}

// Szuka obu instalacji. onlyMissing = uzupełnij tylko nieustawione wersje
// (przy usuwaniu); przycisk "Wykryj" nadpisuje to, co znajdzie.
void App::detectCacheFolders(bool onlyMissing) {
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    win::GameInstalls found = win::detectGameInstalls();
    SetCursor(old);

    for (Slot slot : kSlots) {
        if (onlyMissing && !cacheSlot(slot).path.empty()) continue;
        const fs::path& root = slot == Slot::Release ? found.release : found.dev;
        if (root.empty()) {
            if (!onlyMissing) report(i18n_.format("folder_not_detected", {{"version", slotName(slot)}}));
            continue;
        }
        fs::path cache = root / L"cache";
        setCacheFolder(slot, cache);
        report(i18n_.format("cache_detected", {{"version", slotName(slot)},
                                               {"path", win::toUtf8(cache.wstring())}}));
    }
}

// Ręczny wybór: akceptowany jest tylko folder o nazwie "cache".
fs::path App::chooseCacheFolder(Slot slot) {
    std::map<std::string, std::string> args{{"version", slotName(slot)}};
    fs::path initial = cacheSlot(slot).path.empty() ? fs::path() : cachePath(slot).parent_path();
    for (;;) {
        fs::path p = win::pickFolder(hwnd_, toWide(i18n_.format("select_cache_folder", args)),
                                     initial);
        if (p.empty()) {
            report(i18n_.get("no_folder_selected"));
            return {};
        }
        initial = p;
        if (!win::isCacheFolderName(p)) {
            messageBox(i18n_.format("not_cache_folder", args), MB_OK | MB_ICONWARNING);
            continue; // otwórz okno wyboru ponownie
        }
        if (!win::looksLikeGameFolder(p.parent_path()) &&
            messageBox(i18n_.get("invalid_cache_parent"),
                       MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            initial = p.parent_path();
            continue;
        }
        setCacheFolder(slot, p);
        args["path"] = win::toUtf8(p.wstring());
        report(i18n_.format("cache_folder_saved", args));
        return p;
    }
}

void App::deleteCache() {
    if (busy_) return;

    std::vector<Slot> wanted;
    for (Slot slot : kSlots)
        if (cacheSlot(slot).enabled) wanted.push_back(slot);
    if (wanted.empty()) {
        report(i18n_.get("no_cache_slot_selected"));
        return;
    }

    // Brakujące ścieżki: najpierw wykrywanie, potem ręczny wybór.
    bool missing = false;
    for (Slot slot : wanted) missing |= cacheSlot(slot).path.empty();
    if (missing) detectCacheFolders(true);
    for (Slot slot : wanted)
        if (cacheSlot(slot).path.empty()) chooseCacheFolder(slot);

    std::vector<DeleteItem> items;
    std::string list;
    for (Slot slot : wanted) {
        if (cacheSlot(slot).path.empty()) continue; // anulowano wybór
        fs::path cache = cachePath(slot);
        std::error_code ec;
        if (!fs::is_directory(cache, ec)) {
            report(i18n_.format("cache_not_exists_at", {{"version", slotName(slot)},
                                                         {"path", win::toUtf8(cache.wstring())}}));
            continue;
        }
        items.push_back({slot, cache, ""});
        list += "\n" + slotName(slot) + ":\n" + win::toUtf8(cache.wstring()) + "\n";
    }
    if (items.empty()) return;

    if (messageBox(i18n_.get("confirm_delete_cache") + "\n" + list,
                   MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        report(i18n_.get("delete_cache_cancelled"));
        return;
    }

    // Folder cache potrafi mieć kilka GB – usuwamy w tle, żeby okno nie zamarzło.
    setBusy(true);
    HWND target = hwnd_;
    SendDlgItemMessageW(hwnd_, ID_PROGRESS, PBM_SETPOS, 0, 0);
    std::thread([target, items]() mutable {
        auto* r = new DeleteResult{std::move(items)};
        ProgressPoster progress{target, PHASE_DELETE};

        // 1) policz pliki (pasek w trybie "przewijania", bo jeszcze nie znamy sumy)
        progress.counting();
        std::vector<std::vector<fs::path>> trees;
        size_t total = 0;
        for (const auto& item : r->items) {
            trees.push_back(win::listTree(item.path));
            total += trees.back().size() + 1; // +1 za sam folder cache
        }
        // 2) usuwaj, raportując postęp łącznie dla wszystkich folderów
        size_t base = 0;
        progress(0, total);
        for (size_t i = 0; i < r->items.size(); ++i) {
            r->items[i].error = win::deleteTree(r->items[i].path, trees[i], [&](size_t n) {
                progress(base + n, total);
            });
            base += trees[i].size() + 1;
        }
        progress(total, total);
        if (!PostMessageW(target, WM_APP_DELETE_DONE, 0, (LPARAM)r)) delete r;
    }).detach();
}

void App::onDeleteDone(DeleteResult* r) {
    setBusy(false);
    std::string summary;
    bool anyError = false;
    for (const auto& item : r->items) {
        std::map<std::string, std::string> args{{"version", slotName(item.slot)},
                                                {"path", win::toUtf8(item.path.wstring())},
                                                {"error", item.error}};
        if (item.error.empty()) {
            std::string msg = i18n_.format("cache_deleted_at", args);
            log(kOk + msg);
            if (!anyError) summary = msg;
        } else {
            std::string msg = i18n_.format("delete_cache_error_at", args);
            log(kWarn + msg);
            if (!anyError) summary = msg;
            anyError = true;
        }
    }
    if (r->items.size() > 1 && !anyError) summary = i18n_.get("finished");
    SetDlgItemTextW(hwnd_, ID_INFO, toWide(summary).c_str());
    delete r;
}

void App::setBusy(bool busy) {
    busy_ = busy;
    for (int id : {ID_KILL, ID_DELETE_CACHE, ID_REL_CHANGE, ID_DEV_CHANGE, ID_DETECT})
        EnableWindow(GetDlgItem(hwnd_, id), !busy);
    SetCursor(LoadCursorW(nullptr, busy ? IDC_APPSTARTING : IDC_ARROW));
}

// ====================================================================== zasobnik

void App::handleClose() {
    if (settings_.minimizeToTray)
        hideToTray();
    else
        DestroyWindow(hwnd_);
}

void App::hideToTray() {
    addTrayIcon();
    ShowWindow(hwnd_, SW_HIDE);
}

void App::restoreFromTray() {
    ShowWindow(hwnd_, SW_SHOW);
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    SetForegroundWindow(hwnd_);
    removeTrayIcon(); // jak w wersji Python: ikona tylko gdy okno jest ukryte
}

void App::addTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof nid;
    nid.hWnd = hwnd_;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = iconSmall_ ? iconSmall_ : LoadIconW(nullptr, IDI_APPLICATION);
    lstrcpynW(nid.szTip, tr("app_title").c_str(), ARRAYSIZE(nid.szTip));
    if (trayVisible_) {
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    } else if (Shell_NotifyIconW(NIM_ADD, &nid)) {
        trayVisible_ = true;
    }
}

void App::removeTrayIcon() {
    if (!trayVisible_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof nid;
    nid.hWnd = hwnd_;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    trayVisible_ = false;
}

void App::showTrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_MENU_SHOW, tr("show").c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_MENU_EXIT, tr("exit").c_str());
    SetMenuDefaultItem(menu, ID_MENU_SHOW, FALSE);
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_); // bez tego menu nie znika po kliknięciu obok
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

// ====================================================================== pomocnicze

void App::log(const std::string& utf8) {
    HWND edit = GetDlgItem(hwnd_, ID_LOG);
    std::wstring line = toWide(utf8) + L"\r\n";
    int len = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, len, len);
    SendMessageW(edit, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

void App::report(const std::string& utf8) {
    SetDlgItemTextW(hwnd_, ID_INFO, toWide(utf8).c_str());
    log(utf8);
}

void App::saveSettings() {
    settings_.language = i18n_.language();
    try {
        settings_.save(configPath_);
    } catch (const std::exception& e) {
        log(kWarn + i18n_.format("settings_save_error", {{"error", e.what()}}));
    }
}

bool App::isChecked(int id) const {
    return SendDlgItemMessageW(hwnd_, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void App::setChecked(int id, bool on) {
    SendDlgItemMessageW(hwnd_, id, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

int App::messageBox(const std::string& text, UINT flags) {
    return MessageBoxW(hwnd_, toWide(text).c_str(), tr("app_title").c_str(), flags);
}

} // namespace

// ====================================================================== WinMain

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    // Jedna instancja: druga tylko pokazuje okno pierwszej i kończy pracę.
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kWindowClass, nullptr)) PostMessageW(other, WM_APP_SHOW, 0, 0);
        CloseHandle(mutex);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);

    const bool startMinimized = cmdLine && wcsstr(cmdLine, L"--minimized") != nullptr;

    App app;
    if (!app.create(inst, startMinimized)) {
        MessageBoxW(nullptr, L"Cannot create the main window.", L"WT Cleaner", MB_ICONERROR);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(app.hwnd(), &msg)) continue; // Tab między kontrolkami
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return (int)msg.wParam;
}
