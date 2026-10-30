# Claude Usage Bar

Claude abonelik kullanımını (5 saatlik oturum + haftalık limit) Windows taskbar'ında, bildirim alanının hemen solunda gösteren native bir widget.

```
Oturum %23 · 2s14dk
 Hafta %41 · 3g05s
```

- **Hafif:** Tek `.exe`, statik CRT, ~2–3 MB RAM. Arka planda Python veya .NET çalışmaz.
- **Sadece Claude Desktop açıkken çalışır:** Desktop kapalıyken gizlenir ve hiç ağ isteği atmaz.
- **Otomatik kimlik:** Önce Claude Code'un OAuth token'ını kullanır, süresi dolmuşsa kendisi yeniler. Yedek olarak DPAPI ile şifrelenmiş `sessionKey` kullanır.
- **Renkler:** %75 üstü turuncu, %90 üstü kırmızı. Veri alınamazsa gri. Açık/koyu temaya uyar.

## Mimari

```
src/core/   Platformdan bağımsız mantık, Linux'ta da derlenir ve test edilir
  json_lite       span tabanlı, salt-okunur JSON gezinme + string kaçışları
  time_util       ISO-8601 → Unix, "2s14dk" biçimlendirme
  api             /usage ve /organizations yanıt ayrıştırma
  credentials     .credentials.json ayrıştırma ve 3 alanı byte-korumalı yamalama
  display         snapshot → iki satır + renk seviyesi
  process_kind    claude.exe yolu → Desktop / CLI ayrımı
  usage_math      limit örnekleri: CSV satırı, tempo, %100 tahmini, oturum→hafta kuru
  usage_breakdown oturum dökümü (.jsonl) → token dağılımı (gruplar), oturum özeti JSON
  report_html     tek dosyalık HTML rapor (inline SVG, betik yok)
src/win/    Win32 katmanı
  app             pencere, 1 sn timer, menü, worker thread (condition_variable)
  renderer        UpdateLayeredWindow, per-pixel alpha, gri tonlamalı AA
  taskbar         monitör/taskbar/TrayNotifyWnd konumu, tam ekran tespiti
  process_watch   Claude Desktop penceresi / CLI süreci tespiti
  usage_fetcher   OAuth → yenileme → sessionKey akışı
  http_client     WinHTTP RAII, oturum boyunca bağlantı havuzu
  secret_store    DPAPI (CryptProtectData, kullanıcı kapsamı)
  report_builder  CSV + döküm + özetleri toplar, report.html yazar (tıklanınca, arka planda)
  config / log / util
tests/      GoogleTest, yalnızca core
res/        manifest (PerMonitorV2 DPI, UTF-8), sürüm bilgisi (.rc)
```

## Gereksinimler

- Windows 10/11 x64
- **Seçenek A (önerilen):** Visual Studio 2022 Build Tools
  - "Desktop development with C++" iş yükü
  - "C++ CMake tools for Windows" bileşeni (CMake ve Ninja bununla gelir)
- **Seçenek B:** MSYS2 UCRT64 ortamında şu paketler:
  `mingw-w64-ucrt-x86_64-gcc`, `mingw-w64-ucrt-x86_64-cmake`, `mingw-w64-ucrt-x86_64-ninja`
- Git (testler GoogleTest'i GitHub'dan çeker)

## Derleme (terminal)

