# Hardware Needed — ESP32, Wireless Only (Revised)

> Revised: no Ethernet cable, no W5500 shield. All inter-node traffic over Wi-Fi (IEEE 802.11).

## Full build — 7 nodes (covers all 27 topics in `network.txt`)

| Qty | Part | Role(s) |
|---|---|---|
| 7x | ESP32 DevKit V1 / WROOM-32 (USB-C, 30 or 38-pin) | Core Router, Branch Router, AP/DHCP/DNS, Server1, Server2, Load Balancer, Client/Monitor |
| 7x | USB-C data cable (charge + data) | Flashing + power |
| 1x | 5V 2A powered USB hub (or 5V 2A supply per board) | Power all boards at once for demo |
| 1x | Phone / laptop with Wi-Fi | Join `NetLab-HQ`, open `http://<node-ip>/config` portal |

**Not needed:** Ethernet cables, switch, W5500 shield, Raspberry Pi, Arduino UNO, extra router.

## Minimum viable lab — 3 nodes

| Qty | Role |
|---|---|
| 1x ESP32 | AP/DHCP/DNS 192.168.10.100 |
| 1x ESP32 | CORE_ROUTER 192.168.10.1 |
| 1x ESP32 | CLIENT_MONITOR 192.168.10.50 |

Same firmware, just pick Role in portal. Add Branch, Server1/2, LB later for OSPF/BGP + round-robin demo.

## Notes

- ESP32 is 3.3V logic — USB 5V goes to board VIN only, never to GPIO.
- No inter-board wiring. Nodes talk via Wi-Fi STA/AP (`WIFI_AP_STA`).
- Firmware built with `USE_ETHERNET_W5500=0` (wireless only). See `platformio.ini`.
- Ethernet topic (#1) is covered by wireless Ethernet (802.11) on native ESP32 Wi-Fi.
