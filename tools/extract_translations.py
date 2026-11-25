"""Jednorazowe narzędzie: wyciąga tłumaczenia z WT_Cleaner.py (wersja Python)
i zapisuje je jako lang/<kod>.json, dopisując teksty nowych funkcji.

Użycie: python extract_translations.py ŚCIEŻKA/WT_Cleaner.py ŚCIEŻKA/lang
"""
import ast
import json
import sys
from pathlib import Path

# Klucze używane przez wersję C++ (reszta ze starego słownika była nieużywana).
KEEP = [
    "language", "app_title", "select_language", "action_choice",
    "close_processes_option", "delete_cache_option", "closing_processes",
    "processes_closed", "no_processes_found", "confirm_delete_cache",
    "delete_cache_cancelled", "no_folder_selected", "finished",
    "close", "clear_log", "autostart_option", "autostart_enabled",
    "autostart_disabled", "autostart_error", "error",
    "minimize_to_tray_option", "show", "exit",
]

NEW = {
    "pl": {
        "counting_files": "Liczenie plików do usunięcia...",
        "deleting_progress": "Usuwanie folderu cache... {percent}%",
        "process_terminated": "Zamknięto {name} (PID: {pid})",
        "process_terminate_failed": "Nie udało się zamknąć {name} (PID: {pid}): {error}",
        "settings_save_error": "Błąd zapisu ustawień: {error}",
        "cache_group": "Foldery cache",
        "slot_release": "War Thunder",
        "slot_dev": "War Thunder (wersja deweloperska)",
        "folder_not_set": "(nie ustawiono)",
        "change_folder": "Zmień...",
        "detect_folder": "Wykryj automatycznie",
        "select_cache_folder": "Wybierz folder 'cache' – {version}",
        "not_cache_folder": "Wybrany folder nie jest folderem 'cache'. Wskaż folder o nazwie 'cache' w katalogu gry ({version}).",
        "invalid_cache_parent": "Ten folder 'cache' nie leży w katalogu War Thunder (obok nie ma aces.exe ani launcher.exe). Użyć go mimo to?",
        "cache_folder_saved": "Zapisano folder cache ({version}): {path}",
        "cache_detected": "Wykryto folder cache ({version}): {path}",
        "folder_not_detected": "Nie udało się automatycznie znaleźć instalacji: {version}.",
        "no_cache_slot_selected": "Zaznacz co najmniej jedną wersję gry, której cache chcesz usunąć.",
        "cache_not_exists_at": "Folder 'cache' nie istnieje ({version}): {path}",
        "cache_deleted_at": "Usunięto folder 'cache' ({version}): {path}",
        "delete_cache_error_at": "Błąd podczas usuwania folderu 'cache' ({version}): {error}",
    },
    "en": {
        "counting_files": "Counting files to delete...",
        "deleting_progress": "Deleting cache folder... {percent}%",
        "process_terminated": "Terminated {name} (PID: {pid})",
        "process_terminate_failed": "Failed to terminate {name} (PID: {pid}): {error}",
        "settings_save_error": "Failed to save settings: {error}",
        "cache_group": "Cache folders",
        "slot_release": "War Thunder",
        "slot_dev": "War Thunder (dev version)",
        "folder_not_set": "(not set)",
        "change_folder": "Change...",
        "detect_folder": "Detect automatically",
        "select_cache_folder": "Select the 'cache' folder – {version}",
        "not_cache_folder": "The selected folder is not a 'cache' folder. Please select the folder named 'cache' inside the game directory ({version}).",
        "invalid_cache_parent": "This 'cache' folder is not inside a War Thunder directory (no aces.exe or launcher.exe next to it). Use it anyway?",
        "cache_folder_saved": "Cache folder saved ({version}): {path}",
        "cache_detected": "Cache folder detected ({version}): {path}",
        "folder_not_detected": "Could not find this installation automatically: {version}.",
        "no_cache_slot_selected": "Tick at least one game version whose cache you want to delete.",
        "cache_not_exists_at": "The 'cache' folder does not exist ({version}): {path}",
        "cache_deleted_at": "Deleted the 'cache' folder ({version}): {path}",
        "delete_cache_error_at": "Error while deleting the 'cache' folder ({version}): {error}",
    },
    "cs": {
        "counting_files": "Počítání souborů ke smazání...",
        "deleting_progress": "Mazání složky cache... {percent}%",
        "process_terminated": "Ukončeno {name} (PID: {pid})",
        "process_terminate_failed": "Nepodařilo se ukončit {name} (PID: {pid}): {error}",
        "settings_save_error": "Chyba při ukládání nastavení: {error}",
        "cache_group": "Složky cache",
        "slot_release": "War Thunder",
        "slot_dev": "War Thunder (vývojářská verze)",
        "folder_not_set": "(nenastaveno)",
        "change_folder": "Změnit...",
        "detect_folder": "Zjistit automaticky",
        "select_cache_folder": "Vyberte složku 'cache' – {version}",
        "not_cache_folder": "Vybraná složka není složka 'cache'. Vyberte složku s názvem 'cache' v adresáři hry ({version}).",
        "invalid_cache_parent": "Tato složka 'cache' neleží v adresáři War Thunder (vedle ní není aces.exe ani launcher.exe). Přesto ji použít?",
        "cache_folder_saved": "Složka cache uložena ({version}): {path}",
        "cache_detected": "Nalezena složka cache ({version}): {path}",
        "folder_not_detected": "Tuto instalaci se nepodařilo najít automaticky: {version}.",
        "no_cache_slot_selected": "Zaškrtněte alespoň jednu verzi hry, jejíž cache chcete smazat.",
        "cache_not_exists_at": "Složka 'cache' neexistuje ({version}): {path}",
        "cache_deleted_at": "Složka 'cache' smazána ({version}): {path}",
        "delete_cache_error_at": "Chyba při mazání složky 'cache' ({version}): {error}",
    },
    "sk": {
        "counting_files": "Počítanie súborov na odstránenie...",
        "deleting_progress": "Odstraňovanie priečinka cache... {percent}%",
        "process_terminated": "Ukončené {name} (PID: {pid})",
        "process_terminate_failed": "Nepodarilo sa ukončiť {name} (PID: {pid}): {error}",
        "settings_save_error": "Chyba pri ukladaní nastavení: {error}",
        "cache_group": "Priečinky cache",
        "slot_release": "War Thunder",
        "slot_dev": "War Thunder (vývojárska verzia)",
        "folder_not_set": "(nenastavené)",
        "change_folder": "Zmeniť...",
        "detect_folder": "Zistiť automaticky",
        "select_cache_folder": "Vyberte priečinok 'cache' – {version}",
        "not_cache_folder": "Vybraný priečinok nie je priečinok 'cache'. Vyberte priečinok s názvom 'cache' v adresári hry ({version}).",
        "invalid_cache_parent": "Tento priečinok 'cache' neleží v adresári War Thunder (vedľa neho nie je aces.exe ani launcher.exe). Napriek tomu ho použiť?",
        "cache_folder_saved": "Priečinok cache uložený ({version}): {path}",
        "cache_detected": "Nájdený priečinok cache ({version}): {path}",
        "folder_not_detected": "Túto inštaláciu sa nepodarilo nájsť automaticky: {version}.",
        "no_cache_slot_selected": "Začiarknite aspoň jednu verziu hry, ktorej cache chcete odstrániť.",
        "cache_not_exists_at": "Priečinok 'cache' neexistuje ({version}): {path}",
        "cache_deleted_at": "Priečinok 'cache' odstránený ({version}): {path}",
        "delete_cache_error_at": "Chyba pri odstraňovaní priečinka 'cache' ({version}): {error}",
    },
    "de": {
        "counting_files": "Zu löschende Dateien werden gezählt...",
        "deleting_progress": "Cache-Ordner wird gelöscht... {percent}%",
        "process_terminated": "{name} beendet (PID: {pid})",
        "process_terminate_failed": "{name} konnte nicht beendet werden (PID: {pid}): {error}",
        "settings_save_error": "Fehler beim Speichern der Einstellungen: {error}",
        "cache_group": "Cache-Ordner",
        "slot_release": "War Thunder",
        "slot_dev": "War Thunder (Dev-Version)",
        "folder_not_set": "(nicht festgelegt)",
        "change_folder": "Ändern...",
        "detect_folder": "Automatisch erkennen",
        "select_cache_folder": "Wähle den 'cache'-Ordner – {version}",
        "not_cache_folder": "Der ausgewählte Ordner ist kein 'cache'-Ordner. Bitte wähle den Ordner namens 'cache' im Spielverzeichnis ({version}).",
        "invalid_cache_parent": "Dieser 'cache'-Ordner liegt nicht in einem War Thunder Verzeichnis (keine aces.exe oder launcher.exe daneben). Trotzdem verwenden?",
        "cache_folder_saved": "Cache-Ordner gespeichert ({version}): {path}",
        "cache_detected": "Cache-Ordner erkannt ({version}): {path}",
        "folder_not_detected": "Diese Installation wurde nicht automatisch gefunden: {version}.",
        "no_cache_slot_selected": "Wähle mindestens eine Spielversion aus, deren Cache gelöscht werden soll.",
        "cache_not_exists_at": "Der 'cache'-Ordner existiert nicht ({version}): {path}",
        "cache_deleted_at": "'cache'-Ordner gelöscht ({version}): {path}",
        "delete_cache_error_at": "Fehler beim Löschen des 'cache'-Ordners ({version}): {error}",
    },
    "ru": {
        "counting_files": "Подсчёт файлов для удаления...",
        "deleting_progress": "Удаление папки кэша... {percent}%",
        "process_terminated": "Завершён {name} (PID: {pid})",
        "process_terminate_failed": "Не удалось завершить {name} (PID: {pid}): {error}",
        "settings_save_error": "Ошибка сохранения настроек: {error}",
        "cache_group": "Папки кэша",
        "slot_release": "War Thunder",
        "slot_dev": "War Thunder (версия для разработчиков)",
        "folder_not_set": "(не задана)",
        "change_folder": "Изменить...",
        "detect_folder": "Найти автоматически",
        "select_cache_folder": "Выберите папку 'cache' – {version}",
        "not_cache_folder": "Выбранная папка не является папкой 'cache'. Выберите папку с именем 'cache' в каталоге игры ({version}).",
        "invalid_cache_parent": "Эта папка 'cache' находится не в каталоге War Thunder (рядом нет aces.exe или launcher.exe). Всё равно использовать?",
        "cache_folder_saved": "Папка кэша сохранена ({version}): {path}",
        "cache_detected": "Найдена папка кэша ({version}): {path}",
        "folder_not_detected": "Не удалось автоматически найти установку: {version}.",
        "no_cache_slot_selected": "Отметьте хотя бы одну версию игры, кэш которой нужно удалить.",
        "cache_not_exists_at": "Папка 'cache' не существует ({version}): {path}",
        "cache_deleted_at": "Папка 'cache' удалена ({version}): {path}",
        "delete_cache_error_at": "Ошибка при удалении папки 'cache' ({version}): {error}",
    },
    "es": {
        "counting_files": "Contando archivos para borrar...",
        "deleting_progress": "Borrando la carpeta de caché... {percent}%",
        "process_terminated": "Se terminó {name} (PID: {pid})",
        "process_terminate_failed": "No se pudo terminar {name} (PID: {pid}): {error}",
        "settings_save_error": "Error al guardar la configuración: {error}",
        "cache_group": "Carpetas de caché",
        "slot_release": "War Thunder",
        "slot_dev": "War Thunder (versión de desarrollo)",
        "folder_not_set": "(no establecida)",
        "change_folder": "Cambiar...",
        "detect_folder": "Detectar automáticamente",
        "select_cache_folder": "Selecciona la carpeta 'cache' – {version}",
        "not_cache_folder": "La carpeta seleccionada no es una carpeta 'cache'. Selecciona la carpeta llamada 'cache' dentro del directorio del juego ({version}).",
        "invalid_cache_parent": "Esta carpeta 'cache' no está dentro de un directorio de War Thunder (no hay aces.exe ni launcher.exe junto a ella). ¿Usarla de todos modos?",
        "cache_folder_saved": "Carpeta de caché guardada ({version}): {path}",
        "cache_detected": "Carpeta de caché detectada ({version}): {path}",
        "folder_not_detected": "No se pudo encontrar esta instalación automáticamente: {version}.",
        "no_cache_slot_selected": "Marca al menos una versión del juego cuya caché quieras borrar.",
        "cache_not_exists_at": "No existe la carpeta 'cache' ({version}): {path}",
        "cache_deleted_at": "Se borró la carpeta 'cache' ({version}): {path}",
        "delete_cache_error_at": "Error al borrar la carpeta 'cache' ({version}): {error}",
    },
    "it": {
        "counting_files": "Conteggio dei file da eliminare...",
        "deleting_progress": "Eliminazione della cartella della cache... {percent}%",
        "process_terminated": "Terminato {name} (PID: {pid})",
        "process_terminate_failed": "Impossibile terminare {name} (PID: {pid}): {error}",
        "settings_save_error": "Errore durante il salvataggio delle impostazioni: {error}",
        "cache_group": "Cartelle della cache",
        "slot_release": "War Thunder",
        "slot_dev": "War Thunder (versione di sviluppo)",
        "folder_not_set": "(non impostata)",
        "change_folder": "Cambia...",
        "detect_folder": "Rileva automaticamente",
        "select_cache_folder": "Seleziona la cartella 'cache' – {version}",
        "not_cache_folder": "La cartella selezionata non è una cartella 'cache'. Seleziona la cartella chiamata 'cache' nella directory del gioco ({version}).",
        "invalid_cache_parent": "Questa cartella 'cache' non si trova in una directory di War Thunder (accanto non ci sono aces.exe o launcher.exe). Usarla comunque?",
        "cache_folder_saved": "Cartella della cache salvata ({version}): {path}",
        "cache_detected": "Cartella della cache rilevata ({version}): {path}",
        "folder_not_detected": "Impossibile trovare automaticamente questa installazione: {version}.",
        "no_cache_slot_selected": "Seleziona almeno una versione del gioco di cui vuoi eliminare la cache.",
        "cache_not_exists_at": "La cartella 'cache' non esiste ({version}): {path}",
        "cache_deleted_at": "Cartella 'cache' eliminata ({version}): {path}",
        "delete_cache_error_at": "Errore durante l'eliminazione della cartella 'cache' ({version}): {error}",
    },
}


def find_translations(tree):
    for node in ast.walk(tree):
        if isinstance(node, ast.FunctionDef) and node.name == "load_translations":
            for stmt in node.body:
                if isinstance(stmt, ast.Return):
                    return ast.literal_eval(stmt.value)
    raise SystemExit("load_translations() not found")


def main():
    src, out_dir = Path(sys.argv[1]), Path(sys.argv[2])
    data = find_translations(ast.parse(src.read_text(encoding="utf-8-sig")))
    out_dir.mkdir(parents=True, exist_ok=True)
    for code, table in data.items():
        result = {k: table[k] for k in KEEP if k in table}
        missing = [k for k in KEEP if k not in table]
        if missing:
            print(f"{code}: missing {missing}")
        result.update(NEW[code])
        (out_dir / f"{code}.json").write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"{code}: {len(result)} keys")


if __name__ == "__main__":
    main()
