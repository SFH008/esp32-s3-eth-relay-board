#pragma once
#include <WebServer.h>

extern WebServer server;

// Register all routes and call server.begin()
void httpStart();

// Build state JSON string (used by handlers and MQTT)
String buildStateJson();

// Build log JSON string
String buildLogJson();
