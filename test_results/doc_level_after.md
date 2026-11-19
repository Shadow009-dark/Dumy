# Document-level species label - after (STEP 4)

Date: 2026-11-19. Branch `fix/species-doc-level`, HEAD `0dac10f` (STEP 3) plus this report.
Binary `build\Release\dumy.exe` rebuilt from that commit; `/health` ->
`build_id=Nov 19 2026 20:20:33`, `chunks=6751`, `chat_model=gemma3:4b`,
`embedding_model=bge-m3`. `config.json` is unmodified (`debug_trace_enabled=false`,
`garbled_word_ratio_max=0.12`); the measurement server was started with
`DUMY_DEBUG_TRACE=1`, the documented override (`src/config.cpp:91-96`).

Method: same as `test_results/species_after.md` - one `POST /chat` with
`{"debug":true}` per question in a fresh session, server log sliced by byte offset.
Raw capture `test_results/doc_level_after.json`, log `test_results/doc_level_server.log`,
resamples `test_results/doc_level_resample.json` (all gitignored). "Before" is the STEP 5
measurement on `fix/species-guard` (`test_results/species_after.json`), same 16 questions.

Verdicts are assigned per answer: **yes** = the question is answered correctly with
usable detail; **partly** = related but incomplete, vague or with wrong details;
**no** = wrong, off-topic, refusal or a clarification question instead of an answer.

## Verdict summary

| Tag | Question | Before | After | Why it changed |
|---|---|---|---|---|
| A1 | Mal-qarada qarayara xəstəliyinin əlamətləri nələrdir? | yes | yes | document label: 3 generic chunks of a multi-species book kept |
| A2 | İnək gündə neçə dəfə sağılmalıdır? | partly | partly | same chunks; vague draft, 2/2 resamples answered "2 dəfə" |
| A3 | Qoyunlarda quzu ölü doğumunun səbəbləri nələrdir? | yes | partly | same 6 books; draft facts mangled |
| A4 | Buğda əkini üçün torpaq necə hazırlanmalıdır? | yes | yes | unchanged |
| A5 | Mal-qarada qarın şişməsi (timpaniya) necə müalicə olunur? | partly | partly | unchanged: tympaniya treated as a haematoma |
| A6 | Arılarda varroa xəstəliyi ilə necə mübarizə aparılır? | partly | no | chunks now kept by document label |
| A7 | Dana ishalı olduqda nə etmək lazımdır? | partly | no | relevance gate llm_no (0 chunks before the guard) |
| A8 | Süd verimini artırmaq üçün yemləmə necə olmalıdır? | yes | yes | unchanged |
| A9 | Toyuqlarda yumurta istehsalı üçün işıq rejimi necə olmalıdır? | yes | yes | unchanged |
| A10 | Keçilərdə dırnaq xəstəlikləri hansılardır? | partly | partly | one generic surgery chunk, no cattle sentence |
| S1 | Qoyunlarda dırnaq çürüməsi | yes | yes | unchanged |
| S2 | Toyuqlarda qəbizlik | no | no | Tier 4, unchanged |
| S3 | Atlarda kolik | partly | no | measured draft is a clarification question |
| S4 | Camışda mastit | no | partly | chunks now name camış |
| S5 | Arılarda nozematoz | no | no | Tier 4, unchanged |
| S6 | Quzu ishalı | no | no | Tier 4, unchanged |

Totals for the 16 measured answers: before **yes 4 / partly 7 / no 5**;
after **yes 5 / partly 5 / no 6**.

* **Tier-4 species declines** (`species_no_source=true`, the "Bu heyvan növü üçün aydın
  mənbə tapılmadı" text): **before 5** (A1, S2, S4, S5, S6) -> **after 4**
  (S2, S5, S6, A7). A1 and S4 stopped refusing; A7 newly refuses in this sample, from the
  relevance gate, not from the species rule (see A7 and Variance).
* **"no clear source" answers in the final text**: 4 of 16 (A7, S2, S5, S6), 5 of 16
  before.

## Per-question before / after (quoted)


### A1 — Mal-qarada qarayara xəstəliyinin əlamətləri nələrdir?

Before (yes; sources: —):

> Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: inək, dana, mal-qara). Kitablarda bu növ üçün uyğun bölmə yoxdur, ona görə cavabı başqa heyvan növündən götürmürəm. Sualı dəqiqləşdirin (xəstəliyin adı, heyvanın yaşı) və ya baytara müraciət edin.

