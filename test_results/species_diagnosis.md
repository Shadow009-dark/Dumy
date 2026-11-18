# Species diagnosis (STEP 1)

Date: 2026-11-18. Branch `fix/species-guard`, parent `checkpoint-2026-11-18-gate`.
No source file was changed for this step. Server `build\Release\dumy.exe --no-browser --no-web`,
rebuilt from that commit; `/health` -> `build_id=Nov 18 2026 18:18:22`, `chunks=6751`,
`chat_model=gemma3:4b`, `embedding_model=bge-m3`, pid 10372.
`config.json` `debug_trace_enabled` was switched to `true` for this measurement and is restored
to `false` afterwards (documented, not committed).

## Method

For every question two requests are sent in a fresh session: `POST /chat/stream` (the
concatenated `delta` events are the raw draft) and `POST /chat` with `{"debug":true}` (route,
`trace.selected`, final answer, sources). The server log is sliced between the byte offsets
taken before and after the request, so the `relevance gate` / `word check` lines belong to
that request. The two requests are separate samples of a stochastic pipeline, so the chunk
census and the answer come from the debug request while the word-check line comes from the
paired stream request.

Chunk text is read from `data/index.bin` (`DUMYVEC1`, u32 version, u64 count, then per record
u32 metadata length + UTF-8 JSON + u32 embedding dimension + dim float32). The species census
folds a chunk with `common.h` `az_fold` and matches the group stems as whole tokens (stems
shorter than 4 letters) or as a prefix of at most 7 extra letters - the same rule as
`az_quality.h:279` `has_agri_keyword`. The group table used here is the one added in STEP 2
(`include/dumy/az_quality.h` `az_species_groups`).

Groups: cattle `inek/iney/qaramal/dana/buzov/duve/mal`, sheep `qoyun/quzu/toglu/qoc`,
goat `keci/oglag`, buffalo `camis`, horse `at/atlar/qulun`, poultry
`toyuq/cuce/xoruz/hindus/ordek/qaz/qus`, bee `ari/arici/petek/mum`.

## Per-question result

| Tag | Question | Target | Route | Selected | Species match | Other species only | Generic | Draft chars | Final chars | Unknown-word stats (app log) |
|---|---|---|---|---:|---:|---:|---:|---:|---:|---|
| Q10 | Keçilərdə dırnaq xəstəlikləri hansılardır? | goat | broad_topic | 5 | 1 | 3 | 1 | 873 | 826 | unknown=0/38 ratio=0.000000 |
| Q7 | Dana ishalı olduqda nə etmək lazımdır? | cattle | simple_fact | 0 | 0 | 0 | 0 | 234 | 105 | unknown=1/26 ratio=0.038462 |
| S1 | Qoyunlarda dırnaq çürüməsi | sheep | simple_fact | 6 | 1 | 5 | 0 | 963 | 640 | unknown=0/58 ratio=0.000000 |
| S2 | Toyuqlarda qəbizlik | poultry | simple_fact | 0 | 0 | 0 | 0 | 411 | 586 | unknown=1/44 ratio=0.022727 |
| S3 | Atlarda kolik | horse | simple_fact | 6 | 6 | 0 | 0 | 902 | 513 | unknown=7/50 ratio=0.140000 > 0.120000 |
| S4 | Camışda mastit | buffalo | simple_fact | 0 | 0 | 0 | 0 | 1005 | 313 | unknown=1/65 ratio=0.015385 |
| S5 | Arılarda nozematoz | bee | simple_fact | 0 | 0 | 0 | 0 | 252 | 146 | unknown=2/24 ratio=0.083333 |
| S6 | Quzu ishalı | sheep | simple_fact | 0 | 0 | 0 | 0 | 905 | 83 | unknown=8/93 ratio=0.086022 |

`Species match` counts selected chunks that name the question's group; `Other species only`
counts chunks that name a species of a *different* group and not the target; `Generic` counts
chunks with no species word. A chunk naming the target and another group counts as a match.

## Selected chunks

