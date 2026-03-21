# AISocks Security & Hardening Guide

This document outlines the security features and default hardening configurations built into the AISocks library. These settings are designed to protect the library during standalone public internet deployment.

## 🛡️ Standalone Deployment Checklist
While AISocks is highly hardened, we recommend the following for public exposure:
1. **Always use Https variant servers** over plain `HttpPollServer`.
2. **Monitor the the integrated metrics** to detect and respond to attack patterns.
3. **Use a firewall** (iptables/nftables) for the most robust IP-level protection, although our internal `IpFilter` provides significant mitigation.

---

## 🔒 TLS & Encryption
AISocks uses OpenSSL to provide industry-standard transport security.

### Protocol Defaults
*   **Minimum Protocol**: TLS 1.2
*   **Security Level**: Level 2 (disables weak ciphers like 3DES/RC4, enforces 2048-bit RSA/DH).
*   **Preferred Ciphers**: Prioritizes AEAD ciphers (AES-GCM, ChaCha20-Poly1305) which provide both confidentiality and integrity with performance benefits on modern CPUs (ARM64/x86).

### Anti-Replay & Performance
*   **ALPN Support**: Negotiates modern protocols (h2, http/1.1) to avoid handshake overhead.
*   **Session Resumption**: Supports session tickets and caching to minimize full asymmetric handshakes.

---

## 🚫 DoS (Denial of Service) Mitigation
The library strictly implements protections against common resource exhaustion attacks.

### TLS Handshake Safety
*   **Handshake Timeout**: `5000ms` (5 seconds).  
  Connections that fail to complete a TLS handshake within this window are automatically disconnected. This prevents "stalled handshake" attacks that exhaust thread/memory resources.
*   **Idle Sweep**: A background sweep identifies stalled TLS connections even if they never send data (solving the traditional "blind spot" in many poll systems).

### Request Rate Limiting (`IpFilter`)
AISocks includes a built-in Apache-style IP filtering system with an **Auto-Blacklist** feature:
*   **Burst Threshold**: Max 200 requests.
*   **Window**: 60 seconds.
*   **Penalty**: IPs exceeding the threshold are blocked for **90 minutes**.
*   **Static Config**: Supports manual CIDR blocking via configuration files.

### Resource Limits
*   **Global Connection Cap**: Default `1000` connections (configurable via `ClientLimit`).
*   **O(1) Lookup**: Uses a sparse FD-to-client table to eliminate hashing or collection-traversal overhead under high load.
*   **Non-Blocking I/O**: Mandatory across all subsystems to ensure the poller loop remains responsive.

---

## 🐛 Input Sanitization & Safety
*   **Path Traversal**: All file requests are normalized and validated to prevent access to files outside the document root (covers dot-dot, encoded, and double-encoded attacks).
*   **XSS Protection**: Built-in HTML escaping for directory listings and error pages.
*   **Information Leakage**: Option to hide server versions in HTTP headers and error responses.
*   **Sanitizer Verified**: The library is continuously tested with **ASan (Address), MSan (Memory), and UBSan (Undefined Behavior)** to prevent exploitable memory corruptions.
