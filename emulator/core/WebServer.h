#pragma once
// Minimal stand-in — just enough for PhotoFrameApp.h's `extern WebServer
// photo_server;` to parse. PhotoFrameApp.cpp (the real implementation that
// actually uses this) isn't compiled in the emulator; nothing else
// references this type, so it doesn't need real functionality.
class WebServer {
public:
    WebServer(int /*port*/ = 80) {}
};
