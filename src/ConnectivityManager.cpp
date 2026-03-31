#include "ConnectivityManager.h"
#include <WiFi.h>
#include <Preferences.h>

void ConnectivityManager::begin(const char* ntpServer, const char* tzInfo) {
    _ntpServer = ntpServer;
    _tzInfo = tzInfo;

    _loadCredentials();

    if (_hasCredentials) {
        _startConnect();
    }
}

bool ConnectivityManager::tick(tm& outTime) {
    // Handle async scan lifecycle
    if (_scanActive && !_scanComplete) {
        int result = WiFi.scanComplete();
        if (result >= 0) {
            _scanComplete = true;
            _scanResultCount = result;
        } else if (result == WIFI_SCAN_FAILED) {
            _scanComplete = true;
            _scanResultCount = 0;
        }
        // While scanning, skip normal WiFi management
        if (_hasValidTime && _checkTimeValid()) {
            time_t now = time(nullptr);
            localtime_r(&now, &outTime);
            return true;
        }
        return false;
    }

    // If scan results were consumed, reconnect
    if (_scanActive && _scanComplete && _scanResultCount == -2) {
        _scanActive = false;
        _scanComplete = false;
        if (_hasCredentials) {
            _startConnect();
        }
    }

    // WiFi state machine
    switch (_state) {
    case ConnectivityState::DISCONNECTED:
        if (_connectRequested) {
            _connectRequested = false;
            WiFi.begin(_ssid.c_str(), _password.c_str());
            _connectStartMs = millis();
            _state = ConnectivityState::CONNECTING;
        } else if (_hasCredentials && !_scanActive) {
            // Auto-reconnect with cooldown
            uint32_t now = millis();
            if (now - _lastConnectAttemptMs >= RECONNECT_COOLDOWN_MS) {
                _startConnect();
            }
        }
        break;

    case ConnectivityState::CONNECTING:
        if (_isWiFiConnected()) {
            _configureNtp();
            _state = ConnectivityState::CONNECTED_NO_TIME;
        } else if (millis() - _connectStartMs >= CONNECT_TIMEOUT_MS) {
            WiFi.disconnect();
            _state = ConnectivityState::DISCONNECTED;
            _lastConnectAttemptMs = millis();
        }
        break;

    case ConnectivityState::CONNECTED_NO_TIME:
        if (!_isWiFiConnected()) {
            _state = ConnectivityState::DISCONNECTED;
            _hasValidTime = false;
            break;
        }
        if (_checkTimeValid()) {
            _hasValidTime = true;
            _lastNtpSyncMs = millis();
            _state = ConnectivityState::CONNECTED_WITH_TIME;
        }
        break;

    case ConnectivityState::CONNECTED_WITH_TIME:
        if (!_isWiFiConnected()) {
            _state = ConnectivityState::DISCONNECTED;
            _hasValidTime = false;
            break;
        }
        // Hourly NTP re-sync
        if (millis() - _lastNtpSyncMs >= NTP_SYNC_INTERVAL_MS) {
            _configureNtp();
            _lastNtpSyncMs = millis();
        }
        break;
    }

    // Return current time if valid
    if (_hasValidTime) {
        time_t now = time(nullptr);
        localtime_r(&now, &outTime);
        return true;
    }
    return false;
}

void ConnectivityManager::applyCredentials(const String& ssid, const String& password) {
    _ssid = ssid;
    _password = password;
    _hasCredentials = true;

    _persistCredentials();

    // Start connection immediately
    WiFi.disconnect();
    _startConnect();
}

ConnectivityState ConnectivityManager::getState() const {
    return _state;
}

bool ConnectivityManager::hasValidTime() const {
    return _hasValidTime;
}

void ConnectivityManager::startAsyncScan() {
    if (_scanActive) return;

    _scanActive = true;
    _scanComplete = false;
    _scanResultCount = -1;

    // Disconnect WiFi for scanning
    WiFi.disconnect();
    if (_state != ConnectivityState::DISCONNECTED) {
        _state = ConnectivityState::DISCONNECTED;
    }

    delay(100);  // Brief settle time for WiFi state
    WiFi.scanNetworks(true);  // async = true
}

int ConnectivityManager::checkScanResults() {
    if (!_scanActive) return -2;
    if (!_scanComplete) return -1;

    int count = _scanResultCount;
    // Mark as consumed — next tick() will trigger reconnection
    _scanResultCount = -2;
    return count;
}

void ConnectivityManager::_startConnect() {
    _connectRequested = true;
    _lastConnectAttemptMs = millis();
}

bool ConnectivityManager::_isWiFiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

void ConnectivityManager::_configureNtp() {
    if (_ntpServer && _tzInfo) {
        configTime(0, 0, _ntpServer);
        setenv("TZ", _tzInfo, 1);
        _ntpConfigured = true;
    }
}

bool ConnectivityManager::_checkTimeValid() {
    time_t now = time(nullptr);
    tm timeinfo;
    localtime_r(&now, &timeinfo);
    return timeinfo.tm_year > MIN_VALID_YEAR;
}

void ConnectivityManager::_loadCredentials() {
    Preferences prefs;
    prefs.begin("credentials", true);  // read-only

    _ssid = prefs.getString("ssid", "");
    _password = prefs.getString("password", "");

    prefs.end();

    _hasCredentials = _ssid.length() > 0;
}

void ConnectivityManager::_persistCredentials() {
    Preferences prefs;
    prefs.begin("credentials", false);

    prefs.putString("ssid", _ssid);
    prefs.putString("password", _password);

    prefs.end();
}
