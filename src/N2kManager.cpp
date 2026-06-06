/*
  N2kManager.cpp — NMEA 2000 / CAN integration

  Fixes vs first version:
    • Class is NMEA2000_esp32_twai (no 't' prefix)
    • SetN2kPGN127501 / ParseN2kPGN127502 both use tN2kBinaryStatus (uint64)
      — not individual tN2kOnOff arguments
    • tMsgHandler must be subclassed (pure-virtual HandleMsg), not array-of-structs
    • s_n2k forward-declared via pointer-to-base tNMEA2000 to avoid ordering issues
*/

// ── Pin defines MUST appear before any NMEA2000 include ──────────────────────
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

// ── Forward declaration ───────────────────────────────────────────────────────
static void n2kTask(void *pv);
extern uint8_t readDI_mask();   // defined in main.cpp

// ── Module state ──────────────────────────────────────────────────────────────
static NMEA2000_esp32_twai *s_n2k        = nullptr;
static TaskHandle_t          s_taskHandle = nullptr;
static volatile bool         s_running    = false;
static volatile bool         s_publishReq = false;

// ── PGN 127502 message handler (must subclass tMsgHandler) ───────────────────
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

    // Only act on commands for our relay bank
    if (instance != g_cfg.n2kDeviceInstance) return;

    LOGf("[N2K] SwitchBankControl inst=%u", instance);

    // Read current relay state and apply only the channels that are
    // commanded ON or OFF; leave N2kOnOff_Unavailable channels unchanged.
    uint8_t newMask = relayGetMask();
    for (uint8_t i = 0; i < 8; ++i) {
        tN2kOnOff sw = N2kGetStatusOnBinaryStatus(bankStatus, i + 1);
        if (sw == N2kOnOff_On)  newMask |=  (1u << i);
        if (sw == N2kOnOff_Off) newMask &= ~(1u << i);
    }

    // relaySetMask also notifies MQTT + SK and calls n2kRequestPublish()
    relayApplyMask(newMask);
}

// ── Build a tN2kBinaryStatus word from an 8-bit mask ─────────────────────────
static tN2kBinaryStatus maskToBinaryStatus(uint8_t mask) {
    tN2kBinaryStatus bs;
    N2kResetBinaryStatus(bs);   // sets all 28 channels to N2kOnOff_Unavailable
    for (uint8_t i = 0; i < 8; ++i) {
        N2kSetStatusBinaryOnStatus(bs,
            (mask & (1u << i)) ? N2kOnOff_On : N2kOnOff_Off,
            i + 1);             // ItemIndex is 1-based
    }
    return bs;
}

// ── Public API ────────────────────────────────────────────────────────────────
void n2kRequestPublish() { s_publishReq = true; }
bool n2kIsRunning()      { return s_running; }

void n2kPublishRelayStates(uint8_t relayMask) {
    if (!s_n2k || !s_running) return;
    tN2kMsg msg;
    SetN2kPGN127501(msg, g_cfg.n2kDeviceInstance, maskToBinaryStatus(relayMask));
    s_n2k->SendMsg(msg);
}

void n2kPublishInputStates(uint8_t diMask) {
    if (!s_n2k || !s_running) return;
    tN2kMsg msg;
    SetN2kPGN127501(msg,
        (unsigned char)(g_cfg.n2kDeviceInstance + 1),
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
        delete s_n2k;
        s_n2k = nullptr;
    }
    LOG("[N2K] Stopped");
}

void n2kStart() {
    if (s_running) { LOG("[N2K] Already running"); return; }

    // Stable serial/unique numbers from MAC
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    uint32_t uniqueNum = ((uint32_t)mac[3] << 16)
                       | ((uint32_t)mac[4] <<  8)
                       | (uint32_t)mac[5];
    char serialStr[12];
    snprintf(serialStr, sizeof(serialStr), "%010lu",
             (unsigned long)(((uint32_t)mac[2] << 24) | uniqueNum));

    s_n2k = new NMEA2000_esp32_twai(ESP32_CAN_TX_PIN, ESP32_CAN_RX_PIN);

    s_n2k->SetProductInformation(
        serialStr, 100,
        "ESP32-S3 Relay Board",
        "1.0.0", "Rev A"
    );
    s_n2k->SetDeviceInformation(
        uniqueNum,
        135,    // function: Switch
        25,     // class:    Inter/Intranetwork Device
        2046    // manufacturer: experimental
    );

    s_n2k->SetMode(tNMEA2000::N2km_NodeOnly, g_cfg.n2kSourceAddress);
    s_n2k->SetN2kCANSendFrameBufSize(50);
    s_n2k->SetN2kCANReceiveFrameBufSize(50);

    // Attach PGN 127502 handler
    s_n2k->AttachMsgHandler(&s_swBankHandler);

    static const unsigned long txPGNs[] = {127501L, 0};
    s_n2k->ExtendTransmitMessages(txPGNs);

    if (!s_n2k->Open()) {
        LOG("[N2K] Open failed — check GPIO17/18 and bus power");
        delete s_n2k;
        s_n2k = nullptr;
        return;
    }

    LOGf("[N2K] Bus open — instance %u, preferred addr %u",
         g_cfg.n2kDeviceInstance, g_cfg.n2kSourceAddress);

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