**"Developer PowerShell for VS 2022"** aç ve proje klasöründe çalıştır:

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release
ctest --preset msvc-release
.\build\msvc-release\claude-usage-bar.exe
```

MSYS2 UCRT64 için: `cmake --preset mingw-release`, `cmake --build --preset mingw-release`.

Hata ayıklama için `msvc-debug` preset'ini kullan.

### Geliştirme döngüsü (GitHub)

1. Her değişiklik ayrı bir branch'te yapılır ve PR olarak açılır. `main`'e doğrudan commit yapılmaz.
2. Her PR'da CI iki işi çalıştırır:
   - `windows-msvc`: MSVC ile derler, `ctest` çalıştırır, `claude-usage-bar.exe` artifact'ını üretir.
   - `linux-core-tests`: core testleri Linux'ta çalıştırır.
3. Derlenmiş `.exe` için: PR → **Checks** → CI → **Artifacts** → `claude-usage-bar`.
4. Yerelde derleme hatası çıkarsa çıktıyı `build\` altına kaydet (klasör `.gitignore`'da). Claude bu dosyayı okuyup düzeltmeyi aynı branch'e push edebilir:
   ```powershell
   cmake --preset msvc-release
   cmake --build --preset msvc-release 2>&1 | Tee-Object build\build.log
   ctest --preset msvc-release 2>&1 | Tee-Object build\test.log
   ```
5. Yerel klasörü GitHub'a bağlamak için (bir kez):
   ```powershell
   git init -b main
   git remote add origin https://github.com/kalkanozgur/ai-usage-bar.git
   git fetch origin
   git reset --mixed origin/main   # dosyalar yerinde kalır, geçmiş GitHub'dan gelir
   git checkout feat/usage-report  # incelemek istediğin PR branch'i
   ```

## VS Code

1. Klasörü aç ve önerilen eklentileri kur (C/C++, CMake Tools, EditorConfig).
2. Alt çubuktan configure preset'ini seç: `MSVC x64 Debug`.
   CMake Tools, MSVC ortamını kendisi hazırlar; Developer PowerShell'e gerek yok.
3. Derleme ve debug:
   - `F7`: derle
   - `F5`: "Debug (MSVC)" ile çalıştır
   - Testler Test Explorer'da görünür.

## Kullanım

İlk çalıştırmada ayar dosyası `%LOCALAPPDATA%\ClaudeUsageBar\config.ini` oluşturulur. Fare kontrolleri:
- **Sol tık:** grafik raporunu (`report.html`) oluşturur ve varsayılan tarayıcıda açar. Rapor yalnız tıklanınca hazırlanır; ayrıntı için [docs/usage-analiz.md](docs/usage-analiz.md).
- **Sağ tık:** menüyü açar. En üst satır durum bilgisidir (kaynak, son güncelleme saati veya hata mesajı). Menüde şunlar var:
  - Grafik raporunu aç
  - Kullanım sayfasını aç (claude.ai)
  - Şimdi yenile
  - sessionKey'i panodan kaydet / sil
  - Windows ile başlat (HKCU\…\Run kaydına yazar)
  - Ayar dosyasını aç / Log dosyasını aç
  - Çıkış

### Kimlik doğrulama

| Sıra | Kaynak | Not |
|---|---|---|
| 1 | Claude Code OAuth (`%USERPROFILE%\.claude\.credentials.json`, `CLAUDE_CONFIG_DIR` destekli) | Süresi dolunca `refresh_token` ile yenilenir ve dosyaya atomik olarak geri yazılır (`ReplaceFileW`, ACL korunur). İlk yazmadan önce `.credentials.json.cub-backup` yedeği alınır. |
| 2 | claude.ai `sessionKey` | Tarayıcıda claude.ai → F12 → Application → Cookies → `sessionKey` değerini kopyala, sonra widget menüsünden "panodan kaydet" seç. Değer DPAPI ile şifrelenir ve pano temizlenir. |

**Yenileme koruması:** Refresh token her yenilemede değişir. Bu yüzden widget, Claude Code CLI çalışırken token yenilemez; aksi halde CLI'ın elindeki token geçersizleşir ve `/login` gerekir. CLI açıksa token'ı zaten kendisi yeniler.

### Ayarlar (`config.ini`)

| Anahtar | Varsayılan | Açıklama |
|---|---|---|
| `monitor` | `right` | `left`, `primary` veya `0/1/2` (soldan sağa sıra) |
| `gap_px` | 8 | Tepsi ile yazı arası boşluk (fiziksel px) |
| `secondary_reserved_px` | 110 | İkincil taskbar'da (tepsi yok, sadece saat var) sağda bırakılan alan |
| `manual_right_px` | -1 | ≥0 ise otomatik tespiti ezer |
| `visibility` | `desktop_window` | `desktop_process`: tepsiye küçültülmüşken de göster. `always`: Desktop'tan bağımsız hep göster. |
| `desktop_path_hint` | — | Desktop standart dışı bir klasöre kuruluysa exe yolunun bir parçası |
| `refresh_sec` | 180 | Sorgu aralığı (en az 30 sn) |
| `auth` | `auto` | `oauth` veya `session` ile tek kaynağa sabitlenir |
| `oauth_refresh` | 1 | 0 yapılırsa token yenilemesi kapanır |
| `[report] csv_log` | 1 | Her başarılı sorguyu `usage-YYYY-MM.csv`'ye yazar (yalnız sayılar) |
| `[report] sessions` | 30 | Rapor açılırken incelenen en yeni Claude Code dökümü sayısı (1–200) |
| `[report] summary_dir` | — | Bulut oturum özetleri (`*_usage.json`) klasörü; boşsa `Belgeler\Claude-Logs` |

## Kullanım analizi

Limit zaman serisi (CSV) ve oturum dökümlerinden token dağılımı: format, formüller ve gruplar için [docs/usage-analiz.md](docs/usage-analiz.md).

## Güvenlik

- Token, sessionKey ve istek başlıkları log'a yazılmaz. Kullanım analizi dökümlerden yalnız sayıları ve araç adlarını okur; mesaj metni saklanmaz. Kullanılan bellek tamponları `SecureZeroMemory` ile temizlenir.
- `session.bin` DPAPI ile şifrelidir; başka bir kullanıcı veya başka bir makine çözemez.
- `.gitignore` dosyası `session.bin`, `*.cub-backup` ve `*.log` dosyalarını hariç tutar.

## Bilinen sınırlar

- Usage endpoint'leri resmi olarak belgelenmemiş; Anthropic değiştirirse ayrıştırıcının güncellenmesi gerekir.
- Claude Code'un OAuth client'ı ile token yenilemek ToS açısından gri alanda.
- claude.ai, WinHTTP'nin TLS parmak izini Cloudflare üzerinden engelleyebilir. Bu durumda sessionKey yolu `HTTP 403` verir; OAuth yolu bundan etkilenmez.
- npm ile kurulan Claude Code `node.exe` olarak çalışır ve CLI tespitine yakalanmaz. Native kurulum (`claude.exe`) yakalanır.
- Dikey taskbar desteklenmez.
