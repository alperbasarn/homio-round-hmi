#include "CommandParsers.h"

#include <cerrno>
#include <cstdlib>

namespace cmdparse {

namespace {

std::string stripScheme(const std::string& value) {
    if (value.rfind("mqtt://", 0) == 0) {
        return value.substr(7);
    }
    if (value.rfind("mqtts://", 0) == 0) {
        return value.substr(8);
    }
    return value;
}

bool parsePort(const std::string& value, int& portOut) {
    return parseInt(value, portOut, 1, 65535);
}

bool isUrlKeyword(const std::string& value) {
    return value == "URL" || value == "url";
}

// "mqtts://host" splits on ':' into "mqtts" and "//host"; glue the scheme back onto the host
// wherever the pair occurs (bare host forms and the URL:<host> forms alike).
void reglueSchemes(std::vector<std::string>& parts) {
    for (size_t i = 0; i + 1 < parts.size();) {
        if ((parts[i] == "mqtt" || parts[i] == "mqtts") && parts[i + 1].rfind("//", 0) == 0) {
            parts[i + 1] = parts[i] + ":" + parts[i + 1];
            parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

}  // namespace

bool parseInt(const std::string& text, int& out, int min, int max) {
    if (text.empty() || text.size() > 11) {
        return false;
    }
    // strtol skips leading whitespace; a whole-string parse must not.
    const char first = text[0];
    if (!(first == '-' || first == '+' || (first >= '0' && first <= '9'))) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE) {
        return false;
    }
    if (parsed < min || parsed > max) {
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
}

std::vector<std::string> splitColon(const std::string& params, bool keepTrailingEmpty) {
    std::vector<std::string> parts;
    size_t start = 0;
    size_t end = params.find(':');
    while (end != std::string::npos) {
        parts.push_back(params.substr(start, end - start));
        start = end + 1;
        end = params.find(':', start);
    }
    if (keepTrailingEmpty || start < params.size()) {
        parts.push_back(params.substr(start));
    }
    return parts;
}

bool parseConnectWifi(const std::string& params, int slotCount, WifiCredentials& out, std::string& error) {
    const std::vector<std::string> parts = splitColon(params, false);
    if (parts.size() < 2) {
        error = "invalid_format";
        return false;
    }
    int slot = 0;
    if (parts.size() >= 3 && !parseInt(parts[2], slot, 0, slotCount - 1)) {
        error = "invalid_slot";
        return false;
    }
    out.ssid = parts[0];
    out.password = parts[1];
    out.slot = slot;
    error.clear();
    return true;
}

bool parseConfigureMqttServer(const std::string& params, const std::string& currentPassword,
                              MqttServerConfig& out, std::string& error) {
    std::vector<std::string> parts = splitColon(params, true);
    reglueSchemes(parts);

    MqttServerConfig config;
    if (parts.size() == 5 && isUrlKeyword(parts[0])) {
        config.host = stripScheme(parts[1]);
        if (!parsePort(parts[2], config.port)) {
            error = "invalid_port";
            return false;
        }
        config.username = parts[3];
        config.password = parts[4];
    } else if (parts.size() == 4 && isUrlKeyword(parts[0])) {
        config.host = stripScheme(parts[1]);
        config.username = parts[2];
        config.password = parts[3];
        config.port = 8883;
    } else if (parts.size() == 4) {
        config.host = stripScheme(parts[0]);
        if (!parsePort(parts[1], config.port)) {
            error = "invalid_port";
            return false;
        }
        config.username = parts[2];
        config.password = parts[3];
    } else if (parts.size() == 2) {
        config.host = stripScheme(parts[0]);
        if (!parsePort(parts[1], config.port)) {
            error = "invalid_port";
            return false;
        }
    } else {
        error = "invalid_format";
        return false;
    }

    if (config.host.empty()) {
        error = "empty_host";
        return false;
    }
    if (config.password == "-") {
        config.password = currentPassword;
    }
    out = config;
    error.clear();
    return true;
}

bool parseStaticIp(const std::string& params, StaticIpConfig& out, std::string& error) {
    const std::vector<std::string> parts = splitColon(params, false);
    if (parts.size() < 5) {
        error = "invalid_format";
        return false;
    }
    out.ip = parts[0];
    out.gateway = parts[1];
    out.subnet = parts[2];
    out.dns1 = parts[3];
    out.dns2 = parts[4];
    error.clear();
    return true;
}

}  // namespace cmdparse
