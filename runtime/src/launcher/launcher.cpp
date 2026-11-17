// SuperWonders Launcher (launcher.h): state, the setup job, and the screen.
#include "launcher/launcher.h"
#include "launcher/save_backup.h"
#include "launcher/mod_catalog.h"
#include "launcher/game_source.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <nx/gamelib.h>
#include <nx/runtime.h>

#include "imgui.h"
#include "launcher/test_pad.h"
#include "launcher/updates.h"
#include "nvn/nvn_vk.h"
#include "overlay/option_rows.h"
#include "overlay/overlay.h"
#include "overlay/overlay_ui.h"
#include "platform.h"
#include "netplay_game.h"

namespace fs = std::filesystem;

namespace nx::launcher {

namespace ui = overlay::ui;
namespace rows = overlay::rows;

namespace {

fs::path u8(const std::string& s) { return fs::path(reinterpret_cast<const char8_t*>(s.c_str())); }
std::string str(const fs::path& p) {
    auto s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}
bool file(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}
std::string read_line(const fs::path& p, const char* key) {  // "key value" lines (build-id.txt)
    std::ifstream f(p);
    std::string line, k = std::string(key) + " ";
    while (std::getline(f, line))
        if (line.rfind(k, 0) == 0) return line.substr(k.size());
    return {};
}

// ---- launcher.ini: what the player picked (in the data root) ----
struct Config {
    std::string exefs, romfs;
    int romfs_copy = 1;  // 1 copy the RomFS into the data root (the default, as Savage Wind does), 0 use it in place
};

fs::path config_path() { return u8(plat::data_root()) / "launcher.ini"; }

Config load_config() {
    Config c;
    std::ifstream f(config_path());
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "exefs") c.exefs = v;
        else if (k == "romfs") c.romfs = v;
        else if (k == "romfs_copy") c.romfs_copy = v != "0";
    }
    return c;
}

void save_config(const Config& c) {
    plat::make_dirs(plat::data_root());
    std::ofstream f(config_path(), std::ios::trunc);
    f << "exefs=" << c.exefs << "\nromfs=" << c.romfs << "\nromfs_copy=" << c.romfs_copy << "\n";
}

// ---- the game-build kit: tools/setup_game.py and a Python to run it ----
struct Kit {
    std::string python, script;  // empty: none
    std::string id;              // game-build/kit-id.txt: what the release's game code is built with
};

Kit find_kit() {
    Kit k;
    const fs::path kit = u8(program_dir()) / "game-build";
#if defined(_WIN32)
    const fs::path py = kit / "python" / "python.exe";
#else
    const fs::path py = file(kit / "python" / "bin" / "python3") ? kit / "python" / "bin" / "python3" : fs::path("python3");
#endif
    if (file(kit / "setup_game.py")) {
        k.script = str(kit / "setup_game.py");
        k.python = py.is_absolute() ? str(py) : "python3";
        std::ifstream f(kit / "kit-id.txt");
        std::getline(f, k.id);
    }
    // a developer's build: NX_SETUP_SCRIPT=<checkout>/tools/setup_game.py, NX_SETUP_PYTHON=<a Python with the deps>
    if (const char* s = std::getenv("NX_SETUP_SCRIPT"); s && *s) k.script = s;
    if (const char* p = std::getenv("NX_SETUP_PYTHON"); p && *p) k.python = p;
    if (!k.script.empty() && k.python.empty()) k.python =
#if defined(_WIN32)
        "python";
#else
        "python3";
#endif
    return k;
}

const char* setup_tools_issue(const Kit& kit) {
    if (kit.script.empty())
        return "This is a runtime update, without the tools for a first build. Extract the complete setup package, "
               "keeping game-build beside wonder-launcher.exe. An already prepared installation can still play.";
    if (!file(u8(kit.script)) || kit.python.empty() || (u8(kit.python).is_absolute() && !file(u8(kit.python))))
        return "The setup tools are incomplete. Extract the entire setup package again, keeping game-build, its "
               "Python and both EXEs together.";
    return nullptr;
}

// ---- is the game set up? ----
enum class Ready { Yes, NotSetUp, Rebuild };
struct Status {
    Ready ready = Ready::NotSetUp;
    std::string why;
};

Status check_status(const Kit& kit) {
    const fs::path d = u8(plat::data_root());
    Status s;
    const char* missing = nullptr;
    if (game_lib_path().empty()) missing = "The game code isn't built yet.";
    else if (!file(d / "analysis" / "main.bin") || !file(d / "analysis" / "sdk.bin")) missing = "The game's executable isn't read yet.";
    else if (!file(d / "shader_programs.bin") || !file(d / "spirv" / "results.tsv")) missing = "The shaders aren't translated yet.";
    else if (!fs::exists(d / "extract" / "romfs")) missing = "The game's files aren't set up yet.";
    if (missing) {
        s.why = missing;
        return s;
    }
    // A release whose kit builds the game code differently (an update): build it again before playing.
    const std::string built = read_line(d / "game" / "build-id.txt", "kit");
    if (!kit.id.empty() && !built.empty() && built != kit.id && game_lib_path() == str(d / "game" / NX_GAMELIB_FILE)) {
        s.ready = Ready::Rebuild;
        s.why = "This version of SuperWonders builds the game code differently.";
        return s;
    }
    s.ready = Ready::Yes;
    return s;
}

// ---- the setup job: tools/setup_game.py in a process of its own, its progress lines read here ----
class Job {
public:
    std::atomic<bool> running{false};
    std::atomic<float> progress{0};
    std::atomic<int> result{-1};  // exit code of the last run, -1 none yet
    std::mutex lock;
    std::string what;                // the current step
    std::vector<std::string> tail;   // the last lines (shown when it fails)
    uint64_t started_ns = 0;

    void start(const std::vector<std::string>& args) {
        if (running) return;
        if (worker.joinable()) worker.join();
        {
            std::lock_guard lk(lock);
            what = "Starting";
            tail.clear();
        }
        progress = 0, result = -1, running = true, cancelled = false;
        started_ns = plat::monotonic_ns();
        worker = std::thread([this, args] { run(args); });
    }
    void cancel() {
        cancelled = true;
#if defined(_WIN32)
        if (job) TerminateJobObject(job, 1);
#else
        if (pid > 0) kill(-pid, SIGTERM);
#endif
    }
    bool was_cancelled() const { return cancelled; }
    ~Job() {
        if (running) cancel();
        if (worker.joinable()) worker.join();
    }

private:
    std::thread worker;
    std::atomic<bool> cancelled{false};
#if defined(_WIN32)
    HANDLE job = nullptr;
#else
    pid_t pid = -1;
#endif

    void line(std::string l) {
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
        if (l.empty()) return;
        float f;
        char text[512] = "";
        if (std::sscanf(l.c_str(), "progress %f %511[^\n]", &f, text) >= 1) {
            progress = std::clamp(f, 0.0f, 1.0f);
            std::lock_guard lk(lock);
            if (*text) what = text;
            return;
        }
        log("launcher: setup: %s", l.c_str());
        std::lock_guard lk(lock);
        tail.push_back(l);
        if (tail.size() > 12) tail.erase(tail.begin());
    }

    void run(std::vector<std::string> args) {
        int code = 1;
#if defined(_WIN32)
        auto wide = [](const std::string& s) {
            int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
            std::wstring w(size_t(std::max(1, n)) - 1, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
            return w;
        };
        std::wstring cmd;
        for (const std::string& a : args) {
            std::wstring w = wide(a);
            cmd += (cmd.empty() ? L"\"" : L" \"") + w + L"\"";  // (no argument here ends in a backslash or has quotes)
        }
        SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
        HANDLE rd = nullptr, wr = nullptr;
        CreatePipe(&rd, &wr, &sa, 0);
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW si{sizeof si};
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = si.hStdError = wr;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION pi{};
        // A job object: cancelling ends the whole tree (the compiler and translator processes too).
        job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                           nullptr, &si, &pi)) {
            AssignProcessToJobObject(job, pi.hProcess);
            ResumeThread(pi.hThread);
            CloseHandle(wr);
            wr = nullptr;
            std::string buf;
            char chunk[4096];
            DWORD n = 0;
            while (ReadFile(rd, chunk, sizeof chunk, &n, nullptr) && n) {
                buf.append(chunk, n);
                for (size_t nl; (nl = buf.find('\n')) != std::string::npos; buf.erase(0, nl + 1)) line(buf.substr(0, nl));
            }
            line(buf);
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD ec = 1;
            GetExitCodeProcess(pi.hProcess, &ec);
            code = int(ec);
            CloseHandle(pi.hProcess), CloseHandle(pi.hThread);
        } else {
            line("error: couldn't start " + args[0] + " (error " + std::to_string(GetLastError()) + ")");
        }
        if (wr) CloseHandle(wr);
        CloseHandle(rd);
        CloseHandle(job);
        job = nullptr;
#else
        int fds[2];
        if (pipe(fds) == 0) {
            pid = fork();
            if (pid == 0) {
                setpgid(0, 0);  // its own process group: cancelling ends the whole tree
                dup2(fds[1], 1), dup2(fds[1], 2);
                close(fds[0]), close(fds[1]);
                std::vector<char*> argv;
                for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
                argv.push_back(nullptr);
                execvp(argv[0], argv.data());
                std::fprintf(stderr, "error: couldn't start %s\n", argv[0]);
                _exit(127);
            }
            close(fds[1]);
            std::string buf;
            char chunk[4096];
            for (ssize_t n; (n = read(fds[0], chunk, sizeof chunk)) > 0;) {
                buf.append(chunk, size_t(n));
                for (size_t nl; (nl = buf.find('\n')) != std::string::npos; buf.erase(0, nl + 1)) line(buf.substr(0, nl));
            }
            line(buf);
            close(fds[0]);
            int st = 0;
            waitpid(pid, &st, 0);
            code = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
            pid = -1;
        }
#endif
        result = cancelled ? 2 : code;
        running = false;
    }
};

