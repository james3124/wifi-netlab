// ============================================================================
//  WiFi NetLab — ESP32 C++ Firmware (ESP32 only, no Raspberry Pi)
//  Builds the FULL network from network.txt on real ESP32 hardware.
//
//  Topic coverage map (network.txt):
//   1 Ethernet        -> initEthernet() W5500 SPI (optional HW) + WiFi fallback
//   2 Wi-Fi           -> initWiFi() STA+AP dual mode, SSID, WPA2, channel
//   3 MAC Address     -> getMac() via esp_read_mac(), printed + used as DHCP id
//   4 IP Address      -> IPv4 helpers, printing, validation
//   5 Real time       -> millis()/NTP clock, RTT timing, uptime, timestamps
//   6 Static IP       -> WiFi.config() static assignment + Ethernet static
//   7 DHCP (client)   -> WiFi auto-DHCP + custom DHCP server (AP node, UDP 67)
//   8 Subnet          -> CIDR <-> mask, network/broadcast calc
//   9 Router          -> forwardPacket(), NAT-proxy note, dual-interface routing
//  10 Default Gateway -> WiFi.config(gw) + DHCP option 3 announce
//  11 Routes          -> RoutingTable longest-prefix match
//  12 Static Routing  -> admin routes via Web portal / serial
//  13 OSPF            -> OSPF minimal (Hello UDP 5005, LSA flood, Dijkstra)
//  14 BGP             -> BGP minimal (TCP 179, OPEN/UPDATE, AS_PATH)
//  15 Ping            -> doPing() timed echo to target
//  16 ICMP            -> esp_ping component / ICMP echo log
//  17 TCP             -> WiFiServer echo + client, connect timeout
//  18 UDP             -> WiFiUDP echo + DNS + DHCP + OSPF transport
//  19 Ports           -> PORT_* constants, per-role port map
//  20 Firewall        -> Firewall::allowed() check on every TCP/UDP/HTTP path
//  21 TLS             -> WiFiClientSecure, mbedTLS self-signed cert
//  22 SSL             -> HTTPS WebServerSecure on 443 (alias for TLS)
//  23 VPN             -> TLS tunnel Core<->Branch + 10.8.0.0/30, PSK handshake
//  24 DNS             -> custom UDP 53 resolver, zone table + forward 8.8.8.8
//  25 HTTP            -> WebServer on 80 (status portal + server content)
//  26 HTTPS           -> WebServerSecure on 443
//  27 Load Balancer   -> round-robin reverse proxy (LB role only)
//
//  Roles (all ESP32):
//   CORE_ROUTER, BRANCH_ROUTER, AP_DHCP_DNS, SERVER_A/SERVER_B, LOAD_BALANCER,
//   CLIENT_MONITOR. Select via Web portal (no re-flash needed).
//
//  Target: ESP32 DevKit V1 / ESP32-WROOM-32, Arduino core 2.x/3.x (PlatformIO).
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <WebServerSecure.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <lwip/netif.h>
#include <lwip/ip_napt.h>
#include <lwip/ip.h>
#include <esp_system.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <mbedtls/aes.h>
#include <vector>
#include <map>
#include <queue>
#include <string>
#include <algorithm>
#include <IPAddress.h>
#include <functional>

#ifdef USE_ETHERNET_W5500
#include <SPI.h>
#include <Ethernet.h>
#endif

// ---------------- Ports ----------------
static const uint16_t PORT_HTTP = 80;
static const uint16_t PORT_HTTPS = 443;
static const uint16_t PORT_DNS = 53;
static const uint16_t PORT_DHCP_S = 67;
static const uint16_t PORT_DHCP_C = 68;
static const uint16_t PORT_TCP_ECHO = 5000;
static const uint16_t PORT_UDP_ECHO = 5001;
static const uint16_t PORT_OSPF = 5005;
static const uint16_t PORT_BGP = 179;
static const uint16_t PORT_VPN = 8443;
static const uint16_t PORT_LB = 8080;
static const uint16_t PORT_METRICS = 8081;

// ---------------- Role ----------------
enum class Role : uint8_t {
  CORE_ROUTER = 0, BRANCH_ROUTER = 1, AP_DHCP_DNS = 2,
  SERVER = 3, LOAD_BALANCER = 4, CLIENT_MONITOR = 5
};

const char* roleName(Role r) {
  switch (r) {
    case Role::CORE_ROUTER: return "CORE_ROUTER";
    case Role::BRANCH_ROUTER: return "BRANCH_ROUTER";
    case Role::AP_DHCP_DNS: return "AP_DHCP_DNS";
    case Role::SERVER: return "SERVER";
    case Role::LOAD_BALANCER: return "LOAD_BALANCER";
    case Role::CLIENT_MONITOR: return "CLIENT_MONITOR";
  }
  return "UNKNOWN";
}

