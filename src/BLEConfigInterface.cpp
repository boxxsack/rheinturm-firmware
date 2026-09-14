#include "BLEConfigInterface.h"
#include "ConnectivityManager.h"
#include "TimeDisplay.h"
#include "ConnectivityState.h"
#include "GitHubRootCerts.h"
#include "OtaSigningKey.h"
#include "OtaImageVerifier.h"
#include "OtaUpdater.h"
#include "BleAuthFailurePolicy.h"

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Arduino.h>
#include <mbedtls/md.h>
#include <algorithm>

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
#define CONF_WIFI_RESET_UUID  "4fafff0c-1fb5-459e-8fcc-c5c9c331914b"

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

class WifiResetCallbacks : public BLECharacteristicCallbacks {
public:
    explicit WifiResetCallbacks(BLEConfigInterface& owner) : _owner(owner) {}

    void onWrite(BLECharacteristic* pChar) override {
        std::string value = pChar->getValue();
        if (String(value.c_str()) == "reset-wifi") {
            _owner._stageWifiReset();
        }
    }

private:
    BLEConfigInterface& _owner;
};

// Observes pairing/encryption outcomes so an ESP32-side stale bond heals itself
// (see BleAuthFailurePolicy.h). Runs on the Bluetooth task and acts there
// directly instead of staging for tick(): the bluedroid calls only post messages
// to the stack, and acting before the peer retries is what makes the next
// connection pair cleanly.
//
// The remaining overrides keep the library's behaviour without callbacks:
// security requests are accepted, and with ESP_IO_CAP_NONE ("Just Works") no
// passkey or numeric-comparison event is ever raised.
class SecurityCallbacks : public BLESecurityCallbacks {
public:
    uint32_t onPassKeyRequest() override { return 0; }
    void onPassKeyNotify(uint32_t) override {}
    bool onSecurityRequest() override { return true; }
    bool onConfirmPIN(uint32_t) override { return true; }

    void onAuthenticationComplete(esp_ble_auth_cmpl_t auth) override {
        char addr[18];
        snprintf(addr, sizeof(addr), "%02x:%02x:%02x:%02x:%02x:%02x",
            auth.bd_addr[0], auth.bd_addr[1], auth.bd_addr[2],
            auth.bd_addr[3], auth.bd_addr[4], auth.bd_addr[5]);

        if (auth.success) {
            Serial.printf("BLE: Authentication succeeded, peer=%s addr_type=%u auth_mode=0x%02x\n",
                addr, auth.addr_type, auth.auth_mode);
            return;
        }

        Serial.printf("BLE: Authentication FAILED, peer=%s addr_type=%u reason=0x%02x (%s)\n",
            addr, auth.addr_type, auth.fail_reason,
            BleAuthFailurePolicy::describeFailReason(auth.fail_reason));

        if (BleAuthFailurePolicy::decide(auth.success, auth.fail_reason)
                != BleAuthFailurePolicy::Action::RemoveBondAndDisconnect) {
            Serial.println("BLE: Keeping bond (failure does not indicate a stale bond)");
            return;
        }

        esp_err_t removeErr = esp_ble_remove_bond_device(auth.bd_addr);
        esp_err_t disconnectErr = esp_ble_gap_disconnect(auth.bd_addr);
        Serial.printf("BLE: Removed stale bond for peer=%s (%s), disconnecting (%s)\n",
            addr, esp_err_to_name(removeErr), esp_err_to_name(disconnectErr));
    }
};

// --- OTA production adapters ---

class ArduinoOtaHttpTransport : public IOtaHttpTransport {
public:
    bool resolveRedirect(const std::string& url, std::string& resolvedUrl) override {
        WiFiClientSecure client;
        client.setCACert(GITHUB_OTA_ROOT_CA_BUNDLE);

        HTTPClient http;
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        if (!http.begin(client, String(url.c_str()))) {
            Serial.println("OTA: Could not open URL for redirect resolution: " + String(url.c_str()));
            return false;
        }

        int code = http.GET();
        bool success = false;
        if (code == HTTP_CODE_OK) {
            resolvedUrl = url;
            success = true;
        } else if (code == 301 || code == 302 || code == 307 || code == 308) {
            String location = http.getLocation();
            if (location.length() > 0) {
                resolvedUrl = std::string(location.c_str());
                Serial.println("OTA: Resolved URL: " + location);
                success = true;
            }
        }
        if (!success) {
            Serial.printf("OTA: URL resolution failed, HTTP %d\n", code);
        }
        http.end();
        return success;
    }

