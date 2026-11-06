# Conceptual Framework — All 27 Topics Mapped (ESP32, Wireless Only)

One picture of every item in `network.txt`, how it fits, and — i.e., phone → `google.com` — how a packet flows.

## Layer map (OSI in practice on ESP32)

```
L7  App         HTTP 80 / HTTPS 443 (TLS/SSL)  • DNS 53 • LB :8080 • portal /config /google /ping
L6/5  TLS/SSL   WiFiClientSecure • WebServerSecure 443 • VPN :8443 (TLS tunnel) • cert CN=esp32-netlab.local
L4  Transport   TCP :5000 echo + :179 BGP + :80/443/8443  • UDP :5001 echo + :53 DNS + :67 DHCP + :5005 OSPF  • Ports map
L3  Network     IP • Subnet (/24) • Router • Default Gateway • Routes / Static Routing • OSPF (link-state) • BGP (path-vector) • NAT (phone NAT)
L2  Link        Ethernet (wireless 802.11) • Wi-Fi STA+AP • MAC Address
L1  Phys        2.4 GHz Wi-Fi radio • USB power • no copper cable
Support           Firewall • Ping / ICMP (RTT) • Real time (NTP + millis) • Static IP vs DHCP
```

## Topic → layer → code → where it lives

| # | Topic | Layer | Code (`main.cpp`) | Lives on (ESP32 node) |
|---|---|---|---|---|
| 1 | Ethernet | L2 (wireless 802.11) | `initEthernetFallback()` (wireless-only build, `USE_ETHERNET_W5500=0`) | All nodes — Wi-Fi is Ethernet frame transport |
| 2 | Wi-Fi | L2 | `initWiFi()` `WIFI_AP_STA`, softAP `NetLab-HQ` Ch6 + STA uplink | AP node (AP), all nodes (STA) |
| 3 | MAC Address | L2 | `macStr()` `esp_read_mac(ESP_MAC_WIFI_STA)` → hostname `esp32-<last4>` | Every node, used as DHCP id |
| 4 | IP Address | L3 | `IPAddress` + `isValidIp()` printed at boot | Every node |
| 5 | Real time | Support | `bootMs=millis()` + `configTime(NTP)` → `nowStr()`, `uptimeSec()`, RTT | Every node — portal + serial |
| 6 | Static IP | L3 mgmt | `WiFi.config(ip,gw,subnet,dns)` toggle in `/config` | Core/Branch/Server/LB (fixed `.1`/`.10`/`.11`); Client by choice |
| 7 | DHCP | L3 support | Client auto-DHCP + custom server UDP 67/68 (DISCOVER→OFFER, REQUEST→ACK, option 3/6/51) | Server: AP_DHCP_DNS node, pool `.100-.149`; Clients: all |
| 8 | Subnet | L3 | `SubnetInfo::calc(ip,mask)` → network/broadcast/first/last/hosts (`/24`, VPN `/30`) | Every node (calc) |
| 9 | Router | L3 | `findRoute()` longest-prefix + NAT (`enablePhoneNat()` NAPT) | Core + Branch (real routers); AP node (AP+DHCP router-lite) |
| 10 | Default Gateway | L3 | `cfg.gateway`, `WiFi.config(gw)`, DHCP option 3, `WiFi.softAPConfig(gw,gw,subnet)` | AP node broadcasts `.1`; Core `.1` is upstream gw |
| 11 | Routes | L3 | `routes` vector + `findRoute()` + portal `/routes` | Core + Branch (own table), AP/Server read |
| 12 | Static Routing | L3 | `seedDefaultRoutes()` + `addRoute(...,metric,"static")` | Core/Branch seeded: `0.0.0.0/0`, `20.0/24 via VPN`, `30.0/24`, `10.8.0.0/30` |
| 13 | OSPF | L3 control | UDP 5005 multicast `224.0.0.5` Hello/LSA, `dijkstraHint()` | Core ↔ Branch peer (area 0) |
| 14 | BGP | L3 control | TCP 179 OPEN/KEEPALIVE/UPDATE AS_PATH 65001↔65002 | Core (65001) ↔ Branch (65002) |
| 15 | Ping | L3 test | `doPing()` 4 probes, prints `time=ms`, avg; portal `/ping?ip=` | Client/Monitor auto every 15s; any node via portal |
| 16 | ICMP | L3 test | `esp_ping` hook + TCP fallback timing; counts toward ping | Same as #15 |
| 17 | TCP | L4 | `tcpServer` echo `:5000` + `WiFiClient` | Server nodes + all (BGP/HTTP) |
| 18 | UDP | L4 | `udpEcho` `:5001` + DNS `:53` + DHCP `:67/68` + OSPF `:5005` | All nodes |
| 19 | Ports | L4 map | `PORT_*` constants (`configs/ip-plan.md` table) | Documented; LB `:8080`, VPN `:8443` |
| 20 | Firewall | L3/L4 policy | `FwRule` first-match `fwAllowed()` checked on TCP/UDP/HTTP | Core (enforced) + all nodes check own ports |
| 21 | TLS | L6/5 | `WiFiClientSecure` + mbedTLS AES/PSK | Core/Branch VPN `:8443`, any `testGoogle()` client |
| 22 | SSL | L6/5 alias | Same as TLS; `WebServerSecure` = TLS/SSL on 443 | All nodes (portal/server HTTPS) |
| 23 | VPN | L3 tunnel | `vpnDial()` TLS to `cfg.gateway:8443`, subnet `10.8.0.0/30`, PSK handshake | Core (server) ↔ Branch (client) |
| 24 | DNS | L7 | Custom UDP 53 (`dnsZone` + forward `8.8.8.8`) | AP_DHCP_DNS node serves; all resolve via it |
| 25 | HTTP | L7 | `WebServer` `:80` portal + content server (`/` `/config` `/routes` `/fw`) | All nodes (dual HTTP+HTTPS kept) |
| 26 | HTTPS | L7 | `WebServerSecure` `:443` + cert `configs/certs/`, HSTS | All nodes (dual HTTP+HTTPS kept) |
| 27 | Load Balancer | L7 | Round-robin `lbPickBACKEND()`/`lbHandle()` `:8080` → `30.10:80/30.11:80` | LB node VIP `192.168.30.1` |