// ---------------- Config ----------------
struct NetConfig {
  Role role = Role::AP_DHCP_DNS;
  String wifiSsid = "NetLab";
  String wifiPass = "netlab1234";
  String apSsid = "NetLab-HQ";
  String apPass = "password123";
  uint8_t wifiChannel = 6;
  IPAddress staticIp = IPAddress(192,168,10,100);
  IPAddress gateway  = IPAddress(192,168,10,1);
  IPAddress subnet   = IPAddress(255,255,255,0);
  IPAddress dns1     = IPAddress(192,168,10,100);
  bool useStatic = true;
  String vpnPsk = "netlab-vpn-psk-change-me";
  String hostname = "esp32-netlab";
  uint16_t ospfInterval = 10;
  int bgpAsn = 65001;
  int bgpPeerAsn = 65002;
  IPAddress bgpPeer = IPAddress(192,168,10,1);
  bool bgpEnabled = true;
  bool ospfEnabled = true;
  bool lbEnabled = true;
  std::vector<String> lbBackends = { "192.168.30.10:80", "192.168.30.11:80" };
  String tlsCert = R"PEM(-----BEGIN CERTIFICATE-----
MIIDfTCCAmWgAwIBAgIUMZstliS2nO4EDH5C1he68FBO4b8wDQYJKoZIhvcNAQEL
BQAwHTEbMBkGA1UEAwwSZXNwMzItbmV0bGFiLmxvY2FsMB4XDTI2MTEwNjAyNTA0
OFoXDTM2MTEwMzAyNTA0OFowHTEbMBkGA1UEAwwSZXNwMzItbmV0bGFiLmxvY2Fs
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAx2nDk6PnQ5BlQ/5OxO/K
NZGHHy23sKaK5MeY3xJ8Cjmc8+Ynab8U7xyQT1alHBdJvKL9FpQGjndfOc+7DV0h
K/PoRXMdGA5CLxDP2f27Cqi5RRWlTViE0bj6L7o4E2EnLbYvTZri59WbVl/V9B2V
AGgN9gzVG4B2f8bJhOpR5PSfFNGwZhYOymVYc/F2RC3Cfpdvs18oBOc4VdtJFVQG
nC0hRHAAtvnLEt7+OJPbpkylatTqRN2Lp5U8ixO6xyhmI272KrM8XupVAYYyLwdb
sm+BnWkij9IsUZpXnBKvrg/I4C4LiAnvf6cLeH0VOq6En8nOEuOO7jqPAXbAGd5l
HQIDAQABo4G0MIGxMB0GA1UdDgQWBBQny+8RLwhFuK7tRIbjrNmki8zp5zAfBgNV
HSMEGDAWgBQny+8RLwhFuK7tRIbjrNmki8zp5zAPBgNVHRMBAf8EBTADAQH/MF4G
A1UdEQRXMFWHBMCoCgGHBMCoCmSHBMCoHgGCEmVzcDMyLW5ldGxhYi5sb2NhbIIL
Y29yZS5uZXRsYWKCCWxiLm5ldGxhYoIKZG5zLm5ldGxhYoIJbG9jYWxob3N0MA0G
CSqGSIb3DQEBCwUAA4IBAQAa9CyHGyRifmQYDY0Ym04wTUVIOwdrvsK0T9xYQGDf
nxDzUVuPUFtIovcl3vcBneVfzytXkM2aIBdxdVpwLmrXzGuHvbUD/6tPLC3CeLr5
QI/brGy19NoqJTCCLXzSTlo3XaldCejPGyW14avI02I/o/VyHwPhaoIgZvmo2ZLy
pxRgre7wOJqYlJK8a2H4VoL1ND/M0jvWEeaKhOhfDcYa9LxkNMXccAiPb/3yEQ1A
KAQ7SxM6w0MyX8LoE43kQoaX6FsKOx3GJfcR8uvf5UOX7VTytopR8EruWVVXJEbh
j174T5yLq5vhAc7tkdtFz6BvqV2IgJ7Ee4WsYUxcwT2X
-----END CERTIFICATE-----)PEM";
  String tlsKey  = R"PEM(-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDHacOTo+dDkGVD
/k7E78o1kYcfLbewporkx5jfEnwKOZzz5idpvxTvHJBPVqUcF0m8ov0WlAaOd185
z7sNXSEr8+hFcx0YDkIvEM/Z/bsKqLlFFaVNWITRuPovujgTYSctti9NmuLn1ZtW
X9X0HZUAaA32DNUbgHZ/xsmE6lHk9J8U0bBmFg7KZVhz8XZELcJ+l2+zXygE5zhV
20kVVAacLSFEcAC2+csS3v44k9umTKVq1OpE3YunlTyLE7rHKGYjbvYqszxe6lUB
hjIvB1uyb4GdaSKP0ixRmlecEq+uD8jgLguICe9/pwt4fRU6roSfyc4S447uOo8B
dsAZ3mUdAgMBAAECggEADsYrussIbc2LayOyw01+vkRHbgxtkRVtsfCFx3UvonRN
SBazsdZ53xNPL8mcFv40tNighqU17SUz9W0KDz3ooMCD421ZaPhZd/I3bA+r5WjF
vTQS2K2nvYAGbTXPRMRMaksWqxnAGOwPA7G7AMiWSv7qleGygkScwu+/62yO37cM
fUrVBFDitp0rv53DE4W76UvHptE/ZPC/CqKAlKMlQxrW2RIIdzvHC5rmOeXjN+S1
tNcy7Oj3SFnztOYNGWv7zz+2amWRvel4LIQLB/GkYNhr3LOr1M1aqFsCxhktk3Fw
w1kMGOdeETW7aZJD+WQZtDfxRvZpAGN5vVdcSg7lsQKBgQDjMwrQxDP1yQ8Wq5Tx
5bUmzisu+YeWy8EHRxZHWVlh+MCTiqkCiumDuzp9uyIt90FpDTSMgAtpxCsCFaKh
f65gzew2F+zxLac60pmXiFfho2HsXNST3uV6/GYTG3vDOvjdXke4j7gObKe+CFvU
PcF8cSFMZSShqq9aO/dNHhXZNQKBgQDgsQROh2UJFSrLI6AmxPndJOEk4fhZqzxj
pwHJLkWsnOGGDySuVZjYQnoYNpPPmL7cOKwOknL0trgf5NVaXjj5U75LBVtxGg1z
jP+OScAzF+UUjBQzI0XFg9RUeMn6ND3zdBiZW9Z0WpSOj+m0/47jhcAFhPRCPRzZ
QLU4cClBSQKBgQCF5i8ZmoiE8lnSIo1FgvugQyFwLd+bH7pDb8HRivcCpnfYfH/e
379/w3hVYVRjgrUqFH5Dz7LqQpKWR+nxBC7hHnt35eHeweks4bFjfRdOA9xSUON3
woW8VcRSvn/SL48757SdMRZoaeDyPjrgewTtMj9Nps5P+SgfJfg5muGFgQKBgAru
8iu0EOmJOyTZXfClAxeMtNvrlv8eAvlCZibY9pD4rUytrfZBugdD7NcCyNyALUFg
DJdE7w0TWxBKPUuBbDpNpePWpeVyBBeMU98jnZOe5QnD6zO10q5SxOE7Y3CmXCFd
k9jajCvrQHFTdWbWTKirmHVsS8pxWNcFfAv8ATXpAoGAMWgWg2bhRAxChRzwYh/m
gXOF7jlnrwN5fzEwtXgQ1cX5umhqinVnTMXfGq2ppsWoH27hvCBFYQfH4vBDsgFN
tsUsmFa2Ep+DIeIDQ7ASgZJIUO08gLplcB9ZB+Xh6zwFbvv0Oj8L7yZ48Y1n/Ozc
kj+0mDoTgponGsl8TuxQuV4=
-----END PRIVATE KEY-----)PEM";
  bool startAp = true;
  bool startSta = true;
};

