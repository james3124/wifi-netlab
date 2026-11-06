# DHCP / DNS / Firewall / Load Balancer — ESP32 (no Raspberry Pi)

Code: `firmware/wifi-netlab/src/main.cpp` — sections `dhcpHandle()`, `dnsHandle()`, `fwAllowed()`, `lbHandle()`.

## DHCP server (AP_DHCP_DNS role, UDP 67/68)

- Pool: 192.168.10.100–149 (50 leases), lease 86400s, gateway 192.168.10.1, subnet /24, DNS 192.168.10.100.
- DISCOVER (type 1) → OFFER (yiaddr = free address), REQUEST (type 3) → ACK + lease stored, option 3/6/51.
- Clients toggle static vs DHCP in portal (`WiFi.config()` vs `WiFi.begin()`).

## DNS server (UDP 53)

- Custom A-record resolver, `dnsZone` table (see `ip-plan.md`). Unknown → forward to 8.8.8.8, relay answer.
- Monitor: `nslookup server1.netlab 192.168.10.100`.

## Firewall

- `FwRule { src, dst, port, allow, proto }` first-match. Seed: ALLOW HTTP 80, HTTPS 443, DNS 53, DHCP 67, TCP echo 5000, then default DENY.
- Checked in `handleUdpEcho()`, `handleVpn()`, `portalPing()`. View via portal `/fw`.

## Load Balancer (:8080)

- `lbPickBACKEND()` round-robin over `cfg.lbBackends` (`192.168.30.10:80`, `192.168.30.11:80`).
- `lbHandle()` proxies HTTP request to backend, injects `X-LB-Node`, streams response back. 502 if backend down, 503 if no backends.
- Test: `curl http://192.168.30.1:8080/health` alternates Server1/Server2.

## TCP / UDP echo

- TCP :5000 (`tcpServer`), UDP :5001 (`udpEcho`) with firewall check.
