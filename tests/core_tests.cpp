// Testy części niezależnej od Windows. Uruchom: ctest albo ./wtcore_tests
#include "I18n.hpp"
#include "Json.hpp"
#include "Settings.hpp"
#include "SteamVdf.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

namespace fs = std::filesystem;

static int g_failed = 0, g_passed = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (cond) ++g_passed;                                                          \
        else {                                                                         \
            ++g_failed;                                                                \
            std::cerr << __FILE__ << ":" << __LINE__ << ": FAILED: " #cond << "\n";   \
        }                                                                              \
    } while (0)

static void writeFile(const fs::path& p, const std::string& s) {
    std::ofstream(p, std::ios::binary) << s;
}

static void testJson() {
    auto v = json::parse(R"({"a": 1, "b": [true, false, null], "c": "x\"y\\z\n", "d": -2.5e1})");
    CHECK(v["a"].asNumber() == 1);
    CHECK(v["b"].asArray().size() == 3);
    CHECK(v["b"].asArray()[0].asBool() == true);
    CHECK(v["b"].asArray()[2].isNull());
    CHECK(v["c"].asString() == "x\"y\\z\n");
    CHECK(v["d"].asNumber() == -25);
    CHECK(v["missing"].isNull());

    // \u escape, para surogatów, UTF-8 przepuszczony bez zmian, BOM
    auto u = json::parse("\xEF\xBB\xBF{\"pl\": \"\\u0105\xC5\xBC\", \"emoji\": \"\\ud83d\\ude00\"}");
    CHECK(u["pl"].asString() == "\xC4\x85\xC5\xBC");           // ąż
    CHECK(u["emoji"].asString() == "\xF0\x9F\x98\x80");        // 😀

    // round-trip
    json::Value o(json::Value::Object{});
    o.set("s", "Zażółć \"gęślą\"\tjaźń");
    o.set("b", true);
    o.set("n", 42);
    auto back = json::parse(json::serialize(o));
    CHECK(back["s"].asString() == o["s"].asString());
    CHECK(back["b"].asBool());
    CHECK(back["n"].asNumber() == 42);
    CHECK(json::serialize(json::Value(3)) == "3\n");

    // błędy
    for (const char* bad : {"", "{", "{\"a\" 1}", "[1,]", "{\"a\":1} x", "\"abc", "tru", "01x"}) {
        bool threw = false;
        try { json::parse(bad); } catch (const json::ParseError&) { threw = true; }
        CHECK(threw);
    }

    // copy-on-write: kopia nie zmienia oryginału
    json::Value a(json::Value::Object{});
    a.set("k", 1);
    json::Value b = a;
    b.set("k", 2);
    CHECK(a["k"].asNumber() == 1 && b["k"].asNumber() == 2);
}

static void testI18n() {
    I18n i18n;
    // Bez plików: wbudowany angielski.
    CHECK(i18n.get("close") == "Close");
    CHECK(i18n.get("nope") == "[nope]");
    CHECK(I18n::substitute("{count} x {count} {a}", {{"count", "3"}}) == "3 x 3 {a}");
    CHECK(i18n.format("processes_closed", {{"count", "5"}}) ==
          "Terminated 5 War Thunder processes.");

    auto errors = i18n.loadDirectory(LANG_DIR);
    CHECK(errors.empty());
    auto langs = i18n.languages();
    std::set<std::string> codes;
    for (auto& l : langs) codes.insert(l.code);
    CHECK(codes == (std::set<std::string>{"cs", "de", "en", "es", "it", "pl", "ru", "sk"}));
    CHECK(langs.front().code == "cs"); // posortowane po kodzie, jak w Pythonie

    i18n.setLanguage("pl");
    CHECK(i18n.get("language") == "Polski");
    CHECK(i18n.format("processes_closed", {{"count", "2"}}) == "Zamknięto 2 procesów War Thunder.");
    i18n.setLanguage("xx"); // nieznany – bez zmian
    CHECK(i18n.language() == "pl");

    // Każdy plik ma komplet kluczy z en.json i te same {zmienne}.
    auto en = json::parse([] {
        std::ifstream f(fs::path(LANG_DIR) / "en.json", std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), {});
    }());
    for (const auto& code : codes) {
        std::ifstream f(fs::path(LANG_DIR) / (code + ".json"), std::ios::binary);
        auto doc = json::parse(std::string(std::istreambuf_iterator<char>(f), {}));
        for (const auto& [key, val] : en.asObject()) {
            bool has = doc.has(key);
            if (!has) std::cerr << "  " << code << ".json missing key " << key << "\n";
            CHECK(has);
            for (const char* ph : {"{count}", "{name}", "{pid}", "{error}", "{path}", "{version}", "{percent}"}) {
                bool inEn = val.asString().find(ph) != std::string::npos;
                bool inLang = doc[key].asString().find(ph) != std::string::npos;
                if (inEn != inLang)
                    std::cerr << "  " << code << ".json " << key << " placeholder " << ph << "\n";
                CHECK(inEn == inLang);
            }
        }
    }

    // Wbudowany angielski zna wszystkie klucze z en.json (te same teksty).
    I18n builtin;
    for (const auto& [key, val] : en.asObject()) {
        bool same = builtin.get(key) == val.asString();
        if (!same) std::cerr << "  built-in English differs for " << key << "\n";
        CHECK(same);
    }

    // Uszkodzony plik => błąd w liście, reszta działa.
    fs::path tmp = fs::temp_directory_path() / "wtcleaner_lang_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    writeFile(tmp / "xx.json", "{ broken");
    writeFile(tmp / "yy.json", R"({"close": "Zavřít YY"})");
    I18n other;
    auto errs = other.loadDirectory(tmp);
    CHECK(errs.size() == 1);
    other.setLanguage("yy");
    CHECK(other.get("close") == "Zavřít YY");
    CHECK(other.get("exit") == "Exit"); // brakujący klucz => angielski
    CHECK(other.get("language") == "yy");
    fs::remove_all(tmp);
}