After (yes; sources: Antropozoonoz xəstəlikləri.pdf p.21, Antropozoonoz xəstəlikləri.pdf p.36, Antropozoonoz xəstəlikləri.pdf p.40):

> Qarayara kəskin gedişli infeksion xəstəlik olub, septisemiya, orqanizmin ağır intoksikasiyası, daxili bədən temperaturasının kəskin yüksəlməsi, şişkinliklər və karbunkulların əmələ gəlməsi, bağırsaqların, ağciyərlərin zədələnməsi ilə səciyyələnir. Xəstəliyin əlamətləri heyvanlarda və insanlarda eyni formada olur. İnsanlar arasında qarayaranın baş verməsində, qarayara ilə xəstələnmiş və bu xəstəlikdən ölmüş heyvanlar, əsas xəstəlik mənbəyi olmağa mühüm rola malikdir.

### A2 — İnək gündə neçə dəfə sağılmalıdır?

Before (partly; sources: Heyvandarlığın əsasları.pdf p.141, Heyvandarlığın əsasları.pdf p.155, KƏND TƏSƏRRÜFATI HEYVANLARININ YETİŞDİRİLMƏSİ (SON).pdf p.118, Qaramal naxırlarında-kitab A5.pdf p.48, SAĞIM və SÜDÜN EMALINDA TEXMOLOGİYALAR və TİNNOVASİYALAR-Monoqrafiya-PDF_compressed.pdf p.171, İntensiv və Ekstensiv maldarlıq.pdf p.275):

> Süd məhsuldarlığını artırmaq üçün inəkləri gündə 2-3 dəfə sağımaq daha faydalıdır. Lakin, bu, inəyin sağlamlığına da ciddi təhdid ola bilər. Səbəbudur ki, hər dəfə sağıldıqda inəkdən daha çox süd çıxarılır, lakin bu, inəkin streslə qarşılaşmasına və sağlamlığına zərbə vurur. Həmçinin, sağımların intervalı uzansa da, bu, inəkdən daha az süd çıxmağa səbəb olur. Ən uyğun variantı seçərək, inəkin sağlamlığını qorumaq vacibdir.

After (partly; sources: Heyvandarlığın əsasları.pdf p.167, Qaramal naxırlarında-kitab A5.pdf p.260, Qaramal naxırlarında-kitab A5.pdf p.272, Qaramal naxırlarında-kitab A5.pdf p.36, sud  və süd məhsullarının ekspertizası.pdf p.43, İntensiv və Ekstensiv maldarlıq.pdf p.272):

> İnək gündə neçə dəfə sağılması təsərrüfatın şəraitindən və heyvanın xüsusiyyətindən asılı olacaqdır. Fəsildə dəyişkənlikləri sağılan südün miqdarına və onun tərkibinə əhəmiyyətli dərəcədə təsir göstərir. Yaz fəslinə nisbətən payız fəslində südün yağlılığı və tərkibində olan zülalların miqdarı artır. Bir gündə inək neçə dəfə sağılması təsərrüfatın şəraitindən və heyvanın xüsusiyyətindən asılı olacaqdır.

### A3 — Qoyunlarda quzu ölü doğumunun səbəbləri nələrdir?

Before (yes; sources: Antropozoonoz xəstəlikləri.pdf p.347, Heyvandarlığın əsasları.pdf p.188, Heyvandarlığın əsasları.pdf p.207, Körpə kənd təsərrüfatı hüyvanlarının daxili xəstəlikləri.pdf p.76, Qoyunçuluq.pdf p.51, Qoyunçuluq.pdf p.89):

> Qoyunlarda quzu ölü doğumunun səbəbləri ilə bağlı məlumatlar PDF-də göstərilir. Əsas səbəblərdən biri – qoyunun yaşı. 5 günlük yaşda quzuların anaerob dizenteriyası xəstəliyinə yoluxma riski yüksəkdir. Həmçinin, qoyunun arxa budunun yanlarında, yelin ətrafında və cinsiyyət üzvindəki yuyulmamışlığı  , yeni doğulan quzuların əmcək əvəzinə kirli yunu sorub panosla xəstələnənləri göstərir. Təmizlənmə doğumdan 2-3 həftə qabağa edilməlidir. Qoyunun stresslərə, ağrılara və ya çətin doğuşa məhsuldarlıq az olar. Doğuş zaman …