// ---- mods: <data root>/mods/<name>/ (hle_fs.cpp mod_roots: name order, a leading '-' turns one off) ----
using Mod = ModInfo;
std::vector<Mod> list_mods() { return inspect_mods(u8(plat::data_root())/"mods"); }

bool set_mod(const Mod& m, bool on) {
    const fs::path dir = u8(plat::data_root()) / "mods";
    std::error_code ec;
    const auto destination = dir / u8(on ? m.name : "-"+m.name);
    if (fs::exists(destination,ec) || ec) return false;
    fs::rename(dir / u8(m.on ? m.name : "-" + m.name), destination, ec);
    return !ec;
}

// ---- the launcher's state (the screen runs on the presenting thread, run() on the game's) ----
enum Page { PagePlay, PageFiles, PageSettings, PageMods, PageUpdates, PageQuit, kPages };
const char* const kPageNames[kPages] = {"Play", "Game files", "Settings", "Mods", "Updates", "Quit"};

enum LauncherText {
    LtPlay, LtGameFiles, LtSettings, LtMods, LtUpdates, LtQuit, LtSuperWonders, LtAbout, LtYourGameFiles, LtExeFS,
    LtRomFSFolder, LtChoose, LtUseInPlace, LtCopyIt, LtSetUp, LtSetUpGameFiles, LtOpenDataFolder, LtOpenSettings,
    LtFolder, LtOpenModsFolder, LtRefresh, LtQuitSuperWonders, LtFilesReady, LtNeedsRebuild, LtFilesNotSetUp,
    LtSelect, LtBack, LtPages, LtPlayHint, LtStart, LtSetUpFirst, LtVersion, LtChooseExeFS, LtChooseRomFS, LtUseFolder,
    LtUpFolder, LtHomeFolder, LtDrives, LtFoldersHere, LtOtherWays, LtSystemPicker, LtCancel, LtSetUpAgain, LtTextCount
};

// These short labels follow the language selected in F1 > System; the detailed tool output stays in its original language.
const char* const kLauncherText[10][LtTextCount] = {
    {"Play", "Game files", "Settings", "Mods", "Updates", "Quit", "SuperWonders", "About", "Your game files",
     "ExeFS folder", "RomFS folder", "Choose...", "Use it in place", "Copy it", "Set up", "Set up game files",
     "Open the data folder", "Open settings", "Folder", "Open the mods folder", "Refresh", "Quit SuperWonders",
     "Game files ready", "Game code needs building again", "Game files not set up", "Select", "Back", "Pages", "Play",
     "Start", "Set up first", "Version", "Choose your ExeFS folder", "Choose your RomFS folder", "Use this folder",
     "Up one folder", "Home folder", "Drives", "Folders here", "Other ways", "The system's folder picker", "Cancel",
     "Set up again"},
    {"Gioca", "File di gioco", "Impostazioni", "Mod", "Aggiornamenti", "Esci", "SuperWonders", "Informazioni",
     "File del gioco", "Cartella ExeFS", "Cartella RomFS", "Scegli...", "Usa nella posizione", "Copia", "Configurazione",
     "Configura i file di gioco", "Apri cartella dati", "Apri impostazioni", "Cartella", "Apri cartella mod", "Aggiorna",
     "Esci da SuperWonders", "File di gioco pronti", "Ricompila il codice di gioco", "File di gioco non configurati",
     "Seleziona", "Indietro", "Pagine", "Gioca", "Avvia", "Configura prima", "Versione", "Scegli la cartella ExeFS",
     "Scegli la cartella RomFS", "Usa questa cartella", "Cartella superiore", "Cartella home", "Unità", "Cartelle qui",
     "Altri metodi", "Selettore cartelle di sistema", "Annulla", "Configura di nuovo"},
    {"Spielen", "Spieldateien", "Einstellungen", "Mods", "Updates", "Beenden", "SuperWonders", "Info",
     "Deine Spieldateien", "ExeFS-Ordner", "RomFS-Ordner", "Auswählen...", "Am Ort verwenden", "Kopieren", "Einrichten",
     "Spieldateien einrichten", "Datenordner öffnen", "Einstellungen öffnen", "Ordner", "Mods-Ordner öffnen", "Aktualisieren",
     "SuperWonders beenden", "Spieldateien bereit", "Spielcode muss neu erstellt werden", "Spieldateien nicht eingerichtet",
     "Auswählen", "Zurück", "Seiten", "Spielen", "Starten", "Zuerst einrichten", "Version", "ExeFS-Ordner auswählen",
     "RomFS-Ordner auswählen", "Diesen Ordner verwenden", "Übergeordneter Ordner", "Benutzerordner", "Laufwerke",
     "Ordner hier", "Weitere Möglichkeiten", "System-Dateiauswahl", "Abbrechen", "Erneut einrichten"},
    {"Jouer", "Fichiers du jeu", "Paramètres", "Mods", "Mises à jour", "Quitter", "SuperWonders", "À propos",
     "Fichiers du jeu", "Dossier ExeFS", "Dossier RomFS", "Choisir...", "Utiliser sur place", "Copier", "Configurer",
     "Configurer les fichiers du jeu", "Ouvrir le dossier de données", "Ouvrir les paramètres", "Dossier",
     "Ouvrir le dossier des mods", "Actualiser", "Quitter SuperWonders", "Fichiers du jeu prêts",
     "Le code du jeu doit être recompilé", "Fichiers du jeu non configurés", "Sélectionner", "Retour", "Pages", "Jouer",
     "Démarrer", "Configurer d'abord", "Version", "Choisir le dossier ExeFS", "Choisir le dossier RomFS",
     "Utiliser ce dossier", "Dossier parent", "Dossier personnel", "Lecteurs", "Dossiers ici", "Autres méthodes",
     "Sélecteur de dossiers système", "Annuler", "Reconfigurer"},
    {"Jugar", "Archivos del juego", "Ajustes", "Mods", "Actualizaciones", "Salir", "SuperWonders", "Acerca de",
     "Archivos del juego", "Carpeta ExeFS", "Carpeta RomFS", "Elegir...", "Usar en su ubicación", "Copiar", "Configurar",
     "Configurar archivos del juego", "Abrir carpeta de datos", "Abrir ajustes", "Carpeta", "Abrir carpeta de mods",
     "Actualizar", "Salir de SuperWonders", "Archivos del juego listos", "Hay que recompilar el código del juego",
     "Archivos del juego sin configurar", "Seleccionar", "Atrás", "Páginas", "Jugar", "Iniciar", "Configura primero",
     "Versión", "Elegir carpeta ExeFS", "Elegir carpeta RomFS", "Usar esta carpeta", "Carpeta superior", "Carpeta de inicio",
     "Unidades", "Carpetas aquí", "Otras opciones", "Selector de carpetas del sistema", "Cancelar", "Configurar de nuevo"},
    {"플레이", "게임 파일", "설정", "모드", "업데이트", "종료", "SuperWonders", "정보", "게임 파일", "ExeFS 폴더",
     "RomFS 폴더", "선택...", "현재 위치에서 사용", "복사", "설정", "게임 파일 설정", "데이터 폴더 열기",
     "설정 열기", "폴더", "모드 폴더 열기", "새로고침", "SuperWonders 종료", "게임 파일 준비 완료",
     "게임 코드를 다시 빌드해야 합니다", "게임 파일이 설정되지 않음", "선택", "뒤로", "페이지", "플레이", "시작",
     "먼저 설정", "버전", "ExeFS 폴더 선택", "RomFS 폴더 선택", "이 폴더 사용", "상위 폴더", "홈 폴더", "드라이브",
     "현재 폴더", "다른 방법", "시스템 폴더 선택기", "취소", "다시 설정"},
    {"プレイ", "ゲームファイル", "設定", "MOD", "更新", "終了", "SuperWonders", "概要", "ゲームファイル",
     "ExeFS フォルダー", "RomFS フォルダー", "選択...", "この場所で使う", "コピー", "セットアップ", "ゲームファイルを設定",
     "データフォルダーを開く", "設定を開く", "フォルダー", "MODフォルダーを開く", "更新", "SuperWondersを終了",
     "ゲームファイルの準備完了", "ゲームコードを再ビルドしてください", "ゲームファイル未設定", "選択", "戻る", "ページ",
     "プレイ", "開始", "先に設定", "バージョン", "ExeFSフォルダーを選択", "RomFSフォルダーを選択", "このフォルダーを使う",
     "上のフォルダー", "ホームフォルダー", "ドライブ", "この場所のフォルダー", "その他の方法", "システムのフォルダー選択",
     "キャンセル", "再設定"},
    {"تشغيل", "ملفات اللعبة", "الإعدادات", "التعديلات", "التحديثات", "خروج", "SuperWonders", "حول",
     "ملفات اللعبة", "مجلد ExeFS", "مجلد RomFS", "اختيار...", "استخدم في مكانه", "نسخ", "إعداد", "إعداد ملفات اللعبة",
     "فتح مجلد البيانات", "فتح الإعدادات", "مجلد", "فتح مجلد التعديلات", "تحديث", "إنهاء SuperWonders",
     "ملفات اللعبة جاهزة", "يجب إعادة بناء كود اللعبة", "لم يتم إعداد ملفات اللعبة", "اختيار", "رجوع", "الصفحات",
     "تشغيل", "ابدأ", "أعد الإعداد أولاً", "الإصدار", "اختر مجلد ExeFS", "اختر مجلد RomFS", "استخدم هذا المجلد",
     "المجلد الأعلى", "المجلد الرئيسي", "محركات الأقراص", "المجلدات هنا", "طرق أخرى", "منتقي مجلدات النظام", "إلغاء",
     "إعادة الإعداد"},
    {"Играть", "Файлы игры", "Настройки", "Моды", "Обновления", "Выход", "SuperWonders", "О программе",
     "Файлы игры", "Папка ExeFS", "Папка RomFS", "Выбрать...", "Использовать на месте", "Копировать", "Настройка",
     "Настроить файлы игры", "Открыть папку данных", "Открыть настройки", "Папка", "Открыть папку модов", "Обновить",
     "Выйти из SuperWonders", "Файлы игры готовы", "Нужно пересобрать код игры", "Файлы игры не настроены",
     "Выбрать", "Назад", "Страницы", "Играть", "Запустить", "Сначала настройте", "Версия", "Выберите папку ExeFS",
     "Выберите папку RomFS", "Использовать эту папку", "На уровень выше", "Домашняя папка", "Диски", "Папки здесь",
     "Другие способы", "Системный выбор папки", "Отмена", "Настроить снова"},
    {"Oyna", "Oyun dosyaları", "Ayarlar", "Modlar", "Güncellemeler", "Çıkış", "SuperWonders", "Hakkında",
     "Oyun dosyalarınız", "ExeFS klasörü", "RomFS klasörü", "Seç...", "Yerinde kullan", "Kopyala", "Kurulum",
     "Oyun dosyalarını kur", "Veri klasörünü aç", "Ayarları aç", "Klasör", "Mod klasörünü aç", "Yenile",
     "SuperWonders'tan çık", "Oyun dosyaları hazır", "Oyun kodu yeniden derlenmeli", "Oyun dosyaları kurulmadı",
     "Seç", "Geri", "Sayfalar", "Oyna", "Başlat", "Önce kur", "Sürüm", "ExeFS klasörünü seç", "RomFS klasörünü seç",
     "Bu klasörü kullan", "Üst klasör", "Ana klasör", "Sürücüler", "Buradaki klasörler", "Diğer yollar",
     "Sistem klasör seçicisi", "İptal", "Yeniden kur"},
};

