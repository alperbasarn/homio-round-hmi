// test_command_parsers (APP-21): one case per configureMQTTServer arity, the password "-" reuse
// rule, connectWifi with and without a slot, static IP, and setpoint:abc rejection.
#include "CommandParsers.h"
#include "TestRunner.h"

using namespace cmdparse;

TEST_CASE(parse_int_rejects_everything_that_is_not_a_whole_bounded_integer) {
    int value = -1;
    CHECK(parseInt("42", value, 0, 100));
    CHECK_EQ(value, 42);
    CHECK(!parseInt("abc", value, 0, 100));
    CHECK(!parseInt("", value, 0, 100));
    CHECK(!parseInt("42x", value, 0, 100));
    CHECK(!parseInt("101", value, 0, 100));
    CHECK(!parseInt("-1", value, 0, 100));
    CHECK(parseInt("-1", value, -5, 5));
    CHECK(!parseInt("99999999999999999999", value, 0, 100));
    CHECK(!parseInt(" 5", value, 0, 100));
}

TEST_CASE(connect_wifi_with_and_without_a_slot) {
    WifiCredentials creds;
    std::string error;
    CHECK(parseConnectWifi("Home:secret", 3, creds, error));
    CHECK_EQ(creds.ssid, std::string("Home"));
    CHECK_EQ(creds.password, std::string("secret"));
    CHECK_EQ(creds.slot, 0);

    CHECK(parseConnectWifi("Home:secret:2", 3, creds, error));
    CHECK_EQ(creds.slot, 2);

    CHECK(!parseConnectWifi("Home:secret:3", 3, creds, error));
    CHECK_EQ(error, std::string("invalid_slot"));
    CHECK(!parseConnectWifi("Home:secret:x", 3, creds, error));
    CHECK_EQ(error, std::string("invalid_slot"));
    CHECK(!parseConnectWifi("HomeOnly", 3, creds, error));
    CHECK_EQ(error, std::string("invalid_format"));
    CHECK(!parseConnectWifi("Home:", 3, creds, error));  // a trailing empty password is rejected, as before
    CHECK_EQ(error, std::string("invalid_format"));
}

TEST_CASE(mqtt_arity_url_host_port_user_pass) {
    MqttServerConfig config;
    std::string error;
    CHECK(parseConfigureMqttServer("URL:broker.example:8883:alice:pw", "old", config, error));
    CHECK_EQ(config.host, std::string("broker.example"));
    CHECK_EQ(config.port, 8883);
    CHECK_EQ(config.username, std::string("alice"));
    CHECK_EQ(config.password, std::string("pw"));
    CHECK(!parseConfigureMqttServer("URL:broker.example:notaport:alice:pw", "old", config, error));
    CHECK_EQ(error, std::string("invalid_port"));
}

TEST_CASE(mqtt_arity_url_host_user_pass_defaults_to_8883_and_accepts_a_scheme) {
    MqttServerConfig config;
    std::string error;
    CHECK(parseConfigureMqttServer("URL:mqtts://broker.example:alice:pw", "old", config, error));
    CHECK_EQ(config.host, std::string("broker.example"));
    CHECK_EQ(config.port, 8883);
    CHECK_EQ(config.username, std::string("alice"));
}

TEST_CASE(mqtt_arity_host_port_user_pass_and_password_reuse) {
    MqttServerConfig config;
    std::string error;
    CHECK(parseConfigureMqttServer("broker.example:1883:alice:-", "kept", config, error));
    CHECK_EQ(config.port, 1883);
    CHECK_EQ(config.password, std::string("kept"));
    CHECK(parseConfigureMqttServer("mqtt://broker.example:1883:alice:pw", "kept", config, error));
    CHECK_EQ(config.host, std::string("broker.example"));
    CHECK_EQ(config.password, std::string("pw"));
}

TEST_CASE(mqtt_arity_host_port_only) {
    MqttServerConfig config;
    std::string error;
    CHECK(parseConfigureMqttServer("broker.example:8883", "old", config, error));
    CHECK_EQ(config.host, std::string("broker.example"));
    CHECK_EQ(config.port, 8883);
    CHECK(config.username.empty());
    CHECK(config.password.empty());
}

TEST_CASE(mqtt_rejections) {
    MqttServerConfig config;
    std::string error;
    CHECK(!parseConfigureMqttServer("broker.example", "old", config, error));
    CHECK_EQ(error, std::string("invalid_format"));
    CHECK(!parseConfigureMqttServer("a:b:c", "old", config, error));
    CHECK_EQ(error, std::string("invalid_format"));
    CHECK(!parseConfigureMqttServer(":8883", "old", config, error));
    CHECK_EQ(error, std::string("empty_host"));
    CHECK(!parseConfigureMqttServer("broker.example:70000", "old", config, error));
    CHECK_EQ(error, std::string("invalid_port"));
}

TEST_CASE(static_ip_needs_five_fields) {
    StaticIpConfig config;
    std::string error;
    CHECK(parseStaticIp("192.168.1.50:192.168.1.1:255.255.255.0:1.1.1.1:8.8.8.8", config, error));
    CHECK_EQ(config.ip, std::string("192.168.1.50"));
    CHECK_EQ(config.dns2, std::string("8.8.8.8"));
    CHECK(!parseStaticIp("192.168.1.50:192.168.1.1:255.255.255.0:1.1.1.1", config, error));
    CHECK_EQ(error, std::string("invalid_format"));
}