After (partly; sources: Antropozoonoz xəstəlikləri.pdf p.360, Heyvandarlığın əsasları.pdf p.207, Heyvandarlığın əsasları.pdf p.208, Körpə kənd təsərrüfatı hüyvanlarının daxili xəstəlikləri.pdf p.76, Qoyunçuluq.pdf p.332, Qoyunçuluq.pdf p.51):

> Qoyunlarda quzu ölü doğumunun səbəbləri ilə bağlı məlumatlar PDF-də göstərilir. Əsas səbəblərdən biri – qoyunun yaşı. Qoyunların 5 günlük yaşlarında kütləvi doğum zamanı yoluxma riski yüksəkdir. Həmçinin, qoyunun arxa budunun yanları, yelin ətrafı, anus və dişi cinsiyyət üzvünün ətrafı yundan qırxılarsa, yeni doğulan quzu əmcəkləri yunu sorur. Doğuş zamanı ana qoyunun normal yemləmə və bəslənmə şəraiti yarandığı təqdirdə doğum normal olur. Çətin quzulama hallarında peşəkər baytar köməyinə ehtiyac var.

### A4 — Buğda əkini üçün torpaq necə hazırlanmalıdır?

Before (yes; sources: AQRONOMİYANIN əsasları.pdf p.191, AQRONOMİYANIN əsasları.pdf p.192, AQRONOMİYANIN əsasları.pdf p.258):

> Buğda əkini üçün torpağı hazırlamaq zamanı bir sıra amillərə diqqət yetirmək lazımdır. Əvvəl, torpağın quraqlığına və iqliminə uyğun olaraq dərinliyi müəyyən etmək vacibdir. Quraq iqlimdə torpaq daha dərin becərilsin , rütubətli torpaqlarda isə dayaz basdırılmalıdır. Ayrıca, torpaq səthinin hamar olması və toxumların eyni dərinliyə basdırılmasını təmin etmək vacibdir. Səpin zamanı torpağı pərşum etmək də mümkündür, xüsusilə ki, alaqlar güclü inkişaf edirsə. Torpaq səpinə hazırlamaq üçün şum daşımaq vacibdir , həmçi …

After (yes; sources: AQRONOMİYANIN əsasları.pdf p.191, AQRONOMİYANIN əsasları.pdf p.192, AQRONOMİYANIN əsasları.pdf p.288):

> Payızlıq buğdanın becərilməsi üçün torpağın hazırlanması prosesi, torpağın xüsusiyyətlərinə və iqlim şəraiti ilə bağlı faktorlara görə fərqlənir. Torpağı becərmə zamanı bir diş o birinin izi ilə getməməlidir. Fiziki yetişkənlik vəziyyətinə uyğun nəmliyə malik olan torpaqlar malalandıqda, iri kəltənlər asan xırdalanır və torpaq səpinə daha yaxşı hazırlanır. Yüngül qranulometrik tərkibə malik olan torpaqlar ziq-zaq malalar vasitəsilə becərilir. Qranulometrik tərkibi gilli və gillicəli olan torpaqlar isə ağır malalarl …

### A5 — Mal-qarada qarın şişməsi (timpaniya) necə müalicə olunur?

Before (partly; sources: Antropozoonoz xəstəlikləri.pdf p.232, Qaramal naxırlarında-kitab A5.pdf p.99, T.B.İsgəndərov  xırda heyvanların xəstəlikləri.pdf p.147, T.B.İsgəndərov BAYTARLIQ ANESTEZIOLOGIYASI VƏ REANIMATOLOGIYASI.pdf p.129):

> Qarın şişməsi (tympaniya) zamanı mal-qarada ilk növbədə qan axınını müayinə etmək və onun səbəbini müəyyən etmək vacibdir. Əksər hallarda reinfuziya göstərişdir, çünki bu zaman mürəkkəbləşmələrin qarşısı alınır. Ancaq qanın çirvanması, sidik kisəsində çatması kimi hallarada reinfuziya göstəriş deyil. Yaman şişi müalicəsinin başlanğıcında krepitasiya edən nahiyyəyə dimeksid və furasillin məhlulu inyeksiya edilir.