const char* lt(LauncherText text) {
    const int language = overlay::menu_language();
    if (language >= 10) {
        // Translate the launcher's main navigation for the newly added locales;
        // less common setup/help strings fall back to English for now.
        static constexpr const char* navigation[3][6] = {
            {"Spelen", "Spelbestanden", "Instellingen", "Mods", "Updates", "Afsluiten"},
            {"开始游戏", "游戏文件", "设置", "模组", "更新", "退出"},
            {"開始遊戲", "遊戲檔案", "設定", "模組", "更新", "離開"},
        };
        if (text >= LtPlay && text <= LtQuit) return navigation[language - 10][text - LtPlay];
    }
    // The existing launcher help and setup text has no Dutch or Chinese translation yet.
    const int safe_language = std::min(language, 9);
    return kLauncherText[safe_language][text];
}

struct State {
    Kit kit;
    Config config;
    Status status;
    std::vector<Mod> mods;
    std::atomic<int> page{PagePlay};
    std::atomic<bool> play{false}, quit{false};
    bool config_mode = false;
    bool cache_job = false;
    bool lan_open = false, lan_request_open = false;
    int lan_mode = 0, lan_port = 27846, lan_players = 2;
    char lan_address[64] = "", lan_code[65] = "";
    std::vector<std::string> lan_addresses;
    std::string lan_error;
    Job job;
    std::mutex pick_lock;
    std::string picked_source;
    int picked_kind = 0, finding_kind = 0;
    std::future<source::Found> finding;
    source::Found found;
    bool exefs_valid = false, romfs_valid = false;
    uint64_t shown_ns = 0;
    std::string note;  // a line under the status (an error of the folder picker, a finished setup...)
    bool demo_progress = false;  // NX_LAUNCHER_TOUR's last step: the setup card, not running anything
    // The controller (Savage Wind's launcher: the page list and the page are two places the cursor can be; A or right
    // goes into the page, B back to the list, L / R change the page, + plays). One request at a time, next frame.
    bool focus_list = true;   // the cursor to the selected page's name
    int follow_hold = 0;      // frames the page doesn't follow the cursor (L / R moved it; the cursor catches up)
    bool in_list = true;      // where it is now
    uint64_t pad_before = 0;  // the pad's buttons last frame (edges)
    // The folder browser (a list a controller can walk; the system's picker needs a mouse): 0 closed, 1 for the
    // ExeFS, 2 for the RomFS. "" as the folder: the drives (Windows) or / (elsewhere).
    int browse = 0;
    std::string browse_dir;
    std::vector<std::string> browse_list;  // its subfolders, read when it changes
    std::string browse_listed;
};
State* g = nullptr;

void start_shader_optimization() {
    std::string self;
#if defined(_WIN32)
    std::wstring path(32768,L'\0');
    const auto n = GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));
    if (n && n < path.size()) { path.resize(n); self = str(fs::path(path)); }
#else
    std::error_code ec;
    const auto path = fs::read_symlink("/proc/self/exe",ec);
    if (!ec) self = str(path);
#endif
    if (self.empty()) { ui::notify("Could not locate the program to prepare the cache."); return; }
    g->cache_job = true;
    g->job.start({self,"--pack-shaders"});
}

void refresh_status() {
    g->status = check_status(g->kit);
    g->mods = list_mods();
}

// A folder for a row's value, fitted to the room the row leaves beside its label (in pixels, at the current font and
// scale): the whole path when it fits, else its last parts after "...", else the end of its last part. Measured, so
// a narrow window, a large display scale or a long path can't make the value run over the label.
std::string fit_path(const std::string& path, const char* label) {
    std::string s = path;
    for (char& c : s) c = c == '\\' ? '/' : c;
    std::string up = label;
    for (char& c : up) c = char(std::toupper(static_cast<unsigned char>(c)));
    const float room = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(up.c_str()).x -
                       4 * ImGui::GetStyle().FramePadding.x - 3 * ImGui::GetFontSize();  // (padding, the gap)
    auto fits = [&](const std::string& t) { return ImGui::CalcTextSize(t.c_str()).x <= room; };
    if (fits(s)) return s;
    for (size_t at = s.find('/'); at != std::string::npos; at = s.find('/', at + 1)) {
        const std::string t = "..." + s.substr(at);
        if (fits(t)) return t;
    }
    for (size_t cut = 1; cut < s.size(); ++cut) {  // not even the last part fits: its end
        const std::string t = "..." + s.substr(cut);
        if (fits(t)) return t;
    }
    return "...";
}

bool folder_has(const std::string& dir, const char* name) {
    std::error_code ec;
    return !dir.empty() && fs::exists(u8(dir) / name, ec);
}

void validate_sources() {
    g->exefs_valid = !g->config.exefs.empty() && source::exefs_ok(u8(g->config.exefs));
    g->romfs_valid = !g->config.romfs.empty() && source::romfs_ok(u8(g->config.romfs));
}

void find_sources(std::string folder, int kind) {
    if (folder.empty() || g->finding.valid()) return;
    g->finding_kind = kind;
    g->note = "Looking for main + sdk and Shader/*.zs inside the selected folder...";
    g->finding = std::async(std::launch::async, [folder] { return source::discover(u8(folder)); });
}

void start_setup() {
    if (const auto* issue = setup_tools_issue(g->kit)) {
        g->note = issue;
        return;
    }
    validate_sources();
    if (!g->exefs_valid || !g->romfs_valid) {
        g->note = !g->exefs_valid ? "ExeFS needs the files main and sdk. Choose your extracted game folder."
                                 : "RomFS needs .zs files directly inside Shader. Choose the extracted folder containing Shader; playing first is not required.";
        return;
    }
    save_config(g->config);
    g->job.start({g->kit.python, "-u", g->kit.script, "setup", "--exefs", g->config.exefs, "--romfs", g->config.romfs,
                  "--data-root", plat::data_root(), "--romfs-mode", g->config.romfs_copy ? "copy" : "link"});
    log("launcher: setup started (ExeFS %s, RomFS %s, %s)", g->config.exefs.c_str(), g->config.romfs.c_str(),
        g->config.romfs_copy ? "copy" : "link");
}

// ---- drawing ----
float g_k = 1;

void backdrop(ImDrawList* dl, ImVec2 ds) {
    const ui::Theme& th = ui::theme();
    dl->AddRectFilledMultiColor({0, 0}, {ds.x, ds.y * 0.5f}, th.back_top, th.back_top, th.back_mid, th.back_mid);
    dl->AddRectFilledMultiColor({0, ds.y * 0.5f}, ds, th.back_mid, th.back_mid, th.back_top, th.back_top);
    // a slow glow drifting behind the pages (the compile screen's, overlay_progress.cpp)
    const float t = float(ImGui::GetTime());
    const float gx = ds.x * (0.62f + 0.18f * std::sin(t * 0.3f)), gy = ds.y * 0.42f, r = ds.y * 0.6f;
    const ImU32 in = (th.accent2 & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 30), out = in & ~IM_COL32_A_MASK;
    constexpr int kSeg = 64;
    dl->PrimReserve(kSeg * 3, kSeg + 1);
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    const ImDrawIdx base = ImDrawIdx(dl->_VtxCurrentIdx);
    dl->PrimWriteVtx({gx, gy}, uv, in);
    for (int i = 0; i < kSeg; ++i) {
        const float a = float(i) / kSeg * 6.2831853f;
        dl->PrimWriteVtx({gx + std::cos(a) * r, gy + std::sin(a) * r}, uv, out);
    }
    for (int i = 0; i < kSeg; ++i) {
        dl->PrimWriteIdx(base);
        dl->PrimWriteIdx(ImDrawIdx(base + 1 + i));
        dl->PrimWriteIdx(ImDrawIdx(base + 1 + (i + 1) % kSeg));
    }
}

