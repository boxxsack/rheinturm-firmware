#pragma once

#include <string>

class ServiceChangeLogic {
public:
    void begin(const std::string& storedMarker, const std::string& currentMarker);

    bool shouldTrigger() const;
    void markTriggered();
    void markPeerAuthenticated();
    bool shouldPersist() const;
    void markPersisted();

    bool pending() const { return _pending; }

private:
    bool _pending = false;
    bool _triggered = false;
    bool _peerAuthenticated = false;
};
