#pragma once

#include <cstdint>

enum class ConnectivityState : uint8_t {
    DISCONNECTED,
    CONNECTING,
    CONNECTED_NO_TIME,
    CONNECTED_WITH_TIME
};
