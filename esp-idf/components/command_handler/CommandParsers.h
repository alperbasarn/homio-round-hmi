#pragma once
// Command-line parsing rules with no platform dependency (APP-21), so the host suite can pin
// every arity. CommandHandler owns the side effects; this owns the grammar.
#include <string>
#include <vector>

namespace cmdparse {

// Whole-string decimal integer within [min, max]. The one numeric parser (APP-01).
bool parseInt(const std::string& text, int& out, int min, int max);

// "a:b:c" -> {"a","b","c"}. keepTrailingEmpty controls whether "a:b:" yields a third, empty part:
// connectWifi and configureStaticIP drop it, configureMQTTServer keeps it (as they always did).
std::vector<std::string> splitColon(const std::string& params, bool keepTrailingEmpty);

struct WifiCredentials {
    std::string ssid;
    std::string password;
    int slot = 0;
};
// "SSID:Password[:Slot]". slotCount bounds the optional slot. error is one of
// invalid_format, invalid_slot.
bool parseConnectWifi(const std::string& params, int slotCount, WifiCredentials& out, std::string& error);

struct MqttServerConfig {
    std::string host;
    int port = 0;
    std::string username;
    std::string password;
};
// The five accepted shapes, with a leading mqtt:// or mqtts:// scheme on the host allowed:
//   URL:Host:Port:User:Pass | URL:Host:User:Pass (port 8883) | Host:Port:User:Pass | Host:Port
// A password of "-" reuses currentPassword. error is one of invalid_port, invalid_format,
// empty_host.
bool parseConfigureMqttServer(const std::string& params, const std::string& currentPassword,
                              MqttServerConfig& out, std::string& error);

struct StaticIpConfig {
    std::string ip;
    std::string gateway;
    std::string subnet;
    std::string dns1;
    std::string dns2;
};
// "IP:Gateway:Subnet:DNS1:DNS2". error: invalid_format.
bool parseStaticIp(const std::string& params, StaticIpConfig& out, std::string& error);

}  // namespace cmdparse