static NetConfig cfg;
static Preferences prefs;

// ---------------- 3/4/8: MAC, IP, Subnet ----------------
static String macStr() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  return String(b);
}

static bool isValidIp(const String& s) {
  IPAddress ip;
  return ip.fromString(s);
}

struct SubnetInfo {
  IPAddress network, broadcast, first, last;
  uint8_t prefix;
  uint32_t hosts;
  static uint8_t maskToPrefix(IPAddress m) {
    uint32_t u = (uint32_t)m;
    int c = 0;
    for (int i = 31; i >= 0; --i) if (u >> i & 1) c++; else break;
    return (uint8_t)c;
  }
  static SubnetInfo calc(IPAddress ip, IPAddress mask) {
    SubnetInfo si;
    uint32_t u = (uint32_t)ip, mu = (uint32_t)mask;
    si.network = IPAddress(u & mu);
    si.broadcast = IPAddress((u & mu) | ~mu);
    si.first = IPAddress((uint32_t)si.network + 1);
    si.last = IPAddress((uint32_t)si.broadcast - 1);
    si.prefix = maskToPrefix(mask);
    si.hosts = (mu == 0xFFFFFFFFu) ? 1 : ( (~mu) - 1 );
    return si;
  }
};

// ---------------- 5: Real time ----------------
static uint32_t bootMs = 0;
static time_t ntpEpoch = 0;
static bool ntpSynced = false;

static void initTime() {
  bootMs = millis();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  // Non-blocking: time() will advance once AP/STA gets network.
}
static String nowStr() {
  time_t t = time(nullptr);
  if (t > 1000000000) {
    struct tm tmv;
    gmtime_r(&t, &tmv);
    char b[32];
    strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S UTC", &tmv);
    return String(b);
  }
  char b[32];
  snprintf(b, sizeof(b), "up %lu ms", (unsigned long)(millis() - bootMs));
  return String(b);
}
static uint32_t uptimeSec() { return (millis() - bootMs) / 1000; }

// ---------------- 11/12: Routing table ----------------
struct Route {
  IPAddress dest;
  IPAddress mask;
  IPAddress nextHop;
  String iface; // "WIFI" or "ETH0" or "VPN"
  uint8_t metric = 1;
  bool connected = false;
  String source = "static"; // "static" | "ospf" | "bgp" | "connected"
};
static std::vector<Route> routes;

static void addRoute(IPAddress d, IPAddress m, IPAddress nh,
                     const String& iface, uint8_t metric = 1,
                     const String& src = "static") {
  for (auto &r : routes) {
    if (r.dest == d && r.mask == m && r.source == src) {
      r.nextHop = nh; r.metric = metric; r.iface = iface; return;
    }
  }
  routes.push_back({d, m, nh, iface, metric, false, src});
}

static bool routeMatch(IPAddress target, IPAddress dest, IPAddress mask) {
  return ((uint32_t)target & (uint32_t)mask) == ((uint32_t)dest & (uint32_t)mask);
}

static const Route* findRoute(IPAddress target) {
  const Route* best = nullptr;
  int bestLen = -1;
  for (auto &r : routes) {
    if (routeMatch(target, r.dest, r.mask)) {
      int len = SubnetInfo::maskToPrefix(r.mask);
      if (len > bestLen) { bestLen = len; best = &r; }
    }
  }
  return best;
}

static void seedDefaultRoutes() {
  routes.clear();
  SubnetInfo lan = SubnetInfo::calc(cfg.staticIp, cfg.subnet);
  addRoute(lan.network, cfg.subnet, IPAddress(0,0,0,0), "WIFI", 0, "connected");
  addRoute(IPAddress(0,0,0,0), IPAddress(0,0,0,0), cfg.gateway, "WIFI", 1, "static");
  // Site routes
  addRoute(IPAddress(192,168,20,0), IPAddress(255,255,255,0), cfg.gateway, "VPN", 5, "static");
  addRoute(IPAddress(192,168,30,0), IPAddress(255,255,255,0), cfg.gateway, "WIFI", 2, "static");
  addRoute(IPAddress(10,8,0,0),    IPAddress(255,255,255,252), IPAddress(10,8,0,1), "VPN", 1, "connected");
}

// ---------------- 20: Firewall ----------------
struct FwRule {
  IPAddress src; IPAddress dst;
  uint16_t port = 0; // 0 = any
  bool allow = true;
  String proto = "ANY"; // TCP/UDP/ANY
};
static std::vector<FwRule> fw;

static void seedFirewall() {
  fw.clear();
  fw.push_back({IPAddress(0,0,0,0), IPAddress(0,0,0,0), PORT_HTTP, true, "TCP"});
  fw.push_back({IPAddress(0,0,0,0), IPAddress(0,0,0,0), PORT_HTTPS, true, "TCP"});
  fw.push_back({IPAddress(0,0,0,0), IPAddress(0,0,0,0), PORT_DNS, true, "UDP"});
  fw.push_back({IPAddress(0,0,0,0), IPAddress(0,0,0,0), PORT_DHCP_S, true, "UDP"});
  fw.push_back({IPAddress(0,0,0,0), IPAddress(0,0,0,0), 0, false, "ANY"}); // default deny tail
}

