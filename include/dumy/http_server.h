// http_server.h - cpp-httplib based REST server, website-ready:
//   POST /chat          {"session_id","message"} -> {answer, sources, ...}
//   POST /chat/stream   same via Server-Sent Events
//   POST /ingest        rescan the PDF folder (incremental)
//   GET  /health        service status
// CORS is enabled for every route so a browser app can call it directly.
#pragma once

#include <memory>
#include <string>

#include "dumy/config.h"
#include "dumy/indexer.h"
#include "dumy/rag_pipeline.h"
#include "dumy/session_manager.h"

namespace dumy {

class HttpServer {
public:
    HttpServer(const Config& cfg, RagPipeline& pipeline, Indexer& indexer,
               SessionManager& sessions);
    ~HttpServer();  // defined in the .cpp where Impl is complete

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Binds and serves on cfg.host:cfg.port in a background thread.
    // Returns false + err when the port is busy etc.
    bool start(std::string* err);
    void stop();
    void wait();   // blocks until the server stops

private:
    const Config& cfg_;
    RagPipeline& pipeline_;
    Indexer& indexer_;
    SessionManager& sessions_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dumy