### Q10 - Keçilərdə dırnaq xəstəlikləri hansılardır?

| # | Source | Page | Species | First 200 chars |
|---:|---|---:|---|---|
| 1 | pdfs/T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf | 434 | cattle | FƏSİL 10. DIRNAQ XƏSTƏLİKLƏRİNİN SƏBƏBLƏRİ, DIAQNOZU, MÜALİCƏ VƏ PROFİLAKTİKASI Qaramal dırnaq altlığının, dırnaq yumşaqlığının, dırnaq tacının dırnaqarası yarığın əzələləri, yaraları, xoraları, buynu |
| 2 | pdfs/T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf | 436 | generic | Dırnaq xəstəliklərinin ağır və xronik forması sadə formaların vaxtında müalicə olunmayıb onların xarici mikroflora ilə (koklar, viruslar, göbələklər və s) sirayətlənərək mürəkkəbləşməsindən tədricən b |
| 3 | pdfs/İntensiv və Ekstensiv maldarlıq.pdf | 286 | cattle | Heyvanların davamlı olaraq tövlədə bağlı və ya hərəkətsiz saxlanan, turşu tərkibli rasionlarla (bəkməz, cecə və s.) qidalanan, həddindən artıq konsentrasiyalı yemlə yemlənən, tövlənin döşəməsi daim çi |
| 4 | pdfs/T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf | 435 | cattle | daha çox zədələyir. Pərdili döşəməli pəyələrdə pərdiarası məsafələr standarta uyğun gəlmədikdə, yəni məsafə artıq olduqda bədənin ağırlığı dırnaqlar üzərinə bərabər tərzdə düşmür. Dırnağın müəyyən his |
| 5 | pdfs/Heyvandarlığın əsasları.pdf | 207 | sheep, goat | Nekrobakterioz – (dırnaq xəstəliyi). Xroniki gedişli olub, yaz və payız zamanı qoyunlar uzun müddət çeyilli və bataqlıq sahədə otarıldıqda baş verir. Əlaməti: axsama, müalicə olunmazsa qoyun dizi üstə |


`[18:19:27] INFO  relevance gate: branch=lexical score=0.659221 overlap=2/4 key_terms=[kecilerde,dirnaq,xestelikleri,hansilardir] missing=[kecilerde,hansilardir] decision=keep`

`[18:19:42] INFO  word check: PDF vocabulary=88671 prefixes=25825`

`[18:19:42] INFO  word check: unknown=0/38 ratio=0.000000`

### Q7 - Dana ishalı olduqda nə etmək lazımdır?

No chunk survived retrieval in the debug request (`trace.selected` is empty).

`[18:20:12] INFO  relevance gate: branch=llm_no score=0.517343 overlap=1/3 key_terms=[dana,ishali,olduqda] missing=[ishali,olduqda] decision=drop`

`[18:20:14] INFO  word check: unknown=1/26 ratio=0.038462`

### S1 - Qoyunlarda dırnaq çürüməsi

