#include "web/web_server.h"
#include "web/api/web_api.h"
#include "ota/ota_manager.h"
#include "core/constants.h"
#include "app/app_context.h"
#include "rboot.h"
#include <webui.h>

WebServer web;

namespace {
const char* CAPTIVE_PATHS[] = {
    "/generate_204",
    "/hotspot-detect.html",
    "/library/test/success.html",
    "/connecttest.txt",
    "/connecttest.htm",
    "/redirect",
    "/ncsi.txt",
    "/fwlink",
    "/wpad.dat"
};
}

void WebServer::beginAP(uint16_t port) {
    (void)port;
    server_.begin();
    Serial.println(F("[MODULE] web_server real impl loaded"));
}

void WebServer::beginSTA(uint16_t port) {
    (void)port;
    server_.begin();
}

void WebServer::loop() {
    server_.handleClient();
    if (captiveActive_) dns_.processNextRequest();
}

void WebServer::stop() {
    server_.stop();
}

void WebServer::handleRoot() {
    const size_t total = sizeof(INDEX_HTML) - 1;
    server_.sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
    server_.sendHeader(F("Pragma"), F("no-cache"));
    server_.setContentLength(total);
    server_.send(200, "text/html; charset=UTF-8", "");
    size_t pos = 0;
    while (pos < total) {
        size_t chunk = min((size_t)1400, total - pos);
        server_.sendContent_P(INDEX_HTML + pos, chunk);
        pos += chunk;
        yield();
    }
    server_.sendContent("");
}

void WebServer::handleCaptiveRedirect() {
    String uri = server_.uri();
    if (uri.startsWith("/api/") || uri == "/update") {
        server_.send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
        return;
    }
    if (uri == "/wpad.dat") {
        server_.send(404, "text/plain", "");
        return;
    }
    String host = server_.hostHeader();
    if (host.indexOf(String(AP_IP_STR)) >= 0) {
        handleRoot();
        return;
    }
    String target = (uri == "/connecttest.txt")
                    ? String("http://logout.net")
                    : String("http://") + AP_IP_STR + "/";
    server_.sendHeader(F("Location"), target, true);
    server_.send(302, "text/plain", "");
    server_.client().stop();
}

void WebServer::registerApiRoutes() {
    api::registerAllRoutes();
    server_.on("/", HTTP_GET, [this]() { this->handleRoot(); });

#if IRAC_RBOOT_OTA
    server_.on("/update", HTTP_POST,
        [this]() {
            if (!server_.authenticate("admin", getApPass())) {
                return server_.requestAuthentication();
            }
            if (!ota.lastUploadOk() || ota.lastUploadSize() == 0) {
                String msg = "OTA write failed";
                if (ota.lastRejectReason()[0]) { msg += ": "; msg += ota.lastRejectReason(); }
                server_.send(500, "text/plain", msg);
                return;
            }
            OtaManager::ManifestInfo info{};
            char reason[96] = "";
            uint8_t target = rboot_get_target_rom();
            uint32_t addr  = rboot_get_rom_address(target);
            if (!ota.verifyInFlash(addr, ota.lastUploadSize(), target, info, reason, sizeof(reason))) {
                String msg = "OTA image verify failed";
                if (reason[0]) { msg += ": "; msg += reason; }
                server_.send(400, "text/plain", msg);
                return;
            }
            Serial.printf("[OTA] verified %u bytes, version=%s crc=0x%08X\n",
                          (unsigned)info.imageSize, info.version, info.imageCrc32);
            server_.send(200, "text/plain", "OK, rebooting");
            ota.switchToTargetRom();
            delay(100);
            ESP.restart();
        },
        []() {
            HTTPUpload& upload = web.raw().upload();
            if (upload.status == UPLOAD_FILE_START) {
                if (!web.raw().authenticate("admin", getApPass())) return;
                ota.onUploadStart();
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                ota.onUploadChunk(upload.buf, upload.currentSize);
            } else if (upload.status == UPLOAD_FILE_END) {
                ota.onUploadEnd();
            }
        });

    server_.on("/api/ota/status", HTTP_GET, []() {
        char buf[256];
        uint8_t cur = rboot_get_current_rom();
        uint8_t target = rboot_get_target_rom();
        rboot_config conf = rboot_get_config();
        snprintf(buf, sizeof(buf),
                 "{\"ok\":true,\"current_rom\":%u,\"target_rom\":%u,"
                 "\"rom0_addr\":\"0x%X\",\"rom1_addr\":\"0x%X\","
                 "\"target_addr\":\"0x%X\",\"ota_pending\":%s,"
                 "\"slot_max\":%u,\"sketch_size\":%u,\"free_space\":%u}",
                 cur, target, conf.roms[0], conf.roms[1],
                 rboot_get_rom_address(target),
                 ota.isPending() ? "true" : "false",
                 0xFE000u,
                 (unsigned)ESP.getSketchSize(),
                 (unsigned)ESP.getFreeSketchSpace());
        web.raw().send(200, "application/json", buf);
    });
#else
    server_.on("/update", HTTP_POST, []() {
        web.raw().send(503, "text/plain", "rboot OTA disabled in factory firmware");
    }, []() {
        HTTPUpload& upload = web.raw().upload();
        (void)upload;
    });
    server_.on("/api/ota/status", HTTP_GET, []() {
        web.raw().send(200, "application/json",
                       "{\"ok\":true,\"ota_supported\":false,\"mode\":\"factory\"}");
    });
#endif
}

void WebServer::startCaptivePortal() {
    if (captiveActive_) return;
    captiveActive_ = true;
    dns_.start(53, "*", WiFi.softAPIP());

    for (const char* path : CAPTIVE_PATHS) {
        server_.on(path, [this]() { this->handleCaptiveRedirect(); });
    }
    server_.onNotFound([this]() { this->handleCaptiveRedirect(); });
}

void WebServer::stopCaptivePortal() {
    if (!captiveActive_) return;
    captiveActive_ = false;
    dns_.stop();
}
