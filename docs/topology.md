# Topology — All ESP32, Wireless Only (no Raspberry Pi)

```
                         ┌────────────────────────────────────────┐
                         │          HQ LAN 192.168.10.0/24        │
                         │                                        │
  Client/Monitor ESP32   │   AP/DHCP/DNS ESP32 (192.168.10.100)  │
  192.168.10.50 ──WiFi──► │   AP SSID NetLab-HQ (Ch6, WPA2)       │
                         │   DHCP pool .100-.149, DNS :53         │
                         │          │                             │
                         │   Core Router ESP32 192.168.10.1/24    │
                         │   NAT + Firewall + VPN :8443           │
                         │   BGP AS 65001, OSPF area 0            │
                         └───────┬──────────────────┬─────────────┘
                                 │ VPN 10.8.0.0/30   │ 192.168.30.0/24
                                 │ (TLS tunnel)      │
                         Branch ESP32          LB ESP32 VIP 192.168.30.1 :8080
                         192.168.20.1/24       (round-robin)
                         OSPF + BGP peer       ├─► Server1 ESP32 192.168.30.10 :80/:443
                                              └─► Server2 ESP32 192.168.30.11 :80/:443
```

- Ethernet: covered by **wireless Ethernet** (IEEE 802.11) on native ESP32 Wi-Fi — no cable, no shield. `USE_ETHERNET_W5500=0`.
- Wi-Fi: every node is STA + optionally AP (`WIFI_AP_STA`). AP node hosts `NetLab-HQ`; Core/Branch also softAP their LANs (`WiFi.softAPConfig(gateway,gateway,subnet)`).
- MAC: per-device STA MAC via `esp_read_mac()` — forms hostname `esp32-<last4>`.
- Router: `findRoute()` longest-prefix, seeded static + connected + VPN routes (see `configs/routing-and-routing-protocols.md`).
- OSPF/BGP control plane runs between Core and Branch on the VPN/WiFi path.
