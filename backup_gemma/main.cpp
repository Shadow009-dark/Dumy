// main.cpp - DUMY entry point.
//
// Modes:
//   dumy                 -> REST server (default, website-ready)
//   dumy --console       -> interactive console chat (test without a website)
//   dumy --ingest        -> (re)index the PDF folder and exit
//
// All user-facing output is in Azerbaijani; code/comments stay in English.
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>   // ShellExecuteA (open the browser)
#pragma comment(lib, "shell32.lib")
#else
#include <unistd.h>
#endif

#include "dumy/bm25_index.h"
#include "dumy/common.h"
#include "dumy/config.h"
#include "dumy/http_server.h"
#include "dumy/indexer.h"
#include "dumy/ollama_client.h"
#include "dumy/pdf_loader.h"
#include "dumy/prompts.h"
#include "dumy/rag_pipeline.h"
#include "dumy/retriever.h"
#include "dumy/reranker.h"
#include "dumy/session_manager.h"
#include "dumy/vector_store.h"
#include "dumy/web_search.h"

using namespace dumy;

namespace {

// Windows console: UTF-8 codepages + virtual terminal so Azerbaijani letters
// (ə, ü, ş, ğ...) print correctly instead of mojibake.
void enable_utf8_console() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hout != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hout, &mode)) {
            SetConsoleMode(hout, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
    // Make the C runtime use UTF-8 for std::cout as well.
    setvbuf(stdout, nullptr, _IOFBF, 16384);
#endif
}

// Opens the chat page in the default browser (best effort, never fatal).
void open_browser(const std::string& url) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    (void)system(("open '" + url + "'").c_str());
#else
    (void)system(("xdg-open '" + url + "' >/dev/null 2>&1 &").c_str());
#endif
}

struct CliArgs {
    bool console_mode = false;
    bool ingest_only = false;
    bool force_ingest = false;
    bool show_help = false;
    bool no_web = false;
    bool no_browser = false;
    std::string config_path;
    std::string chat_model;
    std::string helper_model;
    std::string selfcheck_model;
    int port = -1;
};

void print_usage() {
    std::cout <<
        "DUMY - lokal kənd təsərrüfatı AI köməkçisi (Ollama + RAG)\n"
        "\n"
        "İstifadə:\n"
        "  dumy                     REST server işə salır (config.json portu)\n"
        "  dumy --console           əmrlə söhbət rejimi (vebsayt olmadan test)\n"
        "  dumy --ingest            PDF-ləri indeksləşdirib çıxır\n"
        "  dumy --ingest --force    hər şeyi yenidən indeksləşdirir\n"
        "\n"
        "Seçimlər:\n"
        "  --config <path>          config.json faylının yolu\n"
        "  --port <n>               server portu (config-dan üstündür)\n"
        "  --chat-model <ad>        çat modeli (məs. qwen3:8b, llama3.1:8b)\n"
        "  --helper-model <ad>      expansion/rerank modeli (default: config)\n"
        "  --selfcheck-model <ad>   fact-check modeli (default: config)\n"
        "  --no-web                 internet axtarışını söndürür\n"
        "  --no-browser              brauzeri avtomatik açmır (testlər üçün)\n"
        "  --help                   bu köməkçi mətni\n";
}

std::string find_config_file(const std::string& explicit_path) {
    if (!explicit_path.empty()) return explicit_path;

    // 1) current working directory
    std::error_code ec;
    if (fs::exists(fs::u8path("config.json"), ec)) return "config.json";

    // 2) walk up from the executable (build/Release/dumy.exe -> project root)
    std::vector<char> buf(4096);
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    fs::path exe = (n > 0 && n < buf.size()) ? fs::path(buf.data()) : fs::path();
#else
    char tmp[4096];
    ssize_t n = readlink("/proc/self/exe", tmp, sizeof(tmp) - 1);
    fs::path exe;
    if (n > 0) {
        tmp[n] = '\0';
        exe = tmp;
    }
#endif
    if (!exe.empty()) {
        fs::path dir = exe.parent_path();
        for (int depth = 0; depth < 6; ++depth) {
            fs::path candidate = dir / "config.json";
            if (fs::exists(candidate, ec)) return candidate.u8string();
            if (!dir.has_parent_path() || dir == dir.parent_path()) break;
            dir = dir.parent_path();
        }
    }
    return "config.json";  // Config::load will report "not found" + defaults
}