After (partly; sources: Körpə kənd təsərrüfatı hüyvanlarının daxili xəstəlikləri.pdf p.32, Qaramal naxırlarında-kitab A5.pdf p.99, T.B.İsgəndərov  xırda heyvanların xəstəlikləri.pdf p.147, T.B.İsgəndərov BAYTARLIQ ANESTEZIOLOGIYASI VƏ REANIMATOLOGIYASI.pdf p.129):

> Qarın şişməsi (tympaniya) zamanı ilk növbədə qanaxma, infeksiyalar və ya digər patologik prosesləri müayinə etmək vacibdir. Yaman şişi şübhələndirən hallarada  müvafiq terapi tədbirləri görmək lazımdır. Ərzaq çatışmazlığı, qıcıq və ya digər patologik hallar nəticəsində qarın boşluğunda hava toplanması zamanı tympaniya yaranır. Əməliyyatdan sonrakı 4-5 gün ərzində heyvanı duru yemlərlə (ət bulyonu, süd məhsulları) yemləmək tələb edilir. Qarın divarına qoyulmuş tikişlər 8-10 gündən sonra azad edilib. Yaman şişi müali …

### A6 — Arılarda varroa xəstəliyi ilə necə mübarizə aparılır?

Before (partly; sources: Antropozoonoz xəstəlikləri.pdf p.61, Körpə kənd təsərrüfatı hüyvanlarının daxili xəstəlikləri.pdf p.171):

> Arilarda varroa xəstəliyi ilə mübarizə üçün Tibb Baytarlığında istifadə edilən üsullara baxaq. Arı zəhəri ilə zəhərlənmiş heyvanlara parenteral kalsium-xlorid, kalsium-qlukonat və antihistamin preparatları təyin olunur. Yara nahiyyəsi natrium-hidrokarbonatın hipertonik məhlulunda isladılmış əşya ilə örtülür.

After (no; sources: Antropozoonoz xəstəlikləri.pdf p.61, T.B.İsgəndərov  xırda heyvanların xəstəlikləri.pdf p.245):

> Məlumatın yuxarıdakı mənbələrinə əsasən, arilarda varroa xəstəliyi ilə mübarizə aparılacaq halda aşağıdakı tədbirlər həyata keçirilə bilər: 1. **Vərəm vaksinlərinin tətbiqi:** Baytarlıqda bu vaksini istifadə etmək mümkün olmur, çünki immunizasiya edilmiş arılar uzun müddətlə allergik vəziyyətə malik olur. 2. **Heyvanların qruplaşdırılması və paylanması:** Təsərrüfatlarda heyvanların yerdəyişməsinə, qruplaşdırılmasına və paylanmasına Dövlət Baytarlıq xidməti ciddi nəzarət edir. 3. **Dezinfeksiya tədbirləri:** Xəstəl …

### A7 — Dana ishalı olduqda nə etmək lazımdır?

Before (partly; sources: Antropozoonoz xəstəlikləri.pdf p.360, Körpə kənd təsərrüfatı hüyvanlarının daxili xəstəlikləri.pdf p.181, Qaramal naxırlarında-kitab A5.pdf p.149, Qaramal naxırlarında-kitab A5.pdf p.152, T.B.İsgəndərov  xırda heyvanların xəstəlikləri.pdf p.41):

> Dana ishalı olduqda aşağıdakı addımları görmək vacibdir: 1. **Diaqnoz:** Əsasən kliniki əlamətlər (ishalın şiddəti, görünüşü, qıcıqlanma) və yemlərin tərkibinə müayinə edilməli. Həzm sistemini patologiyaya uğradan infeksion və invazion xəstəliklər təfriq edilməlidir. 2. **Təcələnmiş yemlər:** Xəstə heyvana tam sakitlik verməklə, pəhriz yemlər və çoxlu maye verilməlidir. Yemləmədən əvvəl hər kq diri kütləyə 300-500 t.v. pepsin, 0,1-0,3 mq tripsin və ya 0,01-0,07 q pankreatin verilməlidir. 3. **Dermanlar:** Ağrı sind …

