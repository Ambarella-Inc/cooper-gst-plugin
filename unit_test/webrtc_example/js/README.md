# WebRTC Browser Client

A JavaScript-based WebRTC peer application for real-time audio/video streaming with GStreamer.

## Quick Start

1. **Start the signalling server:**
   ```bash
   cd ../signalling
   ./start_server.sh
   ```

2. **Open in browser** (choose one method):

   **Option A: Direct file access (simplest)**
   ```
   file:///work/webrtc/js/index.html
   ```

   **Option B: Use Python HTTP server (if needed)**
   ```bash
   cd /work/webrtc/js
   python3 -m http.server 8080
   # Then open: http://localhost:8080/index.html
   ```

3. **For SSL mode:** Trust the self-signed certificate first by visiting:
   ```
   https://127.0.0.1:8443/health
   ```
   Click "Advanced" → "Proceed" to accept the certificate.

## Features

- **Stream Modes:** Send & Receive, Send Only, Receive Only
- **Configurable Server:** Set server address and port from UI
- **SSL/Non-SSL:** Toggle between WSS and WS protocols
- **Data Channel:** Bidirectional messaging support
- **Auto-reconnect:** Automatic reconnection on disconnect

## Files

| File | Description |
|------|-------------|
| `index.html` | Main UI page |
| `webrtc.js` | WebRTC client logic |
| `adapter-latest.js` | WebRTC adapter for browser compatibility (Refer to https://webrtc.github.io/adapter/adapter-latest.js ) |
| `test_codec_support.html` | Tool to check browser codec support |

## Troubleshooting

### WebSocket Connection Failed

1. Ensure signalling server is running on port 8443
2. For SSL mode, trust the certificate at `https://127.0.0.1:8443/health`
3. Try non-SSL mode by unchecking "Use SSL" in the UI

### Video Not Displaying

**H264 Profile Issue:** Browser WebRTC only supports H264 Constrained Baseline Profile. If using hardware encoder with High Profile, re-encode to Baseline:

```bash
# GStreamer pipeline with software re-encoding
--video_desc="your_source ! h264parse ! openh264dec ! openh264enc ! h264parse ! rtph264pay name=videopay pt=96"
```

### Check Browser Codec Support

Open `test_codec_support.html` in browser or run in console:

```javascript
// Check receiving capabilities
const caps = RTCRtpReceiver.getCapabilities('video');
console.log('H264:', caps.codecs.filter(c => c.mimeType === 'video/H264'));
console.log('VP8:', caps.codecs.filter(c => c.mimeType === 'video/VP8'));
console.log('AV1:', caps.codecs.filter(c => c.mimeType === 'video/AV1'));
```

### Connection Keeps Reconnecting

1. Check server logs for errors
2. Verify peer ID is not already in use
3. Clear browser cache and refresh

## H264 Profile Reference

| Profile | profile-level-id | WebRTC Support |
|---------|------------------|----------------|
| Constrained Baseline | `42c0xx` | ✅ Required by standard |
| Baseline | `4200xx` | ✅ Widely supported |
| Main | `4d00xx` | ⚠️ Decode only |
| High | `6400xx` | ❌ Limited support |

**Key Point:** Even in recv-only mode, browsers reject High Profile in SDP negotiation because WebRTC codec whitelist only includes Baseline profiles.

## Server Commands

From browser console:
```javascript
// Reconnect with new settings
onReconnectClicked();

// Check connection state
console.log(peer_connection.iceConnectionState);
```