    bool fetchSignature(const std::string& url, uint8_t* signatureOut, size_t signatureLen) override {
        WiFiClientSecure client;
        client.setCACert(GITHUB_OTA_ROOT_CA_BUNDLE);

        HTTPClient http;
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        if (!http.begin(client, String(url.c_str()))) {
            Serial.println("OTA: Could not open signature URL: " + String(url.c_str()));
            return false;
        }

        int code = http.GET();
        if (code != HTTP_CODE_OK) {
            Serial.printf("OTA: Signature fetch failed, HTTP %d\n", code);
            http.end();
            return false;
        }

        int len = http.getSize();
        if (len != static_cast<int>(signatureLen)) {
            Serial.printf("OTA: Signature size mismatch: expected %u, got %d\n",
                static_cast<unsigned>(signatureLen), len);
            http.end();
            return false;
        }

        WiFiClient* stream = http.getStreamPtr();
        size_t received = 0;
        size_t stallCount = 0;
        while (received < signatureLen && stallCount < OtaUpdater::kDefaultMaxStalls) {
            size_t available = stream->available();
            if (available == 0) {
                ++stallCount;
                delay(100);
                continue;
            }
            stallCount = 0;
            size_t toRead = std::min(available, signatureLen - received);
            int read = stream->readBytes(signatureOut + received, toRead);
            if (read <= 0) break;
            received += static_cast<size_t>(read);
        }
        http.end();

        if (received != signatureLen) {
            Serial.println("OTA: Signature download incomplete");
            return false;
        }
        return true;
    }

    bool beginImage(const std::string& url, int64_t& contentLength) override {
        contentLength = 0;
        _imageClient.setCACert(GITHUB_OTA_ROOT_CA_BUNDLE);
        _imageHttp.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        if (!_imageHttp.begin(_imageClient, String(url.c_str()))) {
            Serial.println("OTA: Could not open firmware URL: " + String(url.c_str()));
            return false;
        }

        _imageOpen = true;
        int code = _imageHttp.GET();
        if (code != HTTP_CODE_OK) {
            Serial.printf("OTA: Firmware fetch failed, HTTP %d\n", code);
            endImage();
            return false;
        }

        int len = _imageHttp.getSize();
        if (len <= 0) {
            Serial.println("OTA: Firmware content length unknown, aborting");
            return true;
        }
        contentLength = static_cast<int64_t>(len);
        return true;
    }

    int readImage(uint8_t* buffer, size_t maxLen) override {
        if (!_imageOpen) return -1;
        WiFiClient* stream = _imageHttp.getStreamPtr();
        size_t available = stream->available();
        if (available == 0) {
            delay(100);
            return 0;
        }
        return stream->readBytes(buffer, std::min(available, maxLen));
    }

    void endImage() override {
        if (_imageOpen) {
            _imageHttp.end();
            _imageOpen = false;
        }
    }

private:
    WiFiClientSecure _imageClient;
    HTTPClient _imageHttp;
    bool _imageOpen = false;
};

class ArduinoOtaUpdateSink : public IOtaUpdateSink {
public:
    bool begin(size_t contentLength) override {
        return Update.begin(contentLength);
    }

    size_t write(uint8_t* data, size_t length) override {
        return Update.write(data, length);
    }

    bool end() override {
        return Update.end();
    }

    void abort() override {
        Update.abort();
    }

    const char* errorString() const override {
        return Update.errorString();
    }
};

class MbedTlsOtaSha256 : public IOtaSha256 {
public:
    ~MbedTlsOtaSha256() override {
        _free();
    }

    bool begin() override {
        _free();
        mbedtls_md_init(&_context);
        _initialized = true;
        if (mbedtls_md_setup(&_context, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0) != 0 ||
            mbedtls_md_starts(&_context) != 0) {
            _free();
            return false;
        }
        _active = true;
        return true;
    }

    bool update(const uint8_t* data, size_t length) override {
        return _active && mbedtls_md_update(&_context, data, length) == 0;
    }

    bool finish(uint8_t* digestOut, size_t digestLen) override {
        if (!_active || digestLen != OtaImageVerifier::kSha256DigestSize) {
            return false;
        }
        int result = mbedtls_md_finish(&_context, digestOut);
        _free();
        return result == 0;
    }

private:
    mbedtls_md_context_t _context;
    bool _initialized = false;
    bool _active = false;

    void _free() {
        if (_initialized) {
            mbedtls_md_free(&_context);
            _initialized = false;
            _active = false;
        }
    }
};

class ArduinoOtaSignatureVerifier : public IOtaSignatureVerifier {
public:
    bool verify(const uint8_t* digest, const uint8_t* signature, size_t signatureLen) override {
        return OtaImageVerifier::verify(digest, signature, signatureLen, OTA_SIGNING_PUBLIC_KEY);
    }
};

class DisplayOtaProgressReporter : public IOtaProgressReporter {
public:
    explicit DisplayOtaProgressReporter(TimeDisplay& display) : _display(display) {}

    void showProgress(uint8_t percent) override {
        _display.showOtaProgress(percent);
    }

private:
    TimeDisplay& _display;
};

class EspRestart : public IOtaRestart {
public:
    void restart() override {
        delay(1000);
        ESP.restart();
    }
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

