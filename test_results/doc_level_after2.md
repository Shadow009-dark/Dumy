# No single-species book for the asked group -> name it or drop it (STEP 5)

Date: 2026-11-19. Branch `fix/species-doc-level`, parent `63d551f` (STEP 4) plus this change.
Binary `build\Release\dumy.exe` rebuilt from this tree; `/health` ->
`build_id=Nov 19 2026 20:50:21`, `chunks=6751`, `chat_model=gemma3:4b`,
`embedding_model=bge-m3`. `config.json` unmodified (`debug_trace_enabled=false`);
the measurement server was started with `DUMY_DEBUG_TRACE=1`, the documented
override (`src/config.cpp:91-96`). No dependency, threshold or config change:
`kSpeciesDocShareMin`/`kSpeciesDocMentionsMin` are untouched
(`include/dumy/az_quality.h:559-560`).

## What changed

`filter_chunks_by_species` (`include/dumy/az_quality.h:629-682`) now first asks
whether ANY document in the store carries a `Single` label for the questioned
group (`:652-662`). When none does, a generic chunk has no owner to inherit and
the only admissible evidence is a chunk that names the group (`:663`, `:666`).
Everything else is unchanged: a `Single` book for the asked group means generic
chunks of that group's books and of `Multi`/`None` books pass exactly as before
(`:667-669`), and an empty keep set still means Tier-4 naming the species
(`src/rag_pipeline.cpp:1717-1726`). The guard logs which rule fired
(`src/rag_pipeline.cpp:1685-1699`), so the reason is visible per request:

```
species guard: no source for goat (keçi, oğlaq) - 4 chunk(s) ... (no document is
labelled Single for it, so generic chunks cannot inherit) - context dropped
```

## Measured set

Re-run of A1, A6, A10 and S1-S6, one `POST /chat` with `{"debug":true}` per
question in a fresh session, server log sliced by byte offset
(`test_results/doc_level_run2.py`). Raw capture `test_results/doc_level_after2.json`
(gitignored), log `test_results/doc_level2_server.log` (gitignored). "Before" is
the STEP 4 run on `63d551f` (`test_results/doc_level_after.json`, summarised in
`test_results/doc_level_after.md`).

Load-time census unchanged: `doc species labels: 23 document(s), 4
species-specific` - Qaramal A5 (cattle), Qoyunçuluq (sheep), SAĞIM (cattle),
İntensiv maldarlıq (cattle). Bee, goat, buffalo have no `Single` book.

| Tag | Question | Before | After | Tier-4 before -> after |
|---|---|---|---|---|
| A1 | Mal-qarada qarayara xəstəliyinin əlamətləri nələrdir? | yes | yes | no -> no |
| A6 | Arılarda varroa xəstəliyi ilə necə mübarizə aparılır? | no | no | no -> no |
| A10 | Keçilərdə dırnaq xəstəlikləri hansılardır? | partly | no | **no -> YES** |
| S1 | Qoyunlarda dırnaq çürüməsi | yes | yes | no -> no |
| S2 | Toyuqlarda qəbizlik | no | no | yes -> yes |
| S3 | Atlarda kolik | no | no | no -> no |
| S4 | Camışda mastit | partly | partly | no -> no |
| S5 | Arılarda nozematoz | no | no | yes -> yes |
| S6 | Quzu ishalı | no | partly | **YES -> no** |

Tier-4 flips in this set: A10 flips INTO Tier-4 (predicted: its only kept chunk
was the generic surgery page), S6 flips OUT of Tier-4.

## Per question (answers quoted)

### A1 - cattle, no chunk names it, a `Single` cattle book exists: UNCHANGED

Guard log: `species guard: kept 2 chunk(s) for cattle (by document label, no
chunk names it), dropped 1`. Sources: Antropozoonoz xəstəlikləri.pdf pp. 21, 36.

Before (yes):
> Qarayara kəskin gedişli infeksion xəstəlik olub, septisemiya, orqanizmin ağır intoksikasiyası, daxili bədən temperaturasının kəskin yüksəlməsi, şişkinliklər və karbunkulların əmələ gəlməsi, bağırsaqların, ağciyərlərin zədələnməsi ilə səciyyələnir. Xəstəliyin əlamətləri heyvanlarda və insanlarda eyni formada olur. ...

