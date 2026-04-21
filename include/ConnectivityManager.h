#pragma once

#include "ConnectivityState.h"
#include <Arduino.h>
#include <time.h>

class ConnectivityManager {
public:
    // Call once in setup(). Loads credentials from NVS, starts connecting if available.
    void begin(const char* ntpServer = "de.pool.ntp.org",
               const char* tzInfo = "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00");

    // Call every loop() iteration. Fully non-blocking.
    // Drives WiFi reconnection, NTP sync polling, hourly re-sync.
    // Returns true and fills outTime if valid time is available.
    bool tick(tm& outTime);

    // Called from BLE. Stores credentials, persists to NVS, starts WiFi.begin().
    // Non-blocking — returns immediately.
    void applyCredentials(const String& ssid, const String& password);

    // Current connection + time sync state.
    ConnectivityState getState() const;

    // True only when WiFi connected AND valid NTP time obtained.
    bool hasValidTime() const;

    // WiFi scan support (used by BLEConfigInterface)
    // Disconnects WiFi if needed, starts async scan. Suppresses reconnection during scan.
    void startAsyncScan();

    // Returns network count when scan completes, -1 if still in progress, -2 if no scan active.
    // After results are consumed, reconnection resumes on next tick().
    int checkScanResults();

    // Erases WiFi credentials from NVS and disconnects WiFi immediately.
    // Called from BLE when the user requests a WiFi reset.
    void clearCredentials();

private:
    // Configuration
    const char* _ntpServer = nullptr;
    const char* _tzInfo = nullptr;

    // Credentials
    String _ssid;
    String _password;
    bool _hasCredentials = false;

    // State machine
    ConnectivityState _state = ConnectivityState::DISCONNECTED;

    // WiFi reconnection
    uint32_t _connectStartMs = 0;
    uint32_t _lastConnectAttemptMs = 0;
    static constexpr uint32_t CONNECT_TIMEOUT_MS = 30000;
    static constexpr uint32_t RECONNECT_COOLDOWN_MS = 30000;
    bool _connectRequested = false;

    // NTP
    bool _ntpConfigured = false;
    bool _hasValidTime = false;
    uint32_t _lastNtpSyncMs = 0;
    static constexpr uint32_t NTP_SYNC_INTERVAL_MS = 3600000; // 1 hour
    static constexpr int MIN_VALID_YEAR = 116; // 2016 - 1900

    // WiFi scan
    bool _scanActive = false;
    bool _scanRequested = false;
    bool _scanComplete = false;
    int _scanResultCount = -1;

    // Internal helpers
    void _startConnect();
    bool _isWiFiConnected();
    void _configureNtp();
    bool _checkTimeValid();
    void _loadCredentials();
    void _persistCredentials();
};