| # | Source | Page | Species | First 200 chars |
|---:|---|---:|---|---|
| 1 | pdfs/Heyvandarlığın əsasları.pdf | 207 | sheep, goat | Nekrobakterioz – (dırnaq xəstəliyi). Xroniki gedişli olub, yaz və payız zamanı qoyunlar uzun müddət çeyilli və bataqlıq sahədə otarıldıqda baş verir. Əlaməti: axsama, müalicə olunmazsa qoyun dizi üstə |
| 2 | pdfs/T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf | 435 | cattle | daha çox zədələyir. Pərdili döşəməli pəyələrdə pərdiarası məsafələr standarta uyğun gəlmədikdə, yəni məsafə artıq olduqda bədənin ağırlığı dırnaqlar üzərinə bərabər tərzdə düşmür. Dırnağın müəyyən his |
| 3 | pdfs/T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf | 434 | cattle | FƏSİL 10. DIRNAQ XƏSTƏLİKLƏRİNİN SƏBƏBLƏRİ, DIAQNOZU, MÜALİCƏ VƏ PROFİLAKTİKASI Qaramal dırnaq altlığının, dırnaq yumşaqlığının, dırnaq tacının dırnaqarası yarığın əzələləri, yaraları, xoraları, buynu |
| 4 | pdfs/İntensiv və Ekstensiv maldarlıq.pdf | 286 | cattle | Heyvanların davamlı olaraq tövlədə bağlı və ya hərəkətsiz saxlanan, turşu tərkibli rasionlarla (bəkməz, cecə və s.) qidalanan, həddindən artıq konsentrasiyalı yemlə yemlənən, tövlənin döşəməsi daim çi |
| 5 | pdfs/İntensiv və Ekstensiv maldarlıq.pdf | 288 | cattle | tamponlana bilməyən işgənbədə artan turşuluq bağırsaq hərəkətləri ilə yoğun bağırsağın son hissələrinə gedir. Yoğun bağırsaqdakı turşu mühitə görə peyin selikli, axıcı və köpüklü olur. Peyini bu forma |
| 6 | pdfs/İntensiv və Ekstensiv maldarlıq.pdf | 290 | cattle | ayaq-dırnaq quruluşuna malik heyvanların gələcək nəsillərini təkmilləşdirmək çox vaxt aparacaq. 11. Tövlələrdə ayaq xəstəliklərini göstərən sidik və peyinizlənməlidir. 12. Peyin və sidiklə təması azal |


`[18:20:18] INFO  relevance gate: branch=lexical score=0.619769 overlap=2/3 key_terms=[qoyunlarda,dirnaq,curumesi] missing=[curumesi] decision=keep`

`[18:20:33] INFO  word check: unknown=0/58 ratio=0.000000`

### S2 - Toyuqlarda qəbizlik

No chunk survived retrieval in the debug request (`trace.selected` is empty).

`[18:20:46] INFO  relevance gate: branch=llm_no score=0.599003 overlap=1/2 key_terms=[toyuqlarda,qebizlik] missing=[toyuqlarda] decision=drop`

`[18:20:49] INFO  word check: unknown=1/44 ratio=0.022727`

### S3 - Atlarda kolik

| # | Source | Page | Species | First 200 chars |
|---:|---|---:|---|---|
| 1 | pdfs/T.B. İsgəndərov Baytarlıq cərrahiyyəsi praktikum.pdf | 133 | horse | MƏŞĞƏLƏ 14. ARXA ƏTRAFLARDA APARILAN ƏMƏLİYYATLAR. Sinirlərin blokadası. Atlarda qamış sinirinin blokadası. Bu siniri atlarda axsaqlıqların təfriqi diaqnostikasında, ayaq və barmaqlarda əməliyyat apar |
| 2 | pdfs/Heyvandarlığın əsasları.pdf | 273 | goat, horse | ayqıra 15-25 kq yaşıl ot verilir. Ata xörək duzu gün ərzində 30-40 qram təşkil etməlidir. Ürgə və əmizdirən madyanların yemlənməsi. Bu atların yem rasyonuna dənli-paxlalı bitki otu hər 100 kq canlı kü |
| 3 | pdfs/T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf | 56 | cattle, horse | K, vikasol, sanqaston istifadə etmək məsləhət görülür. Dietoprotein və yağ ilə zəngin yemlərdən heyvana vermək qanın laxtalanmasını artırır. 4.3. İti qan azlığı İti qan azlığı iri arteriya və damarlar |
| 4 | pdfs/Heyvandarlığın əsasları.pdf | 268 | horse | Özlərinin təbiətinə görə atlar maraqlı bioloji-təsərrüfat xüsusiyyətlərinə malik heyvanlardır. Onların bioloji xüsusiyyətlərinə aşağıdakılar daxildir: 1. Atlar təbiətçə ilxı heyvanları olmaqla sürü şə |
| 5 | pdfs/T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf | 17 | goat, horse | 1.11. Elektrotravma Travmatizmin bu növü heyvan orqanizminə elektrik cərəyanının təsiri nəticəsində baş verir. Məlumdur ki, müasir dövrdə heyvan tövlələrinin əksəriyyəti elektrikləşdirilmişdir. Heyvan |
| 6 | pdfs/T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf | 162 | horse | təsadüf edilir, ancaq ən çox buna atlarda rast gəlinir. Gərilmələr ən çox barmaq fleksorlarında müşahidə edilir. Etiologiyası. Vətərlərin gərilməsinin və cırılmasının əsas səbəbləri mexaniki travmalar |


