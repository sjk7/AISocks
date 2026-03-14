from pathlib import Path

WIDTH, HEIGHT = 1900, 2500

BG = "#0f1115"
BOX_FILL = "#1a202c"
BOX_STROKE = "#60a5fa"
DEC_FILL = "#2a2118"
DEC_STROKE = "#f59e0b"
ERR_FILL = "#321a1a"
ERR_STROKE = "#f87171"
TEXT = "#d7d7d7"
ARROW = "#a3a3a3"

NODES = {
    "start": (640, 40, 620, 90, BOX_FILL, BOX_STROKE, [
        "Caller: HttpClient::get / post / request",
        "-> HttpClient::performRequest(method, url, body, headers)",
    ]),
    "parse": (640, 170, 620, 80, BOX_FILL, BOX_STROKE, [
        "Parse URL and normalize scheme",
    ]),
    "https": (720, 290, 460, 90, DEC_FILL, DEC_STROKE, [
        "scheme == https ?",
    ]),
    "authority": (640, 430, 620, 90, BOX_FILL, BOX_STROKE, [
        "HttpClient::parseAuthority_",
        "normalizeTlsHost_, isLikelyIpLiteral_, hasNonAsciiHostChar_",
    ]),
    "reuse_tcp": (720, 560, 460, 90, DEC_FILL, DEC_STROKE, [
        "cachedSocket_ valid + same host+port ?",
    ]),
    "conn": (640, 700, 620, 100, BOX_FILL, BOX_STROKE, [
        "Reuse cachedSocket_ OR",
        "SocketFactory::createTcpClient(...)",
    ]),
    "tls_reuse": (720, 840, 460, 90, DEC_FILL, DEC_STROKE, [
        "cachedTlsSession_ exists ?",
    ]),
    "tls_setup": (640, 980, 620, 120, BOX_FILL, BOX_STROKE, [
        "setupTlsForCurrentSocket lambda",
        "TlsContext::create(Mode::Client) when needed",
        "TlsContext::configureVerifyPeer(...)",
    ]),
    "session": (640, 1140, 620, 130, BOX_FILL, BOX_STROKE, [
        "TlsSession::create",
        "TlsSession::attachSocket(fd)",
        "SSL_get0_param + X509_VERIFY_PARAM_set_depth",
    ]),
    "verify": (640, 1310, 620, 130, BOX_FILL, BOX_STROKE, [
        "X509_VERIFY_PARAM_set1_ip_asc OR set1_host",
        "SSL_set_tlsext_host_name (SNI for DNS host)",
        "TlsSession::setConnectState",
    ]),
    "hs": (640, 1480, 620, 130, BOX_FILL, BOX_STROKE, [
        "Handshake loop: TlsSession::handshake",
        "retry on SSL_ERROR_WANT_READ / WANT_WRITE",
        "else fail request",
    ]),
    "postv": (640, 1650, 620, 110, BOX_FILL, BOX_STROKE, [
        "Post-handshake checks when verifyCertificate=true",
        "SSL_get_peer_certificate + SSL_get_verify_result",
    ]),
    "send": (640, 1800, 620, 110, BOX_FILL, BOX_STROKE, [
        "ClientHttpRequest::builder(...).build()",
        "ioBound_sendAll -> TlsSession::write",
    ]),
    "recv": (640, 1950, 620, 110, BOX_FILL, BOX_STROKE, [
        "Receive loop: ioBound_recv -> TlsSession::read",
        "HttpResponseParser::feed",
    ]),
    "redir": (720, 2100, 460, 90, DEC_FILL, DEC_STROKE, [
        "Redirect + followRedirects ?",
    ]),
    "resolve": (1180, 2090, 620, 110, BOX_FILL, BOX_STROKE, [
        "resolveUrl + redirectChain push + listener callback",
        "Loop back to performRequest redirect iteration",
    ]),
    "ka": (640, 2240, 620, 110, BOX_FILL, BOX_STROKE, [
        "HttpClient::shouldKeepAlive_",
        "cache cachedSocket_/cachedTlsSession_ OR clearCachedConnection_",
    ]),
    "done": (640, 2385, 620, 80, BOX_FILL, BOX_STROKE, [
        "Return Result<HttpClientResponse>",
    ]),
    "http_plain": (80, 310, 500, 90, BOX_FILL, BOX_STROKE, [
        "HTTP path (no TLS setup)",
    ]),
    "error": (80, 1500, 500, 90, ERR_FILL, ERR_STROKE, [
        "On TLS setup/handshake failure",
        "return Result failure",
    ]),
}


def esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def center_bottom(name: str):
    x, y, w, h, *_ = NODES[name]
    return x + w / 2, y + h


def center_top(name: str):
    x, y, w, h, *_ = NODES[name]
    return x + w / 2, y


def left_mid(name: str):
    x, y, w, h, *_ = NODES[name]
    return x, y + h / 2


def right_mid(name: str):
    x, y, w, h, *_ = NODES[name]
    return x + w, y + h / 2


parts = [
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">',
    f'<rect width="100%" height="100%" fill="{BG}"/>',
    "<defs>",
    f'  <marker id="arrow" markerWidth="12" markerHeight="12" refX="10" refY="6" orient="auto" markerUnits="strokeWidth">',
    f'    <path d="M0,0 L12,6 L0,12 z" fill="{ARROW}"/>',
    "  </marker>",
    "</defs>",
    '<text x="50%" y="26" text-anchor="middle" font-family="Helvetica" font-size="24" font-weight="700" fill="#e5e7eb">AISocks HttpClient HTTPS Flow (Function-Level)</text>',
]

for name, (x, y, w, h, fill, stroke, lines) in NODES.items():
    is_decision = name in {"https", "reuse_tcp", "tls_reuse", "redir"}
    rx = 45 if is_decision else 16
    parts.append(
        f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="2"/>'
    )
    ty = y + 32
    for line in lines:
        parts.append(
            f'<text x="{x + w / 2}" y="{ty}" text-anchor="middle" font-family="Helvetica" font-size="18" fill="{TEXT}">{esc(line)}</text>'
        )
        ty += 26


def add_arrow(x1, y1, x2, y2, label=None, lx=None, ly=None):
    parts.append(
        f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{ARROW}" stroke-width="2.2" marker-end="url(#arrow)"/>'
    )
    if label:
        tx = (x1 + x2) / 2 if lx is None else lx
        ty = (y1 + y2) / 2 - 6 if ly is None else ly
        parts.append(
            f'<text x="{tx}" y="{ty}" font-family="Helvetica" font-size="16" fill="#cfcfcf">{esc(label)}</text>'
        )


main = [
    "start",
    "parse",
    "https",
    "authority",
    "reuse_tcp",
    "conn",
    "tls_reuse",
    "tls_setup",
    "session",
    "verify",
    "hs",
    "postv",
    "send",
    "recv",
    "redir",
    "ka",
    "done",
]

for a, b in zip(main, main[1:]):
    x1, y1 = center_bottom(a)
    x2, y2 = center_top(b)
    add_arrow(x1, y1, x2, y2)

x1, y1 = left_mid("https")
x2, y2 = right_mid("http_plain")
add_arrow(x1, y1, x2, y2, label="no", lx=(x1 + x2) / 2 - 20, ly=(y1 + y2) / 2 - 10)

x1, y1 = right_mid("redir")
x2, y2 = left_mid("resolve")
add_arrow(x1, y1, x2, y2, label="yes")

rx, ry = center_top("resolve")
px, py = center_top("parse")
parts.append(
    f'<path d="M {rx} {ry} L {rx} 120 L {px + 350} 120 L {px + 350} {py - 10} L {px} {py - 10}" fill="none" stroke="{ARROW}" stroke-width="2.2" marker-end="url(#arrow)"/>'
)
parts.append(
    f'<text x="{px + 420}" y="112" font-family="Helvetica" font-size="16" fill="#cfcfcf">redirect loop</text>'
)

x1, y1 = left_mid("hs")
x2, y2 = right_mid("error")
add_arrow(x1, y1, x2, y2, label="handshake/setup fail")

parts.append('<text x="1190" y="370" font-family="Helvetica" font-size="16" fill="#cfcfcf">yes</text>')
parts.append('<text x="1190" y="920" font-family="Helvetica" font-size="16" fill="#cfcfcf">reuse or setup</text>')
parts.append('<text x="1190" y="2190" font-family="Helvetica" font-size="16" fill="#cfcfcf">no</text>')
parts.append('</svg>')

repo = Path(__file__).resolve().parents[1]
svg_path = repo / "https-flow.svg"
svg_path.write_text("\n".join(parts), encoding="utf-8")
print(str(svg_path))