## Planes

```
Data plane (per packet):  MAC/IP → Subnet check → findRoute() → NAT (phone only) → Firewall? → TCP/UDP → TLS? → HTTP/DNS/VPN
Control plane:            OSPF Hello/LSA (UDP 5005) + BGP OPEN/UPDATE (TCP 179) → Route table (connected/static/OSPF/BGP)
Management plane:         Portal /config /routes /fw /google (HTTP 80 + HTTPS 443 dual) • NTP time • uptime/RTT • Serial 115200
```

## Packet walkthrough — phone on NetLab-HQ → google.com (the framework in motion)

```
Phone (AP client)
  └─ Wi-Fi (802.11) + MAC association → AP ESP32 (softAP 192.168.10.1, AP_DHCP_DNS 192.168.10.100)
        │ DHCP: DISCOVER → OFFER .10x (option 3 gw .1, option 6 DNS .100)
        │ DNS: A www.google.com → unknown → forward 8.8.8.8 → 142.251.x.x (Firewall: ALLOW UDP 53)
        ▼ NAT (enablePhoneNat) 192.168.10.x → STA 192.168.x.x
STA uplink (internet SSID gateway, via Route 0.0.0.0/0)
        ▼ TCP 443 to 142.251.x.x (Firewall: ALLOW TCP 443) → TLS handshake → HTTP GET / (Host: www.google.com)
Google → 200 OK → TLS decrypt → NAPT reverse → Phone renders google.com
```

Same walk for `youtube.com` (302), `facebook.com` (301→302), `video-viewer-gules.vercel.app` (`216.198.79.195`, Vercel) — only the IP/SNI changes; everything else identical.

## Topology in framework terms

```
Phone → [Wi-Fi/AP+DHCP/DNS] → [Core Router: NAT/FW/VPN/BGP-OSPF] ─┬─► [Server1 :80/:443]
                                                                  └─► [Server2 :80/:443]
                                                          [LB VIP :8080] → Server1/Server2 (round-robin)
[Branch Router :20.1] ←VPN 10.8.0.0/30 (TLS) + OSPF/BGP→ Core
[Client/Monitor :10.50] → Ping/ICMP + testGoogle() to any host
```

All ESP32 — no Raspberry Pi, no Ethernet cable. Hardware: `hardware.md` (7 boards full / 3 minimum).