`[18:21:00] INFO  relevance gate: branch=llm_yes score=0.555717 overlap=1/2 key_terms=[atlarda,kolik] missing=[kolik] decision=keep`

`[18:21:14] WARN  garbled check: unknown=7/50 ratio=0.140000 > 0.120000 - retry at temperature 0.1 with shorter num_predict`

`[18:21:19] INFO  [timing] draft_garbled_retry model=gemma3:4b wall=4799ms load=7ms prompt_eval=53ms gen=4712ms tok=3694+260 done_reason=length`

`[18:21:19] WARN  garbled retry still invalid - honest short answer`

### S4 - Camışda mastit

No chunk survived retrieval in the debug request (`trace.selected` is empty).

`[18:21:34] INFO  relevance gate: branch=lexical score=0.543775 overlap=2/2 key_terms=[camisda,mastit] missing=[] decision=keep`

`[18:21:43] INFO  word check: unknown=1/65 ratio=0.015385`

### S5 - Arılarda nozematoz

No chunk survived retrieval in the debug request (`trace.selected` is empty).

`[18:21:50] INFO  relevance gate: branch=llm_no score=0.577823 overlap=0/2 key_terms=[arilarda,nozematoz] missing=[arilarda,nozematoz] decision=drop`

`[18:21:52] INFO  word check: unknown=2/24 ratio=0.083333`

### S6 - Quzu ishalı

No chunk survived retrieval in the debug request (`trace.selected` is empty).

`[18:21:58] INFO  relevance gate: branch=llm_no score=0.523726 overlap=1/2 key_terms=[quzu,ishali] missing=[ishali] decision=drop`

`[18:22:05] INFO  word check: unknown=8/93 ratio=0.086022`

## Draft and final answer

### Q10 - Keçilərdə dırnaq xəstəlikləri hansılardır?

Draft (873 chars):

```
Qaramal dırnaq altlığının, dırnaq yumşaqlığının, dırnaq tacının dırnaqarası yarığın əzələləri, yaraları, xoraları, buynuz hissəsinin çatlaması, qırılması, dırnaq toxumlarının fleqmonası və irinli-nekrozlu iltihabları kimi ortopedik xəstəliklərlə [Mənbə: T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf, səh. 434] tutulduğu bildirilib. Həmçinin, dırnaq xəstəlikləri bəzi təsərrüfatlarda naxırın ümumi sayının 10-55 %-ni əhatə edir [Mənbə: T.B.İsgəndərov ümuni və xüsusi cərrahiyyə.pdf, səh. 434].

Ağır və xronik formalar sadə formaların vaxtında müalicə olunmayıb onların xarici mikroflora ilə (koklar, viruslar, göbələklər və s) sirayətlənərək mürəkkəbləşir [Mənbə: T.B.İsgəndərov ümuni və xüsusi cərrahiyyə.pdf, səh. 436]. Dırnaq altlığının, yarığının və oynağının xoraları, fleqmonaları, irinli, nekrozlu iltihabları və s. [Mənbə: T.B.İsgəndərov ümuni və xüsusi cərrahiyyə
```

Final answer (826 chars, 1 source(s)):