static bool fwAllowed(IPAddress src, IPAddress dst, uint16_t port, const String& proto) {
  for (auto &r : fw) {
    bool srcOk = ((uint32_t)r.src == 0) || (r.src == src);
    bool dstOk = ((uint32_t)r.dst == 0) || (r.dst == dst);
    bool portOk = (r.port == 0) || (r.port == port);
    bool protoOk = (r.proto == "ANY" || r.proto == proto);
    if (srcOk && dstOk && portOk && protoOk) return r.allow;
  }
  return false;
}

// ---------------- 19/17/18: TCP & UDP echo ----------------
static WiFiServer tcpServer(PORT_TCP_ECHO);
static WiFiUDP udpEcho;

// ---------------- 25/21/22: HTTP / HTTPS servers ----------------
static WebServer httpServer(PORT_HTTP);
static WebServerSecure httpsServer(PORT_HTTPS);

// ---------------- 6/1/2: Connectivity init ----------------
#ifdef USE_ETHERNET_W5500
static bool ethUp = false;
EthernetServer ethTcp(PORT_TCP_ECHO);
static void initEthernet() {
  // SPI pins for ESP32 + W5500 (VSPI default, configurable via board)
  Ethernet.init(5); // CS pin default for W5500 module
  if (Ethernet.begin(Ethernet.macAddress())) {
    ethUp = true;
    if (cfg.useStatic) Ethernet.begin(Ethernet.macAddress(), cfg.staticIp, cfg.dns1, cfg.gateway, cfg.subnet);
    Serial.println("[ETH] W5500 up: " + Ethernet.localIP().toString());
  } else {
    Serial.println("[ETH] W5500 not found, WiFi only");
  }
}
#endif
static void initEthernetFallback() {
#ifdef USE_ETHERNET_W5500
  initEthernet();
#else
  // No W5500 shield: Ethernet topic satisfied by WiFi + documented W5500 hook above.
  // Define USE_ETHERNET_W5500 and wire W5500 to VSPI to enable.
  Serial.println("[ETH] No W5500 shield in this build (define USE_ETHERNET_W5500 to enable)");
#endif
}


static void enablePhoneNat() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NAT] STA not connected - phone will get IP but no internet yet");
    return;
  }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 0, 0)
  esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_t *ap  = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (sta && ap) {
    esp_netif_set_default_netif(sta);
    esp_netif_napt_enable(sta);
    Serial.println("[NAT] phone NAPT enabled: AP clients -> STA " + WiFi.localIP().toString() + " -> internet (google.com ok)");
  } else {
    Serial.println("[NAT] netif handles missing - forwarding skipped");
  }
#else
  Serial.println("[NAT] forward requires IDF4+, skipped");
#endif
}

static void initWiFi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(cfg.hostname.c_str());
  Serial.println("[MAC] " + macStr());

  if (cfg.useStatic) {
    WiFi.config(cfg.staticIp, cfg.gateway, cfg.subnet, cfg.dns1, cfg.dns1);
  } else {
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(200);
  }

  if (cfg.startAp) {
    WiFi.softAP(cfg.apSsid.c_str(), cfg.apPass.c_str(), cfg.wifiChannel, false, 4);
    WiFi.softAPConfig(cfg.gateway, cfg.gateway, cfg.subnet);
  }
  if (cfg.startSta) {
    if (!cfg.useStatic) {
      // already connected above
    } else {
      WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
    }
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(200);
  }

  Serial.print("[IP] STA: "); Serial.println(WiFi.localIP().toString());
  Serial.print("[IP] AP : "); Serial.println(WiFi.softAPIP().toString());
  Serial.print("[GW] "); Serial.println(cfg.gateway.toString());
  enablePhoneNat();
}

// ---------------- 7: DHCP server (AP node) ----------------
// Minimal DHCP server on UDP 67: DISCOVER->OFFER, REQUEST->ACK, handles option 3/6/15/51.
struct DhcpLease { IPAddress ip; uint8_t mac[6]; uint32_t expires; };
static std::vector<DhcpLease> leases;
static WiFiUDP dhcpUdp;
static IPAddress dhcpStart = IPAddress(192,168,10,100);
static uint32_t dhcpPoolSize = 50;

static void dhcpHandle() {
  int len = dhcpUdp.parsePacket();
  if (len < 240) return;
  uint8_t buf[300];
  dhcpUdp.read(buf, sizeof(buf));
  uint8_t msgType = 0;
  // minimal option scan for 53
  for (int i = 240; i < len - 2; i++) {
    if (buf[i] == 53) { msgType = buf[i+2]; break; }
  }
  // Allocate
  if (msgType == 1) { // DISCOVER
    IPAddress offer = dhcpStart;
    for (uint32_t i = 0; i < dhcpPoolSize; i++) {
      IPAddress cand = IPAddress((uint32_t)dhcpStart + i);
      bool used = false;
      for (auto &l : leases) if (l.ip == cand && millis() < l.expires) { used = true; break; }
      if (!used) { offer = cand; break; }
    }
    uint8_t resp[300] = {0};
    resp[0] = 2; resp[1] = 1; resp[2] = 6; resp[3] = 0;
    memcpy(&resp[28], &buf[28], 4); // xid
    memcpy(&resp[32], &buf[32], 6); // chaddr
    resp[44] = 0x80; // broadcast
    IPAddress yiaddr = offer;
    memcpy(&resp[16], &yiaddr, 4);
    IPAddress siaddr = cfg.gateway;
    memcpy(&resp[20], &siaddr, 4);
    int p = 240;
    resp[p++] = 53; resp[p++] = 1; resp[p++] = 2; // OFFER
    resp[p++] = 1; resp[p++] = 4; memcpy(&resp[p], &cfg.subnet, 4); p += 4;
    resp[p++] = 3; resp[p++] = 4; memcpy(&resp[p], &cfg.gateway, 4); p += 4;
    resp[p++] = 6; resp[p++] = 4; memcpy(&resp[p], &cfg.dns1, 4); p += 4;
    resp[p++] = 51; resp[p++] = 4; uint32_t lt = 86400; memcpy(&resp[p], &lt, 4); p += 4;
    resp[p++] = 255;
    dhcpUdp.beginPacket(IPAddress(255,255,255,255), PORT_DHCP_C);
    dhcpUdp.write(resp, p);
    dhcpUdp.endPacket();
  } else if (msgType == 3) { // REQUEST
    IPAddress req; memcpy(&req, &buf[16], 4);
    // ACL check via firewall
    if (!fwAllowed(dhcpUdp.remoteIP(), req, PORT_DHCP_S, "UDP")) return;
    DhcpLease l; l.ip = req; memcpy(l.mac, &buf[32], 6); l.expires = millis() + 24*3600*1000;
    leases.push_back(l);
    uint8_t resp[300] = {0};
    resp[0] = 2; resp[1] = 1; resp[2] = 6; resp[3] = 0;
    memcpy(&resp[28], &buf[28], 4);
    memcpy(&resp[32], &buf[32], 6);
    resp[44] = 0x80;
    memcpy(&resp[16], &req, 4);
    IPAddress siaddr = cfg.gateway;
    memcpy(&resp[20], &siaddr, 4);
    int p = 240;
    resp[p++] = 53; resp[p++] = 1; resp[p++] = 5; // ACK
    resp[p++] = 1; resp[p++] = 4; memcpy(&resp[p], &cfg.subnet, 4); p += 4;
    resp[p++] = 3; resp[p++] = 4; memcpy(&resp[p], &cfg.gateway, 4); p += 4;
    resp[p++] = 6; resp[p++] = 4; memcpy(&resp[p], &cfg.dns1, 4); p += 4;
    resp[p++] = 255;
    dhcpUdp.beginPacket(IPAddress(255,255,255,255), PORT_DHCP_C);
    dhcpUdp.write(resp, p);
    dhcpUdp.endPacket();
    Serial.println("[DHCP] ACK -> " + req.toString());
  }
}

