#pragma once

// The adapter layer (D15): MqttModelBridge, ConnectivityPoller, EnvironmentPoller and
// BatteryPoller land here in WP1.4 and WP5. This header exists so the component registers with
// its final dependency edges now (BLD-24) and later files build without a link error.
namespace adapters {

const char* layerVersion();

}  // namespace adapters
