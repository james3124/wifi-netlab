# Routes, Static Routing, OSPF, BGP — ESP32 (no Raspberry Pi)

All routing logic in `firmware/wifi-netlab/src/main.cpp` (`RoutingTable`, `findRoute()` longest-prefix match).

## Connected routes (auto via `SubnetInfo::calc()`)

For 192.168.10.100/24: network 192.168.10.0, broadcast .255, hosts 254, first .1 last .254.

## Default gateway

`WiFi.config(staticIp, gateway, subnet, dns)` on ESP32. DHCP also announces gateway via option 3. Stored in `Preferences`.

## Static routes (seeded in `seedDefaultRoutes()`)

```
0.0.0.0/0 -> 192.168.10.1 (WIFI, metric 1)          # default gateway
192.168.20.0/24 -> 192.168.10.1 via VPN (metric 5)
192.168.30.0/24 -> 192.168.10.1 via WIFI (metric 2)
10.8.0.0/30 -> 10.8.0.1 via VPN (connected)
```

Manage at runtime: portal `/routes`, or add `addRoute(dest,mask,nextHop,iface,metric,"static")`.

## OSPF — minimal, ESP32-native (no Raspberry Pi)

- Transport: UDP 5005, multicast 224.0.0.5
- Hello every 10s (`cfg.ospfInterval`): `HELLO id=<hostname> ip=<localIP>`
- LSA every 30s: `LSA id=<hostname> link=<localIP> cost=10`
- Receiver installs route for learned link. Full Dijkstra cost-based path selection hooks in `dijkstraHint()` / `findRoute()` longest-prefix.

For Cisco-style reference, matching configs would be:
```
router ospf 1
 router-id 10.0.0.1
 network 192.168.10.0 0.0.0.255 area 0
```

## BGP — minimal, ESP32-native (no Raspberry Pi)

- Transport: TCP 179 to `cfg.bgpPeer` (192.168.10.1)
- Core runs BGP server (ASN 65001), Branch dials (ASN 65002)
- Message flow: OPEN (ASN + router-id) → KEEPALIVE → UPDATE `prefix=192.168.10.0/24 AS_PATH=65001`
- Best path: shortest AS_PATH, then metric tie-break. Learned UPDATE is logged; site prefix installed as static in lab scale.

Cisco-style reference:
```
router bgp 65001
 neighbor 192.168.20.1 remote-as 65002
 network 192.168.10.0 mask 255.255.255.0
```

## Ping / ICMP (ESP32)

- `doPing(target)` sends 4 timed probes, prints `reply time=xxms`, reports sent/received/avg.
- True ICMP: ESP-IDF `esp_ping` component (enable `CONFIG_ESP_PING_H`). Fallback measures TCP connect time to :80/:5000 when raw ICMP socket unavailable on Arduino core.

Test: Portal `http://<ip>/ping?ip=192.168.10.1` or serial from CLIENT_MONITOR role (auto every 15s).