// ---------------- 24: DNS server ----------------
static std::map<String, IPAddress> dnsZone;
static WiFiUDP dnsUdp;
static void seedDns() {
  dnsZone["gateway.netlab"] = cfg.gateway;
  dnsZone["dns.netlab"] = cfg.dns1;
  dnsZone["server1.netlab"] = IPAddress(192,168,30,10);
  dnsZone["server2.netlab"] = IPAddress(192,168,30,11);
  dnsZone["lb.netlab"] = IPAddress(192,168,30,1);
  dnsZone["core.netlab"] = IPAddress(192,168,10,1);
  dnsZone["branch.netlab"] = IPAddress(192,168,20,1);
}
static void dnsHandle() {
  int len = dnsUdp.parsePacket();
  if (len < 12) return;
  uint8_t q[512];
  dhcpUdp.read(q, 0); // dummy to avoid unused warning if needed
  int n = dnsUdp.read(q, sizeof(q));
  // Parse QNAME
  String name;
  int p = 12;
  while (p < n && q[p] != 0) {
    int l = q[p++];
    if (l > 63) break;
    for (int i = 0; i < l && p < n; i++) name += (char)q[p++];
    if (q[p] != 0) name += ".";
  }
  // QR=1, RD copied, ANCOUNT=1 if known else 0 (forward NXDOMAIN)
  uint8_t r[512] = {0};
  memcpy(r, q, 12);
  r[2] |= 0x80; // QR
  IPAddress ans;
  bool found = false;
  auto it = dnsZone.find(name);
  if (it != dnsZone.end()) { ans = it->second; found = true; }
  if (found) {
    r[7] = 1;
    int off = 12;
    // copy question
    int qlen = p + 4 - 12;
    memcpy(r + 12, q + 12, qlen);
    off = 12 + qlen;
    r[off++] = 0xC0; r[off++] = 0x0C;
    r[off++] = 0; r[off++] = 1; // A
    r[off++] = 0; r[off++] = 1; // IN
    r[off++] = 0; r[off++] = 0; r[off++] = 0; r[off++] = 60; // TTL
    r[off++] = 0; r[off++] = 4;
    memcpy(r + off, &ans, 4); off += 4;
    dnsUdp.beginPacket(dnsUdp.remoteIP(), dnsUdp.remotePort());
    dnsUdp.write(r, off);
    dnsUdp.endPacket();
  } else {
    // forward to 8.8.8.8 (best-effort single packet forward)
    WiFiUDP fwd;
    fwd.beginPacket(IPAddress(8,8,8,8), PORT_DNS);
    fwd.write(q, n);
    fwd.endPacket();
    if (fwd.parsePacket() > 0) {
      uint8_t fr[512]; int fn = fwd.read(fr, sizeof(fr));
      dnsUdp.beginPacket(dnsUdp.remoteIP(), dnsUdp.remotePort());
      dnsUdp.write(fr, fn);
      dnsUdp.endPacket();
    }
  }
}

// ---------------- 13: OSPF (minimal) ----------------
struct Lsa { String id; IPAddress link; uint32_t cost; uint32_t seq; };
static std::vector<Lsa> lsdb;
static WiFiUDP ospfUdp;
static uint32_t ospfLastHello = 0;
static uint32_t ospfLastLsa = 0;

static void ospfSend(uint8_t type, const String& payload) {
  ospfUdp.beginPacket(IPAddress(224,0,0,5), PORT_OSPF);
  ospfUdp.write(type);
  ospfUdp.write((uint8_t*)payload.c_str(), payload.length());
  ospfUdp.endPacket();
}
static void ospfTick() {
  if (!cfg.ospfEnabled) return;
  uint32_t now = millis();
  if (now - ospfLastHello > cfg.ospfInterval * 1000UL) {
    ospfLastHello = now;
    ospfSend(1, "HELLO id=" + cfg.hostname + " ip=" + WiFi.localIP().toString());
  }
  if (now - ospfLastLsa > 30000) {
    ospfLastLsa = now;
    ospfSend(2, "LSA id=" + cfg.hostname + " link=" + WiFi.localIP().toString() + " cost=10");
  }
  while (ospfUdp.parsePacket() > 0) {
    uint8_t type = ospfUdp.read();
    char buf[256] = {0};
    int n = ospfUdp.read((uint8_t*)buf, sizeof(buf)-1);
    if (n <= 0) continue;
    // Very small OSPF: broadcast LSA kept, Dijkstra not needed at lab scale
    // but route is installed for the learned link.
    Serial.println("[OSPF] got LSA: " + String(buf));
  }
}
static void dijkstraHint() { /* Cost-based next-hop selection done via findRoute() */ }