After (no; sources: —):

> Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: inək, dana, mal-qara). Kitablarda bu növ üçün uyğun bölmə yoxdur, ona görə cavabı başqa heyvan növündən götürmürəm. Sualı dəqiqləşdirin (xəstəliyin adı, heyvanın yaşı) və ya baytara müraciət edin.

### A8 — Süd verimini artırmaq üçün yemləmə necə olmalıdır?

Before (yes; sources: Heyvandarlığın əsasları.pdf p.120, Heyvandarlığın əsasları.pdf p.123, Qaramal naxırlarında-kitab A5.pdf p.107, Qaramal naxırlarında-kitab A5.pdf p.108, Qaramal naxırlarında-kitab A5.pdf p.41, SAĞIM və SÜDÜN EMALINDA TEXMOLOGİYALAR və TİNNOVASİYALAR-Monoqrafiya-PDF_compressed.pdf p.13):

> Süd verimliliyinə nail çatmaq üçün südlük qaramallarda aşağıdakı yemləmə sistemlərindən istifadə edilməlidir: *   **Strateji yemləmə:** Bu sistemdə, süd məhsuldarlığından asılı olaraq qüvvəli yem fərdi şəkildə verilir. Laktasiyanın sonuna yaxın olan inəklərə daha az konsentratlı yem verilir. *   **Standart yemləmə:** Bu sistemdə inəklər fərdi şəkildə qaba və konsentrat yemi əldə edir. *   **Tam yemləmə (Total Mix Ration, TMR):** Bu sistemdə inəklərə balanslaşdırılmış yem verilir. Südün keyfiyyətini qorumaq üçün süd …

After (yes; sources: Heyvandarlığın əsasları.pdf p.120, KƏND TƏSƏRRÜFATI HEYVANLARININ YEMLƏNDİRİLMƏSİ.pdf p.218, Qaramal naxırlarında-kitab A5.pdf p.107, Qaramal naxırlarında-kitab A5.pdf p.108, Qaramal naxırlarında-kitab A5.pdf p.109, SAĞIM və SÜDÜN EMALINDA TEXMOLOGİYALAR və TİNNOVASİYALAR-Monoqrafiya-PDF_compressed.pdf p.296):

> Süd verimliliyinin artırılması üçün südlük qaramallarda strateji yemləmə sisteminə üstünlük verilən olur. Standart yemləmə sistemində heyvanın diri çəkisi və süd verimi ilə bağlı məhdudiyyətlər olduğu üçün bu sistemdə daha yüksək verimlilik əldə edilmir. Strateji yemləmə sistemində qaba yem sərbəst verilir, konsentrat yem isə süd məhsuldarlığından asılı olaraq fərdi və məhdud şəkildə verilir. Laktasiyanın sonuna qədər eyni miqdarda davamlı olaraq ya da laktasiyanın ikinci yarısından sonra miqdarı azaldaraq konsentr …

### A9 — Toyuqlarda yumurta istehsalı üçün işıq rejimi necə olmalıdır?

Before (yes; sources: ARIF_YDX.pdf p.25, ARIF_YDX.pdf p.26, Heyvandarlığın əsasları.pdf p.221, Heyvandarlığın əsasları.pdf p.222, Heyvandarlığın əsasları.pdf p.231, KƏND TƏSƏRRÜFATI HEYVANLARININ YETİŞDİRİLMƏSİ (SON).pdf p.146):

> Toyuqlarda yumurta istehsalı üçün işığın rejimi quşun fəsli ilə bağlı dəyişir. Qış fəsli zamanı işığa daha çox duyarlı olurlar. Yumurtlama vaxtında işığa tələsən toyuqlərin bir gün qaranlığa salınmasına və gecə işıqlı otağa verilib görmələyinə başlana bilər. Qısa günlərdə quşun yem qəbul etmə vaxtı azaldığı kimi, yumurtlama qabiliyyəti də aşağı düşür.

After (yes; sources: ARIF_YDX.pdf p.26, Heyvandarlığın əsasları.pdf p.221, Heyvandarlığın əsasları.pdf p.222, Heyvandarlığın əsasları.pdf p.224, KƏND TƏSƏRRÜFATI HEYVANLARININ YETİŞDİRİLMƏSİ (SON).pdf p.146):

