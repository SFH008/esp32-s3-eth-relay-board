#pragma once
#include <Arduino.h>

// ── Lifecycle ─────────────────────────────────────────────────────────────────

// Start the SignalK WebSocket task (pinned to core 0).
// Token flow (SignalK device access request spec):
//   1. If token present in NVS → connect and authenticate
//   2. If token absent → submit access request, poll until approved/denied
//   3. If server returns 403 → submit new access request
//   4. If denied → stop requesting until reboot or manual clear
// Call after network is up and g_cfg is loaded.
void skStart();

// Gracefully disconnect and delete the task.
void skStop();

// ── Token / access request management ────────────────────────────────────────

// Submit a device access request to /signalk/v1/access/requests.
// Polls until APPROVED, DENIED, or timeout (120 s).
// On APPROVED: saves token to g_cfg.skToken and NVS, returns true.
// On DENIED:   sets denied flag — no further requests until skClearDenied().
// Blocking — call from core 1 only, never from inside the SK task.
bool skRequestAccess();

// Clear the denied flag and stored token — triggers a new access request on
// next skStart() or next 403. Call when user explicitly requests re-pairing.
void skClearDenied();

// True if server denied our last access request.
bool skAccessDenied();

// True if the WS task flagged that the current token was rejected (403).
bool skTokenRefreshNeeded();

// ── State ─────────────────────────────────────────────────────────────────────
bool skIsConnected();

// ── Publish helpers ───────────────────────────────────────────────────────────
// Safe to call from any task — internally queued and sent from the SK task.

void skPublishRelayState(uint8_t idx);
void skPublishAllRelayStates();
void skPublishInputState(uint8_t idx, uint8_t di_mask);
void skPublishAllInputStates();