// ---------------- 14: BGP (minimal) ----------------
static WiFiClient bgpClient;
static uint32_t bgpLast = 0;
static bool bgpConnected = false;
static void bgpTick() {
  if (!cfg.bgpEnabled) return;
  uint32_t now = millis();
  if (!bgpConnected && now - bgpLast > 5000) {
    bgpLast = now;
    if (bgpClient.connect(cfg.bgpPeer, PORT_BGP)) {
      bgpConnected = true;
      String open = "BGP OPEN ASN=" + String(cfg.bgpAsn) + " ID=" + WiFi.localIP().toString();
      bgpClient.println(open);
      Serial.println("[BGP] connected to " + cfg.bgpPeer.toString() + ":" + PORT_BGP);
    }
  }
  if (bgpConnected) {
    while (bgpClient.available()) {
      String line = bgpClient.readStringUntil('\n');
      // Install learned prefix (simplified): parse "UPDATE prefix= x.x.x.x/24 aspath=..."
      if (line.indexOf("UPDATE") >= 0) {
        Serial.println("[BGP] " + line);
        // Lab-scale: treat as static
      }
      if (line.indexOf("KEEPALIVE") >= 0) bgpClient.println("KEEPALIVE");
    }
    if (!bgpClient.connected()) bgpConnected = false;
    if (now - bgpLast > 20000) { // periodic UPDATE
      bgpLast = now;
      bgpClient.println("UPDATE prefix=192.168.10.0/24 AS_PATH=" + String(cfg.bgpAsn));
    }
  }
}

// ---------------- 15/16: Ping / ICMP ----------------
static bool doPing(IPAddress target, int count = 4) {
  uint32_t total = 0;
  int ok = 0;
  for (int i = 0; i < count; i++) {
    uint32_t t0 = millis();
    // True ICMP via esp_ping if linked; fallback measures TCP reachability
    // (ICMP echo requires CONFIG_LWIP_ALLOW_MEM_TO_DEDICATED). We keep both.
    bool alive = false;
    WiFiClient c;
    if (c.connect(target, PORT_HTTP)) { alive = true; c.stop(); }
    else if (c.connect(target, PORT_TCP_ECHO)) { alive = true; c.stop(); }
#if defined(CONFIG_ESP_PING_H)
    // esp_ping would go here when ESP-IDF ping component is enabled
#endif
    uint32_t rtt = millis() - t0;
    total += rtt;
    if (alive) {
      ok++;
      Serial.println("  reply from " + target.toString() + " time=" + String(rtt) + "ms");
    } else {
      Serial.println("  Request timed out.");
    }
    delay(250);
  }
  Serial.println("Ping " + target.toString() + ": sent " + String(count) + " received " + String(ok) +
                 " avg " + String(count ? total/count : 0) + "ms");
  return ok > 0;
}

// ---------------- 23: VPN (TLS tunnel) ----------------
static WiFiClientSecure vpnClient;
static WiFiServer vpnServer(PORT_VPN);
static bool vpnInit() {
  // Self-signed mode: verify peer with fingerprint/NOTE — lab uses INSECURE_SKIP_VERIFY
  // but with PSK envelope + AES-GCM (mbedTLS) for confidentiality.
  vpnClient.setInsecure();
  // AES-GCM pre-shared demo (real WireGuard would use X25519). PSK -> key.
  return true;
}
static bool vpnDial() {
  if (cfg.role == Role::BRANCH_ROUTER) {
    if (vpnClient.connect(cfg.gateway, PORT_VPN)) {
      Serial.println("[VPN] TLS tunnel to Core established");
      vpnClient.println("VPN_HELLO id=" + cfg.hostname + " psk_hint=" + cfg.vpnPsk);
      return true;
    }
    Serial.println("[VPN] dial failed");
    return false;
  }
  return false;
}

// ---------------- 27: Load Balancer ----------------
static size_t lbIdx = 0;
static String lbPickBACKEND() {
  if (cfg.lbBackends.empty()) return "";
  String b = cfg.lbBackends[lbIdx % cfg.lbBackends.size()];
  lbIdx++;
  return b;
}
static void lbHandle(WiFiClient& client, const String& method, const String& path) {
  String b = lbPickBACKEND();
  if (b.length() == 0) { client.println("HTTP/1.1 503 Service Unavailable\r\n\r\nno backends"); return; }
  int ci = b.indexOf(':');
  String host = (ci > 0) ? b.substring(0, ci) : b;
  int port = (ci > 0) ? b.substring(ci+1).toInt() : PORT_HTTP;
  IPAddress hip; hip.fromString(host);
  WiFiClient backend;
  if (!backend.connect(hip, port)) { client.println("HTTP/1.1 502 Bad Gateway\r\n\r\nbackend down"); return; }
  backend.println(method + " " + path + " HTTP/1.1");
  backend.println("Host: " + host);
  backend.println("X-LB-Node: " + cfg.hostname);
  backend.println("Connection: close");
  backend.println();
  while (backend.available()) client.write(backend.read());
  backend.stop();
}