// The setup's progress: a card with the gliding bar and its sheen (the compile screen's look), in `area`.
void progress_card(ImDrawList* dl, ImVec2 p0, ImVec2 p1) {
    const ui::Theme& th = ui::theme();
    const float k = g_k;
    static float shown = 0;
    const float frac = g->job.progress;
    shown = g->job.running ? ui::approach(shown, frac, 5.0f) : frac;
    std::string what;
    {
        std::lock_guard lk(g->job.lock);
        what = g->job.what;
    }
    ImFont* tf = ui::title();
    ImFont* bf = ui::body();
    const float tfs = 46 * k, sfs = 26 * k;
    dl->AddRectFilled(p0, p1, th.card, 14 * k);
    dl->AddRect(p0, p1, ui::fade(th.accent, 0.55f), 14 * k, 0, 1.5f * k);
    const char* title = g->cache_job ? "Optimizing shader loading" : "Setting up your game";
    const ImVec2 ts = tf->CalcTextSizeA(tfs, FLT_MAX, 0, title);
    dl->AddText(tf, tfs, {(p0.x + p1.x - ts.x) * 0.5f, p0.y + 36 * k}, th.accent, title);
    const ImVec2 ba(p0.x + 48 * k, p0.y + 36 * k + ts.y + 30 * k), bb(p1.x - 48 * k, ba.y + 24 * k);
    ui::progress_bar(dl, ba, bb, shown);
    char line[640];
    const double secs = double(plat::monotonic_ns() - g->job.started_ns) / 1e9;
    std::snprintf(line, sizeof line, "%s   %d%%   %d:%02d", what.c_str(), int(shown * 100 + 0.5f), int(secs) / 60,
                  int(secs) % 60);
    const ImVec2 ls = bf->CalcTextSizeA(sfs, FLT_MAX, 0, line);
    dl->AddText(bf, sfs, {(p0.x + p1.x - ls.x) * 0.5f, bb.y + 26 * k}, th.text_dim, line);
    const char* note = "Overall setup progress. A single stage can take several minutes; elapsed time continues above.";
    const ImVec2 ns = bf->CalcTextSizeA(sfs * 0.9f, FLT_MAX, 0, note);
    dl->AddText(bf, sfs * 0.9f, {(p0.x + p1.x - ns.x) * 0.5f, bb.y + 26 * k + ls.y + 16 * k},
                ui::mix(th.text_off, th.text_dim, 0.4f), note);
}

void open_lan() {
    g->lan_open=true; g->lan_request_open=true; g->lan_error.clear();
    try {
        g->lan_addresses=lan::local_addresses();
        if(!g->lan_code[0]) {
            if(std::getenv("NX_LAN_LOBBY_TOUR")) std::snprintf(g->lan_code,sizeof g->lan_code,"LOCALPREVIEW");
            else {
                std::random_device random;
                std::snprintf(g->lan_code,sizeof g->lan_code,"%08x%08x",unsigned(random()),unsigned(random()));
            }
        }
    } catch(const std::exception& e) { g->lan_error=e.what(); }
}

