#include "BLEConfigInterface.h"
#include "ConnectivityManager.h"
#include "TimeDisplay.h"
#include "ConnectivityState.h"

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Arduino.h>

// BLE UUIDs
#define SERVICE_UUID          "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CONF_STATE_UUID       "4fafff01-1fb5-459e-8fcc-c5c9c331914b"
#define CONF_SSID_UUID        "4fafff02-1fb5-459e-8fcc-c5c9c331914b"
#define CONF_PASSWORD_UUID    "4fafff03-1fb5-459e-8fcc-c5c9c331914b"
#define SCAN_STATE_UUID       "4fafff04-1fb5-459e-8fcc-c5c9c331914b"
#define SCAN_LIST_UUID        "4fafff05-1fb5-459e-8fcc-c5c9c331914b"
#define BRIGHTNESS_UUID       "4fafff06-1fb5-459e-8fcc-c5c9c331914b"
#define FIRMWARE_VERSION_UUID "4fafff07-1fb5-459e-8fcc-c5c9c331914b"
#define OTA_CONTROL_UUID      "4fafff08-1fb5-459e-8fcc-c5c9c331914b"
#define RAINBOW_UUID          "4fafff09-1fb5-459e-8fcc-c5c9c331914b"
#define SCHEDULE_UUID         "4fafff0a-1fb5-459e-8fcc-c5c9c331914b"
#define SEPARATOR_CONFIG_UUID "4fafff0b-1fb5-459e-8fcc-c5c9c331914b"

// --- BLE Callback Classes (private to this translation unit) ---

class ServerCallbacks : public BLEServerCallbacks {
public:
    explicit ServerCallbacks(BLEConfigInterface& owner) : _owner(owner) {}

    void onConnect(BLEServer* pServer) override {
        _owner._setClientConnected(true);
    }

    void onDisconnect(BLEServer* pServer) override {
        _owner._setClientConnected(false);
    }

private:
    BLEConfigInterface& _owner;
};

class SSIDCallbacks : public BLECharacteristicCallbacks {
public:
    explicit SSIDCallbacks(BLEConfigInterface& owner) : _owner(owner) {}

    void onWrite(BLECharacteristic* pChar) override {
        std::string value = pChar->getValue();
        _owner._stageSSID(value.c_str(), value.length());
    }

private:
    BLEConfigInterface& _owner;
};

class PasswordCallbacks : public BLECharacteristicCallbacks {
public:
    explicit PasswordCallbacks(BLEConfigInterface& owner) : _owner(owner) {}

    void onWrite(BLECharacteristic* pChar) override {
        std::string value = pChar->getValue();
        _owner._stagePassword(value.c_str(), value.length());
    }

private:
    BLEConfigInterface& _owner;
};

class ScanStateCallbacks : public BLECharacteristicCallbacks {
public:
    explicit ScanStateCallbacks(BLEConfigInterface& owner) : _owner(owner) {}

    void onWrite(BLECharacteristic* pChar) override {
        std::string value = pChar->getValue();
        if (String(value.c_str()) == "scan-start") {
            _owner._stageScanRequest();
        }
    }

private:
    BLEConfigInterface& _owner;
};

class BrightnessCallbacks : public BLECharacteristicCallbacks {
public:
    explicit BrightnessCallbacks(BLEConfigInterface& owner) : _owner(owner) {}

    void onWrite(BLECharacteristic* pChar) override {
        std::string value = pChar->getValue();
        if (value.length() > 0) {
            uint8_t brightness = static_cast<uint8_t>(value[0]);
            _owner._stageBrightness(constrain(brightness, 0, 255));
        }
    }

private:
    BLEConfigInterface& _owner;
};

class OtaControlCallbacks : public BLECharacteristicCallbacks {
public:
    explicit OtaControlCallbacks(BLEConfigInterface& owner) : _owner(owner) {}

    void onWrite(BLECharacteristic* pChar) override {
        std::string value = pChar->getValue();
        if (value.length() > 0) {
            _owner._stageOtaUrl(value.c_str(), value.length());
        }
    }

private:
    BLEConfigInterface& _owner;
};

