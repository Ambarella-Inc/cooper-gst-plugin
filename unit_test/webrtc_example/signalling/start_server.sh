#!/bin/bash
# Convenience script to start the WebRTC signalling server

SIGNALLING_DIR="."

echo "Checking dependencies..."
cd "$SIGNALLING_DIR"

# Check websockets module
if ! python3 -c "import websockets" 2>/dev/null; then
    echo "Installing websockets module..."
    pip3 install --user websockets
fi

# Check certificates
if [ ! -f "$SIGNALLING_DIR/cert.pem" ] || [ ! -f "$SIGNALLING_DIR/key.pem" ]; then
    echo "Generating SSL certificates..."
    chmod +x generate_cert.sh
    ./generate_cert.sh
    if [ $? -ne 0 ]; then
        echo "Error: Certificate generation failed"
        exit 1
    fi
    echo "Certificates generated"
fi

# Check if server is already running
if netstat -tlnp 2>/dev/null | grep -q ":8443 " || ss -tlnp 2>/dev/null | grep -q ":8443 "; then
    echo "Warning: Port 8443 is already in use, server may already be running"
    read -p "Continue anyway? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

echo "Starting WebSocket signalling server..."
echo ""
echo "Select mode:"
echo "  1) Use SSL (default, requires certificates)"
echo "  2) Disable SSL (for development/testing)"
read -p "Choose [1/2] (default: 1): " choice
choice=${choice:-1}

if [ "$choice" = "2" ]; then
    echo ""
    echo "Using non-SSL mode (ws://)"
    echo "Server address: ws://127.0.0.1:8443"
    echo ""
    echo "Enable SDP delay? (helps with transceiver caps race condition)"
    read -p "Delay time (ms, 0=disable, recommended 100-200): " delay
    delay=${delay:-0}
    if [ "$delay" -gt 0 ]; then
        python3 simple_server.py --disable-ssl --sdp-delay "$delay"
    else
        python3 simple_server.py --disable-ssl
    fi
else
    echo ""
    echo "Using SSL mode (wss://)"
    echo "Server address: wss://127.0.0.1:8443"
    echo ""
    echo "Enable SDP delay? (helps with transceiver caps race condition)"
    read -p "Delay time (ms, 0=disable, recommended 100-200): " delay
    delay=${delay:-0}
    echo ""
    echo "IMPORTANT:"
    echo "  Before opening the page in browser, visit the following URL and trust the certificate:"
    echo "  https://127.0.0.1:8443/health"
    echo "  Click 'Advanced' -> 'Proceed' to trust the self-signed certificate"
    echo ""
    echo "Press Ctrl+C to stop the server"
    echo ""
    if [ "$delay" -gt 0 ]; then
        python3 simple_server.py --sdp-delay "$delay"
    else
        python3 simple_server.py
    fi
fi
