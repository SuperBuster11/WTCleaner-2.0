#include "Settings.hpp"

#include "Json.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {

json::Value readJson(const std::filesystem::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return json::Value();
    std::ostringstream ss;
    ss << f.rdbuf();
    try {
        return json::parse(ss.str());
    } catch (const json::ParseError&) {
        return json::Value();
    }
}

} // namespace

Settings Settings::load(const std::filesystem::path& file) {
    Settings s;
    json::Value doc = readJson(file);
    if (!doc.isObject()) return s;
    s.language = doc["language"].asString("");
    s.autostart = doc["autostart"].asBool(s.autostart);
    s.minimizeToTray = doc["minimize_to_tray"].asBool(s.minimizeToTray);
    s.release.path = doc["cache_folder"].asString("");
    s.release.enabled = doc["cache_folder_enabled"].asBool(true);
    s.dev.path = doc["dev_cache_folder"].asString("");
    s.dev.enabled = doc["dev_cache_folder_enabled"].asBool(true);

    // Migracja z poprzedniej wersji, która pamiętała główny folder gry.
    std::string oldGame = doc["game_folder"].asString("");
    if (s.release.path.empty() && !oldGame.empty()) {
        char last = oldGame.back();
        s.release.path = oldGame + ((last == '\\' || last == '/') ? "" : "\\") + "cache";
    }
    return s;
}

void Settings::save(const std::filesystem::path& file) const {
    json::Value doc = readJson(file);
    if (!doc.isObject()) doc = json::Value(json::Value::Object{});
    doc.set("language", language);
    doc.set("autostart", autostart);
    doc.set("minimize_to_tray", minimizeToTray);
    doc.set("cache_folder", release.path);
    doc.set("cache_folder_enabled", release.enabled);
    doc.set("dev_cache_folder", dev.path);
    doc.set("dev_cache_folder_enabled", dev.enabled);
    doc.asObject().erase("game_folder"); // zastąpiony przez cache_folder

    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    // Zapis do pliku tymczasowego i podmiana – nie zostawi połowy pliku.
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("cannot open " + tmp.u8string() + " for writing");
        f << json::serialize(doc);
        if (!f) throw std::runtime_error("write failed: " + tmp.u8string());
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        // Zapas dla starszych bibliotek, gdzie rename nie nadpisuje pliku.
        ec.clear();
        std::filesystem::copy_file(tmp, file, std::filesystem::copy_options::overwrite_existing,
                                   ec);
        std::error_code ignore;
        std::filesystem::remove(tmp, ignore);
        if (ec) throw std::runtime_error(ec.message());
    }
}