class RainbowCallbacks : public BLECharacteristicCallbacks {
public:
    explicit RainbowCallbacks(BLEConfigInterface& owner) : _owner(owner) {}

    void onWrite(BLECharacteristic* pChar) override {
        std::string value = pChar->getValue();
        if (value.length() > 0) {
            _owner._stageRainbow(static_cast<uint8_t>(value[0]) == 1);
        }
    }

private:
    BLEConfigInterface& _owner;
};

class ScheduleCallbacks : public BLECharacteristicCallbacks {
public:
    explicit ScheduleCallbacks(BLEConfigInterface& owner) : _owner(owner) {}

    void onWrite(BLECharacteristic* pChar) override {
        std::string value = pChar->getValue();
        if (value.length() >= 5) {
            _owner._stageSchedule(reinterpret_cast<const uint8_t*>(value.data()), value.length());
        }
    }

private:
    BLEConfigInterface& _owner;
};

class SeparatorConfigCallbacks : public BLECharacteristicCallbacks {
public:
    explicit SeparatorConfigCallbacks(BLEConfigInterface& owner) : _owner(owner) {}

    void onWrite(BLECharacteristic* pChar) override {
        std::string value = pChar->getValue();
        if (value.length() >= 2) {
            _owner._stageSeparatorConfig(reinterpret_cast<const uint8_t*>(value.data()), value.length());
        }
    }

private:
    BLEConfigInterface& _owner;
};

// --- BLEConfigInterface Implementation ---

BLEConfigInterface::BLEConfigInterface(ConnectivityManager& connectivity, TimeDisplay& display)
    : _connectivity(connectivity)
    , _display(display)
{
    memset(_scanBuffer, 0, sizeof(_scanBuffer));
}