```
Keçilərdə dırnaq xəstəlikləri geniş spektrdə mövcud olan problemləri əhatə edir. Bu xəstəliklər, keçidlərə nəmli və beton döşəməlidir pəyələrdə saxlanılması, quru və pərdəli pəyə halında uzun müddət saxlanılması, həmçinin keyfiyyətsiz qaba yemlərlə (bəkməz, cecə) təmin edilməsi kimi səbəblərə görə başlanır. Dırnaq xəstəliklərinin ən vacib növləri arasında dırnaq altlığının, yarığının və oynağının xoraları, fleqmonaları, irinli, nekrozlu iltihabları və s. yerinə alır. Həmçinin, axsaqlıq, irinli-nekrozlu pododermatitlər və digər ağır formalar da keçidlərə xas olan dırnaq xəstəlikləri sırasına aiddir. Ayaq xəstəlikləri də bu problemlərlə sıradan çıxır, çünki ayaqların xəstələnməsi də dırnaq xəstəliklərinin yaranmasına səbəb olur. Bu hallar keçidlər üçün ciddi iqtisadi itkilərə və sağlamlıq problemlərinə gətirib çıxır.
```

### Q7 - Dana ishalı olduqda nə etmək lazımdır?

Draft (234 chars):

```
Dana ishalı olduqda, mütləq bir veterinarə müraciət edin. İshalın səbəbinə görə müalicə fərqlənir. Ərzağını dəyişdirin, qidalandırma zamanlarını artırın və təlimatlara əsasən həkim tərəfindən göstərilən müalicələri görməyə hazır olun.
```

Final answer (105 chars, 1 source(s)):

```
Dana ishalı olduqda, sakitləşdirici diyetə verin və su ilə çətinləşdirin. Həmçinin, həkimə müraciət edin.
```

### S1 - Qoyunlarda dırnaq çürüməsi

Draft (963 chars):

```
Qoyunlarda dırnaq çürüməsi, yəni dırnaq xəstəliyi, təsərrüfatlarda ciddi problemlərə səbəb ola bilər. Bu xəstəliyin başqa adı da var – nekrobakterioz [Mənbə: Heyvandarlığın əsasları.pdf, səh. 207]. Dırnaq çürüməsi qoyunların uzun müddət nemli və çılpaq sahələrdə (çeyilli və bataqlıq sahələrində) otarıldıqda baş verir [Mənbə: Heyvandarlığın əsasları.pdf, səh. 207]. Xəstəliyin əlamətləri axsama, müalicə olunmazsa qoyun dizi üstə yeriyir, dırnağın yumşaq sahəsi və dırnaq arası sahədə yara əmələ gəlir [Mənbə: Heyvandarlığın əsasları.pdf, səh. 207]. Müalicə kimi dırnaq buynuzunda olan ölmüş hüceyrələr götürülür və yaralar 3%-li hidrogen neroksidlə işlənir [Mənbə: Heyvandarlığın əsasları.pdf, səh. 207].

Dırnaq xəstəlikləri təsərrüfatlarda inəklərin 10-55% -ni əhatə edir [Mənbə: T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf, səh. 434]. Dırnaq xəstəlikləri inək çəkisini itirməyinə, südün verilməsinin azaldılmasına səbəb olur [Mənbə: T.B.İsgəndərov ümumi və
```

Final answer (640 chars, 1 source(s)):

```
Qoyunlarda dırnaq çürüməsi, adətən, uzun müddətlər bağlı şəraitdə saxlanma, gündelik fəal hərəkət azlığı və döşəmənin nəm olması kimi faktorlarla bağlıdır . Dırnaqların dəri və buynuz hissələri möhkəmliyini itirməsi, sürtünmələdən tez yeyilməsi və mexaniki zərbələrə qarşı daha az müqavimətli olması da dırnaq çürüməsinin əlamətidir . Həmçinin, yem payının zülallarla, A, D vitaminləri, yod mikroelementləri, kükürdlə, kalsiumla, fosforla tarazlaşmaması da dırnaq xəstəliklərinin baş verməsinə səbəb ola bilər . Müalicə üçün dırnaqların dezinfeksiyası (3%-li hidrogen peroksid ilə), yaraların işlənilib-bağlanması və təmizlənməsi vacibdir .
```

### S2 - Toyuqlarda qəbizlik

Draft (411 chars):

