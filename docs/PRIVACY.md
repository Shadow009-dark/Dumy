# DUMY məxfilik qeydi (yaddaş və söhbətlər)

DUMY tamamilə lokal işləyir: suallar, cavablar və yaddaş qeydləri sizin
kompüterinizdən çıxmır. Web axtarışı default olaraq söndürülüb
(`config.json` → `web_search_enabled: false`).

## Nə saxlanılır

| Fayl / qovluq | Məzmun |
|---|---|
| `data/memory/settings.json` | Yaddaşın yandırılıb-söndürülməsi |
| `data/memory/<client>/facts.json` | Özünüz haqqında açıq şəkildə dediyiniz qısa faktlar |
| `data/memory/<client>/conversations/*.json` | Söhbətlər: başlıq, vaxt, mesajlar, istifadə olunan mənbələr, cavab müddəti |

`<client>` — `X-Session-Id` başlığının təmizlənmiş dəyəri; başlıq yoxdursa
`default`. Müştərilər bir-birinin məlumatını görə bilmir.

## Nə saxlanılmır

* Şifrə, parol, API açarı, token, telefon/sənəd nömrəsi, e-poçt ünvanı,
  doğum tarixi, diaqnoz/sağlamlıq məlumatı — bu sözlərdən biri keçən cümlə
  ümumiyyətlə yaddaşa yazılmır.
* Modelin öz cavabları və ya iddiaları fakt kimi saxlanılmır; yalnız
  istifadəçinin özü haqqında yazdığı cümlələr.
* Suallar heç bir log faylına yazılmır (`log_questions: false`).

## Yaddaşın idarəsi

* **Yaddaş paneli** (solda “Yaddaş” düyməsi): faktları bir-bir silmək,
  “Hamısını sil”, yandır/söndür açarı.
* API: `GET /memory`, `DELETE /memory/{id}`, `DELETE /memory`,
  `POST /memory/toggle` `{"enabled": false}`.
* Söhbəti silmək: `DELETE /conversations/{id}` — mesajlar diskdən silinir.
* Yaddaşı tamamilə söndürmək: `config.json` → `memory_enabled: false`
  (heç nə oxunmur, heç nə yazılmır) və ya paneldəki açar.

## Saxlama müddəti

`config.json` → `conversation_retention_days`:

* `0` (default) — söhbətlər silinənə qədər saxlanılır.
* `N > 0` — `N` gündən köhnə söhbət faylları avtomatik silinir.

Yaddaş qeydləri üçün avtomatik müddət yoxdur; onları paneldən və ya API ilə
silmək lazımdır. Limit `memory_max_facts` (default 50) — limit aşılarsa ən
köhnə fakt düşür.

## Prompt büdcəsi

Yaddaş prompta ən çox `memory_injection_tokens` (default 160, maksimum 500)
token əlavə edir. Yaddaş yalnız ümumi (PDF-siz) cavab promptuna düşür:
fakt-yoxlama və PDF retrieval promptlarına heç vaxt əlavə olunmur, ona görə
yaddaş mətni heç vaxt `[Mənbə: ...]` kimi göstərilmir.

## Yedəkləmə

Bütün yaddaş `data/memory/` qovluğundadır. Kopyalamaq üçün serveri dayandırın
və qovluğu kopyalayın. Qovluq `.gitignore`-dadır, git-ə düşmür.