void BLEConfigInterface::begin(const char* deviceName, const char* firmwareVersion) {
    BLEDevice::init(deviceName);

    _pServer = BLEDevice::createServer();
    _pServer->setCallbacks(new ServerCallbacks(*this));

    static BLEUUID serviceUUID(SERVICE_UUID);
    BLEService* pService = _pServer->createService(serviceUUID, 40, 0);

    _pConfState = pService->createCharacteristic(
        CONF_STATE_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

    _pSsid = pService->createCharacteristic(
        CONF_SSID_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);

    _pPassword = pService->createCharacteristic(
        CONF_PASSWORD_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);

    _pScanState = pService->createCharacteristic(
        SCAN_STATE_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);

    _pScanList = pService->createCharacteristic(
        SCAN_LIST_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

    _pBrightness = pService->createCharacteristic(
        BRIGHTNESS_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);

    _pFirmwareVersion = pService->createCharacteristic(
        FIRMWARE_VERSION_UUID,
        BLECharacteristic::PROPERTY_READ);

    _pOtaControl = pService->createCharacteristic(
        OTA_CONTROL_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);

    _pRainbow = pService->createCharacteristic(
        RAINBOW_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);

    _pSchedule = pService->createCharacteristic(
        SCHEDULE_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);

    _pSeparatorConfig = pService->createCharacteristic(
        SEPARATOR_CONFIG_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);

    _pSsid->setCallbacks(new SSIDCallbacks(*this));
    _pPassword->setCallbacks(new PasswordCallbacks(*this));
    _pScanState->setCallbacks(new ScanStateCallbacks(*this));
    _pBrightness->setCallbacks(new BrightnessCallbacks(*this));
    _pOtaControl->setCallbacks(new OtaControlCallbacks(*this));
    _pRainbow->setCallbacks(new RainbowCallbacks(*this));
    _pSchedule->setCallbacks(new ScheduleCallbacks(*this));
    _pSeparatorConfig->setCallbacks(new SeparatorConfigCallbacks(*this));

    // Set initial values
    uint8_t initialBrightness = 100;
    _pBrightness->setValue(&initialBrightness, 1);
    _pFirmwareVersion->setValue(firmwareVersion);
    uint8_t rainbowOff = 0;
    _pRainbow->setValue(&rainbowOff, 1);
    uint8_t scheduleBytes[5];
    _display.getScheduleBytes(scheduleBytes);
    _pSchedule->setValue(scheduleBytes, 5);
    uint8_t separatorBytes[2];
    _display.getSeparatorConfigBytes(separatorBytes);
    _pSeparatorConfig->setValue(separatorBytes, 2);

    pService->start();

    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x0);
    BLEDevice::startAdvertising();

    Serial.println("BLE initialized, advertising started");
}

void BLEConfigInterface::tick() {
    _handleAdvertisingRestart();
    _dispatchStagedValues();
    _advanceScanStateMachine();
    _syncConfState();
}

bool BLEConfigInterface::isClientConnected() const {
    return _clientConnected;
}

void BLEConfigInterface::_stageSSID(const char* ssid, size_t len) {
    _pendingSsid = String(ssid);
    Serial.println("BLE: SSID staged: " + _pendingSsid);
}

void BLEConfigInterface::_stagePassword(const char* password, size_t len) {
    _pendingPassword = String(password);
    _credentialsReady = true;
    Serial.println("BLE: Password staged, credentials ready");
}

void BLEConfigInterface::_stageScanRequest() {
    _scanRequested = true;
    Serial.println("BLE: Scan requested");
}

void BLEConfigInterface::_stageBrightness(uint8_t value) {
    _pendingBrightnessValue = value;
    _brightnessReady = true;
    Serial.println("BLE: Brightness staged: " + String(value));
}

void BLEConfigInterface::_stageOtaUrl(const char* url, size_t len) {
    _pendingOtaUrl = String(url);
    _otaRequested = true;
    Serial.println("BLE: OTA URL staged: " + _pendingOtaUrl);
}

void BLEConfigInterface::_stageSchedule(const uint8_t* payload, size_t len) {
    if (len < 5) return;
    memcpy(_pendingSchedule, payload, 5);
    _scheduleReady = true;
    Serial.printf("BLE: Schedule staged: enabled=%d on=%02d:%02d off=%02d:%02d\n",
        payload[0], payload[1], payload[2], payload[3], payload[4]);
}

void BLEConfigInterface::_stageSeparatorConfig(const uint8_t* payload, size_t len) {
    if (len < 2) return;
    memcpy(_pendingSeparatorConfig, payload, 2);
    _separatorConfigReady = true;
    Serial.printf("BLE: Separator staged: mode=%u interval=%us\n",
        payload[0], payload[1]);
}

void BLEConfigInterface::_stageRainbow(bool active) {
    if (active) {
        _rainbowRequested = true;
        Serial.println("BLE: Rainbow start staged");
    } else {
        _display.cancelRainbow();
        Serial.println("BLE: Rainbow cancelled");
    }
}

bool BLEConfigInterface::isRainbowRequested() {
    return _rainbowRequested;
}

void BLEConfigInterface::acknowledgeRainbow() {
    _rainbowRequested = false;
    uint8_t on = 1;
    _pRainbow->setValue(&on, 1);
}

void BLEConfigInterface::onRainbowComplete() {
    uint8_t off = 0;
    _pRainbow->setValue(&off, 1);
}

void BLEConfigInterface::_setClientConnected(bool connected) {
    _clientConnected = connected;
    if (connected) {
        // Force re-notification of current state so the app gets it immediately
        _lastConfState = 0xFF;
    }
}

void BLEConfigInterface::_dispatchStagedValues() {
    if (_credentialsReady) {
        _credentialsReady = false;
        Serial.println("BLE: Dispatching credentials to ConnectivityManager");
        _connectivity.applyCredentials(_pendingSsid, _pendingPassword);
    }

    if (_brightnessReady) {
        _brightnessReady = false;
        Serial.println("BLE: Dispatching brightness: " + String(_pendingBrightnessValue));
        _display.setBrightness(_pendingBrightnessValue);
    }

    if (_scheduleReady) {
        _scheduleReady = false;
        Serial.println("BLE: Dispatching schedule");
        _display.setSchedule(_pendingSchedule, 5);
        if (_pSchedule) _pSchedule->setValue(_pendingSchedule, 5);
    }

    if (_separatorConfigReady) {
        _separatorConfigReady = false;
        Serial.println("BLE: Dispatching separator config");
        _display.setSeparatorConfig(_pendingSeparatorConfig, 2);
        if (_pSeparatorConfig) _pSeparatorConfig->setValue(_pendingSeparatorConfig, 2);
    }

    if (_otaRequested) {
        _otaRequested = false;
        Serial.println("BLE: Dispatching OTA update");
        _performOta(_pendingOtaUrl);
    }
}

void BLEConfigInterface::_performOta(const String& url) {
    Serial.println("OTA: Starting update from: " + url);

    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("OTA: WiFi not connected, aborting");
        _pOtaControl->setValue("ota-fail:wifi_not_connected");
        _pOtaControl->notify();
        return;
    }

    // Notify start via BLE before shutting it down
    _pOtaControl->setValue("ota-start");
    _pOtaControl->notify();
    delay(500);

    // Free BLE memory (~60-80 KB) — required for TLS handshake with GitHub
    Serial.println("OTA: Releasing BLE to free heap for TLS");
    BLEDevice::deinit(true);
    _pServer = nullptr;
    _pConfState = nullptr;
    _pSsid = nullptr;
    _pPassword = nullptr;
    _pScanState = nullptr;
    _pScanList = nullptr;
    _pBrightness = nullptr;
    _pFirmwareVersion = nullptr;
    _pOtaControl = nullptr;
    _pRainbow = nullptr;
    _pSchedule = nullptr;
    _pSeparatorConfig = nullptr;

    Serial.printf("OTA: Free heap after BLE deinit: %u bytes\n", ESP.getFreeHeap());

    // Show initial progress on LEDs
    _display.showOtaProgress(0);

    // Resolve any redirect (e.g. github.com → objects.githubusercontent.com) before
    // the actual update. HTTPUpdate reuses the same WiFiClientSecure socket and cannot
    // switch hosts mid-redirect, so we pre-resolve with a short-lived HTTPClient.
    String resolvedUrl = url;
    {
        WiFiClientSecure resolveClient;
        resolveClient.setInsecure();
        HTTPClient resolveHttp;
        resolveHttp.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        if (resolveHttp.begin(resolveClient, url)) {
            int code = resolveHttp.GET();
            if (code == 301 || code == 302 || code == 307 || code == 308) {
                String location = resolveHttp.getLocation();
                if (location.length() > 0) {
                    resolvedUrl = location;
                    Serial.println("OTA: Resolved URL: " + resolvedUrl);
                }
            }
            resolveHttp.end();
        }
    }

    WiFiClientSecure client;
    client.setInsecure();

    // Redirects already resolved above — disable further redirect following.
    httpUpdate.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    httpUpdate.onProgress([this](int current, int total) {
        if (total > 0) {
            int percent = (current * 100) / total;
            Serial.printf("OTA: Progress %d%%\n", percent);
            _display.showOtaProgress(percent);
        }
    });

    t_httpUpdate_return result = httpUpdate.update(client, resolvedUrl);

    switch (result) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("OTA: Update failed (%d): %s\n",
                httpUpdate.getLastError(),
                httpUpdate.getLastErrorString().c_str());
            Serial.println("OTA: Restarting to restore BLE...");
            delay(1000);
            ESP.restart();
            break;

        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("OTA: No updates available, restarting...");
            delay(1000);
            ESP.restart();
            break;

        case HTTP_UPDATE_OK:
            Serial.println("OTA: Update successful, restarting...");
            delay(1000);
            ESP.restart();
            break;
    }
}