    // Require an encrypted, bonded BLE link before the security-sensitive
    // characteristics (WiFi credentials, OTA trigger, WiFi reset) can be used.
    // "Just Works" pairing (no I/O capability) gives link encryption + bonding,
    // defeating passive eavesdropping of the WiFi password and casual
    // unauthorized writes. It does not defend against an active MITM during the
    // pairing handshake — see SECURITY_REVIEW.md for the passkey-display upgrade.
    //
    // Pairing is triggered lazily by the ESP_GATT_PERM_*_ENCRYPTED permissions
    // below, on first access to an encrypted characteristic. We deliberately do
    // NOT call BLEDevice::setEncryptionLevel(): that makes the library force
    // encryption on every connect (esp_ble_set_encryption in the connect event),
    // which produces a second, redundant pairing prompt on top of the
    // permission-triggered one. One trigger = one dialog.
    BLESecurity* pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
    pSecurity->setCapability(ESP_IO_CAP_NONE);
    pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    // Only observes auth outcomes; does not change when encryption is requested.
    BLEDevice::setSecurityCallbacks(new SecurityCallbacks());

    _pServer = BLEDevice::createServer();
    _pServer->setCallbacks(new ServerCallbacks(*this));

    static BLEUUID serviceUUID(SERVICE_UUID);
    BLEService* pService = _pServer->createService(serviceUUID, 50, 0);

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

    _pWifiReset = pService->createCharacteristic(
        CONF_WIFI_RESET_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);

    // Bluedroid does not auto-create a CCCD (0x2902) from the NOTIFY property
    // bit; without one, iOS rejects subscription attempts with
    // CBATTErrorInvalidHandle and clients silently fall back to polling.
    _pConfState->addDescriptor(new BLE2902());
    _pScanList->addDescriptor(new BLE2902());
    _pBrightness->addDescriptor(new BLE2902());
    _pOtaControl->addDescriptor(new BLE2902());
    _pWifiReset->addDescriptor(new BLE2902());

    _pSsid->setCallbacks(new SSIDCallbacks(*this));
    _pPassword->setCallbacks(new PasswordCallbacks(*this));
    _pScanState->setCallbacks(new ScanStateCallbacks(*this));
    _pBrightness->setCallbacks(new BrightnessCallbacks(*this));
    _pOtaControl->setCallbacks(new OtaControlCallbacks(*this));
    _pRainbow->setCallbacks(new RainbowCallbacks(*this));
    _pSchedule->setCallbacks(new ScheduleCallbacks(*this));
    _pSeparatorConfig->setCallbacks(new SeparatorConfigCallbacks(*this));
    _pWifiReset->setCallbacks(new WifiResetCallbacks(*this));

    // Gate the security-sensitive characteristics behind link encryption. A peer
    // must pair/bond before it can submit WiFi credentials, trigger an OTA, or
    // reset WiFi. Status/control characteristics stay open for read access.
    const esp_gatt_perm_t encPerm = static_cast<esp_gatt_perm_t>(
        ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
    _pSsid->setAccessPermissions(encPerm);
    _pPassword->setAccessPermissions(encPerm);
    _pOtaControl->setAccessPermissions(encPerm);
    _pWifiReset->setAccessPermissions(encPerm);

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

void BLEConfigInterface::_stageWifiReset() {
    _wifiResetRequested = true;
    Serial.println("BLE: WiFi reset staged");
}

void BLEConfigInterface::_performWifiReset() {
    Serial.println("BLE: Resetting WiFi credentials");
    _connectivity.clearCredentials();
    if (_pWifiReset) {
        _pWifiReset->setValue("reset-ok");
        _pWifiReset->notify();
    }
    Serial.println("BLE: WiFi reset complete, notified reset-ok");
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

    if (_wifiResetRequested) {
        _wifiResetRequested = false;
        _performWifiReset();
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
    _pWifiReset = nullptr;

    Serial.printf("OTA: Free heap after BLE deinit: %u bytes\n", ESP.getFreeHeap());

    Serial.println("OTA: Fetching signature...");
    Serial.println("OTA: Downloading and writing firmware (verifying signature before activation)...");

    ArduinoOtaHttpTransport http;
    ArduinoOtaUpdateSink update;
    MbedTlsOtaSha256 sha256;
    ArduinoOtaSignatureVerifier verifier;
    DisplayOtaProgressReporter progress(_display);
    EspRestart restart;
    OtaUpdater updater(http, update, sha256, verifier, progress, restart);

    OtaUpdater::Result result = updater.perform(std::string(url.c_str()));
    if (result != OtaUpdater::Result::Success) {
        Serial.printf("OTA: Transaction failed (%u)%s\n",
            static_cast<unsigned>(result), updater.updateError());
    } else {
        Serial.println("OTA: Signature verified, update successful, restarting...");
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