void draw_lan(float w,float h,float k) {
    if(g->lan_request_open) { ImGui::OpenPopup("LAN co-op"); g->lan_request_open=false; }
    ImGui::SetNextWindowSize({800*k,0},ImGuiCond_Appearing);
    ImGui::SetNextWindowPos({w*0.5f,h*0.5f},ImGuiCond_Appearing,{0.5f,0.5f});
    ImGui::SetNextWindowSizeConstraints({800*k,0},{800*k,h-64*k});
    if(!ImGui::BeginPopupModal("LAN co-op",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) return;
    const auto state=lan::lobby_status();
    const bool waiting=state.phase==lan::LobbyPhase::Preparing || state.phase==lan::LobbyPhase::Waiting;
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::theme().accent),"Native co-op  |  Experimental");
    ImGui::TextWrapped("Each PC runs Wonder and controls its own co-op character. Use the same game build and mods on every PC.");
    ImGui::TextWrapped("Synchronization is still being tested. A detected position mismatch stops the session.");
    ImGui::Separator();
    ImGui::BeginDisabled(waiting || state.phase==lan::LobbyPhase::Ready);
    ImGui::RadioButton("Host a room",&g->lan_mode,0); ImGui::SameLine();
    ImGui::RadioButton("Join a room",&g->lan_mode,1);
    if(g->lan_mode==0) {
        std::string addresses;
        for(const auto& ip:g->lan_addresses) { if(!addresses.empty()) addresses+=", "; addresses+=ip; }
        ImGui::TextWrapped("Your LAN address: %s",addresses.empty()?"No private IPv4 address found":addresses.c_str());
        ImGui::SetNextItemWidth(180*k); ImGui::InputInt("Players",&g->lan_players);
        g->lan_players=std::clamp(g->lan_players,2,4);
    } else {
        ImGui::SetNextItemWidth(420*k); ImGui::InputText("Host address",g->lan_address,sizeof g->lan_address);
    }
    ImGui::SetNextItemWidth(270*k); ImGui::InputInt("Port",&g->lan_port);
    g->lan_port=std::clamp(g->lan_port,1,65535);
    ImGui::SetNextItemWidth(420*k); ImGui::InputText("Pairing code",g->lan_code,sizeof g->lan_code);
    if(g->lan_mode==0 && ImGui::Button("Copy pairing code")) ImGui::SetClipboardText(g->lan_code);
    ImGui::TextWrapped("Share the address, port and exact pairing code with the other players privately.");
    ImGui::Separator();
    ImGui::TextWrapped("Host progress is saved normally. Joining uses a separate session copy of the host's save and preserves your own save.");
    ImGui::TextWrapped("The game starts when all players connect. Each PC uses its primary gamepad or keyboard. Choose the matching player count in Wonder.");
    const bool valid=std::strlen(g->lan_code)>=4 && (g->lan_mode==0 || g->lan_address[0]);
    if(!valid) ImGui::BeginDisabled();
    if(ImGui::Button(g->lan_mode==0?"Open room":"Connect",{220*k,0})) {
        g->lan_error.clear();
        lan::start_lobby({g->lan_mode==0,g->lan_mode==0?"0.0.0.0":g->lan_address,g->lan_code,
            uint16_t(g->lan_port),uint8_t(g->lan_players)});
    }
    if(!valid) ImGui::EndDisabled();
    ImGui::EndDisabled();
    if(!state.detail.empty()) ImGui::TextWrapped("%s",state.detail.c_str());
    if(!g->lan_error.empty()) ImGui::TextWrapped("%s",g->lan_error.c_str());
    if(waiting) {
        ImGui::TextWrapped("Waiting for up to three minutes. You can cancel and create another room.");
        if(ImGui::Button("Cancel connection")) lan::cancel_lobby();
    } else if(state.phase==lan::LobbyPhase::Ready) {
        g->play=true; g->lan_open=false; ImGui::CloseCurrentPopup();
    } else if(ImGui::Button("Back to launcher")) {
        g->lan_open=false; ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void page_play() {
    rows::begin("##play", ImGui::GetContentRegionAvail().y);
    rows::section(lt(LtSuperWonders));
    const bool ready = g->status.ready == Ready::Yes;
    if (!ready) rows::disable_next();
    if (rows::action(lt(LtPlay), ready ? lt(LtStart) : lt(LtSetUpFirst),
                     ready ? "Start Super Mario Bros. Wonder." : "Your game files aren't set up yet: open Game files.",
                     true, ready))
        g->play = true;
    if (!ready && rows::action(lt(LtSetUpGameFiles), "", "Pick your ExeFS and RomFS folders and set the game up."))
        g->page = PageFiles, rows::focus_first();
    rows::section("Before you play");
    if (rows::action("Graphics & performance", "Resolution & scaling", "Choose a graphics preset, resolution, scaling and frame pacing."))
        overlay::open_settings(0);
    if (rows::action("Controller setup", "Bindings & rumble", "Configure your controller before starting. Settings are shared with the in-game F1 menu."))
        overlay::open_settings(2);
    if (rows::action("Performance overlay", "FPS & frame times", "Show FPS, 1% lows, game speed and the frame-time graph."))
        overlay::open_settings(3);
    rows::section("Play together");
    if (rows::action("Local co-op", "2-4 players", "Connect independent controllers, or reserve the keyboard for player 1 in Controls. Choose the player count in the game for real local co-op characters."))
        overlay::open_settings(2);
    if (!ready) rows::disable_next();
    if (rows::action("LAN co-op", "Host or join | Experimental", "Run a native Wonder instance on each PC, with independent co-op characters. Pair on a private network. Joining preserves your normal save. Synchronization is still being tested."))
        open_lan();
    rows::section(lt(LtAbout));
    rows::action(lt(LtVersion), NX_VERSION_STRING,
                 "SuperWonders, a static recompilation of Super Mario Bros. Wonder (1.0.0) for PC. Nothing of the game "
                 "ships with it: it's set up from your own copy.",
                 true);
    rows::end();
}

// ---- the folder browser ----
std::string home_folder() {
    const char* h = std::getenv(
#if defined(_WIN32)
        "USERPROFILE"
#else
        "HOME"
#endif
    );
    return h && *h ? h : "";
}

void browse_open(int which) {
    const std::string& now = which == 2 ? g->config.romfs : g->config.exefs;
    std::error_code ec;
    std::string start = !now.empty() && fs::is_directory(u8(now), ec) ? str(u8(now).parent_path()) : home_folder();
    g->browse = which, g->browse_dir = start, g->browse_listed = "\x01";
    rows::focus_first();
    log("launcher: folder browser for the %s, from %s", which == 1 ? "ExeFS" : "RomFS", start.c_str());
}

void browse_close() {
    g->browse = 0;
    rows::focus_first();
}

void browse_read() {
    if (g->browse_listed == g->browse_dir) return;
    g->browse_listed = g->browse_dir;
    g->browse_list.clear();
    if (g->browse_dir.empty()) {  // the top: the drives, or the root
#if defined(_WIN32)
        const DWORD drives = GetLogicalDrives();
        for (int d = 0; d < 26; ++d)
            if (drives & (1u << d)) g->browse_list.push_back(std::string(1, char('A' + d)) + ":/");
#else
        g->browse_list.push_back("/");
#endif
        return;
    }
    std::error_code ec;
    for (auto it = fs::directory_iterator(u8(g->browse_dir), fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code e2;
        if (!it->is_directory(e2)) continue;
        const std::string n = str(it->path().filename());
        if (n.empty() || n[0] == '.' || n[0] == '$') continue;  // (hidden and system folders)
        g->browse_list.push_back(n);
        if (g->browse_list.size() >= 2000) break;
    }
    std::sort(g->browse_list.begin(), g->browse_list.end(), [](const std::string& a, const std::string& b) {
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
            const int x = std::tolower(static_cast<unsigned char>(a[i])), y = std::tolower(static_cast<unsigned char>(b[i]));
            if (x != y) return x < y;
        }
        return a.size() < b.size();
    });
}

void page_browse() {
    browse_read();
    const bool exefs = g->browse == 1;
    const std::string& dir = g->browse_dir;
    rows::begin("##browse", ImGui::GetContentRegionAvail().y);
    rows::section(g->browse == 3 ? "Choose your extracted game folder" : exefs ? lt(LtChooseExeFS) : lt(LtChooseRomFS));
    const bool ok = !dir.empty();
    const std::string where = dir.empty() ? std::string("This PC") : fit_path(dir, lt(LtUseFolder));
    const std::string use_desc = std::string("Search this folder and its subfolders for the extracted game files. You can select their parent folder.") +
                                 (dir.empty() ? std::string() : "\n\n" + dir);
    if (!ok) rows::disable_next();
    if (rows::action(lt(LtUseFolder), where.c_str(), use_desc.c_str(), true, ok)) {
        find_sources(dir, g->browse);
        browse_close();
        rows::end();
        return;
    }
    if (!dir.empty() && rows::action(lt(LtUpFolder), "", "The folder above this one.", true)) {
        const fs::path p = u8(dir);
        g->browse_dir = p.has_parent_path() && p.parent_path() != p ? str(p.parent_path()) : "";
        if (g->browse_dir == dir) g->browse_dir.clear();  // (a drive's root: up to the drives)
        rows::focus_first();
    }
    if (rows::action(lt(LtHomeFolder), "", "Your home folder.", true)) g->browse_dir = home_folder(), rows::focus_first();
    rows::section(dir.empty() ? lt(LtDrives) : lt(LtFoldersHere));
    if (g->browse_list.empty()) rows::action("(no folders here)", "", "This folder has no folders in it.", false);
    std::string go;
    for (const std::string& n : g->browse_list)
        if (rows::action(n.c_str(), "", "Open this folder.", false)) go = n;
    if (!go.empty()) {
        g->browse_dir = dir.empty() ? go : str(u8(dir) / u8(go));
        rows::focus_first();
    }
    rows::section(lt(LtOtherWays));
    if (rows::action(lt(LtSystemPicker), "", "Choose the folder in the system's own window (a mouse helps).",
                     true))
        plat::pick_folder("Your extracted game folder (ExeFS and RomFS inside)", dir, [kind=g->browse](std::string p) {
            std::lock_guard lk(g->pick_lock);
            g->picked_source = p; g->picked_kind = kind;
        });
    if (rows::action(lt(LtCancel), "", "Back, without changing the folder (B).", true)) browse_close();
    rows::end();
}

void page_files() {
    if (g->browse) return page_browse();
    rows::begin("##files", ImGui::GetContentRegionAvail().y);
    const char* kit_issue = setup_tools_issue(g->kit);
    if (kit_issue) rows::page_note(kit_issue);
    rows::section("Easy setup");
    const bool finding = g->finding.valid();
    if (finding) rows::disable_next();
    if (rows::action("Choose game folder", finding ? "Looking for files..." : "Find ExeFS + RomFS",
                     "Choose the extracted folder containing ExeFS and RomFS. Nested folders are detected automatically. This reads your files without moving or changing them.", true, true))
        plat::pick_folder("Choose your extracted Wonder folder", home_folder(), [](std::string p) {
            std::lock_guard lk(g->pick_lock); g->picked_source = p; g->picked_kind = 3;
        });
    if (finding) rows::disable_next();
    if (rows::action("Browse with controller", "", "Find the same folder using the controller-friendly folder list.")) browse_open(3);
    for (int kind : {1, 2}) {
        const auto& choices = kind == 1 ? g->found.exefs : g->found.romfs;
        if (choices.empty() || (choices.size() < 2 && !g->found.limited)) continue;
        rows::section(kind == 1 ? "Multiple ExeFS folders: choose one" : "Multiple RomFS folders: choose one");
        for (const auto& path : choices) {
            ImGui::PushID(path.c_str());
            const std::string label = fit_path(path, "Select");
            const std::string help = "Choose the matching base game 1.0.0 folders.\n\n" + path;
            if (rows::action(label.c_str(), "Select", help.c_str())) {
                (kind == 1 ? g->config.exefs : g->config.romfs) = path;
                validate_sources(); save_config(g->config);
            }
            ImGui::PopID();
        }
    }
    rows::section(lt(LtYourGameFiles));
    const std::string exefs = g->config.exefs.empty() ? lt(LtChoose) : fit_path(g->config.exefs, lt(LtExeFS));
    const std::string exefs_desc =
        "The ExeFS folder of your own copy of the game (version 1.0.0). It holds main and sdk." +
        (g->config.exefs.empty() ? std::string() : "\n\nNow: " + g->config.exefs);
    if (finding) rows::disable_next();
    if (rows::action(lt(LtExeFS), exefs.c_str(), exefs_desc.c_str(), true)) browse_open(1);
    const std::string romfs = g->config.romfs.empty() ? lt(LtChoose) : fit_path(g->config.romfs, lt(LtRomFSFolder));
    const std::string romfs_desc = "The RomFS folder of your own copy of the game." +
                                   (g->config.romfs.empty() ? std::string() : "\n\nNow: " + g->config.romfs);
    if (finding) rows::disable_next();
    if (rows::action(lt(LtRomFSFolder), romfs.c_str(), romfs_desc.c_str(), true)) browse_open(2);
    const std::string checked = std::string("ExeFS: ") + (g->exefs_valid ? "main + sdk found" : "main + sdk needed") +
        "\nRomFS: " + (g->romfs_valid ? "Shader/*.zs found" : "Shader/*.zs needed") +
        "\n\nThe build tools verify the game version during setup. Extracting requires no prior game launch.";
    rows::action("File check", g->exefs_valid && g->romfs_valid ? "Ready to set up" : g->exefs_valid ? "RomFS needed" : g->romfs_valid ? "ExeFS needed" : "Files needed",
        checked.c_str(), true);
    const char* const modes[] = {lt(LtUseInPlace), lt(LtCopyIt)};
    // Both RomFS rows share a translated label, so give the storage mode its own ID scope.
    ImGui::PushID("romfs-storage-mode");
    rows::choice(lt(LtRomFSFolder), &g->config.romfs_copy, modes, 2, 1,
                 "Copy it: SuperWonders keeps its own copy (about 3.6 GB), so it keeps working if you move or delete "
                 "yours. Use it in place: no copy, but SuperWonders needs your RomFS folder where it is.");
    ImGui::PopID();
    rows::section(lt(LtSetUp));
    const bool exefs_ok = g->exefs_valid;
    const bool romfs_ok = g->romfs_valid;
    const char* why = kit_issue ? kit_issue : !exefs_ok ? "Choose your ExeFS folder first (it holds main and sdk)."
                      : !romfs_ok ? "RomFS needs Shader/*.zs. Choose the folder containing Shader, or its parent extraction folder."
                                  : "Reads your game files and builds the game code and the shaders from them, in this "
                                    "PC's SuperWonders data folder. Steps that are already done are skipped.";
    if (finding || kit_issue || !exefs_ok || !romfs_ok) rows::disable_next();
    if (rows::action(g->status.ready == Ready::Yes ? lt(LtSetUpAgain) : lt(LtSetUpGameFiles), "", why, true,
                     !kit_issue && exefs_ok && romfs_ok && g->status.ready != Ready::Yes))
        start_setup();
    if (kit_issue) {
        if (rows::action("Get complete setup package", "Open release page",
                         "Open the compatible v0.4.6 release page. Download and extract the complete package for your "
                         "platform. Keep this fork's runtime update for the prepared installation afterward.", true))
            plat::open_folder("https://github.com/vfjoo888/SuperWonders/releases/tag/v0.4.6");
        if (rows::action("Recheck setup tools", "",
                         "After restoring game-build beside the launcher, check again without restarting.", true)) {
            g->kit = find_kit();
            const char* remaining = setup_tools_issue(g->kit);
            g->note = remaining ? remaining : "Setup tools found. Choose your ExeFS and RomFS folders to continue.";
        }
    }
    if (rows::action(lt(LtOpenDataFolder), "", "Where SuperWonders keeps your saves, settings and the game it built.",
                     true))
        plat::open_folder(plat::data_root());
    rows::end();
}

void page_settings() {
    rows::begin("##settings", ImGui::GetContentRegionAvail().y);
    rows::section(lt(LtSettings));
    const char* aspect_names[] = {"16:9 (original)", "21:9", "32:9"};
    const bool italian = overlay::menu_language() == 1;
    const char* aspect_desc = italian ? "Si applica al prossimo avvio del gioco." : "Takes effect the next time the game starts.";
    int aspect = overlay::aspect_ratio();
    if (rows::choice(italian ? "Formato schermo" : "Aspect ratio", &aspect, aspect_names, 3, 0, aspect_desc))
        overlay::set_aspect_ratio(aspect);

    rows::section("Quick graphics presets");
    const char* names[] = {"Performance", "Balanced", "Quality"};
    const char* values[] = {"720p / 60 FPS", "900p / 60 FPS", "1080p / 60 FPS"};
    const char* descs[] = {
        "Lower internal resolution and compressed textures for older GPUs. FSR 1 scales the picture to your display. Changes apply when you start the game.",
        "900p rendering, FSR 1, FXAA and 4x texture filtering. Keeps all draws visible. A 60 FPS target is a cap, not a performance guarantee.",
        "1080p rendering, FXAA and 8x texture filtering. Needs more GPU memory and processing power. Changes apply when you start the game."};
    for (int i = 0; i < 3; ++i)
        if (rows::action(names[i], values[i], descs[i])) {
            auto s = overlay::current_settings();
            overlay::apply_graphics_profile(s, static_cast<overlay::GraphicsProfile>(i));
            overlay::apply_settings(s);
            ui::notify("Graphics preset saved. Start or restart the game to apply all changes.");
        }
    rows::section("Make it yours");
    const char* sections[] = {"Video & graphics", "Audio & language", "Controllers & keyboard", "FPS & performance", "System & memory"};
    const char* descriptions[] = {
        "Resolution, fullscreen, upscaling, image quality and frame pacing.",
        "Adjust the volume and choose the game's language.",
        "Button layouts, controller bindings, keyboard bindings and vibration.",
        "Choose exactly which performance numbers you want on screen.",
        "Menu language, memory settings and texture memory options."};
    for (int i = 0; i < 5; ++i)
        if (rows::action(sections[i], "Open", descriptions[i])) overlay::open_settings(i);
    rows::section("Loading");
    if (rows::action("Optimize shader loading", "Prepare local cache", "Prepare or rebuild the cache used by Fast shader loading. This can reduce file-opening delays on future launches and uses extra disk space and memory. Original shaders and saves are preserved."))
        start_shader_optimization();
    rows::section("Your saves");
    if (rows::action("Back up saves", "Create a copy", "Copy your saves into a new folder under data/backups. Existing backups and active saves are preserved. The completed backup folder opens when the copy finishes.")) {
        std::string error;
        const auto backup = backup_saves(u8(plat::data_root()), error);
        if (backup.empty()) ui::notify(error.c_str());
        else { ui::notify("Save backup created."); plat::open_folder(str(backup)); }
    }
    std::error_code backup_error;
    if (!fs::is_directory(u8(plat::data_root()) / "backups", backup_error)) rows::disable_next();
    if (rows::action("Open save backups", "Folder", "Browse your saved copies. Close the game before manually restoring a backup."))
        plat::open_folder(plat::data_root() + "/backups");
    rows::page_note("These are the same settings used in-game. Open them any time with Esc, F1 or Minus + Plus.");
    rows::end();
}

void page_mods() {
    rows::begin("##mods", ImGui::GetContentRegionAvail().y);
    rows::section(lt(LtMods));
    if (g->mods.empty())
        rows::action("No mods yet", "", "Put each mod in a folder of its own in the mods folder. A mod holds a romfs "
                                        "folder (romfs/, or contents/<title id>/romfs/ as Switch mods come).",
                     true);
    uint64_t enabled = 0, conflicts = 0;
    for (const auto& m : g->mods) if (m.on) { ++enabled; conflicts += m.shadowed; }
    const std::string summary = std::to_string(enabled)+" enabled | "+std::to_string(conflicts)+" shadowed files";
    rows::action("Load order",summary.c_str(),"Mods load in name order. The first enabled mod supplying a path wins. Shadowed files in later mods are ignored. Changes apply after restarting the game. LAN peers need identical enabled mods.",true);
    bool rescan = false;
    for (Mod& m : g->mods) {
        bool on = m.on;
        std::string details = std::to_string(m.files)+" files, "+std::to_string(m.bytes/1024)+" KiB. ";
        details += m.issue.empty() ? "RomFS layout detected. " : m.issue+" ";
        if (m.shadowed) details += std::to_string(m.shadowed)+" files are overridden by earlier mods. ";
        for (const auto& conflict : m.conflicts) details += "\n"+conflict;
        details += "\nRestart after changes. Custom code patches outside RomFS are not installed by this manager.";
        const auto phase = lan::lobby_status().phase;
        const bool paired = lan::active() || phase == lan::LobbyPhase::Preparing || phase == lan::LobbyPhase::Waiting || phase == lan::LobbyPhase::Ready;
        if (paired) rows::disable_next();
        const std::string toggle_label = m.name+"###mod-"+(m.on ? "on-" : "off-")+m.name;
        if (rows::toggle(toggle_label.c_str(), &on, true,details.c_str())) {
            if (set_mod(m,on)) {
                rescan = true;
                ui::notify("Mod selection saved. Restart the game to apply it.");
            } else ui::notify("Could not change this mod. Check for a duplicate enabled/disabled folder.");
        }
        std::string label = m.name+" details###mod-details-"+(m.on ? "on-" : "off-")+m.name;
        std::string state = !m.issue.empty() ? "Check folder" : m.shadowed ? "File conflicts" : std::to_string(m.files)+" files";
        rows::action(label.c_str(),state.c_str(),details.c_str(),true);
    }
    if (rescan) g->mods = list_mods();
    rows::section(lt(LtFolder));
    if (rows::action(lt(LtOpenModsFolder), "", "Add, remove or rename mods there.", true))
        plat::open_folder(plat::data_root() + "/mods");
    if (rows::action(lt(LtRefresh), "", "Read the mods folder again.", true)) g->mods = list_mods();
    rows::end();
}

void page_quit() {
    rows::begin("##quit", ImGui::GetContentRegionAvail().y);
    rows::section(lt(LtQuit));
    if (rows::action(lt(LtQuitSuperWonders), "", "Close the launcher.", true)) g->quit = true;
    rows::end();
}

void screen(float w, float h) {
    if (!g) return;
    if (g->follow_hold > 0) g->follow_hold--;
    g_k = std::clamp(h / 1080.0f, 0.5f, 3.0f);
    const float k = g_k;
    const ui::Theme& th = ui::theme();
    {
        std::lock_guard lk(g->pick_lock);  // folders the picker returned
        if (!g->picked_source.empty()) {
            g->browse = 0;
            find_sources(std::move(g->picked_source), g->picked_kind);
            g->picked_source.clear();
        }
    }
    if (g->finding.valid() && g->finding.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            g->found = g->finding.get();
            if (g->finding_kind != 2) g->config.exefs = !g->found.limited && g->found.exefs.size() == 1 ? g->found.exefs[0] : "";
            if (g->finding_kind != 1) g->config.romfs = !g->found.limited && g->found.romfs.size() == 1 ? g->found.romfs[0] : "";
            validate_sources(); save_config(g->config);
            g->note = g->exefs_valid && g->romfs_valid ? "Game folders found. Select Set up game files to continue."
                : "Some files are missing, or several extractions were found. Choose the matching folders below. RomFS needs Shader/*.zs.";
            if (g->found.limited) g->note += " Search limit reached; choose a folder closer to your extraction.";
        } catch (...) { g->note = "Could not read that folder. Choose a local extracted game folder and try again."; }
    }
    static bool was_running = false;
    if (was_running && !g->job.running) {  // a setup finished
        refresh_status();
        const int r = g->job.result;
        if (g->cache_job) {
            g->note = r == 0 ? "Shader loading cache is ready." : g->job.was_cancelled() ? "Cache preparation cancelled." : "Cache preparation failed; original shaders are preserved.";
            log("launcher: shader cache preparation exit %d",r);
            g->cache_job = false;
        } else {
            g->note = r == 0 ? "Your game is set up." : g->job.was_cancelled() ? "Setup cancelled." : "Setup failed.";
            log("launcher: setup %s (exit %d)", r == 0 ? "finished" : "failed", r);
            if (r == 0 && !g->config_mode) g->play = true;
            if (r == 0) g->page = PagePlay, rows::focus_first();
        }
    }
    was_running = g->job.running;

    const ImVec2 ds{w, h};
    backdrop(ImGui::GetBackgroundDrawList(), ds);
    // fades and rises in, as the menu does
    const float reveal = ui::smoothstep(std::min(1.0f, float(double(plat::monotonic_ns() - g->shown_ns) / 0.35e9)));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, reveal);
    ImGui::SetNextWindowPos({0, (1 - reveal) * 20 * k});
    ImGui::SetNextWindowSize(ds);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus;
    // The launcher has the controller whenever the settings menu is closed (it closes: back where it was).
    static bool menu_was_open = false;
    const bool menu_open = overlay::is_open();
    if (!menu_open && (menu_was_open || !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))) ImGui::SetNextWindowFocus();
    if (menu_was_open && !menu_open) {
        if (g->in_list) g->focus_list = true;
        else rows::focus_first();
    }
    menu_was_open = menu_open;
    ImGui::Begin("##launcher", nullptr, f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float margin = 56 * k, nav_w = std::max(300 * k, w * 0.24f);

    // the title
    ImGui::SetCursorPos({margin, margin * 0.8f});
    ImGui::PushFont(ui::title());
    ImGui::SetWindowFontScale(1.25f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(th.accent));
    ImGui::TextUnformatted("SUPERWONDERS");
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();
    ImGui::SetCursorPosX(margin);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(th.text_dim));
    ImGui::TextUnformatted(g->config_mode ? "Super Mario Bros. Wonder  -  Configuration" : "Super Mario Bros. Wonder");
    ImGui::PopStyleColor();
    const float top = ImGui::GetCursorPosY() + 40 * k;

    // the pages: a column on the left, the highlight easing in under the selected one
    const bool busy = g->job.running || g->demo_progress;
    ImGui::SetCursorPos({margin, top});
    ImGui::BeginGroup();
    bool list_focused = false;
    for (int i = 0; i < kPages; ++i) {
        ImGui::PushID(i);
        const bool sel = g->page == i;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float row_h = 64 * k;
        const float a = ui::eased(ImGui::GetID("hl"), sel ? 1.0f : 0.0f, 14.0f);
        if (a > 0.01f) {
            dl->AddRectFilled(at, {at.x + nav_w, at.y + row_h}, ui::fade(th.accent, 0.16f * a), 10 * k);
            dl->AddRectFilled(at, {at.x + 5 * k, at.y + row_h}, ui::fade(th.accent, a), 3 * k);
        }
        // The selected page may change during navigation below. Balance this
        // scope using the entry snapshot, never a second read of the page.
        const bool row_disabled = busy && !sel;
        ImGui::BeginDisabled(row_disabled);
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ui::fade(th.accent, 0.10f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ui::fade(th.accent, 0.20f));
        ImGui::PushFont(ui::title());
        ImGui::SetWindowFontScale(0.62f);
        char label[64];
        std::snprintf(label, sizeof label, "   %s", lt(static_cast<LauncherText>(LtPlay + i)));
        for (char* c = label; *c; ++c) *c = char(std::toupper(static_cast<unsigned char>(*c)));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(sel ? th.text : th.text_dim));
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));  // (in the middle of the highlight)
        if (sel && g->focus_list && !menu_open) ImGui::SetKeyboardFocusHere(), g->focus_list = false;
        const bool clicked = ImGui::Selectable(label, sel, ImGuiSelectableFlags_None, {nav_w, row_h});
        ImGui::PopStyleVar();
        const bool focused = ImGui::IsItemFocused();
        list_focused |= focused;
        if (focused && !row_disabled && i != g->page && !g->follow_hold)  // (the page follows the cursor)
            g->page = i, g->browse = 0, log("launcher: page %s", kPageNames[i]);
        if (clicked && !row_disabled) {  // A (or a click): into the page
            if (i != g->page) g->page = i, g->browse = 0, log("launcher: page %s", kPageNames[i]);
            rows::focus_first();
        }
        if (focused && !menu_open) {  // the cursor, always visible: an outline on the page's name
            const ImVec2 b0 = ImGui::GetItemRectMin(), b1 = ImGui::GetItemRectMax();
            dl->AddRect(b0, b1, ui::fade(th.accent, 0.9f), 10 * k, 0, 2.5f * k);
        }
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopFont();
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::EndGroup();
    g->in_list = list_focused;

    // the status, lower left
    {
        const Ready r = g->status.ready;
        const ImU32 c = r == Ready::Yes ? th.good : r == Ready::Rebuild ? th.warn : th.bad;
        const char* s = r == Ready::Yes ? lt(LtFilesReady) : r == Ready::Rebuild ? lt(LtNeedsRebuild) : lt(LtFilesNotSetUp);
        const float y = h - margin - 80 * k;
        dl->AddCircleFilled({margin + 8 * k, y + 15 * k}, 7 * k, c);
        ImGui::SetCursorPos({margin + 26 * k, y});
        ImGui::TextUnformatted(s);
        ImGui::SetCursorPosX(margin + 26 * k);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(th.text_off));
        ImGui::PushTextWrapPos(margin + nav_w);
        ImGui::TextUnformatted(!g->note.empty() ? g->note.c_str() : g->status.why.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    // the page, on the right
    const float px = margin + nav_w + 48 * k, pw = w - px - margin, ph = h - top - margin - 70 * k;
    ImGui::SetCursorPos({px, top});
    if (busy || (g->job.result > 0 && !g->job.was_cancelled() && g->page == PageFiles)) {
        ImGui::BeginChild("##job", {pw, ph}, ImGuiChildFlags_NavFlattened);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float card_h = 300 * k;
        if (busy) {
            progress_card(dl, p0, {p0.x + pw, p0.y + card_h});
            ImGui::SetCursorPosY(card_h + 30 * k);
            rows::begin("##cancel", ph - card_h - 30 * k);
            if (rows::action("Cancel", "", "Stop the setup. What it finished is kept, and it carries on from there "
                                           "next time.", true))
                g->job.cancel();
            rows::end();
        } else {  // the last setup failed: what it said
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(th.bad));
            ImGui::TextUnformatted("The setup stopped with an error:");
            ImGui::PopStyleColor();
            std::vector<std::string> tail;
            {
                std::lock_guard lk(g->job.lock);
                tail = g->job.tail;
            }
            ImGui::PushTextWrapPos(pw);
            for (const std::string& l : tail) ImGui::TextUnformatted(l.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Dummy({0, 20 * k});
            rows::begin("##failed", 200 * k);
            if (rows::action("Back", "", "Check the folders and try again.", true)) g->job.result = -1;
            rows::end();
        }
        ImGui::EndChild();
    } else {
        ImGui::BeginChild("##page", {pw, ph}, ImGuiChildFlags_NavFlattened);
        switch (g->page.load()) {
            case PagePlay: page_play(); break;
            case PageFiles: page_files(); break;
            case PageSettings: page_settings(); break;
            case PageMods: page_mods(); break;
            case PageUpdates: page_updates(g_k); break;
            default: page_quit(); break;
        }
        ImGui::EndChild();
    }

    // the controls, along the bottom
    const ui::Hint hints[] = {{"A", lt(LtSelect)}, {"B", lt(LtBack)}, {"L", ""}, {"R", lt(LtPages)}, {"+", lt(LtPlayHint)}};
    ui::hints(dl, w * 0.5f, h - margin * 0.7f, 34 * k, hints, 5);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    draw_lan(w,h,k);

    // The controller, by the player's own bindings (the pad as the overlay saw it: NpadButton bits), and the keys.
    const uint64_t pad = overlay::pad_buttons(), pressed = pad & ~g->pad_before;
    g->pad_before = pad;
    if (menu_open || g->lan_open) return;
    const bool right = ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, false) ||
                       ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight, false) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, false);
    const bool back = (pressed & plat::kB) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                      ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
    if (list_focused && right) rows::focus_first();  // right: into the page
    else if (!list_focused && back) {                // B: the browser closes, else back to the page list
        if (g->browse) browse_close();
        else g->focus_list = true;
    }
    if (!busy && ((pressed & (plat::kL | plat::kR)) || ImGui::IsKeyPressed(ImGuiKey_PageUp, false) || ImGui::IsKeyPressed(ImGuiKey_PageDown, false))) {  // L / R: the page before / after
        const int step = ((pressed & plat::kR) || ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) ? 1 : kPages - 1;
        g->page = (g->page + step) % kPages, g->browse = 0, g->focus_list = true, g->follow_hold = 3;
        log("launcher: page %s", kPageNames[g->page.load()]);
    }
    if (!busy && g->status.ready == Ready::Yes && ((pressed & plat::kPlus) || ImGui::IsKeyPressed(ImGuiKey_F5, false)))
        g->play = true;  // +: play
}