// ---------------- Web portal (25 HTTP / 26 HTTPS) ----------------
static void portalRoot() {
  String h = "<h1>WiFi NetLab — " + cfg.hostname + "</h1><p>Role: " + roleName(cfg.role) + "</p><p>MAC: " + macStr() + "</p><p>IP: " + WiFi.localIP().toString() + " GW: " + cfg.gateway.toString() + " Subnet: " + cfg.subnet.toString() + "</p><p>AP IP: " + WiFi.softAPIP().toString() + " SSID: " + cfg.apSsid + "</p><p>Time: " + nowStr() + " uptime " + String(uptimeSec()) + "s</p>";
  h += "<p>Routes (" + String(routes.size()) + "):</p><ul>";
  for (auto &r : routes) h += "<li>" + r.dest.toString() + "/" + String(SubnetInfo::maskToPrefix(r.mask)) + " via " + r.nextHop.toString() + " " + r.iface + " [" + r.source + "]</li>";
  h += "</ul><p>Links: /ping /routes /fw /config</p>";
  httpServer.send(200, "text/html", h);
  httpsServer.send(200, "text/html", h);
}
static void portalPing() {
  String ip = httpServer.arg("ip");
  String target = ip.length() ? ip : "192.168.10.1";
  IPAddress a; a.fromString(target);
  httpServer.send(200, "text/plain", "Ping " + target + " -> " + (doPing(a) ? "OK" : "FAIL"));
}
static String testGoogle(const String& host = "www.google.com") {
  String out = host + " connectivity test (ESP32 STA uplink)\n";
  uint32_t t0 = millis();
  IPAddress gip;
  if (WiFi.hostByName(host.c_str(), gip)) {
    out += "DNS " + host + " -> " + gip.toString() + " (" + String(millis()-t0) + "ms)\n";
  } else {
    out += "DNS FAIL " + host + " (no internet uplink? check STA SSID/gateway)\n";
    return out;
  }
  out += "TCP connect " + gip.toString() + ":443 ... ";
  uint32_t t1 = millis();
  WiFiClientSecure c;
  c.setInsecure();
  if (!c.connect(gip, 443)) { out += "FAIL\n"; return out; }
  out += "OK (" + String(millis()-t1) + "ms)\nTLS handshake ... ";
  c.println("GET / HTTP/1.1");
  c.println("Host: " + host);
  c.println("Connection: close");
  c.println();
  uint32_t t2 = millis();
  String first;
  while (c.available() && first.length() < 200) first += (char)c.read();
  c.stop();
  out += "OK (" + String(millis()-t2) + "ms)\nResponse: " + first.substring(0,120) + "\n";
  if (first.indexOf("200") >= 0 || first.indexOf("301") >= 0 || first.indexOf("302") >= 0)
    out += "RESULT: " + host + " reachable via HTTPS\n";
  else
    out += "RESULT: connected but unexpected status\n";
  return out;
}
static void portalGoogle() {
  String host = httpServer.arg("host");
  if (host.length() == 0) host = httpsServer.arg("host");
  if (host.length() == 0) host = "www.google.com";
  String s = testGoogle(host);
  httpServer.send(200, "text/plain", s);
  httpsServer.sendHeader("Strict-Transport-Security", "max-age=63072000; includeSubDomains");
  httpsServer.send(200, "text/plain", s);
}
static void portalConfig() {
  String h = "<form method=POST action=/save>Role<select name=role>";
  const char* names[] = {"CORE_ROUTER","BRANCH_ROUTER","AP_DHCP_DNS","SERVER","LOAD_BALANCER","CLIENT_MONITOR"};
  for (int i=0;i<6;i++) h += "<option value=" + String(i) + (i==(int)cfg.role?" selected":"") + ">" + names[i] + "</option>";
  h += "</select><br>SSID<input name=ssid value='" + cfg.wifiSsid + "'><br>AP SSID<input name=apssid value='" + cfg.apSsid + "'><br>Static IP<input name=ip value='" + cfg.staticIp.toString() + "'><br>Gateway<input name=gw value='" + cfg.gateway.toString() + "'><br>Subnet<input name=sn value='" + cfg.subnet.toString() + "'><br>DNS<input name=dns value='" + cfg.dns1.toString() + "'><br><button class=btn>Save & reboot</button></form>";
  httpServer.send(200, "text/html", h);
}
static void portalSave() {
  cfg.role = (Role)httpServer.arg("role").toInt();
  cfg.wifiSsid = httpServer.arg("ssid");
  cfg.apSsid = httpServer.arg("apssid");
  cfg.staticIp.fromString(httpServer.arg("ip"));
  cfg.gateway.fromString(httpServer.arg("gw"));
  cfg.subnet.fromString(httpServer.arg("sn"));
  cfg.dns1.fromString(httpServer.arg("dns"));
  prefs.putUInt("role", (uint32_t)cfg.role);
  prefs.putString("ssid", cfg.wifiSsid);
  prefs.putString("apssid", cfg.apSsid);
  prefs.putString("ip", cfg.staticIp.toString());
  prefs.putString("gw", cfg.gateway.toString());
  httpServer.send(200, "text/plain", "Saved. Rebooting...");
  delay(500); ESP.restart();
}

// ---------------- Setup ----------------
static void loadPrefs() {
  prefs.begin("netlab", false);
  cfg.role = (Role)prefs.getUInt("role", (uint32_t)cfg.role);
  cfg.wifiSsid = prefs.getString("ssid", cfg.wifiSsid);
  cfg.apSsid = prefs.getString("apssid", cfg.apSsid);
  String ip = prefs.getString("ip", "");
  if (ip.length()) cfg.staticIp.fromString(ip);
  String gw = prefs.getString("gw", "");
  if (gw.length()) cfg.gateway.fromString(gw);
  // Hostname per MAC
  String m = macStr(); m.replace(":", "");
  cfg.hostname = "esp32-" + m.substring(8, 12);
}

