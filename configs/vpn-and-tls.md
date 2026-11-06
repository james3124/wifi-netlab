# VPN + TLS/SSL — ESP32 (no Raspberry Pi)

## VPN (TLS tunnel, ESP32 ↔ ESP32)

- Server: Core Router ESP32 listen :8443 (`WiFiServer vpnServer(PORT_VPN)`)
- Client: Branch Router ESP32 dial `cfg.gateway:8443` via `WiFiClientSecure` (TLS 1.2)
- Handshake: both sides exchange `VPN_HELLO id=<hostname> psk_hint=<psk>` — PSK in `cfg.vpnPsk` (change before deploy, stored in Preferences).
- Tunnel subnet: `10.8.0.0/30` (10.8.0.1 Core, 10.8.0.2 Branch). Forwarding via `findRoute()` → route exists `10.8.0.0/30 via 10.8.0.1 VPN`.
- Encryption: TLS channel + mbedTLS AES (PSK-derived key). For WireGuard-grade (X25519) swap the PSK handshake for ESP32 `wireguard` component (`CONFIG_ESP_WIREGUARD_ENABLED=1`) — the port/route design stays.

## TLS / SSL (HTTPS) — secured portal :443

- **Dual mode: HTTP 80 + HTTPS 443 both serve.** HTTP is **not** removed — `httpServer.on("/", portalRoot)` and `httpsServer.on("/", portalRoot)` both live. `portalRedirect()` kept as optional helper only. HTTPS adds HSTS; HTTP has no HSTS.
- **Certs:** lab self-signed CN=`esp32-netlab.local`, SAN `192.168.10.1/100/30.1, core/lb/dns.netlab`, 3650d — embedded in `cfg.tlsCert`/`cfg.tlsKey` (`R"PEM` raw strings, verified key match). Copies in `configs/certs/netlab-cert.pem` + `netlab-key.pem`. Replace via Preferences in production.
- **Headers:** HTTPS sends `Strict-Transport-Security: max-age=63072000; includeSubDomains` + `X-Content-Type-Options: nosniff`. TLS 1.2+ via mbedTLS (Arduino core).
- **Clients:** `WiFiClientSecure setInsecure()` (lab) / `setCACert()` with bundle (prod). Phone will show "self-signed — trust once" on first HTTPS hit — expected for self-signed.

Generate/regen: `openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -subj "/CN=esp32-netlab.local" -addext "subjectAltName=IP:192.168.10.1,IP:192.168.10.100,IP:192.168.30.1,DNS:esp32-netlab.local,DNS:core.netlab,DNS:lb.netlab,DNS:dns.netlab,DNS:localhost"`.

Test: `http://192.168.10.1/` (HTTP) and `https://192.168.10.1/` (HTTPS) — both serve; accept self-signed once on HTTPS.