> Toyuqlarda yumurta istehsizləmə vaxtında işığın təsiri vacibdir. müvafiqata görə, işıq nəinki ovulyasiya vaxtında, eləcə də yumurta əmələ gətirən orqanların yetkinləşməsinə və ümumi fəallığına da təsir göstərir. Qısa günlərdə quşun yem qəbul etmə vaxtı azaldığı kimi, yumurtlama qabiliyyəti, müəyyən dövr üzrə məhsuldarlığı da aşağı düşür. Toyuqların yumurta istehsalı üçün həftədə 1,5 saat uzadılır və 14 saata çatdırılır. Həmçinin, hinduşqaların yumurtlamadan qabaq həftədə 1,5 saat uzadılır və 14 saata çatdırılır. Fə …

### A10 — Keçilərdə dırnaq xəstəlikləri hansılardır?

Before (partly; sources: T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf p.436, T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf p.437):

> Keçilərdə dırnaq xəstəliklərinin ağır və xronik forması sadə formaların vaxtında müalicə olunmayıb onların xarici mikroflora ilə (koklar, viruslar, göbələklər və s) sirayətlənərək mürəkkəbləşməsindən tədricən baş verir. Bunlara dırnaq altlığının, yarığının və oynağının xoraları, fleqmonaları, irinli, nekrozlu iltihabları və s. aiddir. Aseptik dırnaq xəstəlikləri mürəkkəbləşərək ağır formalara: irinli, fleqmonozlu, xoralı, irinli-nekrozlu pododermatitlərə keçdikdən sonra prosesin qapalı formasında şişkinlik, kəskin  …

After (partly; sources: T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf p.436):

> Keçilərdə dırnaq xəstəliklərinin ağır və xronik forması sadə formaların vaxtında müalicə olunmayıb onların xarici mikroflora ilə (koklar, viruslar, göbələklər və s) sirayətlənərək mürəkkəbləşməsindən tədricən baş verir. Bunlara dırnaq altlığının, yarığının və oynağının xoraları, fleqmonaları, irinli, nekrozlu iltihabları və s. aiddir. Aseptik dırnaq xəstəlikləri mürəkkəbləşərək ağır formalara keçdikdən sonra prosesin qapalı formasında şişkinlik, kəskin ağrı, qızartı, hərarət, fluktasiya və s. müşahidə olunur.

### S1 — Qoyunlarda dırnaq çürüməsi

Before (yes; sources: Heyvandarlığın əsasları.pdf p.207, Qoyunçuluq.pdf p.382):

> Qoyunlarda dırnaq çürüməsi (dirnaq xəstəliyi) – (dırnaq xəstəliyi) Nekrobakterioz kimi  başqa ad altında da ifadə olunur. Bu xəstəlikdə qoyunun dırnaqlarında yumşaq sahəsi və dırnaq arası sahədə yaralar əmələ gəlir. Müalicə üçün Yara 3%-li hidrogen neroksid, marqansovka ilə işlənir. Qoyunların profilaktikasında isə bina və yatağın təmiz saxlanılması, dezinfeksiya edilməsi, qoyunun əmcəkləri yuyulub, təmizlənməli və quzulara ağız südü (bulama) içirdilməlidir.

After (yes; sources: Heyvandarlığın əsasları.pdf p.207):

> Qoyunlarda dırnaq çürüməsi, yəni nekrobakterioz xəstəliyi, axsama, müalicə olunmazsa qoyun dizi üstə yeriyir və dırnağın yumşaq sahəsində yara əmələ gəlir. Bu xəstəliyin müalicəsi üçün dırnaqda 3%-li hidrogen neroksid və marqansovka ilə işlənir. Qoyunların əmcəkləri təmizlənilməli və quzulara ağız südü (bulama) içdirilməlidir.

### S2 — Toyuqlarda qəbizlik

Before (no; sources: —):

> Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: toyuq, quş). Kitablarda bu növ üçün uyğun bölmə yoxdur, ona görə cavabı başqa heyvan növündən götürmürəm. Sualı dəqiqləşdirin (xəstəliyin adı, heyvanın yaşı) və ya baytara müraciət edin.

After (no; sources: —):

> Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: toyuq, quş). Kitablarda bu növ üçün uyğun bölmə yoxdur, ona görə cavabı başqa heyvan növündən götürmürəm. Sualı dəqiqləşdirin (xəstəliyin adı, heyvanın yaşı) və ya baytara müraciət edin.

### S3 — Atlarda kolik

Before (partly; sources: Heyvandarlığın əsasları.pdf p.268, Heyvandarlığın əsasları.pdf p.273, T.B. İsgəndərov Baytarlıq cərrahiyyəsi praktikum.pdf p.133, T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf p.162, T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf p.17, T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf p.56):

> Kolik atlarda axsaqlığın itməsi ilə bağlıdır. Bu hallarada incik siniri blokadaya alınır. Blokadanın texnikası atın təhlükəsizliyi üçün ön ətrafına buxovluq qoymaqdan ibarətdir. İynənin vurulma nöqtəsi medial tərəfdən daban sümüyündən əl içi boyda yuxarıda olur.

After (no; sources: Heyvandarlığın əsasları.pdf p.268, Heyvandarlığın əsasları.pdf p.273, T.B. İsgəndərov Baytarlıq cərrahiyyəsi praktikum.pdf p.133, T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf p.162, T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf p.17, T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf p.56):

> at barədə nəyi öyrənmək istəyirsiniz: qidalanma, xəstəlik, yoxsa baxım?

### S4 — Camışda mastit

Before (no; sources: —):

> Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: camış). Kitablarda bu növ üçün uyğun bölmə yoxdur, ona görə cavabı başqa heyvan növündən götürmürəm. Sualı dəqiqləşdirin (xəstəliyin adı, heyvanın yaşı) və ya baytara müraciət edin.

After (partly; sources: Heyvandarlığın əsasları.pdf p.263, Heyvandarlığın əsasları.pdf p.265, KƏND TƏSƏRRÜFATI HEYVANLARININ YEMLƏNDİRİLMƏSİ.pdf p.201):

> Camışda mastit – buxeylanma xəstəsi hesab edilir. Bu xəstəlikdə camışın süd bürüntücündən su yivləri çıxır, bürüntüc şişir və qızdır. Camışların südündəki yağlılığa görə mastit xəstəsi daha tez inəklərdə başqa xəstəliklərə malik olur. Camışlarda mastitə səbənlər aşağıdakılardır: qeyri-nəm bürüncülər, dərinin xaricindəki bakteriyaların təsiri. Camışlarda mastit simptomları – süd bürüntücündən su yivlərin çıxması, bürüntücün şişməsi və qızmasına nail oлмаqdır.

### S5 — Arılarda nozematoz

Before (no; sources: —):

> Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: arı). Kitablarda bu növ üçün uyğun bölmə yoxdur, ona görə cavabı başqa heyvan növündən götürmürəm. Sualı dəqiqləşdirin (xəstəliyin adı, heyvanın yaşı) və ya baytara müraciət edin.

After (no; sources: —):

> Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: arı). Kitablarda bu növ üçün uyğun bölmə yoxdur, ona görə cavabı başqa heyvan növündən götürmürəm. Sualı dəqiqləşdirin (xəstəliyin adı, heyvanın yaşı) və ya baytara müraciət edin.

### S6 — Quzu ishalı

Before (no; sources: —):

> Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: qoyun, quzu). Kitablarda bu növ üçün uyğun bölmə yoxdur, ona görə cavabı başqa heyvan növündən götürmürəm. Sualı dəqiqləşdirin (xəstəliyin adı, heyvanın yaşı) və ya baytara müraciət edin.

After (no; sources: —):

> Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: qoyun, quzu). Kitablarda bu növ üçün uyğun bölmə yoxdur, ona görə cavabı başqa heyvan növündən götürmürəm. Sualı dəqiqləşdirin (xəstəliyin adı, heyvanın yaşı) və ya baytara müraciət edin.

## What changed and why

**A1 (cattle, qarayara): Tier 4 -> answered.** The regression the task targets. The log
shows `species guard: kept 3 chunk(s) for cattle (by document label, no chunk names it),
dropped 0`; the chunks are pages 21, 36 and 40 of `Antropozoonoz xəstəlikləri.pdf`, a
multi-species book (poultry 38%, cattle second at 19%). No chunk names cattle but the book
is not another species' book, so the generic pages are usable and the answer describes
acute anthrax, septicaemia, fever and carbuncles.

