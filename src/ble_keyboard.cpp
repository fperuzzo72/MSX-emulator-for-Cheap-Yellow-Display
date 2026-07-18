/* ble_keyboard.cpp - BLE HID keyboard host (central) for FNK0103 MSX.
 *
 * Adapted from the connect/subscribe pattern in esp32beans/BLE_HID_Client
 * (MIT license, see /third_party_licenses/BLE_HID_Client.txt), simplified
 * and retargeted at the standard Boot Keyboard Input Report (0x2A22)
 * instead of generic HID reports, and wired to fMSX's key matrix instead
 * of USB HID pass-through.
 */
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <cstring>
#include <vector>
#include "ble_keyboard.h"
#include "msx_keys.h"

static const char HID_SERVICE_UUID[]        = "1812";
static const char HID_BOOT_KBD_INPUT_UUID[] = "2a22"; /* Boot Keyboard Input Report */
static const char HID_PROTOCOL_MODE_UUID[]  = "2a4e"; /* 0 = boot protocol         */
static const char HID_REPORT_DATA_UUID[]    = "2a4d"; /* generic Report (fallback) */

static NimBLEAdvertisedDevice *sAdvDevice = nullptr;
static volatile bool sDoConnect = false;
static volatile bool sConnected = false;

/* Latest 8-byte boot keyboard report: [modifiers, reserved, key1..key6].
 * Written by the NimBLE host task (notify callback), read by the
 * emulation task (ble_keyboard_poll). Guarded by a critical section since
 * both run on different FreeRTOS tasks/cores. */
static portMUX_TYPE sReportMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t sReport[8]     = {0};
static uint8_t sPrevKeys[6]   = {0};
static uint8_t sPrevModifiers = 0;

static void applyReportLocked(const uint8_t *data, size_t len) {
    if (len < 8) return; /* not a boot-protocol-shaped report, ignore */
    portENTER_CRITICAL(&sReportMux);
    memcpy(sReport, data, 8);
    portEXIT_CRITICAL(&sReportMux);
}

static void notifyCB(NimBLERemoteCharacteristic *chr, uint8_t *data, size_t len, bool isNotify) {
    (void)chr; (void)isNotify;
    applyReportLocked(data, len);
}

class KbdAdvertisedCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice *dev) override {
        if (dev->haveServiceUUID() && dev->isAdvertisingService(NimBLEUUID(HID_SERVICE_UUID))) {
            /* Prefer devices that identify as a keyboard (appearance 0x3C1),
             * but accept "HID generic" too since some keyboards don't set
             * the appearance field correctly. */
            uint16_t appearance = dev->getAppearance();
            if (appearance != 0x3C1 /* HID keyboard */ && appearance != 0x3C0 /* HID generic */ && appearance != 0) {
                return;
            }
            NimBLEDevice::getScan()->stop();
            sAdvDevice = dev;
            sDoConnect = true;
        }
    }
};

class KbdClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient *c) override {
        c->updateConnParams(12, 12, 0, 150);
    }
    void onDisconnect(NimBLEClient *c) override {
        (void)c;
        sConnected = false;
        portENTER_CRITICAL(&sReportMux);
        memset(sReport, 0, sizeof(sReport));
        portEXIT_CRITICAL(&sReportMux);
        NimBLEDevice::getScan()->start(0, nullptr);
    }
    uint32_t onPassKeyRequest() override { return 0; } /* most kbds use "just works" or display-only pairing */
    bool onConfirmPIN(uint32_t) override { return true; }
};

static KbdClientCallbacks sClientCB;