void BLEConfigInterface::_advanceScanStateMachine() {
    switch (_scanPhase) {
    case ScanPhase::IDLE:
        if (_scanRequested) {
            _scanRequested = false;
            _pScanState->setValue("scanning");
            _pScanState->notify();
            _scanPhase = ScanPhase::REQUESTED;
            Serial.println("BLE Scan: IDLE -> REQUESTED");
        }
        break;

    case ScanPhase::REQUESTED:
        _connectivity.startAsyncScan();
        _scanPhase = ScanPhase::SCANNING;
        Serial.println("BLE Scan: REQUESTED -> SCANNING");
        break;

    case ScanPhase::SCANNING: {
        int result = _connectivity.checkScanResults();
        if (result >= 0) {
            _scanNetworkCount = result;
            _scanPhase = ScanPhase::ENCODING;
            Serial.println("BLE Scan: SCANNING -> ENCODING (" + String(result) + " networks)");
        }
        // -1 = still in progress, -2 = no scan (shouldn't happen here)
        break;
    }

    case ScanPhase::ENCODING:
        _encodeScanResults(_scanNetworkCount);
        _scanDeliveryOffset = 0;
        _lastChunkMs = 0;
        _scanPhase = ScanPhase::DELIVERING;
        Serial.println("BLE Scan: ENCODING -> DELIVERING (" + String(_scanBufferLen) + " bytes)");
        break;

    case ScanPhase::DELIVERING:
        if (_scanBufferLen == 0 || _scanDeliveryOffset >= _scanBufferLen) {
            _scanPhase = ScanPhase::COMPLETE;
        } else {
            _deliverNextChunk();
        }
        break;

    case ScanPhase::COMPLETE:
        _pScanState->setValue("scan-end");
        _pScanState->notify();
        _scanPhase = ScanPhase::IDLE;
        Serial.println("BLE Scan: COMPLETE -> IDLE");
        break;
    }
}

