# LAN protocol, version 1

TCP defaults to port 24820. Each frame contains a four-byte unsigned big-endian
payload length followed by a compact UTF-8 JSON object. Maximum payload: 65,536
bytes. Invalid lengths, malformed JSON, unsupported events, authentication errors,
and unexpected message types terminate the session. Reads handle fragmented and
coalesced frames. No C++ struct memory crosses the wire.

## Authentication

1. Host sends `challenge` with version 1 and a random 256-bit nonce.
2. Client sends `hello` with its unique ID, independent random nonce, native monitor
   metadata, and an HMAC-SHA256 proof using the shared key. Monitor metadata has a
   second HMAC binding it to the same handshake.
3. Host validates the proof and metadata and sends `welcome` with its own proof.
4. Both directions start sequence numbers at 1. Every subsequent message is signed
   over direction, both session nonces, and the compact JSON object without its
   `mac` field. The receiver requires the exact next sequence and a matching MAC.

The reference encoder uses Qt's compact JSON serialization. The client ID is 1–64
ASCII letters, digits, underscores, or hyphens; `host` is reserved. Client IDs must
be unique within a host session. A connection key must have 16–256 UTF-8 bytes;
the GUI generates a random 32-character key.

Authentication and message integrity do **not** encrypt input. This version is for
the specified trusted LAN; do not expose its port to the internet. The key itself
is never sent, saved by the GUI, or logged. There is no automatic reconnect.

## Messages after authentication

| Type | Direction | Payload / behavior |
|---|---|---|
| `input` | Host → client | Validated key, position, movement, button, or scroll event |
| `reset` | Host → client | Release all tracked injected keys and buttons |
| `cursor` | Client → host | Confirmed native cursor position for status |
| `fault` | Client → host | Input failure; host recovers and ends the session |
| `ping` | Both | Liveness heartbeat, every 500 ms |

Input events encode physical scan code, extended flag, press/release, and virtual-key
fallback; or native desktop coordinates; or mouse button identity; or signed wheel
deltas (Windows units, normally 120 per notch). Mirror routing never sends mouse events.

The receive timeout is 3 seconds after authentication, 5 seconds during connection
and pairing. Queued output is capped at 256 KiB, and an output backlog persisting
more than 1.5 seconds triggers recovery. Input is never replayed after reconnect.
A fresh connection has fresh nonces and sequence counters.