static bool connectToKeyboard() {
    NimBLEClient *client = NimBLEDevice::createClient();
    client->setClientCallbacks(&sClientCB, false);
    client->setConnectTimeout(7);

    if (!client->connect(sAdvDevice)) {
        NimBLEDevice::deleteClient(client);
        return false;
    }

    NimBLERemoteService *hid = client->getService(HID_SERVICE_UUID);
    if (!hid) {
        client->disconnect();
        return false;
    }

    /* Best-effort: ask the device to use boot protocol. Not all devices
     * expose this characteristic (BLE-only keyboards may not need it). */
    NimBLERemoteCharacteristic *proto = hid->getCharacteristic(HID_PROTOCOL_MODE_UUID);
    if (proto && proto->canWrite()) {
        uint8_t bootMode = 0x00;
        proto->writeValue(&bootMode, 1, true);
    }

    bool subscribed = false;
    NimBLERemoteCharacteristic *bootKbd = hid->getCharacteristic(HID_BOOT_KBD_INPUT_UUID);
    if (bootKbd && bootKbd->canNotify()) {
        subscribed = bootKbd->subscribe(true, notifyCB);
    }

    if (!subscribed) {
        /* Fallback: some keyboards only expose the generic Report
         * characteristic (0x2A4D), possibly several instances. Subscribe
         * to all of them; applyReportLocked() ignores anything that isn't
         * shaped like an 8-byte boot report. This won't decode every
         * possible custom Report Map, but covers most simple keyboards. */
        std::vector<NimBLERemoteCharacteristic *> *chars = hid->getCharacteristics(true);
        for (auto &c : *chars) {
            if (c->getUUID() == NimBLEUUID(HID_REPORT_DATA_UUID) && c->canNotify()) {
                if (c->subscribe(true, notifyCB)) subscribed = true;
            }
        }
    }

    if (!subscribed) {
        client->disconnect();
        return false;
    }

    sConnected = true;
    return true;
}

void ble_keyboard_init() {
    NimBLEDevice::init("FNK0103-MSX");
    NimBLEDevice::setSecurityAuth(true, false, true); /* bond, no MITM, secure connections */
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new KbdAdvertisedCallbacks());
    scan->setInterval(45);
    scan->setWindow(15);
    scan->setActiveScan(true);
    scan->start(0, nullptr); /* scan forever until we find/connect a HID device */
}

/* Call frequently from the main/emulation loop (not time critical - the
 * actual connect handshake happens here so it doesn't block the NimBLE
 * host task). */
static void ble_keyboard_service() {
    if (sDoConnect) {
        sDoConnect = false;
        if (!connectToKeyboard()) {
            NimBLEDevice::getScan()->start(0, nullptr);
        }
    }
}

int ble_keyboard_connected() { return sConnected ? 1 : 0; }

void ble_keyboard_poll() {
    ble_keyboard_service();

    uint8_t report[8];
    portENTER_CRITICAL(&sReportMux);
    memcpy(report, sReport, 8);
    portEXIT_CRITICAL(&sReportMux);

    uint8_t modifiers = report[0];
    uint8_t *keys = &report[2]; /* up to 6 simultaneous non-modifier keys */

    /* Modifiers: release-then-set for each tracked bit. */
    for (auto &m : kHidModifiers) {
        if (m.kbd == 0) continue;
        bool wasDown = sPrevModifiers & m.bit;
        bool isDown  = modifiers & m.bit;
        if (isDown && !wasDown) KBD_SET(m.kbd);
        else if (!isDown && wasDown) {
            /* Only release if no other modifier bit mapped to the same
             * KBD_* constant is still held (e.g. left+right shift). */
            bool stillHeld = false;
            for (auto &m2 : kHidModifiers) {
                if (m2.kbd == m.kbd && (modifiers & m2.bit)) { stillHeld = true; break; }
            }
            if (!stillHeld) KBD_RES(m.kbd);
        }
    }
    sPrevModifiers = modifiers;

    /* Regular keys: release ones no longer present, press new ones. */
    for (int i = 0; i < 6; i++) {
        uint8_t prev = sPrevKeys[i];
        if (prev == 0) continue;
        bool stillPresent = false;
        for (int j = 0; j < 6; j++) if (keys[j] == prev) { stillPresent = true; break; }
        if (!stillPresent && prev < sizeof(kHidKeycodeToMsx)) {
            uint8_t tok = kHidKeycodeToMsx[prev];
            if (tok) KBD_RES(tok);
        }
    }
    for (int i = 0; i < 6; i++) {
        uint8_t cur = keys[i];
        if (cur == 0 || cur == 1) continue; /* 0 = no key, 1 = rollover error */
        bool wasPresent = false;
        for (int j = 0; j < 6; j++) if (sPrevKeys[j] == cur) { wasPresent = true; break; }
        if (!wasPresent && cur < sizeof(kHidKeycodeToMsx)) {
            uint8_t tok = kHidKeycodeToMsx[cur];
            if (tok) KBD_SET(tok);
        }
    }
    memcpy(sPrevKeys, keys, 6);
}
