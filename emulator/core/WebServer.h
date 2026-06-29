#pragma once
// L2: fake WebServer — enough of the Arduino-ESP32 WebServer API surface for
// PhotoFrameApp.cpp's HTTP routes to compile and run unmodified. There's no
// real listening socket: the emulator never opens a real WiFi AP (matching
// WiFi.h's "never actually connects" design), so handleClient() never has a
// real request to dispatch and the registered handlers are dead code at
// runtime — same boundary as the rest of L2's networking shims.
#include <cstdint>
#include "Arduino.h"

enum HTTPMethod { HTTP_GET, HTTP_POST, HTTP_ANY };
enum HTTPUploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };

struct HTTPUpload {
    HTTPUploadStatus status = UPLOAD_FILE_START;
    String filename;
    uint8_t* buf = nullptr;
    size_t currentSize = 0;
};

class WebServer {
public:
    using THandlerFunction = void (*)();
    explicit WebServer(int /*port*/ = 80) {}
    void on(const char*, HTTPMethod, THandlerFunction) {}
    void on(const char*, HTTPMethod, THandlerFunction, THandlerFunction) {}
    void send(int /*code*/, const char* /*contentType*/ = "text/plain", const String& /*content*/ = String()) {}
    void sendHeader(const char*, const char*) {}
    String arg(const char*) { return String(); }
    bool hasArg(const char*) { return false; }
    HTTPUpload& upload() { static HTTPUpload u; return u; }
    void begin() {}
    void stop() {}
    void handleClient() {}
};
