# IP Plan — Full Network (all ESP32, wireless only, no Raspberry Pi)

## Topology (7 ESP32 nodes — all ESP32, wireless only, no Raspberry Pi)

```
[Branch ESP32] --WiFi STA--> [AP/DHCP/DNS ESP32] --WiFi STA--> [Core Router ESP32] --WiFi STA--> [Server1 ESP32]
                                                                                          \--WiFi STA--> [Server2 ESP32]
[Client/Monitor ESP32] --WiFi STA--> [AP ESP32]
[Load Balancer ESP32] --WiFi STA--> [Core Router ESP32] (VIP 192.168.30.1 -> Server1:80, Server2:80 round-robin)
[Core <--> Branch] = VPN TLS tunnel 10.8.0.0/30 (also BGP/OSPF control plane)
```
Wireless only — no Ethernet cable, no W5500 shield.

## Subnets (C++ Subnet calc in `firmware/wifi-netlab/src/main.cpp`)

| Subnet | CIDR | Network | Broadcast | Gateway | Use |
|---|---|---|---|---|---|
| HQ LAN | 192.168.10.0/24 | 192.168.10.0 | 192.168.10.255 | 192.168.10.1 | Wi-Fi core (wireless only) |
| Branch LAN | 192.168.20.0/24 | 192.168.20.0 | 192.168.20.255 | 192.168.20.1 | Branch |
| Server LAN | 192.168.30.0/24 | 192.168.30.0 | 192.168.30.255 | 192.168.30.1 (LB VIP) | Servers + LB |
| Guest Wi-Fi | 192.168.40.0/24 | 192.168.40.0 | 192.168.40.255 | 192.168.40.1 | AP SSID guest (optional) |
| VPN tunnel | 10.8.0.0/30 | 10.8.0.0 | 10.8.0.3 | — | Core<->Branch encrypted |

## Host addressing (static — set in Portal /config, or auto per MAC)

| Node | Role | Static IP | MAC | Notes |
|---|---|---|---|---|
| Core Router | CORE_ROUTER | 192.168.10.1/24 | STA MAC via `esp_read_mac()` | NAT, firewall, VPN server :8443, BGP AS 65001 |
| Branch Router | BRANCH_ROUTER | 192.168.20.1/24 | STA MAC | OSPF + BGP peer to Core, VPN client |
| AP/DHCP/DNS | AP_DHCP_DNS | 192.168.10.100/24 | STA MAC | AP SSID `NetLab-HQ`, DHCP pool .100-.149, DNS :53 |
| Server1 | SERVER | 192.168.30.10/24 | STA MAC | HTTP :80 + HTTPS :443, TCP :5000, UDP :5001 |
| Server2 | SERVER | 192.168.30.11/24 | STA MAC | Same as Server1, second LB backend |
| Load Balancer | LOAD_BALANCER | 192.168.30.1/24 | STA MAC | VIP, LB :8080 -> 192.168.30.10:80 / .11:80 round-robin |
| Client/Monitor | CLIENT_MONITOR | 192.168.10.50/24 | STA MAC | Ping, HTTP/S tester, real-time RTT |

## DHCP

- Server (AP node, UDP 67/68): pool 192.168.10.100–149 (50 leases), lease 86400s, DNS 192.168.10.100, GW 192.168.10.1, subnet /24.
- Clients use `WiFi.begin()` auto or static via `WiFi.config()` — toggle in Portal.
- Gateway option (3) = `cfg.gateway`, DNS option (6) = `cfg.dns1`.

## DNS zone (UDP 53, `dnsZone` table)

| Hostname | A record |
|---|---|
| gateway.netlab | 192.168.10.1 |
| dns.netlab | 192.168.10.100 |
| core.netlab | 192.168.10.1 |
| branch.netlab | 192.168.20.1 |
| server1.netlab | 192.168.30.10 |
| server2.netlab | 192.168.30.11 |
| lb.netlab | 192.168.30.1 |
Unknown -> forward to 8.8.8.8.

## Ports (all roles map)

| Port | Proto | Service |
|---|---|---|
| 22 | TCP | (reserved SSH) |
| 53 | UDP | DNS |
| 67 | UDP | DHCP server |
| 68 | UDP | DHCP client |
| 80 | TCP | HTTP |
| 179 | TCP | BGP |
| 443 | TCP | HTTPS (TLS/SSL) |
| 5000 | TCP | TCP echo |
| 5001 | UDP | UDP echo |
| 5005 | UDP | OSPF Hello/LSA |
| 8080 | TCP | Load Balancer |
| 8443 | TCP | VPN (TLS tunnel) |

## Firewall defaults (applied to every TCP/UDP/HTTP path)

ALLOW TCP :80, :443 | ALLOW UDP :53, :67 | ALLOW TCP :5000, :5001 | default DENY
Edit via Portal `/fw` (rules vector, first-match allow/deny).