After (yes, same first sentence):
> Qarayara kəskin gedişli infeksion xəstəlik olub, septisemiya, orqanizmin ağır intoksikasiyası, daxili bədən temperaturasının kəskin yüksəlməsi, şişkinliklər və karbunkulların əmələ gəlməsi, bağırsaqların, ağciyərlərin zədələnməsi ilə səciyyələnir. Xəstəliyin diaqnostikasında xəstəliyin törədicisinin morfoloji və kultural xüsusiyyətlərinin öyrənilməsi mühüm əhəmiyyət kəsb edir. ...

### A6 - bee, no `Single` bee book: the generic TB pages ARE gone, Tier-4 is NOT reached

Guard log: `species guard: kept 1 chunk(s) for bee (chunk names it, no Single
document for it), dropped 4`. The 4 generic chunks (Antropozoonoz p.61 TB
pathology, xırda heyvanlar p.245, ...) are dropped; the one survivor NAMES the
bee: `Körpə kənd təsərrüfatı hüyvanlarının daxili xəstəlikləri.pdf` p.171,
which is a bee-venom (arı zəhəri) poisoning page, not varroa.

Before (no, invented TB vaccines):
> Məlumatın yuxarıdakı mənbələrinə əsasən, arilarda varroa xəstəliyi ilə mübarizə aparılacaq halda aşağıdakı tədbirlər həyata keçirilə bilər: 1. **Vərəm vaksinlərinin tətbiqi:** Baytarlıqda bu vaksini istifadə etmək mümkün olmur, çünki immunizasiya edilmiş arılar uzun müddətlə allergik vəziyyətə malik olur. 2. **Heyvanların qruplaşdırılması və paylanması:** ...

After (still no - "vərəm" is gone, the bee-venom page is now the source):
> Arilarda varroa xəstəliyinə müvafiq olaraq mübarizə aparmaq üçün göstərir ki, varroa varioa qurdudur və arının sağlamlığına ciddi təhdid edir. Bu xəstəliyin müalicəsi üçün serumlar yeridilir. Yara yerindən 10-15 sm aralı 0,5%-li novokainlə dairəvi keçirici blokada aparılır. Həmçinin vena daxilə 200 ml 5%-li qlükoza və 20-30 ml 2%-li novokain məhlulu yeridilir. ... Böv zəhəri ilə zəhərləndikdə heyvanlara ... dimedrol, kalsiumqlukonat və ürək-damar preparatları təyin olunur.

**This is why `tests/cases/dom06_varroa.json` FAILS** (the requested live case:
"must be Tier-4"): with the rule as specified - keep ONLY chunks that name the
asked group - retrieval still returns a bee-naming chunk, so the guard has
something to keep and the Tier-4 branch (`src/rag_pipeline.cpp:1711`) is not
reached. Making A6 Tier-4 would mean dropping bee-naming chunks too, i.e. the
opposite of the S4 requirement below. Reported, not worked around.

### A10 - goat, no `Single` goat book: now Tier-4 (predicted)

Guard log, all four chunks dropped:
> `[Mənbə: T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf, səh. 434] doc=multi | ... səh. 436] doc=multi | ... səh. 435] doc=multi | [Mənbə: İntensiv və Ekstensiv maldarlıq.pdf, səh. 286] doc=cattle`

Before (partly; the only kept chunk was the generic p.436 surgery page):
> Keçilərdə dırnaq xəstəliklərinin ağır və xronik forması sadə formaların vaxtında müalicə olunmayıb onların xarici mikroflora ilə (koklar, viruslar, göbələklər və s) sirayətlənərək mürəkkəbləşməsindən tədricən baş verir. Bunlara dırnaq altlığının, yarığının və oynağının xoraları, fleqmonaları, irinli, nekrozlu iltihabları və s. aiddir. ...

After (no, Tier-4):
> Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: keçi, oğlaq). Kitablarda bu növ üçün uyğun bölmə yoxdur, ona görə cavabı başqa heyvan növündən götürmürəm. Sualı dəqiqləşdirin (xəstəliyin adı, heyvanın yaşı) və ya baytara müraciət edin.

### S1 - sheep, `Single` sheep book: unchanged

Guard log: `kept 1 chunk(s) for sheep (chunk names it), dropped 5`.
Before and after answer from `Heyvandarlığın əsasları.pdf` p.207:

