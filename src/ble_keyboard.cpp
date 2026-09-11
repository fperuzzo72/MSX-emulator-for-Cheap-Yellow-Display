/* ble_keyboard.cpp - BLE HID keyboard host (central) for FNK0103 MSX.
 *
 * Adapted from the connect/subscribe pattern in esp32beans/BLE_HID_Client
 * (MIT license, see /third_party_licenses/BLE_HID_Client.txt), simplified
 * and retargeted at the standard Boot Keyboard Input Report (0x2A22).
 *
 * Written against NimBLE-Arduino 2.x, which is the version that matches
 * Arduino-ESP32 3.x: on 1.x the firmware compiles but aborts during
 * NimBLEDevice::init() (see the note in platformio.ini).
 *
 * This file is only the transport. Everything about what the keys MEAN -
 * the US-International layer, the dead keys, this machine's matrix - is
 * in msx_keys.c, which is plain C and knows nothing about BLE.
 */
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <cstring>
#include "ble_keyboard.h"
#include "msx_keys.h"

static const char HID_SERVICE_UUID[]        = "1812";
static const char HID_BOOT_KBD_INPUT_UUID[] = "2a22"; /* Boot Keyboard Input Report */
static const char HID_PROTOCOL_MODE_UUID[]  = "2a4e"; /* 0 = boot protocol         */
static const char HID_REPORT_DATA_UUID[]    = "2a4d"; /* generic Report (fallback) */

static NimBLEAddress sTarget;
static volatile bool sHaveTarget = false;
static volatile bool sConnected  = false;

/* Latest 8-byte boot keyboard report: [modifiers, reserved, key1..key6].
 * Written by the NimBLE host task, read by the emulation task, so it is
 * guarded - the two run on different cores. */
static portMUX_TYPE sReportMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t sReport[8] = {0};

static void notifyCB(NimBLERemoteCharacteristic *chr, uint8_t *data, size_t len, bool isNotify) {
    (void)chr; (void)isNotify;
    if (len < 8) return; /* not a boot-protocol-shaped report */
    portENTER_CRITICAL(&sReportMux);
    memcpy(sReport, data, 8);
    portEXIT_CRITICAL(&sReportMux);
}

class KbdScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice *dev) override {
        if (!dev->haveServiceUUID() || !dev->isAdvertisingService(NimBLEUUID(HID_SERVICE_UUID))) {
            /* Say what was seen and passed over. Pairing has never been
             * tried on this board, so the first attempt should not have
             * to guess why nothing happened - a keyboard that is
             * Bluetooth Classic rather than BLE, for instance, never
             * shows up here at all. */
            static unsigned seen = 0;
            if (++seen % 20 == 0)
                Serial.printf("BLE: %u advertisements seen, none advertising HID yet\n", seen);
            return;
        }
        /* Prefer something that says it is a keyboard, but accept a
         * generic HID or an unset appearance too - plenty of keyboards
         * never fill that field in. */
        uint16_t appearance = dev->getAppearance();
        if (appearance != 0x3C1 && appearance != 0x3C0 && appearance != 0) return;

        Serial.printf("BLE: HID device %s (appearance 0x%04X), connecting\n",
                      dev->getAddress().toString().c_str(), appearance);
        sTarget = dev->getAddress();
        sHaveTarget = true;
        NimBLEDevice::getScan()->stop();
    }
};

class KbdClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient *c) override {
        c->updateConnParams(12, 12, 0, 150);
    }
    void onDisconnect(NimBLEClient *c, int reason) override {
        (void)c; (void)reason;
        sConnected = false;
        Serial.printf("BLE: disconnected (reason %d), scanning again\n", reason);
        portENTER_CRITICAL(&sReportMux);
        memset(sReport, 0, sizeof(sReport));
        portEXIT_CRITICAL(&sReportMux);
        NimBLEDevice::getScan()->start(0, false, true);
    }
    void onPassKeyEntry(NimBLEConnInfo &connInfo) override {
        NimBLEDevice::injectPassKey(connInfo, 0);
    }
    void onConfirmPasskey(NimBLEConnInfo &connInfo, uint32_t pin) override {
        (void)pin;
        NimBLEDevice::injectConfirmPasskey(connInfo, true);
    }
};

static KbdClientCallbacks sClientCB;

static bool connectToKeyboard() {
    NimBLEClient *client = NimBLEDevice::createClient();
    client->setClientCallbacks(&sClientCB, false);
    client->setConnectTimeout(7000);

    if (!client->connect(sTarget)) {
        Serial.println("BLE: connect failed");
        NimBLEDevice::deleteClient(client);
        return false;
    }

    NimBLERemoteService *hid = client->getService(HID_SERVICE_UUID);
    if (!hid) {
        Serial.println("BLE: connected but no HID service, dropping");
        client->disconnect();
        return false;
    }

    /* Best effort: ask for boot protocol. Not every device exposes this. */
    NimBLERemoteCharacteristic *proto = hid->getCharacteristic(HID_PROTOCOL_MODE_UUID);
    if (proto && proto->canWrite()) {
        uint8_t bootMode = 0x00;
        proto->writeValue(&bootMode, 1, true);
    }

    bool subscribed = false;
    NimBLERemoteCharacteristic *bootKbd = hid->getCharacteristic(HID_BOOT_KBD_INPUT_UUID);
    if (bootKbd && bootKbd->canNotify()) subscribed = bootKbd->subscribe(true, notifyCB);

    if (!subscribed) {
        /* Some keyboards only expose the generic Report characteristic,
         * sometimes several of them. Subscribe to all; notifyCB ignores
         * anything that is not shaped like an 8-byte boot report. */
        const std::vector<NimBLERemoteCharacteristic *> &chars = hid->getCharacteristics(true);
        for (auto c : chars) {
            if (c->getUUID() == NimBLEUUID(HID_REPORT_DATA_UUID) && c->canNotify()) {
                if (c->subscribe(true, notifyCB)) subscribed = true;
            }
        }
    }

    if (!subscribed) {
        Serial.println("BLE: no report characteristic to subscribe to, dropping");
        client->disconnect();
        return false;
    }

    sConnected = true;
    Serial.printf("BLE: keyboard connected, free heap %u\n", (unsigned)ESP.getFreeHeap());
    return true;
}

void ble_keyboard_init() {
    NimBLEDevice::init("FNK0103-MSX");
    NimBLEDevice::setSecurityAuth(true, false, true); /* bond, no MITM, secure connections */
    NimBLEDevice::setPower(9);

    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(new KbdScanCallbacks(), false);
    scan->setInterval(45);
    scan->setWindow(15);
    scan->setActiveScan(true);
    scan->start(0, false, true); /* scan until a HID device turns up */
    Serial.println("BLE: scanning for a keyboard");
}

int ble_keyboard_connected() { return sConnected ? 1 : 0; }

void ble_keyboard_poll() {
    /* The connect handshake runs here, on the emulation task, rather than
     * inside the scan callback on the NimBLE host task. */
    if (sHaveTarget && !sConnected) {
        sHaveTarget = false;
        if (!connectToKeyboard()) NimBLEDevice::getScan()->start(0, false, true);
    }

    uint8_t report[8];
    portENTER_CRITICAL(&sReportMux);
    memcpy(report, sReport, 8);
    portEXIT_CRITICAL(&sReportMux);

    msx_keys_set_report(report);
}
