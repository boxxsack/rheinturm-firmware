#pragma once

#include <cstdint>
#include <cstddef>
#include <Arduino.h>

class ConnectivityManager;
class TimeDisplay;
class BLEServer;
class BLECharacteristic;

class BLEConfigInterface {
public:
    BLEConfigInterface(ConnectivityManager& connectivity, TimeDisplay& display);

    // Call once in setup(). Creates BLE server, service, characteristics, starts advertising.
    void begin(const char* deviceName = "Rheinturm", const char* firmwareVersion = "0.0.0");

    // Call every loop() iteration. Non-blocking. Drives:
    //   - Advertising restart after disconnect
    //   - Staged callback dispatch to ConnectivityManager/TimeDisplay
    //   - Non-blocking scan state machine with chunked notifications
    //   - confState characteristic sync with connectivity state
    //   - OTA update dispatch
    void tick();

    // True when a BLE client is connected.
    bool isClientConnected() const;

    // Called by BLE callbacks (friend classes) to stage values
    void _stageSSID(const char* ssid, size_t len);
    void _stagePassword(const char* password, size_t len);
    void _stageScanRequest();
    void _stageBrightness(uint8_t value);
    void _setClientConnected(bool connected);
    void _stageOtaUrl(const char* url, size_t len);
    void _stageRainbow(bool active);
    void _stageSchedule(const uint8_t* payload, size_t len);
    void _stageSeparatorConfig(const uint8_t* payload, size_t len);
    void _stageWifiReset();

    // Rainbow control — call from the main loop to drive the blocking animation.
    // Returns true once when a BLE client has requested a rainbow start.
    bool isRainbowRequested();
    // Call before starting playRainbow(): clears the request flag and sets the
    // characteristic value to 1 (active).
    void acknowledgeRainbow();
    // Call after playRainbow() returns: resets the characteristic value to 0.
    void onRainbowComplete();

private:
    ConnectivityManager& _connectivity;
    TimeDisplay& _display;

    // BLE objects
    BLEServer* _pServer = nullptr;
    BLECharacteristic* _pConfState = nullptr;
    BLECharacteristic* _pSsid = nullptr;
    BLECharacteristic* _pPassword = nullptr;
    BLECharacteristic* _pScanState = nullptr;
    BLECharacteristic* _pScanList = nullptr;
    BLECharacteristic* _pBrightness = nullptr;
    BLECharacteristic* _pFirmwareVersion = nullptr;
    BLECharacteristic* _pOtaControl = nullptr;
    BLECharacteristic* _pRainbow = nullptr;
    BLECharacteristic* _pSchedule = nullptr;
    BLECharacteristic* _pSeparatorConfig = nullptr;
    BLECharacteristic* _pWifiReset = nullptr;

    // Connection state
    bool _clientConnected = false;
    bool _wasConnected = false;

    // Staged values from callbacks (consumed in tick)
    String _pendingSsid;
    String _pendingPassword;
    bool _credentialsReady = false;
    bool _brightnessReady = false;
    uint8_t _pendingBrightnessValue = 0;
    bool _scanRequested = false;

    // OTA state
    String _pendingOtaUrl;
    bool _otaRequested = false;

    // Rainbow state
    bool _rainbowRequested = false;

    // Schedule state
    bool _scheduleReady = false;
    uint8_t _pendingSchedule[5] = {0, 8, 0, 23, 0};

    // Separator config state
    bool _separatorConfigReady = false;
    uint8_t _pendingSeparatorConfig[2] = {2, 1};

    // WiFi reset state
    bool _wifiResetRequested = false;

    // Scan state machine
    enum class ScanPhase : uint8_t {
        IDLE,
        REQUESTED,
        SCANNING,
        ENCODING,
        DELIVERING,
        COMPLETE
    };
    ScanPhase _scanPhase = ScanPhase::IDLE;
    uint8_t _scanBuffer[22 * 20];  // max 20 networks * 22 bytes each
    uint16_t _scanBufferLen = 0;
    uint16_t _scanDeliveryOffset = 0;
    int _scanNetworkCount = 0;
    uint32_t _lastChunkMs = 0;
    static constexpr uint16_t CHUNK_SIZE = 20;
    static constexpr uint32_t CHUNK_INTERVAL_MS = 100;
    static constexpr uint8_t MAX_SSID_LENGTH = 20;

    // confState tracking
    uint8_t _lastConfState = 0xFF;

    // Internal methods
    void _dispatchStagedValues();
    void _advanceScanStateMachine();
    void _encodeScanResults(int networkCount);
    void _deliverNextChunk();
    void _handleAdvertisingRestart();
    void _syncConfState();
    void _performOta(const String& url);
    void _performWifiReset();
};
