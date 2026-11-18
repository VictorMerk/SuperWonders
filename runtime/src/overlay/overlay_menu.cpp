// The settings menu's tabs (Video, Audio, Controls, Overlay, System), at parity with the Win32 menu
// (pc_menu_win.cpp), whose language settings it shares. The layout follows Savage Wind's in-game
// Settings window (github.com/Revivedx/SavageWindRecomp, runtime/ui/menu.mm menu_render and tabs_body, MIT): a title
// with a close X, tabs, and two-pane pages of option rows with the selected row's description on the right
// (option_rows.cpp). Descriptions are English for now.
#include <algorithm>
#include <cstdio>
#include <iterator>

#include "imgui.h"
#include "imgui_internal.h"  // NavCursorVisible

#include <nx/runtime.h>

#include "overlay/option_rows.h"
#include "controllers.h"
#include "netplay_game.h"
#include "button_font.h"
#include "overlay/overlay_internal.h"
#include "overlay/usability.h"
#include "profiles.h"
#include "keyboard_actions.h"
#if NX_GAME_LIBRARY
#include "launcher/launcher.h"
#endif
#include "overlay/overlay_ui.h"

namespace nx::overlay {

namespace {

struct Labels {
    const char *title, *close, *video, *audio, *controls, *overlay, *window_size, *internal_resolution, *fullscreen,
        *topmost, *volume, *mute, *game_language, *game_language_restart, *control_preset, *standard_desc,
        *nintendo_desc, *restart_note, *frame_cap_label, *system, *memory_limit, *low_vram, *restart_low_vram;
};

constexpr Labels kLabels[] = {
    {"Quick Settings", "F1, Esc or Minus + Plus to close", "Video", "Audio", "Controls", "Overlay",
     "Window / output size", "Internal resolution", "Fullscreen", "Always on top", "Game volume", "Mute audio",
     "Game language", "Restart the game to apply the language.", "Button layout",
     "A: bottom (A / Cross)     B: right (B / Circle)", "A: right (B / Circle)     B: bottom (A / Cross)",
     "Internal resolution applies after restarting the game.", "Frame rate cap", "System", "Memory cleanup threshold", "Low VRAM profile",
     "Restart the game to apply the texture profile."},
    {"Impostazioni", "F1, Esc o Meno + Più per chiudere", "Video", "Audio", "Controlli", "Overlay",
     "Dimensione finestra / output", "Risoluzione interna", "Schermo intero", "Sempre in primo piano",
     "Volume gioco", "Disattiva audio", "Lingua del gioco", "Riavvia il gioco per applicare la lingua.",
     "Disposizione tasti", "A: in basso (A / Croce)     B: a destra (B / Cerchio)",
     "A: a destra (B / Cerchio)     B: in basso (A / Croce)", "La risoluzione interna si applica al riavvio.",
     "Limite FPS", "Sistema", "Soglia pulizia memoria", "Profilo VRAM ridotta",
     "Riavvia il gioco per applicare il profilo texture."},
    {"Einstellungen", "F1, Esc oder Minus + Plus zum Schließen", "Video", "Audio", "Steuerung", "Overlay",
     "Fenster- / Ausgabegröße", "Interne Auflösung", "Vollbild", "Immer im Vordergrund", "Spiel-Lautstärke",
     "Ton stummschalten", "Spielsprache", "Spiel zum Übernehmen neu starten.", "Tastenbelegung",
     "A: unten (A / Kreuz)     B: rechts (B / Kreis)", "A: rechts (B / Kreis)     B: unten (A / Kreuz)",
     "Interne Auflösung wird nach Neustart übernommen.", "FPS-Begrenzung", "System", "Speicherbereinigungsschwelle",
     "Niedriger VRAM-Verbrauch", "Spiel neu starten, um das Texturprofil anzuwenden."},
    {"Paramètres", "F1, Échap ou Moins + Plus pour fermer", "Vidéo", "Audio", "Commandes", "Overlay",
     "Taille fenêtre / sortie", "Résolution interne", "Plein écran", "Toujours au premier plan", "Volume du jeu",
     "Couper le son", "Langue du jeu", "Redémarrez le jeu pour appliquer la langue.", "Disposition des touches",
     "A : bas (A / Croix)     B : droite (B / Cercle)", "A : droite (B / Cercle)     B : bas (A / Croix)",
     "La résolution interne s'applique après redémarrage.", "Limite d’images/s", "Système", "Seuil de nettoyage mémoire",
     "Profil VRAM réduit", "Redémarrez pour appliquer le profil de textures."},
    {"Ajustes rápidos", "F1, Esc o Menos + Más para cerrar", "Vídeo", "Audio", "Controles", "Superposición",
     "Tamaño de ventana / salida", "Resolución interna", "Pantalla completa", "Siempre visible", "Volumen del juego", "Silenciar audio",
     "Idioma del juego", "Reinicia el juego para aplicar el idioma.", "Distribución de botones", "A: abajo (A / Cruz)     B: derecha (B / Círculo)",
     "A: derecha (B / Círculo)     B: abajo (A / Cruz)", "La resolución interna se aplica al reiniciar.", "Límite de FPS", "Sistema", "Umbral de limpieza de memoria", "Perfil de poca VRAM",
     "Reinicia el juego para aplicar el perfil de texturas."},
    {"빠른 설정", "F1, Esc 또는 마이너스 + 플러스로 닫기", "비디오", "오디오", "조작", "오버레이",
     "창 / 출력 크기", "내부 해상도", "전체 화면", "항상 위에 표시", "게임 음량", "음소거",
     "게임 언어", "언어를 적용하려면 게임을 다시 시작하세요.", "버튼 배치", "A: 아래 (A / 십자)     B: 오른쪽 (B / 원)",
     "A: 오른쪽 (B / 원)     B: 아래 (A / 십자)", "내부 해상도는 재시작 후 적용됩니다.", "프레임 제한", "시스템", "메모리 정리 기준", "저 VRAM 프로필",
     "텍스처 프로필을 적용하려면 게임을 다시 시작하세요."},
    {"クイック設定", "F1、Esc またはマイナス + プラスで閉じる", "映像", "音声", "操作", "オーバーレイ",
     "ウィンドウ / 出力サイズ", "内部解像度", "全画面表示", "常に手前に表示", "ゲーム音量", "ミュート",
     "ゲームの言語", "言語の変更にはゲームの再起動が必要です。", "ボタン配置", "A: 下 (A / クロス)     B: 右 (B / サークル)",
     "A: 右 (B / サークル)     B: 下 (A / クロス)", "内部解像度は再起動後に適用されます。", "フレームレート上限", "システム", "メモリ解放のしきい値", "低 VRAM プロファイル",
     "テクスチャ設定の適用には再起動が必要です。"},
    {"الإعدادات السريعة", "اضغط F1 أو Esc أو ناقص + زائد للإغلاق", "الفيديو", "الصوت", "التحكم", "عرض الأداء",
     "حجم النافذة / الإخراج", "الدقة الداخلية", "ملء الشاشة", "دائماً في المقدمة", "مستوى صوت اللعبة", "كتم الصوت",
     "لغة اللعبة", "أعد تشغيل اللعبة لتطبيق اللغة.", "تخطيط الأزرار", "A: أسفل     B: يمين", "A: يمين     B: أسفل",
     "تُطبق الدقة الداخلية بعد إعادة التشغيل.", "حد معدل الإطارات", "النظام", "حد تنظيف الذاكرة", "ملف VRAM منخفض",
     "أعد تشغيل اللعبة لتطبيق ملف الأنسجة."},
    {"Быстрые настройки", "F1, Esc или Минус + Плюс — закрыть", "Видео", "Звук", "Управление", "Оверлей",
     "Размер окна / вывода", "Внутреннее разрешение", "Полный экран", "Поверх других окон", "Громкость игры", "Без звука",
     "Язык игры", "Перезапустите игру, чтобы применить язык.", "Раскладка кнопок", "A: снизу     B: справа", "A: справа     B: снизу",
     "Внутреннее разрешение применится после перезапуска.", "Ограничение FPS", "Система", "Порог очистки памяти", "Профиль низкой VRAM",
     "Перезапустите игру, чтобы применить профиль текстур."},
    {"Hızlı Ayarlar", "Kapatmak için F1, Esc veya Eksi + Artı", "Video", "Ses", "Kontroller", "Performans katmanı",
     "Pencere / çıktı boyutu", "Dahili çözünürlük", "Tam ekran", "Her zaman üstte", "Oyun sesi", "Sesi kapat",
     "Oyun dili", "Dili uygulamak için oyunu yeniden başlatın.", "Düğme düzeni", "A: altta     B: sağda", "A: sağda     B: altta",
     "Dahili çözünürlük yeniden başlatınca uygulanır.", "Kare hızı sınırı", "Sistem", "Bellek temizleme eşiği", "Düşük VRAM profili",
     "Doku profilini uygulamak için oyunu yeniden başlatın."},
    {"Snelle instellingen", "Druk op F1, Esc of Min + Plus om te sluiten", "Beeld", "Audio", "Besturing", "Overlay",
     "Venster- / uitvoergrootte", "Interne resolutie", "Volledig scherm", "Altijd op voorgrond", "Spelvolume", "Geluid dempen",
     "Speltaal", "Start het spel opnieuw om de taal toe te passen.", "Knopindeling", "A: onder     B: rechts", "A: rechts     B: onder",
     "Interne resolutie wordt toegepast na opnieuw starten.", "FPS-limiet", "Systeem", "Geheugenopruimdrempel", "Laag-VRAM-profiel",
     "Start het spel opnieuw om het textuurprofiel toe te passen."},
    {"快速设置", "按 F1、Esc 或减号 + 加号关闭", "视频", "音频", "控制", "性能叠加层",
     "窗口 / 输出大小", "内部解析度", "全屏", "窗口置顶", "游戏音量", "静音",
     "游戏语言", "重新启动游戏以应用语言。", "按键布局", "A：下方     B：右侧", "A：右侧     B：下方",
     "重新启动游戏后应用内部解析度。", "帧率上限", "系统", "内存清理阈值", "低显存配置",
     "重新启动游戏以应用纹理配置。"},
    {"快速設定", "按 F1、Esc 或減號 + 加號關閉", "影片", "音訊", "控制", "效能覆蓋層",
     "視窗 / 輸出大小", "內部解析度", "全螢幕", "視窗置頂", "遊戲音量", "靜音",
     "遊戲語言", "重新啟動遊戲以套用語言。", "按鍵配置", "A：下方     B：右側", "A：右側     B：下方",
     "重新啟動遊戲後套用內部解析度。", "影格率上限", "系統", "記憶體清理閾值", "低顯示記憶體設定",
     "重新啟動遊戲以套用材質設定。"},
};
constexpr const char* kLanguageNames[] = {"English", "Italiano", "Deutsch", "Français", "Español", "한국어", "日本語", "العربية", "Русский", "Türkçe", "Nederlands", "简体中文", "繁體中文"};
constexpr const char* kGameLanguageNames[] = {"English", "Italiano", "Deutsch", "Français", "Português (Brasil)", "Español", "한국어", "日本語", "Nederlands", "Русский", "Simplified Chinese", "Traditional Chinese"};
constexpr const char* kInternal[13][7] = {
    {"Original / Dynamic", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Native (screen)"},
    {"Originale / dinamica", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Nativa (schermo)"},
    {"Original / Dynamisch", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Nativ (Bildschirm)"},
    {"Originale / dynamique", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Native (écran)"},
    {"Original / dinámico", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Nativa (pantalla)"},
    {"원본 / 동적", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "네이티브 (화면)"},
    {"オリジナル / 動的", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "ネイティブ (画面)"},
    {"أصلية / ديناميكية", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "أصلية (الشاشة)"},
    {"Исходное / динамическое", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Экранное"},
    {"Özgün / Dinamik", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Doğal (ekran)"},
    {"Origineel / dynamisch", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "Schermresolutie"},
    {"原始 / 动态", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "屏幕原生"},
    {"原始 / 動態", "1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160", "螢幕原生"},
};
constexpr int kSizes[][2] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}};
constexpr const char* kOverlayLabels[13][6] = {
    {"Metrics overlay", "FPS", "1% lows", "Game speed", "Frametime graph", "Overlay position"},
    {"Overlay metriche", "FPS", "Minimi 1%", "Velocità gioco", "Grafico frametime", "Posizione overlay"},
    {"Metrik-Overlay", "FPS", "1%-Tiefstwerte", "Spieltempo", "Frametime-Diagramm", "Overlay-Position"},
    {"Superposition métriques", "FPS", "1 % les plus bas", "Vitesse du jeu", "Graphique frametime",
     "Position de l’overlay"},
    {"Superposición de métricas", "FPS", "Mínimos del 1 %", "Velocidad del juego", "Gráfico de frametime", "Posición"},
    {"성능 오버레이", "FPS", "1% 하위 FPS", "게임 속도", "프레임 시간 그래프", "오버레이 위치"},
    {"パフォーマンス表示", "FPS", "1% Low", "ゲーム速度", "フレーム時間グラフ", "表示位置"},
    {"عرض الأداء", "FPS", "أدنى 1٪", "سرعة اللعبة", "مخطط زمن الإطار", "موضع العرض"},
    {"Оверлей производительности", "FPS", "1% минимум", "Скорость игры", "График времени кадра", "Положение"},
    {"Performans katmanı", "FPS", "%1 düşük", "Oyun hızı", "Kare süresi grafiği", "Konum"},
    {"Prestatie-overlay", "FPS", "1%-laagste", "Spelsnelheid", "Framet grafiek", "Overlaypositie"},
    {"性能叠加层", "FPS", "1%低帧", "游戏速度", "帧时间图表", "叠加层位置"},
    {"效能覆蓋層", "FPS", "1%低幀", "遊戲速度", "影格時間圖表", "覆蓋層位置"},
};
constexpr const char* kPositions[13][4] = {
    {"Top left", "Top right", "Bottom left", "Bottom right"},
    {"In alto a sinistra", "In alto a destra", "In basso a sinistra", "In basso a destra"},
    {"Oben links", "Oben rechts", "Unten links", "Unten rechts"},
    {"En haut à gauche", "En haut à droite", "En bas à gauche", "En bas à droite"},
    {"Arriba izquierda", "Arriba derecha", "Abajo izquierda", "Abajo derecha"},
    {"Sol üst", "Sağ üst", "Sol alt", "Sağ alt"},
    {"左上", "右上", "左下", "右下"},
    {"أعلى اليسار", "أعلى اليمين", "أسفل اليسار", "أسفل اليمين"},
    {"Сверху слева", "Сверху справа", "Снизу слева", "Снизу справа"},
    {"Sol üst", "Sağ üst", "Sol alt", "Sağ alt"},
    {"Linksboven", "Rechtsboven", "Linksonder", "Rechtsonder"},
    {"左上", "右上", "左下", "右下"},
    {"左上", "右上", "左下", "右下"},
};
constexpr const char* kHeaders[13][2] = {
    {"Keyboard", "Controller"}, {"Tastiera", "Controller"}, {"Tastatur", "Controller"}, {"Clavier", "Manette"},
    {"Teclado", "Mando"}, {"키보드", "컨트롤러"}, {"キーボード", "コントローラー"}, {"لوحة المفاتيح", "وحدة التحكم"},
    {"Клавиатура", "Контроллер"}, {"Klavye", "Kontrolcü"}, {"Toetsenbord", "Controller"},
    {"键盘", "控制器"}, {"鍵盤", "控制器"}};
// Secondary option names and section headings not covered by the main label table.
constexpr const char* kCommon[13][12] = {
    {"Display", "Image", "Frame pacing", "Upscaling", "FSR sharpness", "Anti-aliasing", "Anisotropic filtering", "VSync", "Rumble", "Game", "Quit game", "Menu language"},
    {"Schermo", "Immagine", "Sincronizzazione", "Upscaling", "Nitidezza FSR", "Anti-aliasing", "Filtro anisotropico", "V-Sync", "Vibrazione", "Gioco", "Esci dal gioco", "Lingua menu"},
    {"Anzeige", "Bild", "Frame-Pacing", "Upscaling", "FSR-Schärfe", "Kantenglättung", "Anisotrope Filterung", "VSync", "Vibration", "Spiel", "Spiel beenden", "Menüsprache"},
    {"Affichage", "Image", "Synchronisation", "Mise à l’échelle", "Netteté FSR", "Anticrénelage", "Filtrage anisotrope", "VSync", "Vibration", "Jeu", "Quitter le jeu", "Langue du menu"},
    {"Pantalla", "Imagen", "Ritmo de fotogramas", "Escalado", "Nitidez FSR", "Antialiasing", "Filtrado anisotrópico", "VSync", "Vibración", "Juego", "Salir del juego", "Idioma del menú"},
    {"디스플레이", "화면", "프레임 동기화", "업스케일링", "FSR 선명도", "안티앨리어싱", "비등방성 필터링", "VSync", "진동", "게임", "게임 종료", "메뉴 언어"},
    {"画面", "映像", "フレーム同期", "アップスケーリング", "FSR シャープネス", "アンチエイリアス", "異方性フィルタリング", "VSync", "振動", "ゲーム", "ゲームを終了", "メニュー言語"},
    {"الشاشة", "الصورة", "مزامنة الإطارات", "تحسين الدقة", "حدة FSR", "تنعيم الحواف", "ترشيح متباين الخواص", "مزامنة رأسية", "اهتزاز", "اللعبة", "إنهاء اللعبة", "لغة القائمة"},
    {"Экран", "Изображение", "Синхронизация кадров", "Масштабирование", "Резкость FSR", "Сглаживание", "Анизотропная фильтрация", "VSync", "Вибрация", "Игра", "Выйти из игры", "Язык меню"},
    {"Ekran", "Görüntü", "Kare zamanlaması", "Yükseltme", "FSR keskinliği", "Kenar yumuşatma", "Anizotropik filtreleme", "VSync", "Titreşim", "Oyun", "Oyundan çık", "Menü dili"},
    {"Beeldscherm", "Afbeelding", "Framepacing", "Opschaling", "FSR-scherpte", "Anti-aliasing", "Anisotrope filtering", "VSync", "Trillen", "Spel", "Spel afsluiten", "Menutaal"},
    {"显示", "画面", "帧同步", "升频", "FSR锐化", "抗锯齿", "各向异性过滤", "垂直同步", "震动", "游戏", "退出游戏", "菜单语言"},
    {"顯示", "畫面", "影格同步", "升頻", "FSR銳化", "反鋸齒", "各向異性過濾", "垂直同步", "震動", "遊戲", "結束遊戲", "選單語言"},
};
constexpr const char* kActions[kControlCount] = {
    "A", "B", "X", "Y", "L", "R", "ZL", "ZR", "Plus", "Minus", "L Stick", "R Stick",
    "D-pad Left", "D-pad Up", "D-pad Right", "D-pad Down",
};
constexpr int kTabs = 5;

namespace desc {
constexpr const char* language = "Language of this menu.";
constexpr const char* window_size = "Size of the game window in pixels. Not available in fullscreen.";
constexpr const char* internal = "The resolution the game renders at. Original lets the game choose (it lowers it under "
                                 "load); higher is sharper and costs more GPU time. Native renders at the size the "
                                 "image is shown, so nothing is stretched. Applies after restarting the game.";
constexpr const char* vsync = "How frames reach the screen. On: waits for the display (no tearing; smooth with "
                              "FreeSync/G-Sync). Fast: the newest frame at each refresh (the default). Adaptive: "
                              "waits unless a frame is late. Off: at once (can tear).";
constexpr const char* frame_cap = "Maximum frame rate. 30, 60, 90, 120 and 144 FPS are available. Higher caps need a "
                                  "display and PC that can sustain them; the game simulation remains at normal speed.";
constexpr const char* fullscreen = "Fill the whole screen. F11 also switches it.";
constexpr const char* topmost = "Keep the game window above other windows. Not available on Wayland.";
constexpr const char* volume = "The game's volume.";
constexpr const char* mute = "Silence the game without changing its volume.";
constexpr const char* game_language = "Select the game's text language. Changes apply after restarting.";
constexpr const char* preset = "Which controller button is A. Xbox / DualSense: A at the bottom. Nintendo: A on the "
                               "right, as on a Switch controller.";
constexpr const char* rumble = "How strongly the controller vibrates when the game asks for it. 0% turns it off.";
constexpr const char* key = "Select, then press the key for this action. Esc cancels.";
constexpr const char* pad = "The controller button for this action: left / right picks another. The layout's default "
                            "clears the change.";
constexpr const char* metrics = "A performance panel in a corner of the screen.";
constexpr const char* fps = "Frames per second over the last 60 frames.";
constexpr const char* low = "The frame rate of the slowest 1% of the last 300 frames: it shows stutter.";
constexpr const char* speed = "How fast the game runs against its own target: 100% is full speed.";
constexpr const char* graph = "The time of each of the last 300 frames, 0 to 50 ms.";
constexpr const char* position = "The corner the panel sits in.";
constexpr const char* memory = "On desktop, passing this threshold asks Vulkan to release unused reusable images and "
                               "keeps the game running. Android uses it as a safety stop. It cannot free textures in use "
                               "and is not a direct GPU VRAM limit.";
constexpr const char* upscaler = "How the game's image is enlarged when it is smaller than the window (a lower internal "
                                 "resolution, or fullscreen above 1080p). FSR 1 (AMD FidelityFX Super Resolution) keeps "
                                 "edges sharp; Bilinear is a plain stretch.";
constexpr const char* antialiasing = "Smooths jagged edges on the final image. FXAA is quick and can soften fine "
                                     "detail a little; the game's own anti-aliasing stays as it is.";
constexpr const char* anisotropy = "Keeps textures sharp on surfaces seen at an angle. Game default uses the game's "
                                   "own setting; higher costs a little GPU time. Applies after restarting the game.";
constexpr const char* sharpness = "FSR 1's sharpening: higher is sharper (more contrast on edges), lower is softer.";
constexpr const char* low_vram = "For graphics cards with little video memory: decoded textures are kept compressed "
                                 "(BC3), at a small cost in quality. Applies after restarting the game.";
constexpr const char* quit = "Close the game. Progress since the game last saved is lost. Select twice to confirm.";
}  // namespace desc


const char* translated_desc(const char* text, int lang) {
    if (lang == 2) {
        if (text == desc::language) return "Sprache dieses Menüs.";
        if (text == desc::window_size) return "Fenstergröße in Pixeln. Im Vollbild nicht verfügbar.";
        if (text == desc::internal) return "Interne Auflösung. Höhere Werte belasten die GPU stärker. Neustart erforderlich.";
        if (text == desc::vsync) return "Synchronisiert Bilder mit dem Bildschirm. Ohne Synchronisation kann Tearing auftreten.";
        if (text == desc::frame_cap) return "Maximale Bildrate. Hohe Werte benötigen einen geeigneten PC und Monitor.";
        if (text == desc::fullscreen) return "Vollbild ein- oder ausschalten. Auch mit F11.";
        if (text == desc::topmost) return "Fenster im Vordergrund halten. Unter Wayland nicht verfügbar.";
        if (text == desc::volume) return "Lautstärke des Spiels.";
        if (text == desc::mute) return "Spiel stummschalten.";
        if (text == desc::game_language) return "Spielsprache auswählen. Neustart erforderlich.";
        if (text == desc::preset) return "Position der A-Taste auf dem Controller.";
        if (text == desc::rumble) return "Vibrationsstärke. 0 % deaktiviert die Vibration.";
        if (text == desc::key) return "Auswählen und Taste drücken. Esc bricht ab.";
        if (text == desc::pad) return "Controller-Taste für diese Aktion auswählen.";
        if (text == desc::metrics) return "Leistungsanzeige einblenden.";
        if (text == desc::fps) return "Bilder pro Sekunde der letzten 60 Bilder.";
        if (text == desc::low) return "Bildrate der langsamsten 1 % der letzten 300 Bilder.";
        if (text == desc::speed) return "Spielgeschwindigkeit: 100 % entspricht normaler Geschwindigkeit.";
        if (text == desc::graph) return "Dauer der letzten 300 Bilder von 0 bis 50 ms.";
        if (text == desc::position) return "Position der Leistungsanzeige.";
        if (text == desc::memory) return "Ab diesem Wert gibt Vulkan ungenutzte Bilder frei. Kein direktes VRAM-Limit.";
        if (text == desc::upscaler) return "Verfahren zur Bildvergrößerung. FSR 1 hält Kanten scharf.";
        if (text == desc::antialiasing) return "Glättet Kanten. FXAA kann feine Details leicht verwischen.";
        if (text == desc::anisotropy) return "Schärft Texturen aus schrägen Blickwinkeln. Neustart erforderlich.";
        if (text == desc::sharpness) return "FSR-1-Schärfe einstellen.";
        if (text == desc::low_vram) return "Komprimiert dekodierte Texturen, um VRAM zu sparen. Neustart erforderlich.";
        if (text == desc::quit) return "Spiel beenden. Ungespeicherter Fortschritt geht verloren. Zweimal auswählen.";
    }
    if (lang == 3) {
        if (text == desc::language) return "Langue de ce menu.";
        if (text == desc::window_size) return "Taille de la fenêtre en pixels. Indisponible en plein écran.";
        if (text == desc::internal) return "Résolution interne. Une valeur élevée sollicite davantage le GPU. Redémarrage nécessaire.";
        if (text == desc::vsync) return "Synchronise les images avec l’écran. Sans synchronisation, des déchirures peuvent apparaître.";
        if (text == desc::frame_cap) return "Nombre maximal d’images par seconde. Une valeur élevée exige un PC et un écran adaptés.";
        if (text == desc::fullscreen) return "Activer le plein écran. F11 permet aussi de le changer.";
        if (text == desc::topmost) return "Garder la fenêtre au premier plan. Indisponible sous Wayland.";
        if (text == desc::volume) return "Volume du jeu.";
        if (text == desc::mute) return "Couper le son sans changer le volume.";
        if (text == desc::game_language) return "Choisir la langue du jeu. Redémarrage nécessaire.";
        if (text == desc::preset) return "Position du bouton A sur la manette.";
        if (text == desc::rumble) return "Intensité des vibrations. À 0 %, elles sont désactivées.";
        if (text == desc::key) return "Sélectionnez puis appuyez sur une touche. Échap annule.";
        if (text == desc::pad) return "Choisir le bouton de manette pour cette action.";
        if (text == desc::metrics) return "Afficher les performances à l’écran.";
        if (text == desc::fps) return "Images par seconde sur les 60 dernières images.";
        if (text == desc::low) return "Fréquence des 1 % d’images les plus lentes parmi les 300 dernières.";
        if (text == desc::speed) return "Vitesse du jeu : 100 % correspond à la vitesse normale.";
        if (text == desc::graph) return "Durée des 300 dernières images, de 0 à 50 ms.";
        if (text == desc::position) return "Position du panneau de performances.";
        if (text == desc::memory) return "Au-delà de ce seuil, Vulkan libère les images inutilisées. Ce n’est pas une limite directe de VRAM.";
        if (text == desc::upscaler) return "Méthode d’agrandissement. FSR 1 préserve la netteté des contours.";
        if (text == desc::antialiasing) return "Lisse les contours. FXAA peut adoucir légèrement les détails.";
        if (text == desc::anisotropy) return "Améliore la netteté des textures vues en biais. Redémarrage nécessaire.";
        if (text == desc::sharpness) return "Régler la netteté de FSR 1.";
        if (text == desc::low_vram) return "Compresse les textures décodées pour économiser la VRAM. Redémarrage nécessaire.";
        if (text == desc::quit) return "Quitter le jeu. La progression non sauvegardée sera perdue. Sélectionnez deux fois.";
    }
    if (lang == 4) {
        if (text == desc::language) return "Idioma de este menú.";
        if (text == desc::window_size) return "Tamaño de la ventana en píxeles. No disponible en pantalla completa.";
        if (text == desc::internal) return "Resolución interna. Los valores altos requieren más potencia gráfica. Requiere reiniciar.";
        if (text == desc::vsync) return "Sincroniza los fotogramas con la pantalla. Sin sincronización puede aparecer tearing.";
        if (text == desc::frame_cap) return "Límite de fotogramas por segundo. Los valores altos requieren un PC y monitor adecuados.";
        if (text == desc::fullscreen) return "Activa la pantalla completa. También se cambia con F11.";
        if (text == desc::topmost) return "Mantiene la ventana encima de otras. No disponible en Wayland.";
        if (text == desc::volume) return "Volumen del juego.";
        if (text == desc::mute) return "Silencia el juego sin cambiar el volumen.";
        if (text == desc::game_language) return "Elige el idioma del juego. Requiere reiniciar.";
        if (text == desc::preset) return "Posición del botón A en el mando.";
        if (text == desc::rumble) return "Intensidad de la vibración. Con 0 % se desactiva.";
        if (text == desc::key) return "Selecciona y pulsa una tecla. Esc cancela.";
        if (text == desc::pad) return "Elige el botón del mando para esta acción.";
        if (text == desc::metrics) return "Muestra un panel de rendimiento.";
        if (text == desc::fps) return "Fotogramas por segundo de los últimos 60 fotogramas.";
        if (text == desc::low) return "Frecuencia del 1 % de los fotogramas más lentos de los últimos 300.";
        if (text == desc::speed) return "Velocidad del juego: 100 % es la velocidad normal.";
        if (text == desc::graph) return "Duración de los últimos 300 fotogramas, de 0 a 50 ms.";
        if (text == desc::position) return "Posición del panel de rendimiento.";
        if (text == desc::memory) return "Al superar este umbral Vulkan libera imágenes sin usar. No es un límite directo de VRAM.";
        if (text == desc::upscaler) return "Método para ampliar la imagen. FSR 1 conserva los bordes nítidos.";
        if (text == desc::antialiasing) return "Suaviza bordes dentados. FXAA puede suavizar detalles finos.";
        if (text == desc::anisotropy) return "Mejora las texturas vistas en ángulo. Requiere reiniciar.";
        if (text == desc::sharpness) return "Ajusta la nitidez de FSR 1.";
        if (text == desc::low_vram) return "Comprime texturas decodificadas para ahorrar VRAM. Requiere reiniciar.";
        if (text == desc::quit) return "Cierra el juego. Se perderá el progreso no guardado. Selecciona dos veces.";
    }
    if (lang == 5) {
        if (text == desc::language) return "이 메뉴의 언어입니다.";
        if (text == desc::window_size) return "창 크기입니다. 전체 화면에서는 사용할 수 없습니다.";
        if (text == desc::internal) return "내부 해상도입니다. 높을수록 GPU 부하가 증가합니다. 재시작 후 적용됩니다.";
        if (text == desc::vsync) return "화면과 프레임을 동기화합니다. 끄면 화면이 찢어질 수 있습니다.";
        if (text == desc::frame_cap) return "최대 프레임 속도입니다. 높은 값에는 충분한 PC와 모니터가 필요합니다.";
        if (text == desc::fullscreen) return "전체 화면으로 전환합니다. F11도 사용할 수 있습니다.";
        if (text == desc::topmost) return "창을 항상 위에 표시합니다. Wayland에서는 사용할 수 없습니다.";
        if (text == desc::volume) return "게임 음량입니다.";
        if (text == desc::mute) return "음량을 바꾸지 않고 소리를 끕니다.";
        if (text == desc::game_language) return "게임 언어를 선택합니다. 재시작 후 적용됩니다.";
        if (text == desc::preset) return "컨트롤러의 A 버튼 위치를 선택합니다.";
        if (text == desc::rumble) return "진동 강도입니다. 0%는 진동을 끕니다.";
        if (text == desc::key) return "선택한 후 키를 누릅니다. Esc로 취소합니다.";
        if (text == desc::pad) return "이 동작에 사용할 컨트롤러 버튼을 선택합니다.";
        if (text == desc::metrics) return "화면에 성능 패널을 표시합니다.";
        if (text == desc::fps) return "최근 60프레임의 초당 프레임 수입니다.";
        if (text == desc::low) return "최근 300프레임 중 가장 느린 1%의 프레임 속도입니다.";
        if (text == desc::speed) return "게임 속도입니다. 100%가 정상 속도입니다.";
        if (text == desc::graph) return "최근 300프레임의 시간입니다. 범위는 0~50ms입니다.";
        if (text == desc::position) return "성능 패널의 위치입니다.";
        if (text == desc::memory) return "이 기준을 넘으면 Vulkan이 사용하지 않는 이미지를 해제합니다. 직접적인 VRAM 제한은 아닙니다.";
        if (text == desc::upscaler) return "이미지 확대 방식을 선택합니다. FSR 1은 가장자리를 선명하게 유지합니다.";
        if (text == desc::antialiasing) return "계단 현상을 줄입니다. FXAA는 세부 묘사를 약간 흐리게 할 수 있습니다.";
        if (text == desc::anisotropy) return "비스듬히 보이는 텍스처를 선명하게 합니다. 재시작 후 적용됩니다.";
        if (text == desc::sharpness) return "FSR 1 선명도를 조절합니다.";
        if (text == desc::low_vram) return "디코딩된 텍스처를 압축해 VRAM을 절약합니다. 재시작 후 적용됩니다.";
        if (text == desc::quit) return "게임을 종료합니다. 저장하지 않은 진행 상황은 사라집니다. 두 번 선택하세요.";
    }
    if (lang == 6) {
        if (text == desc::language) return "このメニューの言語です。";
        if (text == desc::window_size) return "ウィンドウのサイズです。全画面では変更できません。";
        if (text == desc::internal) return "内部解像度です。高いほど GPU の負荷が増えます。再起動後に適用されます。";
        if (text == desc::vsync) return "画面とフレームを同期します。無効にすると画面が乱れることがあります。";
        if (text == desc::frame_cap) return "最大フレームレートです。高い設定には対応する PC とモニターが必要です。";
        if (text == desc::fullscreen) return "全画面表示を切り替えます。F11 でも切り替えられます。";
        if (text == desc::topmost) return "ウィンドウを最前面に表示します。Wayland では利用できません。";
        if (text == desc::volume) return "ゲームの音量です。";
        if (text == desc::mute) return "音量を変えずに音を消します。";
        if (text == desc::game_language) return "ゲームの言語を選択します。再起動後に適用されます。";
        if (text == desc::preset) return "コントローラーの A ボタンの位置です。";
        if (text == desc::rumble) return "振動の強さです。0% で無効になります。";
        if (text == desc::key) return "選択してキーを押します。Esc でキャンセルします。";
        if (text == desc::pad) return "この操作に使うコントローラーボタンを選択します。";
        if (text == desc::metrics) return "画面にパフォーマンスを表示します。";
        if (text == desc::fps) return "直近 60 フレームの毎秒フレーム数です。";
        if (text == desc::low) return "直近 300 フレームのうち最も遅い 1% のフレームレートです。";
        if (text == desc::speed) return "ゲーム速度です。100% が通常速度です。";
        if (text == desc::graph) return "直近 300 フレームの時間を 0〜50ms で表示します。";
        if (text == desc::position) return "パフォーマンス表示の位置です。";
        if (text == desc::memory) return "この値を超えると Vulkan が未使用の画像を解放します。VRAM の直接的な上限ではありません。";
        if (text == desc::upscaler) return "画像の拡大方法です。FSR 1 は輪郭を鮮明に保ちます。";
        if (text == desc::antialiasing) return "輪郭のギザギザを軽減します。FXAA は細部を少しぼかす場合があります。";
        if (text == desc::anisotropy) return "斜めから見たテクスチャを鮮明にします。再起動後に適用されます。";
        if (text == desc::sharpness) return "FSR 1 の鮮明さを調整します。";
        if (text == desc::low_vram) return "デコード済みテクスチャを圧縮して VRAM を節約します。再起動後に適用されます。";
        if (text == desc::quit) return "ゲームを終了します。未保存の進行状況は失われます。2 回選択してください。";
    }
    if (lang == 8) {
        if (text == desc::language) return "Язык этого меню.";
        if (text == desc::window_size) return "Размер окна в пикселях. Недоступно в полноэкранном режиме.";
        if (text == desc::internal) return "Внутреннее разрешение. Высокие значения нагружают GPU. Требуется перезапуск.";
        if (text == desc::vsync) return "Синхронизация кадров с экраном. При отключении возможны разрывы изображения.";
        if (text == desc::frame_cap) return "Максимальная частота кадров. Высоким значениям нужны подходящие ПК и монитор.";
        if (text == desc::fullscreen) return "Переключить полноэкранный режим. Также доступна клавиша F11.";
        if (text == desc::topmost) return "Поверх других окон. Недоступно в Wayland.";
        if (text == desc::volume) return "Громкость игры.";
        if (text == desc::mute) return "Выключить звук без изменения громкости.";
        if (text == desc::game_language) return "Выберите язык игры. Требуется перезапуск.";
        if (text == desc::preset) return "Положение кнопки A на контроллере.";
        if (text == desc::rumble) return "Сила вибрации. 0% отключает вибрацию.";
        if (text == desc::key) return "Выберите и нажмите клавишу. Esc отменяет выбор.";
        if (text == desc::pad) return "Выберите кнопку контроллера для действия.";
        if (text == desc::metrics) return "Показать панель производительности.";
        if (text == desc::fps) return "Кадры в секунду за последние 60 кадров.";
        if (text == desc::low) return "Частота 1% самых медленных из последних 300 кадров.";
        if (text == desc::speed) return "Скорость игры: 100% — нормальная скорость.";
        if (text == desc::graph) return "Время последних 300 кадров от 0 до 50 мс.";
        if (text == desc::position) return "Положение панели производительности.";
        if (text == desc::memory) return "При превышении Vulkan освобождает неиспользуемые изображения. Это не прямой лимит VRAM.";
        if (text == desc::upscaler) return "Способ увеличения изображения. FSR 1 сохраняет чёткость краёв.";
        if (text == desc::antialiasing) return "Сглаживает края. FXAA может слегка размывать детали.";
        if (text == desc::anisotropy) return "Повышает чёткость текстур под углом. Требуется перезапуск.";
        if (text == desc::sharpness) return "Настройка резкости FSR 1.";
        if (text == desc::low_vram) return "Сжимает декодированные текстуры для экономии VRAM. Требуется перезапуск.";
        if (text == desc::quit) return "Выйти из игры. Несохранённый прогресс будет потерян. Выберите дважды.";
    }
    if (lang == 10) {
        if (text == desc::language) return "Taal van dit menu.";
        if (text == desc::window_size) return "Venstergrootte in pixels. Niet beschikbaar in volledig scherm.";
        if (text == desc::internal) return "Interne resolutie. Hogere waarden belasten de GPU meer. Herstart vereist.";
        if (text == desc::vsync) return "Synchroniseert beelden met het scherm. Zonder synchronisatie kan tearing optreden.";
        if (text == desc::frame_cap) return "Maximale beeldsnelheid. Hogere waarden vereisen een geschikte pc en monitor.";
        if (text == desc::fullscreen) return "Schakelt volledig scherm in. Gebruik ook F11.";
        if (text == desc::topmost) return "Houdt het venster boven andere vensters. Niet beschikbaar op Wayland.";
        if (text == desc::volume) return "Volume van het spel.";
        if (text == desc::mute) return "Dempt het spel zonder het volume te wijzigen.";
        if (text == desc::game_language) return "Kies de speltaal. Herstart vereist.";
        if (text == desc::preset) return "Positie van de A-knop op de controller.";
        if (text == desc::rumble) return "Trilsterkte. 0% schakelt trillingen uit.";
        if (text == desc::key) return "Selecteer en druk op een toets. Esc annuleert.";
        if (text == desc::pad) return "Kies de controllerknop voor deze actie.";
        if (text == desc::metrics) return "Toont een prestatiepaneel.";
        if (text == desc::fps) return "Beelden per seconde over de laatste 60 beelden.";
        if (text == desc::low) return "Beeldsnelheid van de traagste 1% van de laatste 300 beelden.";
        if (text == desc::speed) return "Spelsnelheid: 100% is normale snelheid.";
        if (text == desc::graph) return "Tijd van de laatste 300 beelden, van 0 tot 50 ms.";
        if (text == desc::position) return "Positie van het prestatiepaneel.";
        if (text == desc::memory) return "Boven deze grens geeft Vulkan ongebruikte afbeeldingen vrij. Dit is geen directe VRAM-limiet.";
        if (text == desc::upscaler) return "Methode om de afbeelding te vergroten. FSR 1 houdt randen scherp.";
        if (text == desc::antialiasing) return "Maakt randen glad. FXAA kan details iets verzachten.";
        if (text == desc::anisotropy) return "Maakt schuin bekeken texturen scherper. Herstart vereist.";
        if (text == desc::sharpness) return "Past de scherpte van FSR 1 aan.";
        if (text == desc::low_vram) return "Comprimeert gedecodeerde texturen om VRAM te besparen. Herstart vereist.";
        if (text == desc::quit) return "Sluit het spel. Niet-opgeslagen voortgang gaat verloren. Selecteer twee keer.";
    }
    if (lang == 11) {
        if (text == desc::language) return "此菜单的语言。";
        if (text == desc::window_size) return "窗口的像素大小。全屏时不可用。";
        if (text == desc::internal) return "内部渲染分辨率。较高数值需要更多显卡性能。重启后生效。";
        if (text == desc::vsync) return "将帧与屏幕同步。关闭后可能出现画面撕裂。";
        if (text == desc::frame_cap) return "最大帧率。较高数值需要性能足够的电脑和显示器。";
        if (text == desc::fullscreen) return "切换全屏。也可以按 F11。";
        if (text == desc::topmost) return "窗口置顶。Wayland 下不可用。";
        if (text == desc::volume) return "游戏音量。";
        if (text == desc::mute) return "不改变音量即可静音。";
        if (text == desc::game_language) return "选择游戏语言。重启后生效。";
        if (text == desc::preset) return "手柄 A 键的位置。";
        if (text == desc::rumble) return "振动强度。0% 表示关闭。";
        if (text == desc::key) return "选择后按下按键。Esc 取消。";
        if (text == desc::pad) return "选择此操作的手柄按键。";
        if (text == desc::metrics) return "显示性能面板。";
        if (text == desc::fps) return "最近 60 帧的每秒帧数。";
        if (text == desc::low) return "最近 300 帧中最慢 1% 的帧率。";
        if (text == desc::speed) return "游戏速度：100% 为正常速度。";
        if (text == desc::graph) return "最近 300 帧的耗时，范围为 0 至 50 毫秒。";
        if (text == desc::position) return "性能面板的位置。";
        if (text == desc::memory) return "超过阈值时 Vulkan 会释放未使用的图像。这不是直接的显存上限。";
        if (text == desc::upscaler) return "图像放大方式。FSR 1 可保持边缘清晰。";
        if (text == desc::antialiasing) return "平滑锯齿。FXAA 可能稍微模糊细节。";
        if (text == desc::anisotropy) return "提高斜向纹理清晰度。重启后生效。";
        if (text == desc::sharpness) return "调整 FSR 1 锐度。";
        if (text == desc::low_vram) return "压缩解码后的纹理以节省显存。重启后生效。";
        if (text == desc::quit) return "退出游戏。未保存的进度将丢失。请选择两次确认。";
    }
    if (lang == 12) {
        if (text == desc::language) return "此選單的語言。";
        if (text == desc::window_size) return "視窗的像素大小。全螢幕時無法使用。";
        if (text == desc::internal) return "內部算繪解析度。較高數值需要更多顯示卡效能。重新啟動後生效。";
        if (text == desc::vsync) return "將影格與螢幕同步。關閉後可能出現畫面撕裂。";
        if (text == desc::frame_cap) return "最大影格率。較高數值需要效能足夠的電腦與螢幕。";
        if (text == desc::fullscreen) return "切換全螢幕。也可以按 F11。";
        if (text == desc::topmost) return "視窗置頂。Wayland 下無法使用。";
        if (text == desc::volume) return "遊戲音量。";
        if (text == desc::mute) return "不改變音量即可靜音。";
        if (text == desc::game_language) return "選擇遊戲語言。重新啟動後生效。";
        if (text == desc::preset) return "控制器 A 鍵的位置。";
        if (text == desc::rumble) return "震動強度。0% 表示關閉。";
        if (text == desc::key) return "選擇後按下按鍵。Esc 取消。";
        if (text == desc::pad) return "選擇此操作的控制器按鍵。";
        if (text == desc::metrics) return "顯示效能面板。";
        if (text == desc::fps) return "最近 60 個影格的每秒影格數。";
        if (text == desc::low) return "最近 300 個影格中最慢 1% 的影格率。";
        if (text == desc::speed) return "遊戲速度：100% 為正常速度。";
        if (text == desc::graph) return "最近 300 個影格的耗時，範圍為 0 至 50 毫秒。";
        if (text == desc::position) return "效能面板的位置。";
        if (text == desc::memory) return "超過門檻時 Vulkan 會釋放未使用的影像。這不是直接的顯示記憶體上限。";
        if (text == desc::upscaler) return "影像放大方式。FSR 1 可保持邊緣清晰。";
        if (text == desc::antialiasing) return "平滑鋸齒。FXAA 可能稍微模糊細節。";
        if (text == desc::anisotropy) return "提高斜向材質清晰度。重新啟動後生效。";
        if (text == desc::sharpness) return "調整 FSR 1 銳利度。";
        if (text == desc::low_vram) return "壓縮解碼後的材質以節省顯示記憶體。重新啟動後生效。";
        if (text == desc::quit) return "結束遊戲。未儲存的進度將遺失。請選擇兩次確認。";
    }
    if (lang == 7) {
        if (text == desc::language) return "لغة هذه القائمة.";
        if (text == desc::window_size) return "حجم النافذة بالبكسل. غير متاح في وضع ملء الشاشة.";
        if (text == desc::internal) return "دقة العرض الداخلية. القيم الأعلى تحتاج إلى قوة رسومية أكبر. تسري بعد إعادة التشغيل.";
        if (text == desc::vsync) return "مزامنة الإطارات مع الشاشة. قد يظهر تمزق الصورة عند إيقافها.";
        if (text == desc::frame_cap) return "الحد الأقصى للإطارات في الثانية. القيم الأعلى تحتاج إلى جهاز وشاشة مناسبين.";
        if (text == desc::fullscreen) return "تبديل ملء الشاشة. يمكن استخدام F11 أيضاً.";
        if (text == desc::topmost) return "إبقاء النافذة في المقدمة. غير متاح على Wayland.";
        if (text == desc::volume) return "مستوى صوت اللعبة.";
        if (text == desc::mute) return "كتم صوت اللعبة دون تغيير مستوى الصوت.";
        if (text == desc::game_language) return "اختر لغة اللعبة. تسري بعد إعادة التشغيل.";
        if (text == desc::preset) return "موضع زر A على وحدة التحكم.";
        if (text == desc::rumble) return "شدة اهتزاز وحدة التحكم. 0% يعطل الاهتزاز.";
        if (text == desc::key) return "اختر ثم اضغط مفتاحاً. اضغط Esc للإلغاء.";
        if (text == desc::pad) return "اختر زر وحدة التحكم لهذا الإجراء.";
        if (text == desc::metrics) return "عرض لوحة الأداء على الشاشة.";
        if (text == desc::fps) return "الإطارات في الثانية خلال آخر 60 إطاراً.";
        if (text == desc::low) return "معدل أبطأ 1% من آخر 300 إطار.";
        if (text == desc::speed) return "سرعة اللعبة: 100% هي السرعة الطبيعية.";
        if (text == desc::graph) return "مدة آخر 300 إطار من 0 إلى 50 مللي ثانية.";
        if (text == desc::position) return "موضع لوحة الأداء.";
        if (text == desc::memory) return "عند تجاوز الحد يحرر Vulkan الصور غير المستخدمة. هذا ليس حداً مباشراً لذاكرة الفيديو.";
        if (text == desc::upscaler) return "طريقة تكبير الصورة. يحافظ FSR 1 على وضوح الحواف.";
        if (text == desc::antialiasing) return "ينعم الحواف المتعرجة. قد يقلل FXAA وضوح التفاصيل قليلاً.";
        if (text == desc::anisotropy) return "يحسن وضوح الخامات المائلة. يسري بعد إعادة التشغيل.";
        if (text == desc::sharpness) return "ضبط حدة FSR 1.";
        if (text == desc::low_vram) return "ضغط الخامات المفككة لتوفير ذاكرة الفيديو. يسري بعد إعادة التشغيل.";
        if (text == desc::quit) return "إغلاق اللعبة. سيضيع التقدم غير المحفوظ. اختر مرتين للتأكيد.";
    }
    if (lang == 9) {
        if (text == desc::language) return "Bu menünün dili.";
        if (text == desc::window_size) return "Pencere boyutu piksel cinsindendir. Tam ekranda kullanılamaz.";
        if (text == desc::internal) return "Dahili çözünürlük. Daha yüksek değerler GPU yükünü artırır. Yeniden başlatınca uygulanır.";
        if (text == desc::vsync) return "Kareleri ekranla eşzamanlar. Kapalıyken görüntü yırtılması oluşabilir.";
        if (text == desc::frame_cap) return "En yüksek kare hızı. Yüksek değerler uygun bir bilgisayar ve ekran gerektirir.";
        if (text == desc::fullscreen) return "Tam ekranı değiştirir. F11 de kullanılabilir.";
        if (text == desc::topmost) return "Pencereyi diğerlerinin üstünde tutar. Wayland üzerinde kullanılamaz.";
        if (text == desc::volume) return "Oyunun ses düzeyi.";
        if (text == desc::mute) return "Ses düzeyini değiştirmeden oyunu sessize alır.";
        if (text == desc::game_language) return "Oyun dilini seçin. Yeniden başlatınca uygulanır.";
        if (text == desc::preset) return "Kontrolcüdeki A düğmesinin konumu.";
        if (text == desc::rumble) return "Titreşim gücü. %0 titreşimi kapatır.";
        if (text == desc::key) return "Seçip bir tuşa basın. Esc iptal eder.";
        if (text == desc::pad) return "Bu işlem için kontrolcü düğmesini seçin.";
        if (text == desc::metrics) return "Ekranda performans panelini gösterir.";
        if (text == desc::fps) return "Son 60 karede saniye başına kare sayısı.";
        if (text == desc::low) return "Son 300 karenin en yavaş %1 bölümünün kare hızı.";
        if (text == desc::speed) return "Oyun hızı: %100 normal hızdır.";
        if (text == desc::graph) return "Son 300 karenin süresi, 0 ile 50 ms arasında.";
        if (text == desc::position) return "Performans panelinin konumu.";
        if (text == desc::memory) return "Bu eşik aşılınca Vulkan kullanılmayan görüntüleri serbest bırakır. Doğrudan VRAM sınırı değildir.";
        if (text == desc::upscaler) return "Görüntüyü büyütme yöntemi. FSR 1 kenarları keskin tutar.";
        if (text == desc::antialiasing) return "Tırtıklı kenarları yumuşatır. FXAA ince ayrıntıları biraz yumuşatabilir.";
        if (text == desc::anisotropy) return "Açıyla görülen dokuları keskinleştirir. Yeniden başlatınca uygulanır.";
        if (text == desc::sharpness) return "FSR 1 keskinliğini ayarlar.";
        if (text == desc::low_vram) return "VRAM tasarrufu için çözümlenmiş dokuları sıkıştırır. Yeniden başlatınca uygulanır.";
        if (text == desc::quit) return "Oyunu kapatır. Kaydedilmemiş ilerleme kaybolur. Onaylamak için iki kez seçin.";
    }
    if (lang != 1) return text;
    if (text == desc::language) return "Lingua di questo menu.";
    if (text == desc::window_size) return "Dimensioni della finestra in pixel. Non disponibile a schermo intero.";
    if (text == desc::internal) return "Risoluzione del gioco. Originale lascia scegliere al gioco; valori più alti richiedono più potenza grafica. Si applica al riavvio.";
    if (text == desc::vsync) return "Sincronizza i fotogrammi con lo schermo. Disattivandola possono comparire interruzioni nell’immagine.";
    if (text == desc::frame_cap) return "Limite massimo di fotogrammi al secondo. Valori più alti richiedono un PC e uno schermo adeguati.";
    if (text == desc::fullscreen) return "Mostra il gioco a schermo intero. Puoi usare anche F11.";
    if (text == desc::topmost) return "Mantiene la finestra sopra le altre. Non disponibile su Wayland.";
    if (text == desc::volume) return "Regola il volume del gioco.";
    if (text == desc::mute) return "Disattiva il suono senza cambiare il volume.";
    if (text == desc::game_language) return "Scegli la lingua del gioco. La modifica si applica al riavvio.";
    if (text == desc::preset) return "Scegli la posizione del pulsante A sul controller.";
    if (text == desc::rumble) return "Regola la vibrazione del controller. A 0% è disattivata.";
    if (text == desc::key) return "Seleziona e premi il tasto desiderato. Esc annulla.";
    if (text == desc::pad) return "Scegli il pulsante del controller per questa azione.";
    if (text == desc::metrics) return "Mostra un pannello delle prestazioni sullo schermo.";
    if (text == desc::fps) return "Fotogrammi al secondo negli ultimi 60 fotogrammi.";
    if (text == desc::low) return "Frequenza dell’1% dei fotogrammi più lenti tra gli ultimi 300.";
    if (text == desc::speed) return "Velocità del gioco rispetto alla velocità prevista: 100% è normale.";
    if (text == desc::graph) return "Durata degli ultimi 300 fotogrammi, da 0 a 50 ms.";
    if (text == desc::position) return "Scegli l’angolo in cui mostrare il pannello.";
    if (text == desc::memory) return "Soglia oltre la quale Vulkan libera le immagini riutilizzabili non in uso. Non è un limite diretto della VRAM.";
    if (text == desc::upscaler) return "Scegli come ingrandire l’immagine. FSR 1 mantiene i contorni più nitidi.";
    if (text == desc::antialiasing) return "Riduce le scalettature. FXAA può rendere meno nitidi i dettagli sottili.";
    if (text == desc::anisotropy) return "Migliora la nitidezza delle texture viste di sbieco. Si applica al riavvio.";
    if (text == desc::sharpness) return "Regola la nitidezza di FSR 1.";
    if (text == desc::low_vram) return "Mantiene compresse le texture decodificate per ridurre la memoria video, con una lieve perdita di qualità. Si applica al riavvio.";
    if (text == desc::quit) return "Chiude il gioco. I progressi non salvati andranno persi. Seleziona due volte per confermare.";
    return text;
}

const Settings kDefaults{};

void video(MenuContext& c, const Labels& l) {
    Settings& s = c.settings;
    const int lang = std::clamp(s.language, 0, 12);
    rows::begin("##video", c.height);
    rows::section(kCommon[lang][0]);
    const char* aspect_names[] = {"16:9 (original)", "21:9", "32:9"};
    int aspect = std::max(0, s.aspect_ratio);
    const char* aspect_label = lang == 1 ? "Formato schermo" : "Aspect ratio";
    const char* aspect_desc = lang == 1 ? "Si applica al prossimo avvio del gioco." : "Takes effect the next time the game starts.";
    if (rows::choice(aspect_label, &aspect, aspect_names, 3, 0, aspect_desc)) {
        s.aspect_ratio = aspect;
        settings_changed(true);
        ui::notify(aspect_desc);
    }
    // the window's size: one of four, or the size it has now (dragged) as a fifth
    const char* sizes[5] = {"1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", nullptr};
    char custom[32];
    int size = -1;
    for (int i = 0; i < 4; ++i)
        if (kSizes[i][0] == s.width && kSizes[i][1] == s.height) size = i;
    int n = 4;
    if (size < 0) std::snprintf(custom, sizeof custom, "%d x %d", s.width, s.height), sizes[4] = custom, size = 4, n = 5;
    if (!c.host.can_resize || s.fullscreen) rows::disable_next();
    if (rows::choice(l.window_size, &size, sizes, n, 0, translated_desc(desc::window_size, lang)) && size < 4)
        s.width = kSizes[size][0], s.height = kSizes[size][1], settings_changed(true);
    if (rows::toggle(l.fullscreen, &s.fullscreen, kDefaults.fullscreen, translated_desc(desc::fullscreen, lang))) settings_changed(true);
    if (!c.host.can_always_on_top) rows::disable_next();
    if (rows::toggle(l.topmost, &s.always_on_top, kDefaults.always_on_top, translated_desc(desc::topmost, lang))) settings_changed(true);
    if (rows::toggle(lang == 2 ? "Seitenverhältnis beibehalten" : "Preserve aspect ratio", &s.preserve_aspect, true,
        lang == 2 ? "Erhält die Form des Spielbilds und zentriert es bei Bedarf mit schwarzen Balken. Aus streckt es auf das Fenster. Wirkt sofort, auch mit FSR 1 und Bildgenerierung. Erweitert nicht den sichtbaren Spielbereich." :
        "Keep the game's picture shape and centre it with black bars when needed. Off stretches it to the window. Applies immediately, including FSR 1 and frame generation. This does not expand the game's field of view.")) settings_changed(true);
    rows::section(kCommon[lang][1]);
    if (rows::choice(l.internal_resolution, &s.internal_resolution, kInternal[lang], 7, kDefaults.internal_resolution,
                     translated_desc(desc::internal, lang)))
        settings_changed(true), ui::notify(l.restart_note);
    const char* upscalers[] = {"Bilinear", "FSR 1"};
    if (rows::choice(kCommon[lang][3], &s.upscaler, upscalers, 2, kDefaults.upscaler, translated_desc(desc::upscaler, lang))) settings_changed(true);
    rows::indent(1);
    if (s.upscaler != 1) rows::disable_next();
    int sharp = 200 - s.fsr_sharpness;  // shown as sharpness: 0 soft .. 200 sharp
    if (rows::integer(kCommon[lang][4], &sharp, 0, 200, 5, 200 - kDefaults.fsr_sharpness, "%d%%", translated_desc(desc::sharpness, lang)))
        s.fsr_sharpness = 200 - sharp, settings_changed(true);
    rows::indent(-1);
    const char* aa[] = {"Off", "FXAA"};
    if (rows::choice(kCommon[lang][5], &s.antialiasing, aa, 2, kDefaults.antialiasing, translated_desc(desc::antialiasing, lang)))
        settings_changed(true);
    const char* af_names[] = {"Game default", "2x", "4x", "8x", "16x"};
    const int af_levels[] = {0, 2, 4, 8, 16};
    int af = 0;
    for (int i = 0; i < 5; ++i)
        if (af_levels[i] == s.anisotropy) af = i;
    if (rows::choice(kCommon[lang][6], &af, af_names, 5, 0, translated_desc(desc::anisotropy, lang)))
        s.anisotropy = af_levels[af], settings_changed(true), ui::notify(l.restart_note);
    rows::section(kCommon[lang][2]);
    const char* vsync_names[] = {"On", "Fast", "Adaptive", "Off"};
    if (rows::choice(kCommon[lang][7], &s.vsync, vsync_names, 4, kDefaults.vsync, translated_desc(desc::vsync, lang))) settings_changed(true);
    const char* caps[] = {"30 fps", "60 fps", "90 fps", "120 fps", "144 fps"};
    constexpr int cap_values[] = {30, 60, 90, 120, 144};
    int cap = 1;
    for (int i = 0; i < 5; ++i)
        if (cap_values[i] == s.frame_cap) cap = i;
    if (lan::active()) rows::disable_next();
    if (rows::choice(l.frame_cap_label, &cap, caps, 5, 1,
        lan::active()?"The LAN host sets the shared simulation target. Change it when creating the next room.":translated_desc(desc::frame_cap, lang)))
        s.frame_cap = cap_values[cap], settings_changed(true);
    const char* latency_modes[] = {lang == 2 ? "Niedrig (1 Bild voraus)" : "Low (1 frame ahead)",
                                  lang == 2 ? "Ausgewogen (2 Bilder voraus)" : "Balanced (2 frames ahead)"};
    int ahead = std::clamp(s.render_ahead,1,2)-1;
    if (rows::choice(lang == 2 ? "Eingabeverzögerung" : "Input latency", &ahead, latency_modes, 2, 0,
        lang == 2 ? "Niedrig begrenzt die Renderwarteschlange auf ein Bild. Ausgewogen erlaubt zwei Bilder und mehr gleichzeitige CPU-/GPU-Arbeit. Das ist keine Messung der Eingabelatenz: VSync, Bildschirm, Bildgenerierung und LAN-Wartezeiten wirken weiterhin mit." :
        "Low limits the game to one queued picture instead of two, reducing render-ahead latency. Balanced allows more CPU/GPU overlap and can be faster. This is not an input-lag measurement: VSync, display processing, frame generation and LAN waits still add latency.")) {
        s.render_ahead = ahead+1; settings_changed(true);
    }
    const char* fg_modes[] = {lang == 2 ? "Aus" : "Off", "2x", "3x", "4x", "5x", "6x", "7x", "8x"};
    int fg = s.framegen ? std::clamp(s.framegen_multiplier, 2, 8)-1 : 0;
    if (rows::choice(lang == 2 ? "Bildgenerierung" : "Frame generation", &fg, fg_modes, 8, 0,
        lang == 2 ? "Experimentelle Zwischenbilder per Optical Flow. 4x erzeugt drei Zwischenbilder je Spielbild, ohne die Simulation zu beschleunigen. Höhere Faktoren kosten GPU-Zeit und können Bildfehler oder Verzögerung verstärken. 60 Spiel-FPS bei 4x benötigen für die volle Ausgabe einen 240-Hz-Bildschirm. Bei GPU-Limit ausgeschaltet lassen." :
        "Experimental optical-flow interpolation. 4x makes three intermediate pictures per game frame; it does not speed up the game. Higher factors cost more GPU time and may add artifacts or latency. Use a matching high-refresh display (60 game FPS at 4x needs 240 Hz). Leave off when GPU-limited.")) {
        s.framegen = fg != 0;
        if (fg) s.framegen_multiplier = fg+1;
        settings_changed(true);
    }
    if (rows::toggle(lang == 2 ? "Objekte beim Kompilieren auslassen" : "Skip draws while compiling", &s.pipeline_skip_draws, kDefaults.pipeline_skip_draws,
                     lang == 2 ? "Aus erhält alle Zeichenbefehle. Neue Shader können beim ersten Auftreten kurz blockieren. Ein lässt Objekte während der Kompilierung weg und kann dadurch Flackern oder verschwundene Geometrie verursachen." :
                     "Off keeps objects visible while new shaders compile, but may pause on first use. "
                     "On reduces those pauses by temporarily omitting objects and can cause flickering."))
        settings_changed(true);
    rows::page_note(l.restart_note);
    rows::end();
}

void audio(MenuContext& c, const Labels& l) {
    Settings& s = c.settings;
    const int lang = std::clamp(s.language, 0, 12);
    rows::begin("##audio", c.height);
    rows::section(l.audio);
    if (rows::integer(l.volume, &s.volume, 0, 200, 5, kDefaults.volume, "%d%%", translated_desc(desc::volume, lang))) settings_changed(true);
    if (rows::toggle(l.mute, &s.muted, kDefaults.muted, translated_desc(desc::mute, lang))) settings_changed(true);
    if (rows::toggle(lang == 2 ? "Audio-Vorladung (experimentell)" : "Audio prefetch (experimental)", &s.preload_music, kDefaults.preload_music,
        lang == 2 ? "Optional: kleinere Musik- und Umgebungsstreams im Hintergrund in bis zu 64 MiB RAM vorladen (8 MiB je Datei). Die Vergleichstests zeigten keine verlässliche Verbesserung der Bildzeiten; deshalb standardmäßig aus. Klang und Format bleiben erhalten, ohne Datenträger-Cache. Normales Streaming bleibt aktiv, bis die Daten bereit sind. Gilt für zukünftige Audio-Ladevorgänge." :
        "Optionally prefetch smaller music and ambience streams using up to 64 MiB RAM (8 MiB per file). Comparison tests did not show a consistent frame-time improvement, so this defaults off. Audio bytes and format are preserved without a disk cache. Normal streaming remains active until ready. Applies to future audio loads.")) settings_changed(true);
    rows::section(l.game_language);
    if (rows::choice(l.game_language, &s.game_language, kGameLanguageNames, int(std::size(kGameLanguageNames)),
                     kDefaults.game_language, translated_desc(desc::game_language, lang)))
        settings_changed(true), ui::notify(l.game_language_restart);
    rows::page_note(l.game_language_restart);
    rows::end();
}

void controls(MenuContext& c, const Labels& l) {
    Settings& s = c.settings;
    const int lang = std::clamp(s.language, 0, 12);
    // Show the actual game-action layout and live input before the editable bindings.
    const float k=std::clamp(c.height/1080.f,.5f,3.f);
    const auto corner=ImGui::GetCursorScreenPos();
    const auto width=ImGui::GetContentRegionAvail().x;
    auto* dl=ImGui::GetWindowDrawList();
    const uint64_t buttons=pad_buttons();
    const char* action_names[]={"X","Y","A","B"};
    const size_t actions[]={CtlX,CtlY,CtlA,CtlB};
    const uint64_t bits[]={plat::kX,plat::kY,plat::kA,plat::kB};
    const auto device_kind=plat::controller_kind(0);
    for(int i=0;i<4;++i) {
        const ImVec2 p{corner.x+i*width/4,corner.y},q{p.x+width/4-6*k,p.y+76*k};
        const bool held=(buttons&bits[i])!=0;
        dl->AddRectFilled(p,q,ui::fade(held?ui::theme().accent:IM_COL32(255,255,255,18)),8*k);
        const std::string text=std::string(action_names[i])+" / "+physical_button_name(device_kind,controller_binding(s,actions[i]));
        dl->AddText({p.x+8*k,p.y+10*k},held?IM_COL32(12,16,24,255):ui::theme().text,text.c_str());
        dl->AddText({p.x+8*k,p.y+42*k},held?IM_COL32(12,16,24,255):ui::theme().text_dim,held?"Pressed":"Ready");
    }
    ImGui::Dummy({width,84*k});
    rows::begin("##controls", c.height);
    rows::section("Players & devices");
    if(lan::active()) rows::disable_next();
    if (rows::toggle("Keyboard as player 1", &s.keyboard_separate_player, false,
        lan::active() ? "LAN uses this PC's primary gamepad or keyboard for your assigned player. The local-co-op keyboard preference applies after leaving LAN." : "Reserve player 1 for the keyboard and assign gamepads to players 2-4. Turn this off to use up to four gamepads, with keyboard input shared by player 1. Changing this reassigns controllers."))
        settings_changed(true);
    for (uint32_t player = 0; player < plat::kPlayerCount; ++player) {
        char label[32]; std::snprintf(label, sizeof label, "Player %u", player + 1);
        const bool network=lan::active(), local=!network || player==lan::local_player();
        const auto name = network ? (local ? "This PC: "+plat::controller_name(0) : lan::connected(player) ? std::string("LAN peer") : std::string("Not connected")) : plat::controller_name(player);
        const bool connected = network ? lan::connected(player) : plat::player_connected(player);
        if (!connected || !local) rows::disable_next();
        if (rows::action(label, name.c_str(), network ? "Every PC contributes its primary gamepad or keyboard. Choose the matching number of players in Wonder. Select this PC's row to test local rumble." : "Connect up to four gamepads for local co-op. Player 1 also uses the keyboard. Select a connected controller to test its rumble. Unplugging one pad preserves the other player slots."))
            plat::rumble_player(network?0:player, 0.3f, 0.3f);
    }
    const char* icon_names[] = {"Automatic at startup", "Keyboard", "Nintendo", "Xbox", "PlayStation"};
    if (rows::choice("Button prompts", &s.button_icons, icon_names, 5, 0,
        "Choose the button icons shown in the game. Automatic detects player 1's controller at startup. Icons follow your bindings. Restart the game after changing prompts or bindings.")) {
        settings_changed(true); ui::notify("Restart the game to update its button prompts.");
    }
    rows::section(kHeaders[lang][1]);
    if(rows::action("Reset controller bindings","Use selected layout","Clear custom button overrides and return to the selected Xbox, Nintendo or PlayStation preset. Keyboard bindings stay as configured.")) {
        s.controller_custom_mask=0;settings_changed(true);ui::notify("Controller bindings reset to the selected layout.");
    }
    const char* presets[] = {"Xbox / PlayStation: bottom confirms", "Nintendo: right confirms", "PlayStation: Circle confirms"};
    if (rows::choice(l.control_preset, &s.controller_preset, presets, 3, kDefaults.controller_preset,
        "Choose bottom-button confirmation (Xbox A / PlayStation Cross), Nintendo layout, or PlayStation Circle confirmation. Circle confirmation swaps Cross and Circle only. Per-action custom bindings below take priority. Restart to refresh in-game button prompts."))
        settings_changed(true), ui::notify("Restart the game to update its button prompts.");
    if (rows::integer(kCommon[lang][8], &s.rumble, 0, 100, 10, kDefaults.rumble, "%d%%", translated_desc(desc::rumble, lang))) settings_changed(true);
    const auto device = plat::controller_kind(0);
    const char* physical[kControlCount];
    for (size_t i = 0; i < kControlCount; ++i) physical[i] = physical_button_name(device, int(i));
    for (size_t a = 0; a < kControlCount; ++a) {
        int phys = controller_binding(s, a);
        const int def = default_controller_binding(a, s.controller_preset);
        char id[48];
        std::snprintf(id, sizeof id, "%s###pad%zu", kActions[a], a);
        if (rows::choice(id, &phys, physical, int(kControlCount), def, translated_desc(desc::pad, lang))) {
            s.controller_bindings[a] = phys;
            s.controller_custom_mask |= 1u << a;
            if (phys == def) s.controller_custom_mask &= ~(1u << a);
            settings_changed(true);
        }
    }
    rows::section(kHeaders[lang][0]);
    int keyboard_mode=s.keyboard_actions?0:1;
    const char* modes[]={"Dedicated PC actions","Legacy Switch buttons"};
    if(rows::choice("Keyboard controls",&keyboard_mode,modes,2,0,
        "Named PC actions include single-key ground pound and standee shortcuts. Sprint/grab/throw remain one contextual action. Legacy preserves direct Switch-button mappings. Switching mode applies its default keyboard layout; controllers are unchanged.")) {
        s.keyboard_actions=keyboard_mode==0;
        s.keyboard_bindings=s.keyboard_actions?input::recommended_keyboard_bindings():input::legacy_keyboard_bindings(input::native_keyboard_codes());
        s.keyboard_standee_key=input::default_standee_key();c.capture_action=-1;settings_changed(true);
    }
    if(rows::action("Reset keyboard layout",s.keyboard_actions?"Space jump / Shift sprint":"Legacy defaults",
        "Replace the current keyboard keys with this mode's defaults. PC mode: WASD movement, Space jump, Shift sprint, E spin, Ctrl ground pound, F use item/confirm, Q greet, V standee, Enter pause. Esc/F1 opens PC settings. Restart to refresh game prompts.")) {
        s.keyboard_bindings=s.keyboard_actions?input::recommended_keyboard_bindings():input::legacy_keyboard_bindings(input::native_keyboard_codes());
        s.keyboard_standee_key=input::default_standee_key();c.capture_action=-1;settings_changed(true);
        ui::notify("Keyboard layout reset. Restart to refresh the game's key prompts.");
    }
    auto keyboard_row=[&](size_t action,const char* label) {
        char id[96];std::snprintf(id,sizeof id,"%s###key%zu",label,action);
        const bool waiting=c.capture_action==int(action);
        const int binding=action==kControlCount?s.keyboard_standee_key:s.keyboard_bindings[action];
        const char* name=waiting?"Press a key...":binding==0?"Unbound":c.host.key_name?c.host.key_name(binding):"?";
        if(rows::action(id,name,"Select and press a key. Standee briefly crouches then presses use; stand on the ground. Delete clears the binding; Esc cancels. Keys apply immediately; restart to update game prompts.",true,waiting))c.capture_action=int(action);
    };
    if(s.keyboard_actions) {
        const char* schemes[]={"B jump / Y dash (standard)","A jump / B dash (alternate)"};
        if(rows::choice("Wonder's game layout",&s.keyboard_game_layout,schemes,2,0,
            "Match Wonder's own Settings > Dash and Jump Controls. This translates your PC action keys to either game setting; the PC keys stay the same. It does not change gamepad mappings or edit your game save."))settings_changed(true);
        constexpr size_t order[]={CtlLeft,CtlRight,CtlUp,CtlDown,CtlB,CtlY,CtlR,CtlZL,kControlCount,CtlA,CtlX,CtlPlus,CtlMinus};
        for(size_t action:order)keyboard_row(action,action==kControlCount?"Place standee":input::keyboard_action_name(action));
        static bool advanced=false;
        if(rows::action("Advanced keyboard bindings",advanced?"Hide":"Show","Optional original shoulder/stick inputs. Duplicate spin and unused stick clicks are unbound in the PC defaults."))advanced=!advanced;
        if(advanced)for(size_t action:{size_t(CtlL),size_t(CtlZR),size_t(CtlLStick),size_t(CtlRStick)})keyboard_row(action,kActions[action]);
    } else {
        for(size_t action=0;action<kControlCount;++action)keyboard_row(action,kActions[action]);
    }
    rows::page_note(s.controller_preset == 2 ? "Circle confirms; Cross backs out. Custom bindings take priority." : s.controller_preset == 1 ? l.nintendo_desc : l.standard_desc);
    rows::end();
}

void overlay_tab(MenuContext& c) {
    Settings& s = c.settings;
    const int lang = std::clamp(s.language, 0, 12);
    const auto& o = kOverlayLabels[lang];
    rows::begin("##overlay", c.height);
    if (rows::toggle(o[0], &s.metrics_enabled, kDefaults.metrics_enabled, translated_desc(desc::metrics, lang))) settings_changed(true);
    rows::indent(1);
    const struct {
        bool* v;
        bool def;
        const char *label, *desc;
    } items[] = {{&s.metric_fps, kDefaults.metric_fps, o[1], translated_desc(desc::fps, lang)},
                 {&s.metric_one_percent_low, kDefaults.metric_one_percent_low, o[2], translated_desc(desc::low, lang)},
                 {&s.metric_game_speed, kDefaults.metric_game_speed, o[3], translated_desc(desc::speed, lang)},
                 {&s.metric_frametime_graph, kDefaults.metric_frametime_graph, o[4], translated_desc(desc::graph, lang)}};
    for (const auto& it : items) {
        if (!s.metrics_enabled) rows::disable_next();
        if (rows::toggle(it.label, it.v, it.def, it.desc)) settings_changed(true);
    }
    rows::indent(-1);
    if (!s.metrics_enabled) rows::disable_next();
    if (rows::choice(o[5], &s.overlay_position, kPositions[lang], 4, kDefaults.overlay_position, translated_desc(desc::position, lang)))
        settings_changed(true);
    rows::section(lang == 2 ? "Schnellauswahl" : "Quick presets");
    const bool compact = s.metrics_enabled && s.metric_fps && !s.metric_one_percent_low &&
                         !s.metric_game_speed && !s.metric_frametime_graph && s.overlay_position == 1;
    if (rows::action(lang == 2 ? "Kompakte FPS-Anzeige" : "Compact FPS counter",
                     compact ? (lang == 2 ? "Aktiv" : "Active") : (lang == 2 ? "Anwenden" : "Apply"),
                     lang == 2 ? "Zeigt FPS und Bildzeit oben rechts ohne Diagramm. So bleibt mehr vom Spiel sichtbar. Bei LAN zeigt die Anzeige zusätzlich die gemessene Eingabe-Wartezeit; diese ist kein Netzwerk-Ping." :
                     "Show FPS and frame time at the top right without the graph, leaving more of the game visible. During LAN play the measured input wait is also shown; it is not network ping.")) {
        s.metrics_enabled = s.metric_fps = true;
        s.metric_one_percent_low = s.metric_game_speed = s.metric_frametime_graph = false;
        s.overlay_position = 1;
        settings_changed(true);
    }
    if (rows::action(lang == 2 ? "Ausführliche Leistungsanzeige" : "Detailed performance panel",
                     lang == 2 ? "Anwenden" : "Apply",
                     lang == 2 ? "Aktiviert FPS, 1%-Lows, Spielgeschwindigkeit und das Bildzeit-Diagramm an der gewählten Position. Ändert weder Grafikqualität noch Eingabe- oder Audioeinstellungen." :
                     "Enable FPS, 1% lows, game speed and the frame-time graph at the selected position. Graphics quality, input and audio settings stay as configured.")) {
        s.metrics_enabled = s.metric_fps = s.metric_one_percent_low = s.metric_game_speed = s.metric_frametime_graph = true;
        settings_changed(true);
    }
    rows::end();
}

void system_tab(MenuContext& c, const Labels& l) {
    Settings& s = c.settings;
    rows::begin("##system", c.height);
    rows::section("Menu & comfort");
    auto& comfort=usability_preferences();
    if(rows::toggle("Full-window settings",&comfort.menu_fullscreen,false,"Fill the window, or use a floating settings panel that you can move and resize.")) persist_usability();
    if(rows::toggle("Reduced menu motion",&comfort.reduced_motion,false,"Remove menu transitions and moving highlights. Applies immediately.")) persist_usability();
    if(rows::integer("Menu opacity",&comfort.menu_opacity,75,100,5,100,"%d%%","Adjust the settings panel background. The game remains visible around a floating panel.")) persist_usability();
    rows::section(l.system);
    const int lang = std::clamp(s.language, 0, 12);
    if (rows::choice(kCommon[lang][11], &s.language, kLanguageNames, int(std::size(kLanguageNames)), kDefaults.language, translated_desc(desc::language, lang)))
        settings_changed(true);
    if (rows::integer(l.memory_limit, &s.memory_limit_gb, 2, 32, 1, kDefaults.memory_limit_gb, "%d GB", translated_desc(desc::memory, lang)))
        settings_changed(true);
    if (rows::toggle(l.low_vram, &s.low_vram_profile, kDefaults.low_vram_profile, translated_desc(desc::low_vram, lang)))
        settings_changed(true), ui::notify(l.restart_low_vram);
    if (rows::toggle("Fast shader loading", &s.shader_bundle, true,
        "Cache translated shaders as you play to speed later launches. Needs extra disk space and up to 512 MiB of memory. Modified or damaged entries fall back to the original shader files. Restart to apply.")) {
        settings_changed(true); ui::notify("Restart the game to change shader loading.");
    }
    if (rows::toggle(lang == 2 ? "Textur-Konvertierungen speichern" : "Cache texture conversions", &s.texture_cache, true,
        lang == 2 ? "Speichert Textur-Konvertierungen des Low-VRAM-Profils für spätere Ladevorgänge. Bis zu 256 MiB auf dem Datenträger; Schreibzugriffe erfolgen im Hintergrund. Fehlerhafte oder geänderte Daten werden neu konvertiert. Ändert nicht die gewählte Texturqualität. Wirkt sofort auf zukünftige Konvertierungen." :
        "Reuse low-VRAM texture conversions on later loads. Uses up to 256 MiB on disk, with bounded background writes. Changed or damaged data falls back to conversion. This does not change the selected texture quality. Applies immediately to future conversions.")) settings_changed(true);
    // Quit: A once arms it (the row pulses, "Press again to quit"), A again within 4 s quits as closing the window does
    // (the pipeline cache is saved); progress since the game's last save is lost
    static double armed_at = -10;
    const bool armed = ImGui::GetTime() - armed_at < 4.0;
    rows::section(kCommon[lang][9]);
    if (rows::action(kCommon[lang][10], armed ? "Press again to quit" : "", translated_desc(desc::quit, lang), true, armed)) {
        if (armed) log("overlay: quit from the menu"), plat::exit_process(0);
        armed_at = ImGui::GetTime();
    }
#if NX_GAME_LIBRARY
    if(c.startup_settings) {
        static double return_armed_at=-10;
        const bool return_armed=ImGui::GetTime()-return_armed_at<4;
        if(rows::action("Return to launcher",return_armed?"Press again to leave":"Profiles & game options",
            "Close the game and reopen the launcher to switch profiles, manage saves or mods. Unsaved progress is lost. Leaving LAN disconnects this PC; the host leaving ends the room. Select twice within four seconds to confirm.",true,return_armed)) {
            if(return_armed)launcher::return_to_launcher();
            return_armed_at=ImGui::GetTime();
        }
    }
#endif
    rows::page_note(l.restart_low_vram);
    rows::end();
}

// The close X in the top-right corner (as Savage Wind's menu.mm: lit when hovered or selected).
bool close_button(float k) {
    const float bx = 44 * k;
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bx);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
    const bool close = ImGui::InvisibleButton("##close", ImVec2(bx, bx), ImGuiButtonFlags_EnableNav);
    ImGui::PopStyleColor();
    const bool hot = ImGui::IsItemHovered() || (ImGui::IsItemFocused() && ImGui::GetCurrentContext()->NavCursorVisible);
    const float t = ui::eased(ImGui::GetItemID(), hot ? 1.0f : 0.0f, 22.0f);
    const ImU32 col = ui::fade(ui::mix(ui::theme().text_dim, ui::theme().accent, t));
    const float m = bx * 0.3f, w = (2.4f + 0.6f * t) * 1.5f * k;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + bx, p.y + bx), ui::fade(IM_COL32(255, 255, 255, 20), t), 8 * k);
    dl->AddLine(ImVec2(p.x + m, p.y + m), ImVec2(p.x + bx - m, p.y + bx - m), col, w);
    dl->AddLine(ImVec2(p.x + bx - m, p.y + m), ImVec2(p.x + m, p.y + bx - m), col, w);
    return close;
}

}  // namespace

int menu_tab_count() { return kTabs; }
const char* const* menu_language_names() { return kLanguageNames; }

void draw_menu(MenuContext& c) {
    Settings& s = c.settings;
    const Labels& l = kLabels[std::clamp(s.language, 0, 12)];
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float k = std::clamp(c.height / 1080.0f, 0.5f, 3.0f);
    auto& comfort=usability_preferences();
    const float margin=comfort.menu_fullscreen?0.f:16*k;
    const ImVec2 maximum{std::max(1.f,display.x-2*margin),std::max(1.f,display.y-2*margin)};
    const ImVec2 minimum{std::min(maximum.x,760*k),std::min(maximum.y,620*k)};
    static bool previous_fullscreen=false;
    static ImVec2 floating_size{0,0};
    if(!comfort.menu_fullscreen && previous_fullscreen && floating_size.x>0)
        ImGui::SetNextWindowSize(floating_size);
    previous_fullscreen=comfort.menu_fullscreen;
    if(comfort.menu_fullscreen) {
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(display);
    } else {
        ImGui::SetNextWindowPos({display.x*.5f,display.y*.5f},ImGuiCond_Appearing,{.5f,.5f});
        ImGui::SetNextWindowSize({std::min(maximum.x,1440*k),std::min(maximum.y,900*k)},ImGuiCond_FirstUseEver);
    }
    ImGui::SetNextWindowSizeConstraints(minimum,maximum);
    ImGui::SetNextWindowBgAlpha(comfort.menu_opacity/100.f);
    ImGuiWindowFlags f=ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings;
    if(comfort.menu_fullscreen) f|=ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize;
    if(!c.open)f|=ImGuiWindowFlags_NoInputs;
    if (c.appearing) ImGui::SetNextWindowFocus();  // keyboard and controller navigation start in the menu
    if (!ImGui::Begin("##nx_menu", nullptr, f)) {
        ImGui::End();
        return;
    }
    if(!comfort.menu_fullscreen) {
        floating_size=ImGui::GetWindowSize();
        const auto pos=ImGui::GetWindowPos(), size=ImGui::GetWindowSize();
        ImGui::SetWindowPos({std::clamp(pos.x,margin,std::max(margin,display.x-margin-size.x)),
                             std::clamp(pos.y,margin,std::max(margin,display.y-margin-size.y))});
    }
    ImDrawList* wdl = ImGui::GetWindowDrawList();
    {  // a thin accent rule along the top edge
        const ImVec2 a = ImGui::GetWindowPos(), b(a.x + ImGui::GetWindowWidth(), a.y);
        wdl->AddRectFilled(ImVec2(a.x + 14 * k, a.y), ImVec2(b.x - 14 * k, a.y + 3 * k), ui::fade(ui::theme().accent, 0.85f));
    }
    ImGui::PushFont(ui::title());
    ImGui::SetWindowFontScale(0.74f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::theme().accent));
    ImGui::TextUnformatted(l.title);
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();
    if (close_button(k)) c.open = false;
    ImGui::TextDisabled("%s",comfort.menu_fullscreen ? "Esc / F1: resume  |  L / R: tabs" : "Drag the background to move  |  Drag a corner to resize  |  Esc: resume");
    if(!c.startup_settings) ImGui::TextDisabled("Launcher settings");
    else ImGui::TextDisabled("%s",lan::active()?"LAN continues while settings are open.":"Settings do not pause gameplay. Use the game's pause button first.");

    if (c.startup_settings) {
        const uint32_t changes = restart_changes(*c.startup_settings, c.settings);
        if (changes) {
            const bool german = s.language == 2;
            std::string pending = german ? "Neustart ausstehend: " : "Restart pending: ";
            const uint32_t flags[] = {RestartResolution, RestartTextures, RestartFiltering, RestartShaders, RestartLanguage, RestartPrompts, RestartAspect};
            const char* english[] = {"internal resolution", "texture profile", "texture filtering", "shader loading", "game language", "button prompts", "aspect ratio"};
            const char* deutsch[] = {"interne Auflösung", "Texturprofil", "Texturfilterung", "Shader-Laden", "Spielsprache", "Tastensymbole", "Seitenverhältnis"};
            bool first = true;
            for (int i = 0; i < 7; ++i) if (changes & flags[i]) {
                if (!first) pending += ", ";
                pending += german ? deutsch[i] : english[i];
                first = false;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::theme().accent));
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(pending.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
    }

    if(ImGui::Button(c.startup_settings ? "Resume game" : "Back to launcher")) c.open=false;
    ImGui::SameLine();
    if(ImGui::Button(s.muted?"Unmute":"Mute audio")) {s.muted=!s.muted;settings_changed(true);}
    ImGui::SameLine();
    if(ImGui::Button(s.metrics_enabled?"Hide FPS":"Show FPS")) {s.metrics_enabled=!s.metrics_enabled;settings_changed(true);}
    const char* names[kTabs] = {l.video, l.audio, l.controls, l.overlay, l.system};
    int current = -1;
    // Tabs follow the keyboard / controller focus: moving onto a tab with the arrows or the D-pad shows it (next
    // frame), with no Enter or A needed. The mouse still selects by clicking. Only a change of focus moves the tab: L / R
    // select a tab without moving the focus, which must not pull it back.
    static int follow = -1, focused_before = -1, shown_tab = -1, hold_follow = 0;
    // L / R: the controller's cursor goes to the new page's first row, and following the focus pauses a few frames.
    // Otherwise the cursor, left on a row of the page that went away, fell back onto the first tab and the
    // tabs-follow-focus rule switched straight back (seen as "tab 1, tab 0" in the logs, before this change too).
    if (c.tab_request >= 0 && c.tab_request != shown_tab) rows::focus_first(), hold_follow = 4;
    static float page_a = 1, page_dir = 0, line_x0 = -1, line_x1 = -1;
    int follow_next = -1, focused = -1;
    ImVec2 tab_min[kTabs], tab_max[kTabs];
    const float footer = 64 * k;
    // Down with the cursor on the tab bar: the page's first row (Savage Wind found ImGui's own pick across the nested
    // panes unreliable, menu.mm tabs_body)
    if (focused_before >= 0 && ImGui::GetCurrentContext()->NavCursorVisible &&
        (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickDown) ||
         ImGui::IsKeyPressed(ImGuiKey_DownArrow)))
        rows::focus_first();
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));  // (the underline shows where the cursor is)
    if (ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_NoTooltip | ImGuiTabBarFlags_NoTabListScrollingButtons)) {
        for (int t = 0; t < kTabs; ++t) {
            ImGuiTabItemFlags tf = c.tab_request == t || follow == t ? ImGuiTabItemFlags_SetSelected : 0;
            char label[64];
            std::snprintf(label, sizeof label, "  %s  ###tab%d", names[t], t);
            const bool shown = ImGui::BeginTabItem(label, nullptr, tf);
            tab_min[t] = ImGui::GetItemRectMin(), tab_max[t] = ImGui::GetItemRectMax();
            if (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible) focused = t;
            if (!shown && focused == t && focused_before != t && hold_follow == 0) follow_next = t;
            if (shown && c.appearing) ImGui::SetItemDefaultFocus();  // navigation starts on the open tab
            if (!shown) continue;
            current = t;
            if (shown_tab != t) {  // another page: it slides in from the side it was asked from, and fades in
                page_dir = shown_tab < 0 ? 0.0f : t > shown_tab ? 1.0f : -1.0f;
                page_a = shown_tab < 0 ? 1.0f : 0.0f, shown_tab = t;
            }
            page_a = comfort.reduced_motion ? 1.f : ui::approach(page_a, 1.0f, 14.0f);
            ImGui::PopStyleColor();  // (the rows want their own)
            ImGui::Dummy(ImVec2(0, 4 * k));
            const ImVec2 at = ImGui::GetCursorPos();
            const float page_w = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPos(ImVec2(at.x + (1.0f - ui::smoothstep(page_a)) * 36 * k * page_dir, at.y));
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (0.15f + 0.85f * ui::smoothstep(page_a)));
            ImGui::BeginChild("page", {page_w, -footer}, ImGuiChildFlags_NavFlattened);
            switch (t) {
                case 0: video(c, l); break;
                case 1: audio(c, l); break;
                case 2: controls(c, l); break;
                case 3: overlay_tab(c); break;
                default: system_tab(c, l); break;
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleColor();
    // the underline glides to the selected tab; brighter and thicker while the cursor is on the tab bar
    if (current >= 0) {
        const float x0 = tab_min[current].x + 6 * k, x1 = tab_max[current].x - 6 * k;
        if (line_x0 < 0 || c.appearing) line_x0 = x0, line_x1 = x1;
        line_x0 = ui::approach(line_x0, x0, 18.0f), line_x1 = ui::approach(line_x1, x1, 18.0f);
        const float on_bar = ui::eased(ImGui::GetID("##tabfocus"), focused >= 0 ? 1.0f : 0.0f, 18.0f);
        const float th = (3.0f + 2.0f * on_bar) * k, y = tab_max[current].y;
        wdl->AddRectFilled(ImVec2(line_x0, y - th), ImVec2(line_x1, y), ui::fade(ui::theme().accent, 0.75f + 0.25f * on_bar), th);
    }
    follow = follow_next;
    focused_before = focused;
    if (hold_follow > 0) hold_follow--;
    c.tab_request = current;  // out: the tab shown, for the shoulder buttons

    // the footer: what the buttons do, as Savage Wind's start screen shows them (badges)
    const ImVec2 wp = ImGui::GetWindowPos();
    const float fy = wp.y + ImGui::GetWindowHeight() - footer * 0.5f - 4 * k;
    wdl->AddLine(ImVec2(wp.x + 28 * k, fy - footer * 0.5f + 4 * k),
                 ImVec2(wp.x + ImGui::GetWindowWidth() - 28 * k, fy - footer * 0.5f + 4 * k), ui::fade(IM_COL32(255, 255, 255, 26)));
    const ui::Hint list[] = {{"A", "Select"}, {"B", "Back"}, {"L", ""}, {"R", "Tab"}, {"Menu", "Close"}};
    ui::hints(wdl, wp.x + ImGui::GetWindowWidth() * 0.5f, fy, 30 * k, list, int(sizeof list / sizeof list[0]));
    ImGui::End();
}

}  // namespace nx::overlay
