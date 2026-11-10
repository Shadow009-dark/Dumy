# Exposing DUMY to a website

DUMY is a local server (`127.0.0.1:8085`). Do **not** bind it to `0.0.0.0`
directly. Put a tunnel or reverse proxy in front of it and keep the API key on.

## 1. Build and start

```powershell
$env:VCPKG_ROOT = "C:/vcpkg"   # your vcpkg checkout, wherever it lives
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_INSTALLED_DIR="$PWD/build/vcpkg_installed" -DDUMY_WITH_POPPLER=ON
cmake --build build --config Release --target dumy
.\start_dumy.bat
```

`start_dumy.bat` preloads `gemma3:4b` + `bge-m3` and starts the server with the
browser UI. `--no-browser` starts it headless.

## 2. Set an API key (required before exposure)

Use an environment variable, not a tracked file:

```powershell
$env:DUMY_API_KEY = "<long random string>"
.\build\Release\dumy.exe --no-browser
```

Or put `"api_key": "..."` in `config.json`, but never commit that file with a
real key (`config.json` is tracked; use the environment variable instead).
`/health` stays open; `/chat` and `/chat/stream` require `X-API-Key`.

## 3. Allow the website origin

In `config.json`:

```json
"allowed_origins": ["https://your-site.example", "http://localhost:8085"],
"rate_limit_per_minute": 20,
"max_question_chars": 2000,
"max_body_kb": 256
```

`Access-Control-Allow-Origin: *` is no longer sent. Requests from an origin not
listed here get no CORS headers, so the browser blocks them.

## 4. Cloudflare Tunnel (free, no inbound ports)

```powershell
winget install --id Cloudflare.cloudflared
cloudflared tunnel --url http://127.0.0.1:8085
```

The command prints an `https://<random>.trycloudflare.com` URL. Add that exact
origin to `allowed_origins` and restart DUMY. Quick tunnels are for testing;
for a stable hostname create a named tunnel and a DNS route in the Cloudflare
dashboard.

Security notes:
- anyone with the URL can reach `/health`; `/chat` needs the key
- keep the key in your website's server-side proxy, not in browser JavaScript
  if the site is public; a static page with the key in source is public too
- `/upload` and `/ingest` reject non-loopback clients, so the tunnel cannot
  change the index
- the tunnel terminates TLS; DUMY itself stays on loopback HTTP

## 5. Reverse proxy alternative (same machine)

Caddy:

```
dumy.example.com {
    reverse_proxy 127.0.0.1:8085
}
```

nginx: `proxy_pass http://127.0.0.1:8085;` plus TLS certificates. Add
`https://dumy.example.com` to `allowed_origins`.

## 6. Call the API

```powershell
curl.exe -s http://127.0.0.1:8085/chat ^
  -H "Content-Type: application/json; charset=utf-8" ^
  -H "X-API-Key: %DUMY_API_KEY%" ^
  --data-binary "@question.json"
```

`question.json`: `{"session_id":"demo","message":"Fotosintez nədir?"}`.
Response: `answer`, `sources`, `qtype`, `web_used`, `warning`.

## 7. Minimal web page

`docs/web_demo/index.html` is a self-contained chat page. Host it on the same
origin as the API (recommended) or add its origin to `allowed_origins`. It asks
for the key at runtime and keeps it in memory only (not localStorage).

## 8. Checks before going live

- `tests\run_all.ps1` passes locally
- `/chat` without the key returns 401
- a foreign `Origin` gets no CORS headers
- 21 requests in a minute return 429
- `ollama ps` shows one chat model + `bge-m3`, no reload between questions