bool wanted(const std::vector<std::string>& args, bool& config_mode) {
    auto has = [&](const char* a) { return std::find(args.begin(), args.end(), a) != args.end(); };
    config_mode = has("--config");
    if (has("--play")) return false;
#if defined(_WIN32)
    // Release packages contain two copies of the runtime with distinct entry points.
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (length && length < path.size()) {
        path.resize(length);
        const auto name = fs::path(path).filename().wstring();
        if (_wcsicmp(name.c_str(), L"wonder.exe") == 0 && !config_mode) return false;
    }
#endif
    auto env_on = [](const char* n) {
        const char* e = std::getenv(n);
        return e && *e && std::strcmp(e, "0") != 0;
    };
    if ((env_on("NX_LAUNCHER_TOUR") || env_on("NX_LAN_LOBBY_TOUR") || env_on("NX_LAN_LOBBY_AUTOSTART")) && env_on("NX_HEADLESS_PRESENT")) return true;
    if (env_on("NX_NO_LAUNCHER") || env_on("NX_HEADLESS") || env_on("NX_INPUT_SCRIPT") || env_on("NX_INPUT_REPLAY"))
        return config_mode && !env_on("NX_HEADLESS");
    return true;
}

}  // namespace

void prepare_ultrawide_layouts() {
    const char* profile = std::getenv("NX_ASPECT_PROFILE");
    if (!profile || (std::strcmp(profile, "21:9") != 0 && std::strcmp(profile, "32:9") != 0)) return;
    const Kit kit = find_kit();
    fs::path root = u8(kit.script).parent_path();
    fs::path script = root / "tools" / "wonder_uw_prepare.py";
    if (!file(script)) script = root / "wonder_uw_prepare.py";
    if (kit.python.empty() || !file(script)) {
        log("ultrawide: missing layout preparation tools");
#if defined(_WIN32)
        MessageBoxA(nullptr, "Ultrawide setup tools are missing. Repair or reinstall the game-build kit.",
                    "SuperWonders - Ultrawide setup", MB_OK | MB_ICONERROR);
#endif
        plat::exit_process(1);
    }
    Job job;
    job.start({kit.python, "-u", str(script), "--data-root", plat::data_root()});
    while (job.running) plat::sleep_ns(50'000'000);
    if (job.result != 0) {
        log("ultrawide: layout preparation failed (%d)", int(job.result));
#if defined(_WIN32)
        MessageBoxA(nullptr, "Could not prepare ultrawide layouts from your game files. See data/nx.log for details.",
                    "SuperWonders - Ultrawide setup", MB_OK | MB_ICONERROR);
#endif
        plat::exit_process(1);
    }
}

