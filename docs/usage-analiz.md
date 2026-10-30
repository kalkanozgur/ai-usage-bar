# Kullanım analizi

Widget iki tür veri toplar ve grafik raporunda gösterir. İkisi de yalnız sayı ve araç adı içerir; mesaj metni hiçbir yerde saklanmaz.

| Analiz | Kaynak | Kod |
|---|---|---|
| Limit zaman serisi | Widget'ın kendi CSV logu | `src/core/usage_math.*` |
| Token dağılımı | Claude Code oturum dökümleri (`.jsonl`) ve oturum özetleri (`*_usage.json`) | `src/core/usage_breakdown.*` |

## 1. Limit zaman serisi (CSV)

**Konum:** `%LOCALAPPDATA%\ClaudeUsageBar\usage-YYYY-MM.csv`. Ay UTC'ye göre seçilir ve her ay için yeni bir dosya açılır.
**Yazma:** Her başarılı kullanım sorgusunda bir satır yazılır. `refresh_sec=180` ile bu günde ~480 satır, ~25 KB eder. Yeni ya da boş dosyaya önce başlık yazılır.

```
ts,session_pct,session_reset,week_pct,week_reset
1793383200,23.0,1793397600,41.0,1793800000
```

| Alan | Birim | Not |
|---|---|---|
| `ts` | Unix saniye (UTC) | Örnek zamanı |
| `session_pct` | % (0–100, 1 ondalık) | `five_hour.utilization` |
| `session_reset` | Unix saniye | `five_hour.resets_at` |
| `week_pct` | % | `seven_day.utilization` |
| `week_reset` | Unix saniye | `seven_day.resets_at` |

Boş alan, o yanıtta değerin gelmediği anlamına gelir. Satır sonu `\r\n` kullanılır. Okuyucu `\n` ile biten satırları da kabul eder.

### Hesaplar

| Fonksiyon | Formül |
|---|---|
| `pace()` (tempo) | `beklenen = 100 × geçen / pencere`, `fark = kullanım − beklenen`. Pencere: oturum 5 sa, hafta 7 gün. `fark > 0` ise limit, eşit dağıtıma göre önde harcanıyor demektir. |
| `projected_full()` (%100 tahmini) | Son penceredeki örneklere en küçük kareler doğrusu oturtulur, `t₁₀₀ = t_son + (100 − y_son) / eğim`. Tahmin, reset zamanından sonraya düşerse değer döndürülmez. `resets_at` 120 sn'ye kadar oynayabilir; bu aralıktaki örnekler aynı pencere sayılır. |
| `session_to_week_rate()` (oturum → hafta kuru) | Aynı pencerelerdeki ardışık örnekler üzerinden `Σ Δhafta / Σ Δoturum`. Yalnız oturumun arttığı adımlar sayılır. Toplam Δoturum 1 puanın altındaysa değer döndürülmez. |

## 2. Token dağılımı

### Kaynak

- Claude Code dökümü: `<CLAUDE_CONFIG_DIR veya %USERPROFILE%\.claude>\projects\<proje>\<oturum>.jsonl`. Subagent dökümleri `…\<oturum>\subagents\*.jsonl` altında durur.
- Bulut oturumları (claude.ai): PC'de dökümleri yoktur. Bunların yerine aşağıdaki şemadaki oturum özeti dosyası (`*_usage.json`) okunur.

### Ayrıştırma kuralları

- Her satır bir JSON nesnesidir. Bozuk satırlar atlanır.
- `type:"assistant"`: `message.id` başına **bir istek** sayılır. Aynı istek birden çok satıra bölünebilir; bu durumda her alanın en büyük değeri alınır. Yeni bir istek usage değerleri sıfırla başlıyorsa (sentetik/hata satırı) tamamen atlanır.
- `type:"user"`: `tool_result` içeriği, onu çağıran aracın grubuna yazılır. Diğer metin ve görseller "Konuşma/diğer" grubuna gider.
- `isCompactSummary:true`: bağlam sıkıştırılmış demektir. Biriken içerik sıfırlanır ve özet metni yeni içerik olarak sayılır.
- Boyut tahmini: metin ≈ bayt / 4 token, görsel ≈ 800 token. Metin yalnız ölçülür, saklanmaz.

### Maliyet

```
efektif = input + 0.1 × cache_read + 2 × cache_creation + 5 × output
```

| Pay | Kural |
|---|---|
| Sabit taban (sistem talimatı + araç listesi) | İlk isteğin bağlamından (`input + cache_read + cache_creation`) o ana kadar biriken içerik çıkarılır |
| Girdi maliyeti | Taban ve o ana kadar biriken içerik grupları arasında token oranıyla dağıtılır |
| Çıktı maliyeti | O isteğin çağırdığı araçların gruplarına eşit bölünür. Araç çağrısı yoksa "Konuşma/diğer" grubuna yazılır |
| Subagent dökümü | Toplamın tamamı "Subagent'lar" grubuna yazılır |

### Gruplar (araç adına göre)