static void testSettings() {
    fs::path dir = fs::temp_directory_path() / "wtcleaner_settings_test";
    fs::remove_all(dir);
    fs::path file = dir / "sub" / "config.json";

    Settings d = Settings::load(file); // brak pliku
    CHECK(d.language.empty() && !d.autostart && d.minimizeToTray);
    CHECK(d.release.path.empty() && d.dev.path.empty() && d.release.enabled && d.dev.enabled);

    // Plik z wersji Python + nieznany klucz, który ma przetrwać zapis.
    fs::create_directories(file.parent_path());
    writeFile(file, R"({"language": "pl", "autostart": false, "extra": [1, 2]})");
    Settings s = Settings::load(file);
    CHECK(s.language == "pl" && !s.autostart && s.minimizeToTray);

    s.autostart = true;
    s.minimizeToTray = false;
    s.release.path = "D:\\Gry\\War Thunder ąę\\cache";
    s.dev.path = "E:\\WarThunderDev\\cache";
    s.dev.enabled = false;
    s.save(file);
    Settings r = Settings::load(file);
    CHECK(r.autostart && !r.minimizeToTray && r.language == "pl");
    CHECK(r.release.path == s.release.path && r.release.enabled);
    CHECK(r.dev.path == s.dev.path && !r.dev.enabled);
    std::ifstream f(file, std::ios::binary);
    auto doc = json::parse(std::string(std::istreambuf_iterator<char>(f), {}));
    CHECK(doc["extra"].asArray().size() == 2);
    CHECK(!fs::exists(file.string() + ".tmp"));

    // Migracja z wersji 2.0 (pamiętała główny folder gry).
    writeFile(file, R"({"game_folder": "C:\\Games\\War Thunder"})");
    Settings m = Settings::load(file);
    CHECK(m.release.path == "C:\\Games\\War Thunder\\cache");
    CHECK(m.dev.path.empty());
    m.save(file);
    {
        std::ifstream mf(file, std::ios::binary);
        auto md = json::parse(std::string(std::istreambuf_iterator<char>(mf), {}));
        CHECK(!md.has("game_folder"));
        CHECK(md["cache_folder"].asString() == m.release.path);
    }
    writeFile(file, R"({"game_folder": "C:\\WT\\", "cache_folder": "X:\\cache"})");
    CHECK(Settings::load(file).release.path == "X:\\cache"); // nowy klucz ma pierwszeństwo
    writeFile(file, R"({"game_folder": "C:\\WT\\"})");
    CHECK(Settings::load(file).release.path == "C:\\WT\\cache");

    writeFile(file, "not json");
    Settings c = Settings::load(file);
    CHECK(c.language.empty() && c.minimizeToTray);
    fs::remove_all(dir);
}

static void testVdf() {
    const char* modern = R"("libraryfolders"
{
	"0"
	{
		"path"		"C:\\Program Files (x86)\\Steam"
		"label"		""
		"apps"
		{
			"236390"		"40000000000"
		}
	}
	"1"
	{
		"path"		"D:\\SteamLibrary"
	}
})";
    auto p = steam::parseLibraryPaths(modern);
    CHECK(p.size() == 2);
    CHECK(p.size() == 2 && p[0] == "C:\\Program Files (x86)\\Steam");
    CHECK(p.size() == 2 && p[1] == "D:\\SteamLibrary");

    const char* legacy = "\"LibraryFolders\"\n{\n\t\"TimeNextStatsReport\"\t\t\"123\"\n"
                         "\t\"1\"\t\t\"E:\\\\Games\\\\Steam\"\n}\n";
    auto q = steam::parseLibraryPaths(legacy);
    CHECK(q.size() == 1 && q[0] == "E:\\Games\\Steam");
    CHECK(steam::parseLibraryPaths("").empty());
}

int main() {
    testJson();
    testI18n();
    testSettings();
    testVdf();
    std::cout << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed ? 1 : 0;
}
