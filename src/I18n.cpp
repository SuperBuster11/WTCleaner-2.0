#include "I18n.hpp"

#include "Json.hpp"

#include <fstream>
#include <sstream>

namespace {

// Wbudowany angielski – zapas, gdy brakuje pliku lub klucza.
const std::map<std::string, std::string> kEnglish = {
    {"language", "English"},
    {"app_title", "WT Cleaner"},
    {"select_language", "Select language"},
    {"action_choice", "What do you want to do?"},
    {"close_processes_option", "Terminate War Thunder processes"},
    {"delete_cache_option", "Delete cache folder"},
    {"closing_processes", "Terminating War Thunder processes..."},
    {"processes_closed", "Terminated {count} War Thunder processes."},
    {"no_processes_found", "No War Thunder processes found to terminate."},
    {"confirm_delete_cache", "Are you sure you want to delete the 'cache' folder?"},
    {"delete_cache_cancelled", "Deletion of the 'cache' folder has been cancelled."},
    {"no_folder_selected", "No folder was selected."},
    {"finished", "Finished."},
    {"close", "Close"},
    {"clear_log", "Clear log"},
    {"autostart_option", "Run at system startup"},
    {"autostart_enabled", "Run at system startup has been enabled."},
    {"autostart_disabled", "Run at system startup has been disabled."},
    {"autostart_error", "An error occurred while configuring autostart."},
    {"error", "Error"},
    {"minimize_to_tray_option", "Minimize to system tray"},
    {"show", "Show"},
    {"exit", "Exit"},
    {"counting_files", "Counting files to delete..."},
    {"deleting_progress", "Deleting cache folder... {percent}%"},
    {"process_terminated", "Terminated {name} (PID: {pid})"},
    {"process_terminate_failed", "Failed to terminate {name} (PID: {pid}): {error}"},
    {"settings_save_error", "Failed to save settings: {error}"},
    {"cache_group", "Cache folders"},
    {"slot_release", "War Thunder"},
    {"slot_dev", "War Thunder (dev version)"},
    {"folder_not_set", "(not set)"},
    {"change_folder", "Change..."},
    {"detect_folder", "Detect automatically"},
    {"select_cache_folder", "Select the 'cache' folder \xE2\x80\x93" " {version}"},
    {"not_cache_folder", "The selected folder is not a 'cache' folder. Please select the folder named 'cache' inside the game directory ({version})."},
    {"invalid_cache_parent", "This 'cache' folder is not inside a War Thunder directory (no aces.exe or launcher.exe next to it). Use it anyway?"},
    {"cache_folder_saved", "Cache folder saved ({version}): {path}"},
    {"cache_detected", "Cache folder detected ({version}): {path}"},
    {"folder_not_detected", "Could not find this installation automatically: {version}."},
    {"no_cache_slot_selected", "Tick at least one game version whose cache you want to delete."},
    {"cache_not_exists_at", "The 'cache' folder does not exist ({version}): {path}"},
    {"cache_deleted_at", "Deleted the 'cache' folder ({version}): {path}"},
    {"delete_cache_error_at", "Error while deleting the 'cache' folder ({version}): {error}"},
};

bool readFile(const std::filesystem::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

} // namespace

I18n::I18n() { tables_["en"] = kEnglish; }

std::vector<std::string> I18n::loadDirectory(const std::filesystem::path& dir) {
    std::vector<std::string> errors;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        errors.push_back("Language folder not found: " + dir.u8string());
        return errors;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        std::string code = entry.path().stem().u8string();
        std::string text;
        if (!readFile(entry.path(), text)) {
            errors.push_back("Cannot read " + entry.path().filename().u8string());
            continue;
        }
        try {
            json::Value doc = json::parse(text);
            if (!doc.isObject()) throw json::ParseError("root is not an object");
            // Plik nadpisuje/uzupełnia istniejącą tabelę (dla "en" – wbudowaną).
            Table& table = tables_[code];
            for (const auto& [key, val] : doc.asObject())
                if (val.isString()) table[key] = val.asString();
            if (!table.count("language")) table["language"] = code;
        } catch (const std::exception& e) {
            errors.push_back(entry.path().filename().u8string() + ": " + e.what());
        }
    }
    return errors;
}

std::vector<I18n::Language> I18n::languages() const {
    std::vector<Language> out;
    for (const auto& [code, table] : tables_) {
        auto it = table.find("language");
        out.push_back({code, it != table.end() ? it->second : code});
    }
    return out; // std::map => już posortowane po kodzie
}

void I18n::setLanguage(const std::string& code) {
    if (hasLanguage(code)) current_ = code;
}

std::string I18n::get(const std::string& key) const {
    auto t = tables_.find(current_);
    if (t != tables_.end()) {
        auto it = t->second.find(key);
        if (it != t->second.end()) return it->second;
    }
    auto en = tables_.find("en");
    if (en != tables_.end()) {
        auto it = en->second.find(key);
        if (it != en->second.end()) return it->second;
    }
    return "[" + key + "]";
}

std::string I18n::substitute(std::string text, const std::map<std::string, std::string>& args) {
    for (const auto& [name, value] : args) {
        const std::string token = "{" + name + "}";
        size_t pos = 0;
        while ((pos = text.find(token, pos)) != std::string::npos) {
            text.replace(pos, token.size(), value);
            pos += value.size();
        }
    }
    return text;
}

std::string I18n::format(const std::string& key,
                         const std::map<std::string, std::string>& args) const {
    return substitute(get(key), args);
}