```
Toyuqlarda qəbizliklə üzləşmə halı nadir deyil və tədricən baş verən qıcıqların nəticəsi ola bilər. Qəbizliyin səbəbləri arasında ziyanlı materialların çatışması, suyun az alınması, həmçinin bədən fəaliyyətinin azalması da rol oynaya bilər. Bu hallarada tədricən qıcıqları azaldıla bilər. Qəbizliyin müalicəsi üçün həkimin tövsiyələrinə əsasən, lifli qida və ya laxan kimi vasitələr istifadə edilməli ola bilər.
```

Final answer (586 chars, 1 source(s)):

```
Toyuqlarda qəbizliklə üzləşmə halı nadir olsa da, bu, həzm sistemində problemlərə işarə edən ola bilər. Qəbizliyin səbəbləri və onunla mübarazlığ üçün ümumi tədbirlər aşağıdakılardır:
*   **Qidalanma:** Toyuqlara qəbizliklə üzləşmə hallarında qida diyetinin vacibliyi önə çixir. Quru, yağsuz qidalardan (tərəyağı, yağlı peynir, müalicə olunmamış toxumlar) istifadə etmək lazımdır.
*   **Su ilə təmin edilmə:** Toyuqlara qəbizliklə üzləşmə hallarında günün əvvəlində 1,5-2 litr su içmək lazımdır.
*   **Tərkibində lif olan qidalardan istifadə etmək:** Lif, toyuğun keçməsi üçün vacibdir.
```

### S3 - Atlarda kolik

Draft (902 chars):

```
Atlarda kolik – bu, atın bağıracığında yaranan bir xəstəlikdir. Kolikin simptomları arasında şiddətli ağrı, qıcıqlanma, iynəyiş, qısalma və ya qısqaldırma var [Mənbə: T.B. İsgəndərov Baytarlıq cərrahiyyəsi praktikum.pdf, səh. 133]. At ayaq üstə təsbit edilir, dərinliyinə 2 sm-ə qədər inyeksiya olunur [Mənbə: T.B. İsgəndərov Baytarlıq cərrahiyyəsi praktikum.pdf, səh. 133]. İynənin vurulma nöqtəsi medial tərəfdə daban sümüyündən əl içi boyuda yuxarıda olur [Mənbə: T.B. İsgəndərov Baytarlıq cərrahiyyəsi praktikum.pdf, səh. 133].

Qan azlığı hallarında qanın miqdarı üçdə birinin itirilməsi öldürücü sayılır [Mənbə: T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf, səh. 56]. Qan axınını kəsildikdən sonra heyvanın qan təzyiqinin qalxmasına köməklik göstərir [Mənbə: T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf, səh. 56].

İti qan azlığı hallarında heyvanın ölümünə səbəb olur [Mənbə: T.B.İsgəndərov ü
```

Final answer (513 chars, 4 source(s)):

```
Atlarda kolik – bu, atın bağıracığında yaranan bir xəstəlikdir. Kolikin simptomları arasında şiddətli ağrı, qıcıqlanma, iynəyiş və qidalanma azlığı var. Kolikin diağnozunda ağrı nöqtələrinin müayinəsi vacibdir. Əgər axsaqlıq itirsə, onda, deməli axsaqlığı törədən səbəb, buxov və ayaq darağı oynaqları arasında məskunlaşmışdır. Blokadanın texnikası ilə sinir blokadaları edilməklidir.
İti qan azlığı hallarında da qanlı axınları kəsildikdən sonra ürək fəaliyyətinin zəifləməsi varsa kamfora yağı, kofein işlədir [
```

### S4 - Camışda mastit

Draft (1005 chars):