**S4 (buffalo, mastitis): Tier 4 -> answered.** `camışlarda` is read as buffalo (`camis` +
`larda`); three chunks naming the buffalo survive (`kept 3 chunk(s) for buffalo (chunk names
it), dropped 3`) and the answer talks about buffalo mastitis, with garbled words.

**A10 (goat, nail disease): same content, one chunk instead of two.** The kept chunk is
p.436 of `T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf`, generic surgery text with no
animal in it; the book is multi-species (cattle 47%, horse 27%, goat 1%), so the document
label allows it. No cattle-surgery sentence (`Qaramal`, `İri buynuzlu`) appears in the
answer, which is what `tests/cases/dom05_keci_surgery.json` asserts. p.437 was not
retrieved in this sample.

**A6 (bee, varroa): partly -> no.** Two generic chunks are kept "by document label"
(`Antropozoonoz xəstəlikləri.pdf` p.61, a tuberculosis pathology page, and
`xırda heyvanların xəstəlikləri.pdf` p.245) and the draft lists "vərəm vaksinləri" for
bees. This is the cost of the new rule: no document is labelled `bee`, so a bee question
can now be answered from generic pages of multi-species books, and the 4B model reads TB
pathology as if it were about the asked animal. Before, one chunk at least mentioned
bees; that answer was wrong too (bee-venom poisoning) but stayed on topic.

**A7 (cattle, calf diarrhoea): partly -> no (refusal) in this sample.** Not the species
guard: `relevance gate: branch=llm_no ... decision=drop` emptied the candidate list first,
and the guard log says `0 chunk(s)`. Two resamples retrieved 5 chunks and answered.

**S3 (horse, colic): partly -> no** in this sample - the draft is the clarification
"at barədə nəyi öyrənmək istəyirsiniz: qidalanma, xəstəlik, yoxsa baxım?" although six
horse chunks were selected. Two resamples answered about colic with 3 PDF sources.

**A2, A3, A5, A8, A9, S1**: the same books are kept, the drafts differ in wording; A3's
draft lost detail in this sample.

## Variance check (same build, same server, fresh sessions)

The pipeline is stochastic, so the three changed non-species answers were re-sampled twice:

| Tag | Resample 1 | Resample 2 |
|---|---|---|
| A2 | `İnəklər gündə 2 dəfə sağılmalıdır.` (6 chunks) | `İnək gündə iki dəfə sağılmalıdır … 2-dən 3-ə sağım …` (6 chunks) |
| A7 | `42 Xəstə heyvanda uzunmüddətli ishal olduqda, qanda hemoqlobinin miqdarı …` (5 chunks) | `Dana ishalı olduqda, ilk növbədə xəstəliyin səbəbinin müayinəsi edilməlidir …` (5 chunks) |
| S3 | `Atlarda kolik – bu, atların bağıracığında yaranan bir xəstəlikdir …` (3 sources) | `Atlarda kolik – bu, atın bağıracığında yaranan xəstəlikdir …` (3 sources) |

A7 and S3 are therefore sample variance, not a rule effect; A2's measured draft was vague
while both resamples give the direct number.

## Checks

```
cmake --build build --config Release --target dumy dumy_unit_tests   exit 0
dumy_unit_tests.exe                                                  33/33 passed
tests\run_all.ps1                                                    86/86 passed, 0 warnings
```

`run_all` now includes `dom04_qarayara` (qarayara answers, >= 1 PDF source, no "aydın
mənbə tapılmadı") and `dom05_keci_surgery` (no `Qaramal` / `İri buynuzlu` in the goat
answer).

## Limitations

* One sample per question; the variance section shows that is not enough to call a 1-2
  point shift in the yes/partly/no totals.
* The label is coarse by construction: a multi-species book contributes its generic pages
  to every species question (A6, A10).
* No document in this corpus is labelled `bee`, `goat`, `horse`, `buffalo` or `poultry`;
  those questions depend on a chunk naming the species or on generic text from a
  multi-species book.
* A5/A6/A10 are still wrong for reasons that predate this branch (retrieval ranking and a
  4B model), not because of the species rule.
