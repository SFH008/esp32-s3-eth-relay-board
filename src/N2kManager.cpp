/*
  N2kManager.cpp — NMEA 2000 / CAN integration

  Cerbo GX integration:
    Listens for PGN 127501 (Binary Switch Bank Status) from the Cerbo GX.
    When cerboN2kEnable is true and the source instance matches cerboN2kInstance,
    the 8 channel states are mapped 1:1 to the relay board's 8 relays.
    Last-write-wins — web UI / MQTT / SK commands are not blocked.
*/

#define ESP32_CAN_TX_PIN GPIO_NUM_17
#define ESP32_CAN_RX_PIN GPIO_NUM_18

#include <NMEA2000_esp32_twai.h>
#include <N2kMessages.h>
#include <NMEA2000.h>

#include "N2kManager.h"
#include "Config.h"
#include "Logger.h"
#include "RelayControl.h"
#include "BoardPins.h"

static void n2kTask(void *pv);
extern uint8_t readDI_mask();

// ── Module state ──────────────────────────────────────────────────────────────
static NMEA2000_esp32_twai *s_n2k        = nullptr;
static TaskHandle_t          s_taskHandle = nullptr;
static volatile bool         s_running    = false;
static volatile bool         s_publishReq = false;

// Cerbo watchdog — updated every time a matching PGN 127501 arrives
static volatile uint32_t     s_cerboLastMs  = 0;
static volatile bool         s_cerboSeen    = false;

// ── PGN 127501 handler — Cerbo GX switch bank status ─────────────────────────
class CerboSwitchBankHandler : public tNMEA2000::tMsgHandler {
public:
    CerboSwitchBankHandler() : tMsgHandler(127501L) {}
    void HandleMsg(const tN2kMsg &msg) override;
};

static CerboSwitchBankHandler s_cerboHandler;

void CerboSwitchBankHandler::HandleMsg(const tN2kMsg &msg) {
    if (!g_cfg.cerboN2kEnable) return;

    unsigned char instance;
    tN2kBinaryStatus bankStatus;
    if (!ParseN2kPGN127501(msg, instance, bankStatus)) return;
    if (instance != g_cfg.cerboN2kInstance) return;

    // Build relay mask from channels 1–8
    uint8_t newMask = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        tN2kOnOff sw = N2kGetStatusOnBinaryStatus(bankStatus, i + 1);
        if (sw == N2kOnOff_On) newMask |= (1u << i);
        // N2kOnOff_Off and N2kOnOff_Unavailable both leave bit clear
    }

    s_cerboLastMs = millis();
    s_cerboSeen   = true;

    LOGf("[N2K] Cerbo inst=%u mask=0x%02X", instance, newMask);
    relayApplyMask(newMask);
}

// ── PGN 127502 handler — Switch Bank Control (from MFD etc.) ─────────────────
class SwitchBankControlHandler : public tNMEA2000::tMsgHandler {
public:
    SwitchBankControlHandler() : tMsgHandler(127502L) {}
    void HandleMsg(const tN2kMsg &msg) override;
};

static SwitchBankControlHandler s_swBankHandler;

void SwitchBankControlHandler::HandleMsg(const tN2kMsg &msg) {
    unsigned char instance;
    tN2kBinaryStatus bankStatus;
    if (!ParseN2kPGN127502(msg, instance, bankStatus)) return;
    if (instance != g_cfg.n2kDeviceInstance) return;

    LOGf("[N2K] SwitchBankControl inst=%u", instance);

    uint8_t newMask = relayGetMask();
    for (uint8_t i = 0; i < 8; ++i) {
        tN2kOnOff sw = N2kGetStatusOnBinaryStatus(bankStatus, i + 1);
        if (sw == N2kOnOff_On)  newMask |=  (1u << i);
        if (sw == N2kOnOff_Off) newMask &= ~(1u << i);
    }
    relayApplyMask(newMask);
}

// ── Helpers ───────────────────────────────────────────────────────────────────
static tN2kBinaryStatus maskToBinaryStatus(uint8_t mask) {
    tN2kBinaryStatus bs;
    N2kResetBinaryStatus(bs);
    for (uint8_t i = 0; i < 8; ++i)
        N2kSetStatusBinaryOnStatus(bs,
            (mask & (1u << i)) ? N2kOnOff_On : N2kOnOff_Off, i + 1);
    return bs;
}

