#pragma once
#include <WebServer.h>

// Transfer an upgraded connection without closing the shared socket.
class RelayWebServer : public WebServer {
public:
  using WebServer::WebServer;
  void releaseClient() {
    _currentClient = NetworkClient();
    _currentStatus = HC_NONE;
  }
};
extern RelayWebServer server;

// Register all routes and call server.begin()
void httpStart();

// Build state JSON string (used by handlers and MQTT)
String buildStateJson();

// Build log JSON string
String buildLogJson();