```
Camışlarda mastit xəstəsi süd məhsuldarlığını və keyfiyyətini ciddi şəkildə azalda bilər [Mənbə: İntensiv və Ekstensiv maldarlıq, səh. 279]. Bu xəstəliyin klinik (aydın) və subklinik (gizli) növləri var. Klinik mastitdə inəkdə qızdırma, şişmə, ağrı kimi simptomlar müşahidə olunur [Mənbə: İntensiv və Ekstensiv maldarlıq, səh. 279]. Subklinik mastit isə süd məhsuldarlığını azaldır, lakin heç bir açıq simptom yaratmır [Mənbə: İntensiv və Ekstensiv maldarlıq, səh. 278]. Subklinik mastitin ölkəmizdə orta hesabla 30%-ni təşkil etdiyinə də qeyd olunub [Mənbə: İntensiv və Ekstensiv maldarlıq, səh. 278]. Mastitlə bağlı itkilər inəkdən olan süd məhsuldarlığının azmasına, südün keyfiyyətinin düşməsinə, əmək və zaman itkisinə, müalicə xərclərinə, balavermədə azalmaya kimi geniş diapazonda başa gələ bilər [Mənbə: İntensiv və Ekstensiv maldarlıq, səh. 279].

İstifadə olunan mənbələr:
*   İntensiv və Ekstensiv maldarlıq.pdf, səh. 278-279
*   KƏND TƏSƏRRÜFATI HEYVANLARININ YEMLƏNDİRİLMƏSİ.pdf, səh. 201-202
```

Final answer (313 chars, 1 source(s)):

```
Camışda mastit – buxeyək iltihabıdır. Bu xəstəlikdə qəfəs altındakı bədəl toxumlarında iltihabi proses başlanır. Xidmətdən çıxdığı, hamilə olduğu ya da yeni doğulmuş yavrularını sığınacaq hesab etdiyi zaman daha çox görülür. Mastit simptomları arasında qəfəs altının şişməsi, ağrı, qızdırıcılıq və tənbəlilik var.
```

### S5 - Arılarda nozematoz

Draft (252 chars):

```
Nozematoz arıların qrupu arasında yayılmış, şiddətli və xəcalət edən bir xəstəlikdir [Ümumi bilik]. Bu xəstəliyin simptomları arasında arıların qrupunun qurbanlarını öldürməsi, qrupun içindəki arıları da öldürməsi və qrup üzrə davranışın dəyişməsi var.
```

Final answer (146 chars, 1 source(s)):

```
Nozematoz arıların qrupu arasında yayılmış yüksək şiddətli virus xəstəliyidir. Bu xəstəlik arıların sürünün hızır şəkildə silinməsinə səbəb olur .
```

### S6 - Quzu ishalı

Draft (905 chars):

```
Quzu ishalı – bu, quarlarda baş verən müxtəlif xörü simptomudur. Onun səbəbləri və əlamətləri aşağıdakılardır:

*   **Səbəbləri:** Quzu ishalının başlıca səbəbləri arasında bakteriyaların (özgün olaraq *Pasteurella multocida*) təraüd olunması, qidalanma problemi, stress və ya digər xəstəliklər yerinə alır.
*   **Əlamətləri:** İshal, quşların qızğınlığı, iştahasının azalması, qısqınlığa meyllilik, ağızdan su axırılması, qəbizlik və ya ishal kombinasiyası kimi əlamətlər müşahidə edilə bilər.
*   **Müalicəsi:** Müalicə, simptomları müvafiq şəkildə azaldmağa diqqət etməkdən ibarətdir. Bu, quşların qızğınlığını azaltmaq, istifadəsi üçün taze və yaxşı hazırlanmış qidalar təmin etmək, antibiotiklərlə mübarizə etmək (bəzi hallarada) və idrar yolu üçün dərmanlar vermək olar.

Quzu ishalı ciddi bir xəstəlik ola bilər, ona görə də quşlarınızın sağlamlığı haqqında bir veterinarla məsləhət almaq vacibdir.
```

Final answer (83 chars, 1 source(s)):

```
Quzu ishalı, əczaçıların ticarətında satılan antibiotiklər ilə müalicə olunacaqdır.
```

## Sampling variance on Q7 (5 identical requests, same binary)

| Run | Route | Selected | Unknown-word stats | Final chars |
|---|---|---:|---|---:|
| 0 | simple_fact | 6 | unknown=0/46 ratio=0.000000 | 442 |
| 1 | simple_fact | 4 | unknown=3/73 ratio=0.041096 | 713 |
| 2 | simple_fact | 0 | unknown=2/32 ratio=0.062500 | 73 |
| 3 | simple_fact | 6 | unknown=3/62 ratio=0.048387 | 606 |
| 4 | simple_fact | 5 | unknown=5/152 ratio=0.032895 | 112 |

