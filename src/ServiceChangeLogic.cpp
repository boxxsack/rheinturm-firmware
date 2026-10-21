#include "ServiceChangeLogic.h"

void ServiceChangeLogic::begin(const std::string& storedMarker, const std::string& currentMarker) {
    _pending = storedMarker != currentMarker;
    _triggered = false;
    _peerAuthenticated = false;
}

bool ServiceChangeLogic::shouldTrigger() const {
    return _pending && !_triggered;
}

void ServiceChangeLogic::markTriggered() {
    if (_pending) _triggered = true;
}

void ServiceChangeLogic::markPeerAuthenticated() {
    _peerAuthenticated = true;
}

bool ServiceChangeLogic::shouldPersist() const {
    return _pending && _triggered && _peerAuthenticated;
}

void ServiceChangeLogic::markPersisted() {
    _pending = false;
    _triggered = false;
    _peerAuthenticated = false;
}
