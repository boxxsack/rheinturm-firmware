#include "BLEConfigInterface.h"
#include "ConnectivityManager.h"
#include "TimeDisplay.h"
#include "ConnectivityState.h"

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <WiFi.h>
#include <Arduino.h>

// BLE UUIDs
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CONF_STATE_UUID     "4fafff01-1fb5-459e-8fcc-c5c9c331914b"
#define CONF_SSID_UUID      "4fafff02-1fb5-459e-8fcc-c5c9c331914b"
#define CONF_PASSWORD_UUID  "4fafff03-1fb5-459e-8fcc-c5c9c331914b"
#define SCAN_STATE_UUID     "4fafff04-1fb5-459e-8fcc-c5c9c331914b"
#define SCAN_LIST_UUID      "4fafff05-1fb5-459e-8fcc-c5c9c331914b"
#define BRIGHTNESS_UUID     "4fafff06-1fb5-459e-8fcc-c5c9c331914b"

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

// --- BLEConfigInterface Implementation ---

BLEConfigInterface::BLEConfigInterface(ConnectivityManager& connectivity, TimeDisplay& display)
    : _connectivity(connectivity)
    , _display(display)
{
    memset(_scanBuffer, 0, sizeof(_scanBuffer));
}

void BLEConfigInterface::begin(const char* deviceName) {
    BLEDevice::init(deviceName);

    _pServer = BLEDevice::createServer();
    _pServer->setCallbacks(new ServerCallbacks(*this));

    static BLEUUID serviceUUID(SERVICE_UUID);
    BLEService* pService = _pServer->createService(serviceUUID, 30, 0);

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

    _pSsid->setCallbacks(new SSIDCallbacks(*this));
    _pPassword->setCallbacks(new PasswordCallbacks(*this));
    _pScanState->setCallbacks(new ScanStateCallbacks(*this));
    _pBrightness->setCallbacks(new BrightnessCallbacks(*this));

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

void BLEConfigInterface::_setClientConnected(bool connected) {
    _clientConnected = connected;
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
        const char* stateStr = (current == static_cast<uint8_t>(ConnectivityState::CONNECTED_WITH_TIME))
            ? "connected"
            : "not_connected";
        _pConfState->setValue(stateStr);
        _pConfState->notify();
    }
}
