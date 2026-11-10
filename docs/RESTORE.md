# DUMY — bərpa təlimatı (RESTORE)

Bu sənəd DUMY-ni **yeni kompüterdə sıfırdan** qurmaq üçün lazım olan hər şeyi
izah edir. Repo təkbaşına kifayət etmir: PDF korpusu, indeks və bəzi verilənlər
git-də saxlanmır (aşağıdakı cədvələ bax).

Hədəf göstəricilər (bu maşında ölçülüb): **23 PDF fayl → 6 751 chunk**,
indeks `data\index.bin` ≈ 43 MB, çat modeli `gemma3:4b`, embedding `bge-m3`.

## 1. Tələblər

- Windows 10/11 x64 (layihə MSVC ilə qurulur)
- Visual Studio 2022/2026 — **Desktop development with C++** iş yükü
- CMake 3.20+ və Ninja/MSBuild (VS ilə gəlir)
- [vcpkg](https://github.com/microsoft/vcpkg) checkout-u (`C:\vcpkg` kimi)
- [Ollama](https://ollama.com) 0.40+ işləyən servis (`127.0.0.1:11434`)
- Git

## 2. Klonla və qur

```powershell
git clone <repo-url> dumy
cd dumy

# vcpkg harada olursa olsun - mütləq yol YAZMA:
$env:VCPKG_ROOT = "C:/vcpkg"

cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_INSTALLED_DIR="$PWD/build/vcpkg_installed" `
  -DDUMY_WITH_POPPLER=ON

cmake --build build --config Release --target dumy
```

`vcpkg.json` manifesti asılılıqları (nlohmann-json, poppler və s.) konfiqurasiya
zamanı özü quraşdırır. `VCPKG_INSTALLED_DIR` layihə daxilində **nisbi** yoldur,
yəni repo başqa qovluğa köçürülsə də işləyir. Hazır binary:
`build\Release\dumy.exe` (vcpkg DLL-ləri onun yanına kopyalanır).

## 3. Modelləri endir

```powershell
ollama pull gemma3:4b
ollama pull bge-m3
```

`gemma3:4b` yeganə çat modelidir (draft + helper + self-check rolları da ondan
istifadə edir). 6 GB VRAM-də ikinci çat modeli birincini sıxışdırır, ona görə
ikinci model əlavə etməyin. `OLLAMA_MAX_LOADED_MODELS=2` saxlanmalıdır ki,
`bge-m3` CPU-da rezident qalsın.

## 4. PDF korpusunu bərpa et

PDF-lər git-də **yoxdur**. Onları köçürmək lazımdır:

```
<köhnə maşın>\dumy\pdfs\*.pdf   →   <yeni maşın>\dumy\pdfs\
```

Ehtiyat nüsxə layihə kökündəki **`data_backup_20261013_221930\`** qovluğundadır
(git-ə düşmür, silinməməlidir). Orada PDF-lərin və köhnə indeksin surəti var.

## 5. İndeksləşdir və yoxla

```powershell
.\build\Release\dumy.exe --ingest
```

Gözlənilən nəticə: `23 fayl`, **≈ 6 751 chunk**. Sonra:

```powershell
.\build\Release\dumy.exe --no-browser --no-web
# başqa terminalda:
curl.exe -s http://127.0.0.1:8085/health
```

`/health` cavabında `"chunks":6751`, `"chat_model":"gemma3:4b"`,
`"web_search_enabled":false` görünməlidir. `chunks:0` = korpus yoxdur, tələsik
nəticə çıxarma.

## 6. İşə sal ve test et

```powershell
.\start_dumy.bat            # brauzer UI ilə
# və ya
.\build\Release\dumy.exe --no-browser --no-web
```

Regressiya dəsti (özü serveri qaldırır, port məşğuldursa imtina edir):

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_all.ps1
```

Əgər artıq işləyən serveri test etmək istəyirsinizsə: `-ReuseServer`.
Nəticə: `test_results\latest.md`.

## 7. Git-də OLMAYAN fayllar

| Yol | Nədir | Bərpa mənbəyi |
|---|---|---|
| `pdfs\*.pdf` | 23 mənbə PDF (korpus) | `data_backup_20261013_221930\` və ya köhnə maşın |
| `data\index.bin` | Vektor indeksi (~43 MB) | `dumy.exe --ingest` ilə yenidən qurulur |
| `data\*.log`, `data\new_general\`, `data\probe\` | test/log çıxışları | lazım deyil |
| `db\` | lokal köməkçi baza/qeydlər | avtomatik yaranır |
| `data_backup_20261013_221930\` | **əvəzolunmaz ehtiyat nüsxə** | əl ilə saxlanılır, silinmir |
| `build\`, `vcpkg_installed\` | qurulma çıxışı | yenidən qurulur |
| `test_results\` | test hesabatları | yenidən yaranır |

## 8. Məlum məhdudiyyətlər

- 6 GB VRAM-də tək 4B model: bəzi faktlar səhv ola bilər, uzun cavablarda
  qrammatik qüsurlar qalır. Pipeline səhvləri *azaldır* (sitat yoxlaması,
  word-validity, honest refusal), amma 4B modelin biliyini düzəltmir.
- Skannedilmiş ~48 PDF səhifəsi çıxarılmır (`ocr_enabled=false`).
- Veb axtarışı qəsdən söndürülüb (`web_search_enabled=false`).
