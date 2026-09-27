#pragma once
// Copy to network_secrets.h (ignored by Git), then fill in locally.
namespace network_config {
inline constexpr char ssid[] = "";
inline constexpr char password[] = "";
inline constexpr char endpoint[] =
    "mqtts://a1pv0kof3jbrbo-ats.iot.ap-northeast-1.amazonaws.com:8883";
inline constexpr char client_id[] = "delta-machine-alpha";
inline constexpr char topic[] = "v0/delta_machine_alpha/telemetry/racing_data";
inline constexpr char machine_id[] = "pi";
inline constexpr char memo[] = "Delta Vega v2";
inline constexpr char root_ca[] = "";      // PEM root CA
inline constexpr char client_cert[] = "";  // PEM device certificate
inline constexpr char client_key[] = "";   // PEM private key
inline constexpr char ntp_server[] = "pool.ntp.org";
}  // namespace network_config