void BLEConfigInterface::_encodeScanResults(int networkCount) {
    _scanBufferLen = 0;

    int count = min(networkCount, 20);  // cap at 20 networks
    for (int i = 0; i < count; i++) {
        String ssid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);

        if (ssid.length() > MAX_SSID_LENGTH) {
            ssid = ssid.substring(0, MAX_SSID_LENGTH);
        }

        // SSID length byte
        _scanBuffer[_scanBufferLen++] = ssid.length();

        // SSID characters (padded to MAX_SSID_LENGTH)
        for (int j = 0; j < ssid.length(); j++) {
            _scanBuffer[_scanBufferLen++] = ssid[j];
        }
        for (int j = ssid.length(); j < MAX_SSID_LENGTH; j++) {
            _scanBuffer[_scanBufferLen++] = 0x00;
        }

        // RSSI byte
        _scanBuffer[_scanBufferLen++] = static_cast<uint8_t>(rssi);
    }
}

void BLEConfigInterface::_deliverNextChunk() {
    uint32_t now = millis();
    if (now - _lastChunkMs < CHUNK_INTERVAL_MS) return;

    uint16_t remaining = _scanBufferLen - _scanDeliveryOffset;
    uint16_t bytesToSend = min((uint16_t)CHUNK_SIZE, remaining);

    _pScanList->setValue(_scanBuffer + _scanDeliveryOffset, bytesToSend);
    _pScanList->notify();

    _scanDeliveryOffset += bytesToSend;
    _lastChunkMs = now;
}

void BLEConfigInterface::_handleAdvertisingRestart() {
    if (!_clientConnected && _wasConnected) {
        // Client just disconnected — restart advertising
        delay(500);  // brief settle
        _pServer->startAdvertising();
        Serial.println("BLE: Client disconnected, restarting advertising");
    }
    _wasConnected = _clientConnected;
}

void BLEConfigInterface::_syncConfState() {
    uint8_t current = static_cast<uint8_t>(_connectivity.getState());
    if (current != _lastConfState) {
        _lastConfState = current;
        const char* stateStr = (current >= static_cast<uint8_t>(ConnectivityState::CONNECTED_NO_TIME))
            ? "connected"
            : "not_connected";
        _pConfState->setValue(stateStr);
        _pConfState->notify();
    }
}