| Grup | Araçlar |
|---|---|
| Claude talimatları | Sabit taban |
| Yerleşik tarayıcı | Adında `Claude_Browser` geçenler |
| Claude in Chrome | `mcp__claude-in-chrome__*` |
| Ekran kontrolü | Adında `computer` geçen diğer araçlar |
| Web | `WebSearch`, `WebFetch` |
| Dosya/shell | `Read`, `Write`, `Edit`, `MultiEdit`, `Glob`, `Grep`, `Bash`, `BashOutput`, `KillShell`, `NotebookEdit`, `LS`, `mcp__remote-devices__device_*` |
| Beceriler | `Skill` |
| Araç yükleme | `ToolSearch` |
| Subagent'lar | `Agent`, `Task` ve subagent dökümleri |
| Bağlantı: `<sunucu>` | Diğer `mcp__<sunucu>__*` araçları. Sunucu adı rastgele bir ID ise (≥16 karakter, yalnız hex ve `-`) bunun yerine araç adı kullanılır |
| Konuşma/diğer | Kalan her şey |

Gruplar ilk eşleşmeden itibaren yukarıdan aşağıya sırayla denenir (`Claude_Browser__computer` → Yerleşik tarayıcı).

### Oturum özeti şeması (`*_usage.json`)

Bulut oturumlarında Claude, oturum sonunda kendi dökümünü aynı yöntemle analiz eder ve dosyayı log klasörüne `<log-adı>_usage.json` adıyla yazar.

```json
{
  "session": "s-123",
  "proje": "Claude Widget",
  "baslangic": "2026-10-30T18:00:00Z",
  "bitis": "2026-10-30T18:05:00Z",
  "istek_sayisi": 3,
  "toplam_efektif": 3048.0,
  "gruplar": [
    {"ad": "Claude talimatları", "efektif": 2357.4, "pay": 0.7734}
  ]
}
```

| Alan | Tip | Not |
|---|---|---|
| `baslangic`, `bitis` | ISO-8601 UTC ya da `null` | |
| `efektif` | sayı, 1 ondalık | |
| `pay` | 0–1 arası, 4 ondalık | Grup payları toplamı ≈ 1 |

`to_summary_json()` bu dosyayı yazar, `parse_summary_json()` okur. `gruplar` dizisi yoksa okuma başarısız sayılır.

## 3. Grafik raporu

Widget yazısına sol tıklanınca (veya menüde "Grafik raporunu aç" seçilince) ayrı bir thread şu adımları izler:

1. Bu ayın ve 8 gün öncesinin CSV dosyalarını okur.
2. `projects\*\*.jsonl` altındaki en yeni `[report] sessions` dökümü (varsayılan 30) seçer. Son 8 günden eski olanları alır, ama en yenisi her zaman listede kalır.
3. Her dökümü önbellekte arar (`breakdown-cache.jsonl`, anahtar: yol + mtime + boyut). Bulamazsa satır satır okuyup analiz eder; subagent dökümlerini de ekler.
4. `summary_dir` klasöründeki son 8 güne ait `*_usage.json` özetlerini "(bulut)" etiketiyle ekler.
5. `report_html::render_html()` ile `%LOCALAPPDATA%\ClaudeUsageBar\report.html` dosyasını atomik olarak yazar ve varsayılan tarayıcıda açar. Süre ve sayılar log'a yazılır.

| Bölüm | İçerik |
|---|---|
| Kartlar | Oturum ve hafta için şimdiki %, tempo farkı, reset zamanı, %100 tahmini; oturum → hafta kuru |
| Sekmeler | **Son oturum** (son 5 sa / en yeni döküm) · **Bugün** (yerel gece yarısından beri) · **Bu hafta** (son 7 gün, varsayılan) |
| Limit grafiği | Oturum % ve hafta % tek eksende (0–100). 20 dk'dan uzun boşluklarda çizgi kesilir. Fareyle üzerine gelince değer ve saat görünür |
| Token dağılımı | Gruplar yatay çubuk olarak, efektif token ve pay ile |
| Oturum tablosu | Bitiş, proje klasörü, istek, efektif, en büyük grup |

Raporda betik yok, dış kaynak yüklenmiyor. Açık ve koyu temayı `prefers-color-scheme` ile otomatik izliyor. Analiz bittikten sonra bellekte kalıcı bir yapı tutulmuyor.

## 4. Testler

| Dosya | Kapsam |
|---|---|
| `tests/test_usage_math.cpp` | CSV gidiş-dönüş testi, başlık ve bozuk satır reddi, aylık dosya adı, başlığın yalnız bir kez yazılması, tempo, %100 tahmini (önceki pencere ve geç bitiş durumları), kur, ISO-UTC biçimi |
| `tests/test_usage_breakdown.cpp` | Grup eşlemesi, istek sayımı ve sıfır-usage satırlarının atlanması, girdi/çıktı dağılımının elle hesaplanan değerlerle karşılaştırılması, compaction, subagent, özet JSON gidiş-dönüş testi |
| `tests/test_report_html.cpp` | HTML kaçışı, token biçimi, oturum birleştirme, boş girdi, varsayılan sekme, proje adı kaçışı, 7 günden eski oturumun dışarıda kalması, betik olmaması |
| `tests/fixtures/session_sample.jsonl` | Tamamen sentetik döküm. Yinelenen `message.id`, sıfır-usage satırı, bozuk satır, görsel içeren `tool_result` ve MCP aracı içerir |