// ── Public API ────────────────────────────────────────────────────────────────
void n2kRequestPublish() { s_publishReq = true; }
bool n2kIsRunning()      { return s_running; }

// Returns ms since last Cerbo frame, or 0 if none ever received
uint32_t n2kCerboLastSeenMs() {
    if (!s_cerboSeen) return 0;
    return millis() - s_cerboLastMs;
}

void n2kPublishRelayStates(uint8_t relayMask) {
    if (!s_n2k || !s_running) return;
    tN2kMsg msg;
    SetN2kPGN127501(msg, g_cfg.n2kDeviceInstance, maskToBinaryStatus(relayMask));
    s_n2k->SendMsg(msg);
}

void n2kPublishInputStates(uint8_t diMask) {
    if (!s_n2k || !s_running) return;
    tN2kMsg msg;
    SetN2kPGN127501(msg, (unsigned char)(g_cfg.n2kDeviceInstance + 1),
                    maskToBinaryStatus(diMask));
    s_n2k->SendMsg(msg);
}

void n2kStop() {
    s_running = false;
    if (s_taskHandle) {
        vTaskDelay(pdMS_TO_TICKS(200));
        vTaskDelete(s_taskHandle);
        s_taskHandle = nullptr;
    }
    if (s_n2k) {
        s_n2k->DetachMsgHandler(&s_swBankHandler);
        s_n2k->DetachMsgHandler(&s_cerboHandler);
        delete s_n2k;
        s_n2k = nullptr;
    }
    LOG("[N2K] Stopped");
}

void n2kStart() {
    if (s_running) { LOG("[N2K] Already running"); return; }

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    uint32_t uniqueNum = ((uint32_t)mac[3] << 16)
                       | ((uint32_t)mac[4] <<  8) | (uint32_t)mac[5];
    char serialStr[12];
    snprintf(serialStr, sizeof(serialStr), "%010lu",
             (unsigned long)(((uint32_t)mac[2] << 24) | uniqueNum));

    s_n2k = new NMEA2000_esp32_twai(ESP32_CAN_TX_PIN, ESP32_CAN_RX_PIN);
    s_n2k->SetProductInformation(serialStr, 100,
        "ESP32-S3 Relay Board", "1.0.0", "Rev A");
    s_n2k->SetDeviceInformation(uniqueNum, 135, 25, 2046);
    s_n2k->SetMode(tNMEA2000::N2km_NodeOnly, g_cfg.n2kSourceAddress);
    s_n2k->SetN2kCANSendFrameBufSize(50);
    s_n2k->SetN2kCANReceiveFrameBufSize(50);

    s_n2k->AttachMsgHandler(&s_swBankHandler);
    s_n2k->AttachMsgHandler(&s_cerboHandler);

    static const unsigned long txPGNs[] = {127501L, 0};
    s_n2k->ExtendTransmitMessages(txPGNs);

    if (!s_n2k->Open()) {
        LOG("[N2K] Open failed — check GPIO17/18 and bus power");
        delete s_n2k; s_n2k = nullptr; return;
    }

    LOGf("[N2K] Bus open — inst=%u addr=%u cerbo=%s inst=%u",
         g_cfg.n2kDeviceInstance, g_cfg.n2kSourceAddress,
         g_cfg.cerboN2kEnable ? "enabled" : "disabled",
         g_cfg.cerboN2kInstance);

    s_running    = true;
    s_publishReq = true;

    xTaskCreatePinnedToCore(n2kTask, "n2kTask", 4096, nullptr, 2,
                            &s_taskHandle, 0);
}

// ── FreeRTOS task ─────────────────────────────────────────────────────────────
static void n2kTask(void *pv) {
    const TickType_t kPublishInterval = pdMS_TO_TICKS(1000);
    TickType_t       lastPublish      = xTaskGetTickCount();

    while (s_running) {
        s_n2k->ParseMessages();

        TickType_t now = xTaskGetTickCount();
        if ((now - lastPublish) >= kPublishInterval || s_publishReq) {
            s_publishReq = false;
            lastPublish  = now;
            n2kPublishRelayStates(relayGetMask());
            n2kPublishInputStates(readDI_mask());
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    vTaskDelete(nullptr);
}