int run_console(RagPipeline& pipeline, Indexer& indexer, const Config& cfg) {
    std::cout <<
        "\nDUMY konsol rejimi.  Sual yazın və Enter-a basın.\n"
        "  /help   - köməkçi    /ingest - PDF-ləri yenidən indeksləşdir\n"
        "  /info   - status      /exit   - çıxış\n"
        "---------------------------------------------\n";

    std::string line;
    for (;;) {
        std::cout << "\nSual > " << std::flush;
        if (!std::getline(std::cin, line)) break;
        line = trim(line);
        if (line.empty()) continue;

        if (line == "/exit" || line == "/quit" || line == "/q") break;
        if (line == "/help" || line == "/h") {
            std::cout << "Sadə sual yazın. Məsələn:\n"
                      << "  Qoyunların xəstəlikləri haqqında yaz\n"
                      << "  Heyvanların bütün xəstəlikləri haqqında yaz\n"
                      << "  Telefon nədir?\n"
                      << "  Mənə Python kodu yaz      (nə üçün inkar olunur)\n";
            continue;
        }
        if (line == "/info") {
            std::cout << "Model: " << pipeline.chat_model()
                      << " | köməkçi: " << pipeline.helper_model()
                      << " | embed: " << pipeline.embedding_model()
                      << " | Ollama: " << (pipeline.ollama_ok() ? "bağlı" : "YOX")
                      << " | PDF dəstəyi: " << (pdf_support_enabled() ? "var" : "yoxdur")
                      << " | internet: " << (cfg.web_search_enabled ? "açiq" : "bağlı")
                      << "\n";
            continue;
        }
        if (line == "/ingest") {
            std::cout << "İndeks yenilənir...\n";
            Indexer::Report r = indexer.ingest(true);
            pipeline.refresh_document_species();
            std::cout << "Fayl: " << r.files_indexed << " indeksləşdi, " << r.files_failed
                      << " uğursuz, +" << r.chunks_added << " chunk\n";
            continue;
        }

        std::cout << "\n";
        std::string streamed;
        try {
            AnswerResult res = pipeline.answer(line, "console",
                                               [&](const std::string& delta) {
                                                   std::cout << delta << std::flush;
                                                   streamed += delta;
                                               });

        // The streamed text is the draft; the final answer (after self-check)
        // plus the sources footer is authoritative. Reprint only if changed.
        const std::string full = res.answer;
        const size_t foot = full.find(SOURCES_HEADER);
        const std::string core = (foot == std::string::npos) ? full : full.substr(0, foot);
        const std::string footer = (foot == std::string::npos) ? "" : full.substr(foot);

        if (trim(core) != trim(streamed)) {
            std::cout << "\n" << trim(core) << "\n";
        }
        if (!footer.empty()) {
            std::cout << (streamed.empty() || streamed.back() == '\n' ? "" : "\n")
                      << footer;
            if (footer.back() != '\n') std::cout << "\n";
        }
        // Sources (console equivalent of the UI's collapsible "Mənbələr" list)
        const nlohmann::json srcs = res.sources_json();
        if (srcs.is_array() && !srcs.empty()) {
            std::cout << "\nMənbələr:\n";
            for (const auto& s : srcs)
                std::cout << "  - " << s.value("file", std::string()) << "\n";
        }
        } catch (const std::exception& e) {
            std::cerr << "\n[XƏTA] " << e.what() << "\n";
        } catch (...) {
            std::cerr << "\n[XƏTA] naməlum xəta\n";
        }
    }
    std::cout << "Sağ olun! 🌾\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    enable_utf8_console();
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--console" || a == "-c") args.console_mode = true;
        else if (a == "--ingest") args.ingest_only = true;
        else if (a == "--force") args.force_ingest = true;
        else if (a == "--no-web") args.no_web = true;
        else if (a == "--no-browser") args.no_browser = true;
        else if (a == "--help" || a == "-h") args.show_help = true;
        else if (a == "--config" && i + 1 < argc) args.config_path = argv[++i];
        else if (a == "--port" && i + 1 < argc) args.port = std::atoi(argv[++i]);
        else if (a == "--chat-model" && i + 1 < argc) args.chat_model = argv[++i];
        else if (a == "--helper-model" && i + 1 < argc) args.helper_model = argv[++i];
        else if (a == "--selfcheck-model" && i + 1 < argc) args.selfcheck_model = argv[++i];
        else {
            std::cerr << "Naməlum seçim: " << a << " (--help ilə kömək)\n";
            return 2;
        }
    }
    if (args.show_help) {
        print_usage();
        return 0;
    }

    // --- configuration ------------------------------------------------------
    const std::string config_path = find_config_file(args.config_path);
    std::string cfg_err;
    Config cfg = Config::load(config_path, &cfg_err);
    if (!cfg_err.empty()) log_warn(cfg_err);
    if (args.no_web) cfg.web_search_enabled = false;
    if (args.no_browser) cfg.open_browser = false;
    if (args.port > 0) cfg.port = args.port;
    if (!args.chat_model.empty()) cfg.chat_model = args.chat_model;
    // Benchmarking a single main model: keep expansion/rerank/self-check on
    // the SAME model, otherwise Ollama swaps a second model in on every call.
    if (!args.helper_model.empty()) cfg.helper_model = args.helper_model;
    if (!args.selfcheck_model.empty()) cfg.selfcheck_model = args.selfcheck_model;

    std::error_code ec;
    fs::create_directories(cfg.data_dir(), ec);

    std::cout << "\n=== DUMY - kənd təsərrüfatı AI köməkçisi ===\n"
              << "Konfiqurasiya : " << fs::u8path(config_path).u8string() << "\n"
              << "Ollama        : " << cfg.ollama_url << "\n"
              << "PDF qovluğu   : " << cfg.pdf_dir().u8string() << "\n"
              << "Məlumat qovluğu: " << cfg.data_dir().u8string() << "\n";

    if (!pdf_support_enabled()) {
        log_warn("PDF dəstəyi YOXDUR (poppler-cpp tapılmadı) - yalnız veb və "
                 "ümumi bilik istifadə olunacaq");
    }

    // --- components ---------------------------------------------------------
    OllamaClient ollama(cfg.ollama_url, cfg.request_timeout);
    auto store = std::make_shared<InMemoryVectorStore>();
    Bm25Index bm25;
    WebSearch web(cfg);
    Retriever retriever(cfg, ollama, store, &bm25, &web);
    Reranker reranker(cfg, ollama);
    SessionManager sessions(static_cast<size_t>(cfg.max_history_messages));
    ConversationStore conversations(cfg);  // data/memory: conversations + facts
    Indexer indexer(cfg, ollama, store, bm25);
    indexer.set_embedding_model(cfg.embedding_model);
    RagPipeline pipeline(cfg, ollama, store, bm25, web, retriever, reranker, sessions,
                         &conversations);

    // --- persisted index ----------------------------------------------------
    {
        std::string lerr;
        const bool had_file = fs::exists(cfg.data_dir() / "index.bin", ec);
        if (indexer.load_index(&lerr)) {
            log_info("İndeks yükləndi: " + std::to_string(store->size()) + " chunk");
            indexer.rebuild_bm25();
            // Document-level species labels: computed once from the loaded
            // index, so a generic chunk knows which book it came from.
            pipeline.refresh_document_species();
        } else if (!lerr.empty() && had_file) {
            log_warn("İndeks yüklənmədi: " + lerr + " (yenidən qurulacaq)");
        } else {
            log_info("İndeks faylı yoxdur - birinci işə salınışda qurulacaq");
        }
    }

    // --- Ollama check + model resolution ------------------------------------
    std::string init_err;
    const bool ollama_ok = pipeline.initialize(&init_err);
    if (!ollama_ok) log_error(init_err + "\n (Server yenə də işə düşəcək; cavab zamanı "
                                         "bu barədə xəbərdarlıq alınacaq)");

    // --- ingest -------------------------------------------------------------
    if (args.ingest_only) {
        if (!ollama_ok) {
            std::cerr << "İndeksləşdirmək üçün Ollama lazımdır: " << init_err << "\n";
            return 1;
        }
        Indexer::Report r = indexer.ingest(args.force_ingest);
        std::cout << "Nəticə: " << r.files_indexed << " fayl indeksləşdi, "
                  << r.files_skipped << " dəyişmədi, " << r.files_failed
                  << " uğursuz oldu, +" << r.chunks_added << " chunk\n";
        return r.ok ? 0 : 1;
    }
    if (cfg.auto_ingest && ollama_ok) {
        if (store->size() == 0)
            log_info("İndeks boşdur - PDF-lər indeksləşdirilir (ilk dəfə bir neçə dəqiqə "
                     "çəkə bilər)...");
        indexer.ingest(false);
    } else if (cfg.auto_ingest && !ollama_ok) {
        log_warn("Ollama işləmədiyi üçün indeksləşdirmə təxirə salındı");
    }
    // New chunks may have arrived: keep the document labels in step with them.
    pipeline.refresh_document_species();

    // --- step 1: preload both models BEFORE the server reports ready --------
    // Ingest runs first because it loads bge-m3; warming afterwards means no
    // model load is left for the first real question.
    if (ollama_ok) {
        log_info("Modellər yaddaşa yüklənir (warm-up)...");
        std::string werr;
        if (!pipeline.warmup(&werr)) {
            log_warn("Warm-up tamamlanmadı: " + werr);
        }
    }

    // --- modes --------------------------------------------------------------
    if (args.console_mode) {
        return run_console(pipeline, indexer, cfg);
    }

    HttpServer server(cfg, pipeline, indexer, sessions, conversations);
    std::string serr;
    if (!server.start(&serr)) {
        log_error(serr);
        std::cerr << "\nServer işə düşmədi: " << serr << "\n";
        return 1;
    }
    std::cout << "\nHazırdır! Brauzer açılır...\n"
              << "  Səhifə   : http://" << cfg.host << ":" << cfg.port << "/\n"
              << "  API      :\n"
              << "    POST http://" << cfg.host << ":" << cfg.port
              << "/chat        {\"session_id\":\"...\",\"message\":\"...\"}\n"
              << "    POST http://" << cfg.host << ":" << cfg.port
              << "/chat/stream  (SSE)\n"
              << "    POST http://" << cfg.host << ":" << cfg.port
              << "/ingest      /upload (multipart PDF)\n"
              << "    GET  http://" << cfg.host << ":" << cfg.port
              << "/health      /documents\n"
              << "Konsol rejimi üçün: dumy --console\n"
              << "Çıxış üçün Ctrl+C\n";
    if (cfg.open_browser) {
        open_browser("http://" + cfg.host + ":" + std::to_string(cfg.port) + "/");
    }
    server.wait();
    return 0;
}