> Qoyunlarda dırnaq çürüməsi, yəni nekrobakterioz xəstəliyi, ... Əlaməti: axsama, müalicə olunmazsa qoyun dizi üstə yeriyir, dırnağın yumşaq sahəsi və dırnaq arası sahədə yara əmələ gəlir. Müalicə üçün dırnaqda olan ölmüş hüceyrələri götürərək, yaraya 3%-li hidrogen neroksid və marqansovka ilə işləmək lazımdır.

### S2 - poultry: Tier-4 before and after

Relevance gate dropped every chunk (`branch=llm_no ... decision=drop`), then the
guard had nothing to filter. Tier-4 text names `toyuq, quş` - identical before
and after.

### S3 - horse: unchanged, still a clarification question

Guard log: `kept 6 chunk(s) for horse (chunk names it, no Single document for
it), dropped 0` - naming chunks still pass, exactly the S4 case. The draft is
still `at barədə nəyi öyrənmək istəyirsiniz: qidalanma, xəstəlik, yoxsa baxım?`
(no verdict change; the STEP 4 note about this draft being a clarification stands).

### S4 - buffalo, no `Single` buffalo book, chunks name camış: STILL ANSWERS

Guard log: `kept 3 chunk(s) for buffalo (chunk names it, no Single document for
it), dropped 3`. Sources: KƏND TƏSƏRRÜFATI HEYVANLARININ YEMLƏNDİRİLMƏSİ.pdf
p.201, Heyvandarlığın əsasları.pdf pp. 263, 265 - all naming camış.

After (partly):
> Camışlarda mastit - süd pərdələrinin infeksiyasıdır. Bu xəstəlik camışların süd verilmə qabiliyyətini azaldır və südün keyfiyyətini pisləndirir. Camışlarda mastitə səbənlər arasında bakteriyaların daxili və xarici etirakı, süd pərdələrindəki yaralar və ya qıcıqlar yer alır. ...

(Before, same question, answered with camış-naming chunks too, but two of the
three were about buffalo husbandry rather than mastitis; the guard now drops the
non-naming ones - 3 dropped.)

### S5 - bee, nozematoz: Tier-4 before and after

`branch=llm_no ... decision=drop`, no chunk reached the guard; Tier-4 names `arı`.

### S6 - sheep, `Single` sheep book exists: flipped OUT of Tier-4 by variance, not by this rule

Guard log: `kept 6 chunk(s) for sheep (chunk names it), dropped 0`. Sheep has a
`Single` book, so `single_for_query` is true and the new branch never runs for
this question - the code path is the STEP 4 one. In the STEP 4 run the same
question was Tier-4 because the relevance gate dropped everything (the same
gate variance documented for A7/S3 in `doc_level_after.md`); this run it kept
2/2 key terms (`score=0.523726 overlap=2/2 decision=keep`).

After (partly):
> Quzuda ishal xəstəliyi qoxalama keçirilən sahədə yolaxan anaerob dizenteriyadan başqa ola bilər. Xəstəliyin əlaməti köpüklü ishal, kal sarımtıl rəngdə, bəzən qanlı olur. Quzun yunu pırpızlı olmaqla, əyilmiş vəziyyətdədir. Müalicəsi kimi bina və yatağın təmiz saxlanması, dezinfeksiya edilməsi, qoyunun əmcəkləri yuyulub, təmizlənməli və quzulara ağız südü (bulama) içirdilməlidir.

## Checks

* `dumy_unit_tests` 34/34, including the two new cases:
  `doc label: no single goat book drops generic chunks of every book`
  (`tests/test_quality_gate.cpp:346-358`) and
  `doc label: a chunk naming the species survives without a single book`
  (`tests/test_quality_gate.cpp:369-381`).
* `tests\run_all.ps1`: 86/87. The one failure is the new
  `dom06_varroa` case, for the A6 reason above (`pdf=1`, Tier-4 text absent).
* `tests/test_quality_gate.cpp:346-358` (case 27, "generic chunk of a
  multi-species book stays") was REWRITTEN, not deleted: with no `Single` goat
  book it now expects the multi-species generic chunk to be dropped too. That is
  the behaviour this step asks for; the old assertion encoded the old rule.

## Not verified

* Only the 9 questions above were re-run; A2-A5, A7-A9 were not.
* A6 answer quality: the bee-venom page is not varroa material. Retrieval
  ranking, not the species guard, decides that; not touched here.