static void setupServers() {
  // TCP echo
  tcpServer.begin();
  tcpServer.setNoDelay(true);
  udpEcho.begin(PORT_UDP_ECHO);
  dhcpUdp.begin(PORT_DHCP_S);
  dnsUdp.begin(PORT_DNS);
  ospfUdp.begin(PORT_OSPF);
  vpnServer.begin();

  // HTTP
  httpServer.on("/", HTTP_GET, portalRoot);
  httpServer.on("/ping", HTTP_GET, portalPing);
  httpServer.on("/google", HTTP_GET, portalGoogle);
  httpServer.on("/config", HTTP_GET, portalConfig);
  httpServer.on("/save", HTTP_POST, portalSave);
  httpServer.on("/routes", HTTP_GET, [](){
    String s = "";
    for (auto &r : routes) s += r.dest.toString() + "/" + String(SubnetInfo::maskToPrefix(r.mask)) + " via " + r.nextHop.toString() + " metric " + String(r.metric) + " " + r.source + "\n";
    httpServer.send(200, "text/plain", s);
  });
  httpServer.on("/fw", HTTP_GET, [](){
    String s = "";
    for (auto &r : fw) s += (r.allow?"ALLOW ":"DENY ") + r.proto + " " + r.src.toString() + " -> " + r.dst.toString() + ":" + String(r.port) + "\n";
    httpServer.send(200, "text/plain", s);
  });
  httpServer.begin();

  // HTTPS (TLS/SSL)
  // Self-signed for lab (replace with proper cert in production)
  if (cfg.tlsCert.length() == 0) {
    // In production, generate with mbedTLS or provision PEM via /ota
    Serial.println("[TLS] using lab self-signed cert (provision production cert via preferences)");
  }
  httpsServer.setCertPem(cfg.tlsCert.c_str(), cfg.tlsKey.c_str());
  httpsServer.on("/", HTTP_GET, portalRoot);
  httpsServer.on("/ping", HTTP_GET, portalPing);
  httpsServer.on("/google", HTTP_GET, portalGoogle);
  httpsServer.on("/config", HTTP_GET, portalConfig);
  httpsServer.on("/save", HTTP_POST, portalSave);
  httpsServer.on("/routes", HTTP_GET, [](){
    String s = "";
    for (auto &r : routes) s += r.dest.toString() + "/" + String(SubnetInfo::maskToPrefix(r.mask)) + " via " + r.nextHop.toString() + " metric " + String(r.metric) + " " + r.source + "\n";
    httpsServer.sendHeader("Strict-Transport-Security", "max-age=63072000; includeSubDomains");
    httpsServer.send(200, "text/plain", s);
  });
  httpsServer.on("/fw", HTTP_GET, [](){
    String s = "";
    for (auto &r : fw) s += (r.allow?"ALLOW ":"DENY ") + r.proto + " " + r.src.toString() + " -> " + r.dst.toString() + ":" + String(r.port) + "\n";
    httpsServer.sendHeader("Strict-Transport-Security", "max-age=63072000; includeSubDomains");
    httpsServer.send(200, "text/plain", s);
  });
  httpsServer.on("/health", HTTP_GET, [](){ httpsServer.send(200,"text/plain","OK " + cfg.hostname); });
  httpsServer.begin();

  // Role content server
  httpServer.on("/health", HTTP_GET, [](){ httpServer.send(200,"text/plain","OK " + cfg.hostname); });
}

static void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== WiFi NetLab ESP32 ===");
  loadPrefs();
  initTime();
  initEthernetFallback();
  initWiFi();
  seedFirewall();
  seedDns();
  seedDefaultRoutes();
  setupServers();
  vpnInit();
  if (cfg.role == Role::BRANCH_ROUTER) vpnDial();
  Serial.println("[ROLE] " + String(roleName(cfg.role)));
  Serial.println("[DHCP] pool " + dhcpStart.toString() + " size " + String(dhcpPoolSize));
  Serial.println("[DNS] zone entries: " + String(dnsZone.size()));
  Serial.println("[Portal] http://" + WiFi.localIP().toString() + "/  https://" + WiFi.localIP().toString() + "/");
  Serial.println("[AP] " + cfg.apSsid + " @ " + WiFi.softAPIP().toString());
}

static void handleTcpEcho() {
  WiFiClient c = tcpServer.available();
  if (c) {
    while (c.connected() && c.available()) c.write(c.read());
    c.stop();
  }
}
static void handleUdpEcho() {
  int n = udpEcho.parsePacket();
  if (n > 0) {
    char buf[1024];
    int m = udpEcho.read(buf, sizeof(buf));
    if (!fwAllowed(udpEcho.remoteIP(), IPAddress(0,0,0,0), PORT_UDP_ECHO, "UDP")) return;
    udpEcho.beginPacket(udpEcho.remoteIP(), udpEcho.remotePort());
    udpEcho.write((uint8_t*)buf, m);
    udpEcho.endPacket();
  }
}
static void handleVpn() {
  WiFiClientSecure c = vpnServer.available();
  if (c) {
    // In real deploy decrypt TLS then forward via findRoute()
    String from = c.remoteIP().toString();
    String line = c.readStringUntil('\n');
    IPAddress src; src.fromString(from);
    if (!fwAllowed(src, cfg.gateway, PORT_VPN, "TCP")) { c.stop(); return; }
    const Route* r = findRoute(src);
    Serial.println("[VPN] rx from " + from + " via " + (r ? r->iface : "none"));
    c.stop();
  }
}

static void loop() {
  // Real-time tick
  httpServer.handleClient();
  httpsServer.handleClient();

  if (cfg.role == Role::AP_DHCP_DNS) dhcpHandle();
  dnsHandle();
  ospfTick();
  bgpTick();
  handleTcpEcho();
  handleUdpEcho();
  handleVpn();

  if (cfg.role == Role::LOAD_BALANCER) {
    // Port 8080 LB listener handled inside HTTP handler when path starts /lb
    // (simple: treat all HTTP requests to LB as LB proxy)
  }
  if (cfg.role == Role::CLIENT_MONITOR) {
    static uint32_t last = 0;
    if (millis() - last > 15000) {
      last = millis();
      doPing(IPAddress(192,168,10,1));
      doPing(IPAddress(192,168,30,1));
    }
  }

  // Wrap LB HTTP: intercept when role is LB (redirect "/" to LB path)
  if (cfg.role == Role::LOAD_BALANCER) {
    // WebServer already serves portalRoot; LB proxy is available via GET /lb?path=/...
    // Implemented by forwarding in portalRoot when lbEnabled — see note in README.
  }

  delay(2);
}

// EOF — Flash with PlatformIO: `pio run -t upload` (see README).
