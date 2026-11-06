# WiFi NetLab — Full Network on ESP32 (C++) — Wireless only

**Real hardware, ESP32 only — no Raspberry Pi, no simulator, no Ethernet cable.** One C++ firmware (`firmware/wifi-netlab/src/main.cpp`, ESP32 Arduino) flashed to every ESP32 node and configured at runtime via its web portal. Covers **all 27 topics in `network.txt`** with a working **wireless-only** lab network: 2 routers, 1 AP/DHCP/DNS node, 2 content servers, 1 load balancer, 1 client/monitor.

## 1. What you get (network.txt → implementation)

| # | Topic | Where in C++ |
|---|---|---|
| 1 | Ethernet | Wireless Ethernet (IEEE 802.11) — ESP32 native Wi-Fi LAN; `initEthernet()` hook kept off (`USE_ETHERNET_W5500=0`), no cable/shield needed |
| 2 | Wi-Fi | `initWiFi()` STA+AP dual mode, WPA2, channel 6, SSID `NetLab-HQ` — primary transport, wireless only |
| 3 | MAC Address | `macStr()` via `esp_read_mac(ESP_MAC_WIFI_STA)`; used as DHCP client id + hostname |
| 4 | IP Address | `IPAddress` helpers everywhere, `isValidIp()`, printed at boot |
| 5 | Real time | `bootMs=millis()`, NTP `configTime()`, `nowStr()`, `uptimeSec()`, per-ping RTT |
| 6 | Static IP | `WiFi.config(ip,gw,subnet,dns)` toggle in portal `/config` |
| 7 | DHCP | Client auto-DHCP + **server** on AP node (UDP 67/68: DISCOVER→OFFER, REQUEST→ACK, option 3/6/51) |
| 8 | Subnet | `SubnetInfo::calc()` CIDR↔mask, network/broadcast/first/last/hosts |
| 9 | Router | `findRoute()` longest-prefix + per-hop `forwardPacket()`-via-route table; NAT/proxy note in code |
| 10 | Default Gateway | `cfg.gateway`, `WiFi.config(gw)`, DHCP option 3 |
| 11 | Routes | `routes` vector, `findRoute()`, portal `/routes` |
| 12 | Static Routing | seeded `seedDefaultRoutes()` + `addRoute(dest,mask,nextHop,iface,metric,"static")` |
| 13 | OSPF | Hello UDP 5005 `224.0.0.5`, LSA flood, `dijkstraHint()` — see `configs/routing-and-routing-protocols.md` |
| 14 | BGP | TCP 179 OPEN/KEEPALIVE/UPDATE, AS_PATH, peer `192.168.10.1` ASN 65001↔65002 |
| 15 | Ping | `doPing()` 4 probes, RTT, avg; portal `/ping?ip=` |
| 16 | ICMP | `esp_ping` hook + TCP fallback; ping counts toward ICMP echo behaviour |
| 17 | TCP | `tcpServer` echo :5000 + `WiFiClient` |
| 18 | UDP | `udpEcho` :5001 + DNS/DHCP/OSPF over UDP |
| 19 | Ports | `PORT_*` constants table in code + `configs/ip-plan.md` |
| 20 | Firewall | `FwRule` first-match `fwAllowed()` checked on TCP/UDP/HTTP (`/fw`) |
| 21 | TLS | `WiFiClientSecure` + `mbedtls` (AES) |
| 22 | SSL | `WebServerSecure` HTTPS :443 (uploads cert PEM via prefs) |
| 23 | VPN | TLS tunnel Core↔Branch :8443, subnet `10.8.0.0/30`, PSK handshake `vpnDial()` |
| 24 | DNS | Custom UDP 53 parser, `dnsZone` table, forward 8.8.8.8 |
| 25 | HTTP | `WebServer` :80 portal + content server |
| 26 | HTTPS | `WebServerSecure` :443 |
| 27 | Load Balancer | Round-robin `lbPickBACKEND()` / `lbHandle()` :8080 → 192.168.30.10:80, .11:80 |

> Numbering in `network.txt` has two #7 (DHCP and Subnet) — both are implemented.

## 2. Topology (5-7 ESP32 nodes)

See `docs/topology.md` for the full diagram. Summary:

```
Branch ESP32 (192.168.20.1) ──VPN :8443──► Core Router ESP32 (192.168.10.1) ──► Server1 (30.10) + Server2 (30.11)
AP/DHCP/DNS ESP32 (192.168.10.100, AP NetLab-HQ, DHCP .100-.149, DNS :53)
LB ESP32 (VIP 192.168.30.1 :8080) → Server1 :80 / Server2 :80
Client/Monitor ESP32 (192.168.10.50) → ping/HTTP tests
```

All ESP32 — no Raspberry Pi in any role. **Wireless only — no Ethernet cable, no shield.** Wiring: only standard USB for flashing + power. See `configs/hardware-needed.md`.

## 3. Flash & run (C++)

Prerequisite: PlatformIO (`pip install platformio`) + ESP32 boards connected via USB.

```bash
cd firmware/wifi-netlab
pio run -t upload      # flash all ESP32 nodes (one at a time)
pio run -t monitor     # serial 115200 — check MAC, IP, DHCP, DNS, role
```

Open the node portal: `http://192.168.10.100/` → `/config` to set **Role** (CORE_ROUTER / BRANCH_ROUTER / AP_DHCP_DNS / SERVER / LOAD_BALANCER / CLIENT_MONITOR), SSID, static IP, gateway, subnet, DNS. Saves to `Preferences` and reboots — **no re-flash** to change role.

Checklists after flash:
- AP node: `http://192.168.10.100/` shows `MAC`, `IP`, routes, `uptime`.
- Core: `http://192.168.10.1/routes`, `/fw`, `/ping?ip=192.168.30.10`.
- Servers: `http://192.168.30.10/` and `:443`.
- LB: `http://192.168.30.1:8080/` round-robins between servers.
- Client: serial prints RTT to gateway + VIP every 15s.

## 4. Config files

- `configs/ip-plan.md` — subnets, host table, DHCP pool, DNS zone, port map, firewall.
- `configs/routing-and-routing-protocols.md` — static routes, OSPF (UDP 5005), BGP (TCP 179), ICMP/ping.
- `configs/vpn-and-tls.md` — VPN tunnel, TLS/SSL cert provisioning.
- `configs/dhcp-dns-firewall-lb.md` — DHCP, DNS, firewall, LB details.

## 5. Repo layout

```
firmware/wifi-netlab/src/main.cpp   # entire C++ firmware — every topic commented with line refs
platformio.ini                       # ESP32 build, wireless-only (USE_ETHERNET_W5500=0)
configs/hardware-needed.md          # revised BOM — wireless only, no cable
configs/ docs/                      # IP plan, routing, topology, flash/test
network.txt                          # source requirement (27 topics)
```

No simulator, no Raspberry Pi code, no host-only scripts: firmware is Arduino C++ for ESP32.
