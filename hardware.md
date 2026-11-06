# Hardware — ESP32, Wireless Only (Revised)

> No Ethernet cable, no switch, no W5500 shield, no Raspberry Pi. All traffic over Wi-Fi (IEEE 802.11).

## Full build — 7 nodes (covers all 27 topics in `network.txt`)

| Qty | Part | Role(s) |
|---|---|---|
| **7x** | **ESP32 DevKit V1 / WROOM-32** (USB-C, 30 or 38-pin) | Core Router `192.168.10.1`, Branch Router `192.168.20.1`, AP/DHCP/DNS `192.168.10.100`, Server1 `192.168.30.10`, Server2 `192.168.30.11`, Load Balancer VIP `192.168.30.1`, Client/Monitor `192.168.10.50` |
| **7x** | **USB-C data cable** (charge + data) | Flashing + power per board |
| **1x** | **5V 2A powered USB hub** (or 5V 2A supply per board) | Powers all boards at once for a demo. Unpowered hub will brown-out. |
| **1x** | **Phone / laptop with Wi-Fi** | Join `NetLab-HQ`, open `http://<node-ip>/` or `https://<node-ip>/` portal, run `google.com` / `youtube.com` / `facebook.com` / `video-viewer-gules.vercel.app` tests |

One firmware (`firmware/wifi-netlab/src/main.cpp`) flashed to all 7 boards — role picked at runtime in portal `/config` (no re-flash).

## Minimum viable lab — 3 nodes

| Qty | Role |
|---|---|
| 1x ESP32 | AP/DHCP/DNS `192.168.10.100` — AP `NetLab-HQ`, DHCP pool `.100-.149`, DNS `:53` |
| 1x ESP32 | CORE_ROUTER `192.168.10.1` — NAT + firewall + VPN `:8443`, BGP AS 65001 |
| 1x ESP32 | CLIENT_MONITOR `192.168.10.50` — ping/ICMP, TCP/UDP, TLS test `/google?host=…` |

Same firmware. Add Branch, Server1/2, LB later for OSPF/BGP + round-robin demo.

## What NOT to buy

- Ethernet cables / CAT6 — wireless only.
- Switch / router / access point — ESP32 AP replaces it.
- W5500 Ethernet shield — not needed (`USE_ETHERNET_W5500=0`).
- Raspberry Pi (any model) — ESP32 only.
- Arduino UNO — ESP32 Arduino core only.

## Wiring & power

- **Flashing:** one board at a time over USB: `pio run -t upload` in `firmware/wifi-netlab`.
- **Power:** USB 5V → board VIN. **3.3V logic** — never feed 5V into GPIO/3V3 pins.
- **Inter-board:** none. Nodes talk via Wi-Fi STA/AP (`WIFI_AP_STA`). AP node hosts `NetLab-HQ`, STA joins your internet SSID for `google.com` etc., NAT (`enablePhoneNat()` + `sdkconfig.defaults`) lets phones on `NetLab-HQ` surf.

## Search shorthand

`ESP32 DevKit V1 30pin`, `USB-C 2A supply`, `powered USB hub 5V 2A`

## Firmware map

- Build: `platformio.ini` (ESP32 Arduino, wireless-only).
- Portal: `http://<ip>/` (HTTP 80) **and** `https://<ip>/` (HTTPS 443, self-signed `CN=esp32-netlab.local`, certs in `configs/certs/`).
- NAT/phone: `docs/topology.md`, `configs/ip-plan.md`, `configs/vpn-and-tls.md`.