Every Q7 answer in this sample keeps invented words (`Danaın`, `bəluruminin`, `Dişorun`,
`Anamaz`, `şöriəsi`) while the word check reports 0.000-0.062, i.e. below the configured
`garbled_word_ratio_max=0.12` (`config.json`). The same binary produced the 1111-char draft
captured earlier today (`%TEMP%\species_step1.json`, Q7: `unknown=4/97 ratio=0.041237`).

## What the measurement shows

1. **Q10 (goats) is answered with the cattle surgery chapter.** 4-5 chunks are selected;
   only one names goats, and it is a paragraph of `T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf`
   p.437 that also lists `keci`. The other 3 are cattle-only (p.434, p.436, `İntensiv və
   Ekstensiv maldarlıq` p.286) and one is generic. The final answer opens
   "Qaramal dırnaq altlığının, dırnaq yumşaqlığının ..." - cattle text for a goat question.
2. **Q7 (calf diarrhoea) loses its sources or invents words.** In the fresh run the relevance
   gate dropped every chunk (`decision=drop`), the off-domain gate then rejected the draft and
   the pipeline returned 73 chars; in the five repeats 3-6 chunks survive, the answers keep
   invented words and the ratio stays at 0.000-0.062, under the 0.12 threshold.
3. **S1 (sheep, hoof rot) retrieves mostly cattle**: 1 sheep chunk against 5 cattle-only ones.
4. **S2/S4/S5** have no species-matching chunk at all in this run
   (`S2` 0 selected, `S4` 0 selected, `S5` 0 selected); their answers are model prose.
5. **S3 (horse colic)** is the one fully on-species case: 6/6 chunks name horses.

So 5 of the 8 questions have selected chunks from another species, and 3 of 8 have none at
all: exactly the two cases the STEP 2 guard must separate.

## Earlier baseline sample (same commit, 17:08-17:11)

| Tag | Selected | Match | Other | Generic | Final chars | Unknown-word stats |
|---|---:|---:|---:|---:|---:|---|
| Q10 | 4 | 1 | 2 | 1 | 358 | unknown=0/41 ratio=0.000000 |
| Q7 | 6 | 3 | 3 | 0 | 465 | unknown=4/97 ratio=0.041237 |
| S1 | 6 | 1 | 5 | 0 | 494 | unknown=1/36 ratio=0.027778 |
| S2 | 0 | 0 | 0 | 0 | 74 | unknown=1/36 ratio=0.027778 |
| S3 | 6 | 6 | 0 | 0 | 408 | unknown=4/38 ratio=0.105263 |
| S4 | 0 | 0 | 0 | 0 | 122 | unknown=4/31 ratio=0.129032 > 0.120000 |
| S5 | 0 | 0 | 0 | 0 | 156 | unknown=8/55 ratio=0.145455 > 0.120000 |
| S6 | 5 | 3 | 0 | 2 | 793 | unknown=3/27 ratio=0.111111 |

This is the sample that produced the STEP 3 defect report: Q7 selected 6 chunks (3 cattle,
3 sheep/goat), the draft opened "... qaraşdırıq. Küçüklərdə ishalın inkubasiya dövrü ..."
and the final answer carried `bəluruminin` with `unknown=4/97 ratio=0.041237`.

## Reproduce

```
build\Release\dumy.exe --no-browser --no-web        # debug_trace_enabled=true
python %TEMP%\species_step1b.py                      # -> %TEMP%\species_step1b.json
python %TEMP%\q7_repeat.py                           # -> %TEMP%\q7_before_repeats.json
```

Raw captures: `%TEMP%\species_step1b.json` (fresh run), `%TEMP%\species_step1.json` (earlier
run), `%TEMP%\q7_before_repeats.json` (five Q7 repeats), `%TEMP%\species_server.log`.