void run() {
    if (!overlay::enabled()) return;
    bool config_mode = false;
    if (!wanted(plat::program_args(), config_mode)) {
#if defined(_WIN32)
        // Direct game launch should explain missing setup before booting the guest.
        const char* headless = std::getenv("NX_HEADLESS");
        std::wstring path(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
        if ((!headless || !*headless || std::strcmp(headless, "0") == 0) && length && length < path.size()) {
            path.resize(length);
            if (_wcsicmp(fs::path(path).filename().c_str(), L"wonder.exe") == 0) {
                const Status status = check_status(find_kit());
                if (status.ready != Ready::Yes) {
                    log("game: cannot start: %s", status.why.c_str());
                    const std::string message = status.why +
                        "\n\nOpen wonder-launcher.exe, select your game files and complete Set up game files.";
                    MessageBoxA(nullptr, message.c_str(), "SuperWonders - Game setup required", MB_OK | MB_ICONINFORMATION);
                    plat::exit_process(1);
                }
            }
        }
#endif
        return;
    }
    static State state;
    g = &state;
    g->kit = find_kit();
    g->config = load_config();
    // A portable layout can keep its own extracted folders next to the launcher.
    // Only the explicit layout is considered automatically; no background drive scan.
    if (g->config.exefs.empty() && g->config.romfs.empty()) {
        const auto here = u8(program_dir());
        const auto exe = source::exefs_ok(here / "exefs") ? here / "exefs" : here;
        if (source::exefs_ok(exe) && source::romfs_ok(here / "romfs")) {
            g->config.exefs = str(exe); g->config.romfs = str(here / "romfs");
            save_config(g->config);
            g->note = "Found your extracted game folders beside the launcher. Select Set up game files to continue.";
        }
    }
    validate_sources();
    g->config_mode = config_mode;
    const auto arguments=plat::program_args();
    if(std::find(arguments.begin(),arguments.end(),"--lan-ended")!=arguments.end()) {
        std::ifstream note(u8(plat::data_root())/"lan-last-session.txt",std::ios::binary);
        char text[2048]{}; note.read(text,sizeof(text)-1);
        g->note=note.gcount()>0?std::string(text,size_t(note.gcount())):"The LAN session ended. You can reconnect from a new room.";
    }
    refresh_status();
    updates_init(config_mode);
    // A regular launch always opens the launcher; RecompLauncher uses --play to bypass it.
    log("launcher: shown (%s; %s; kit %s)", config_mode ? "configuration" : "setup",
        g->status.why.empty() ? "ready" : g->status.why.c_str(), g->kit.script.empty() ? "none" : g->kit.script.c_str());
    if (!config_mode) g->page = g->status.ready == Ready::Yes ? PagePlay : PageFiles;
    g->shown_ns = plat::monotonic_ns();
    overlay::set_screen(&screen);

    // Test hooks (documented here, as Savage Wind's are where they're defined):
    //   NX_LAUNCHER_TOUR=<n>   each page for n frames, then the settings menu over it, then quits (with
    //                          NX_DUMP_PRESENTED=1 NX_DUMP_PREP=<n>: the last frame of each to frames/9NNNNN.ppm)
    //   NX_LAUNCHER_SETUP=1    starts Set up game files at once, with the folders in launcher.ini (then plays, unless
    //                          --config: the configuration screen stays, as after any setup there)
    const int tour = [] {
        const char* e = std::getenv("NX_LAUNCHER_TOUR");
        return e ? std::max(0, std::atoi(e)) : 0;
    }();
    if (const char* e = std::getenv("NX_LAUNCHER_SETUP"); e && *e == '1') start_setup();
    // An update brought a kit that builds the game code differently, and the folders are known: build it again at
    // once, as Savage Wind does after an update, instead of asking for the folders again.
    else if (g->status.ready == Ready::Rebuild && folder_has(g->config.exefs, "main") &&
             folder_has(g->config.romfs, "Shader"))
        g->page = PageFiles, start_setup();
    const bool lobby_tour=std::getenv("NX_LAN_LOBBY_TOUR")!=nullptr;
    if(lobby_tour) open_lan();
    // Repro hook: exercise the real asynchronous launcher-to-game path without
    // desktop input. Normal launches configure these fields in the native popup.
    if(const auto* mode=std::getenv("NX_LAN_LOBBY_AUTOSTART")) {
        open_lan(); g->lan_mode=std::strcmp(mode,"host")?1:0;
        if(const auto* code=std::getenv("NX_LAN_CODE")) std::snprintf(g->lan_code,sizeof g->lan_code,"%s",code);
        const auto* address=std::getenv("NX_LAN_ADDRESS");
        if(!address) address=g->lan_mode?"127.0.0.1":"0.0.0.0";
        std::snprintf(g->lan_address,sizeof g->lan_address,"%s",address);
        if(const auto* port=std::getenv("NX_LAN_PORT")) g->lan_port=std::clamp(std::atoi(port),1,65535);
        if(const auto* players=std::getenv("NX_LAN_PLAYERS")) g->lan_players=std::clamp(std::atoi(players),2,4);
        lan::start_lobby({g->lan_mode==0,g->lan_address,g->lan_code,uint16_t(g->lan_port),uint8_t(g->lan_players)});
    }
    uint64_t frames = 0;
    test_pad_start();  // (NX_TEST_PAD: a scripted controller, launcher/test_pad.h)
    for (;;) {
        const uint64_t t0 = plat::monotonic_ns();
        test_pad_frame(frames);
        plat::PadState pad{};
        plat::read_pad(pad);  // (feeds the overlay's controller navigation: overlay::filter_pad)
        if (tour) {
            g->follow_hold = 2;  // the tour owns the page even while a sidebar item has keyboard focus
            const int step = int(frames / uint64_t(tour));
            if (step < kPages - 1) g->page = step;  // (not Quit)
            if (step >= kPages - 1 && step < kPages - 1 + 5)
                g->page = PageSettings, overlay::open_settings(step - (kPages - 1));
            if (step == kPages - 1 + 5) {  // the setup card at 62%, its sheen sweeping
                if (overlay::is_open()) overlay::toggle();
                g->page = PageFiles, g->demo_progress = true, g->job.progress = 0.62f;
                if (!g->job.started_ns) g->job.started_ns = plat::monotonic_ns();
                std::lock_guard lk(g->job.lock);
                g->job.what = "Translating the shaders";
            }
            if (step > kPages - 1 + 5 && !updates_busy()) plat::exit_process(0);  // (an update finishes first)
        }
        if (!nvn::present_ui_frame()) plat::sleep_ns(16'000'000);
        frames++;
        if(lobby_tour && frames>=120) plat::exit_process(0);
        if (g->quit) {
            log("launcher: quit");
            plat::exit_process(0);
        }
        if (g->play && !g->job.running) {
            refresh_status();
            if (g->status.ready == Ready::Yes) {
#if defined(_WIN32)
                std::wstring self(32768, L'\0');
                DWORD n = GetModuleFileNameW(nullptr, self.data(), DWORD(self.size()));
                if (n && n < self.size()) {
                    self.resize(n);
                    if (!lan::active() && _wcsicmp(fs::path(self).filename().c_str(), L"wonder-launcher.exe") == 0) {
                        const std::wstring game = (fs::path(self).parent_path() / L"wonder.exe").wstring();
                        std::wstring command = L"\"" + game + L"\" --play";
                        STARTUPINFOW si{};
                        si.cb = sizeof(si);
                        PROCESS_INFORMATION pi{};
                        if (CreateProcessW(game.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                            CloseHandle(pi.hThread);
                            CloseHandle(pi.hProcess);
                            plat::exit_process(0);
                        }
                        log("launcher: could not start wonder.exe (Windows error %lu)", GetLastError());
                        ui::notify("Could not start wonder.exe. Check that it is in the same folder as the launcher.");
                        g->play = false;
                        continue;
                    }
                }
#endif
                break;
            }
            g->play = false;
        }
        const uint64_t spent = plat::monotonic_ns() - t0;
        if (spent < 16'000'000) plat::sleep_ns(int64_t(16'000'000 - spent));
    }
    log("launcher: play (%llu frames shown)", (unsigned long long)frames);
    if (overlay::is_open()) overlay::toggle();
    overlay::set_screen(nullptr);
}

}  // namespace nx::launcher
