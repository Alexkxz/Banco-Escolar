#include "panel_http_server.h"

#include <Arduino.h>
#include <WebServer.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../storage/sd_manager.h"

namespace {
constexpr uint16_t HTTP_PORT = 80;
constexpr char PANEL_VERSION_PATH[] = "panel-test/version.txt";
constexpr size_t MAX_VERSION_FILE_BYTES = 32;
constexpr size_t HTTP_STREAM_CHUNK_BYTES = 1024;
WebServer server(HTTP_PORT);
bool server_started = false;

const char *content_type_for_path(const String &path)
{
    if (path.endsWith(".html") || path.endsWith(".htm")) return "text/html; charset=utf-8";
    if (path.endsWith(".css")) return "text/css; charset=utf-8";
    if (path.endsWith(".js")) return "text/javascript; charset=utf-8";
    if (path.endsWith(".json")) return "application/json; charset=utf-8";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
    if (path.endsWith(".gif")) return "image/gif";
    if (path.endsWith(".webp")) return "image/webp";
    if (path.endsWith(".ico")) return "image/x-icon";
    if (path.endsWith(".txt")) return "text/plain; charset=utf-8";
    return nullptr;
}

void send_error(int status, const char *message)
{
    server.sendHeader("Cache-Control", "no-store");
    server.send(status, "text/plain; charset=utf-8", message);
}

struct VersionReadContext {
    String contents;
    bool valid_size = false;
};

bool begin_version_read(void *context, uint64_t file_size_bytes)
{
    auto *version = static_cast<VersionReadContext *>(context);
    version->valid_size = file_size_bytes > 0 && file_size_bytes <= MAX_VERSION_FILE_BYTES;
    version->contents.reserve(version->valid_size ? static_cast<size_t>(file_size_bytes) : 0);
    return version->valid_size;
}

bool collect_version_chunk(void *context, const uint8_t *data, size_t length)
{
    auto *version = static_cast<VersionReadContext *>(context);
    if (!version->valid_size || version->contents.length() + length > MAX_VERSION_FILE_BYTES) return false;
    for (size_t index = 0; index < length; ++index) version->contents += static_cast<char>(data[index]);
    return true;
}

String page_version()
{
    if (!sd_manager.isReady()) return "unknown";
    VersionReadContext context;
    uint64_t file_size = 0;
    uint64_t bytes_streamed = 0;
    const SdFileResult result = sd_manager.streamFile(
        PANEL_VERSION_PATH, begin_version_read, collect_version_chunk, &context, false,
        file_size, bytes_streamed);
    if (!result.ok() || !context.valid_size || bytes_streamed != file_size) return "unknown";

    String version;
    version.reserve(context.contents.length());
    for (size_t index = 0; index < context.contents.length(); ++index) {
        const uint8_t byte = static_cast<uint8_t>(context.contents[index]);
        if (byte == '\r' || byte == '\n' || byte == ' ' || byte == '\t') continue;
        if (!((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
              (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' || byte == '-'))
            return "unknown";
        version += static_cast<char>(byte);
    }
    return version.length() == 0 ? String("unknown") : version;
}

void handle_status()
{
    char body[128];
    const String version = page_version();
    snprintf(body, sizeof(body), "{\"sd_mounted\":%s,\"page_version\":\"%s\"}",
             sd_manager.isReady() ? "true" : "false", version.c_str());
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("X-Content-Type-Options", "nosniff");
    server.setContentLength(strlen(body));
    if (server.method() == HTTP_HEAD) server.send(200, "application/json; charset=utf-8", "");
    else server.send(200, "application/json; charset=utf-8", body);
}

struct HttpFileContext {
    const char *content_type = nullptr;
    bool headers_sent = false;
};

bool begin_http_file(void *context, uint64_t file_size_bytes)
{
    auto *http = static_cast<HttpFileContext *>(context);
    if (file_size_bytes > static_cast<uint64_t>(SIZE_MAX)) {
        send_error(413, "File is too large for this server");
        http->headers_sent = true;
        return false;
    }
    server.sendHeader("Cache-Control", "no-cache");
    server.sendHeader("X-Content-Type-Options", "nosniff");
    server.setContentLength(static_cast<size_t>(file_size_bytes));
    server.send(200, http->content_type, "");
    http->headers_sent = true;
    return true;
}

bool stream_to_http(void *, const uint8_t *data, size_t length)
{
    if (length > HTTP_STREAM_CHUNK_BYTES) return false;
    server.sendContent(reinterpret_cast<const char *>(data), length);
    return true;
}

bool resolve_panel_path(const String &uri, String &relative_path)
{
    if (uri == "/" || uri == "/panel-test" || uri == "/panel-test/") {
        relative_path = "panel-test/index.html";
        return true;
    }
    if (!uri.startsWith("/panel-test/")) return false;

    const String requested = uri.substring(1);
    if (requested.indexOf('%') >= 0 || requested.indexOf('\\') >= 0 ||
        requested.indexOf(':') >= 0 || requested.indexOf('\0') >= 0)
        return false;
    relative_path = requested;
    return true;
}

void handle_file()
{
    if (server.method() != HTTP_GET && server.method() != HTTP_HEAD) {
        server.sendHeader("Allow", "GET, HEAD");
        send_error(405, "Method not allowed");
        return;
    }

    String relative_path;
    if (!resolve_panel_path(server.uri(), relative_path)) {
        send_error(400, "Invalid file route");
        return;
    }
    if (!sd_manager.isReady()) {
        send_error(503, "microSD is not mounted");
        return;
    }

    const char *content_type = content_type_for_path(relative_path);
    if (content_type == nullptr) {
        send_error(415, "Unsupported file type");
        return;
    }

    HttpFileContext context = {content_type, false};
    uint64_t file_size = 0;
    uint64_t bytes_streamed = 0;
    const bool headers_only = server.method() == HTTP_HEAD;
    const SdFileResult stream_result = sd_manager.streamFile(
        relative_path.c_str(), begin_http_file, stream_to_http, &context, headers_only,
        file_size, bytes_streamed);
    if (!stream_result.ok() && !context.headers_sent) {
        if (stream_result.error == SdFileError::NOT_FOUND) send_error(404, "File not found");
        else if (stream_result.error == SdFileError::INVALID_PATH) send_error(400, "Invalid file path");
        else if (stream_result.error == SdFileError::NOT_MOUNTED) send_error(503, "microSD is not mounted");
        else if (stream_result.error != SdFileError::RESOURCE_LIMIT)
            send_error(500, "Unable to read file metadata");
    } else if (!stream_result.ok() || (!headers_only && bytes_streamed != file_size)) {
        Serial.printf("[HTTP] Error al transmitir archivo SD (%s, bytes=%llu/%llu)\n",
                      sd_file_error_name(stream_result.error),
                      static_cast<unsigned long long>(bytes_streamed),
                      static_cast<unsigned long long>(file_size));
    }
}

void handle_not_found()
{
    if (server.uri() == "/status" &&
        (server.method() == HTTP_GET || server.method() == HTTP_HEAD)) {
        handle_status();
        return;
    }
    handle_file();
}
}

PanelHttpServer panel_http_server;

void PanelHttpServer::begin()
{
    if (server_started) return;
    server.on("/status", HTTP_GET, handle_status);
    server.on("/status", HTTP_HEAD, handle_status);
    server.onNotFound(handle_not_found);
    server.begin();
    server_started = true;
    Serial.printf("[HTTP] Servidor de solo lectura iniciado en puerto %u\n", HTTP_PORT);
}

void PanelHttpServer::update()
{
    if (server_started) server.handleClient();
}
