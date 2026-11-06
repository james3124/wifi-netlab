# Flash & Test — ESP32 C++ (no Raspberry Pi)

## Flash

```bash
cd firmware/wifi-netlab
pip install platformio
pio run -t upload    # repeat per ESP32 board (USB, 115200 monitor)
pio run -t monitor
```

ESP32 DevKit V1 / WROOM-32, USB. **Wireless only — no Ethernet cable, no shield, no inter-board wiring.**

## Set role (once per node, no re-flash)

On serial note `AP tethering: NetLab-HQ` + `IP AP: 192.168.x.x`, or STA IP from serial. Open:
`http://<node-ip>/config` → pick Role, set SSID / static IP / gateway / subnet / DNS → Save & reboot.
Roles: `CORE_ROUTER`, `BRANCH_ROUTER`, `AP_DHCP_DNS`, `SERVER`, `LOAD_BALANCER`, `CLIENT_MONITOR`.

Recommended assignment:
- Board 1 → CORE_ROUTER 192.168.10.1
- Board 2 → AP_DHCP_DNS 192.168.10.100 (SSID NetLab-HQ)
- Board 3 → BRANCH_ROUTER 192.168.20.1
- Board 4 → SERVER 192.168.30.10
- Board 5 → SERVER 192.168.30.11
- Board 6 → LOAD_BALANCER 192.168.30.1
- Board 7 → CLIENT_MONITOR 192.168.10.50

## Tests (expected outputs)

1. **Real time**: serial `Time: 2026-… UTC uptime 123s` (after NTP sync), portal shows uptime.
2. **Ping**: `http://192.168.10.1/ping?ip=192.168.30.10` → `Ping 192.168.30.10: sent 4 received 4 avg ~12ms`
3. **HTTP**: `curl http://192.168.30.10/` → portal + `OK esp32-xxxx`
4. **HTTPS**: `curl -k https://192.168.30.10/` → same with TLS
5. **DNS**: `nslookup server1.netlab 192.168.10.100` → `192.168.30.10`
6. **DHCP**: connect phone to SSID `NetLab-HQ` → lease from .100-.149, DNS = AP node.
7. **LB**: `for i in 1 2 3 4; do curl -s http://192.168.30.1:8080/health; done` → alternates Server1/Server2.
8. **Firewall**: `http://192.168.10.1/fw` shows ALLOW then DENY tail.
9. **Routes/OSPF/BGP**: serial shows `[OSPF] got LSA…` and `[BGP] connected…` between Core/Branch.

## Troubleshooting (ESP32)

- DHCP empty pool: restart AP node (leases reset). Lease expiry on serial `[DHCP] ACK -> …`.
- No STA IP: check SSID/pass in portal, 15s timeout, AP fallback still serves portal.
- VPN down: `vpnDial()` prints reason; verify `cfg.gateway` = Core 192.168.10.1 and PSK match.
- HTTPS error: provision `tlsCert/tlsKey` in prefs before production; lab accepts self-signed.
