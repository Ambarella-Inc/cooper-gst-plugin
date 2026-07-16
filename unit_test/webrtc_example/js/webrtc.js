/* vim: set sts=4 sw=4 et :
 *
 * Demo Javascript app for negotiating and streaming a sendrecv webrtc stream
 * with a GStreamer app. Runs only in passive mode, i.e., responds to offers
 * with answers, exchanges ICE candidates, and streams.
 *
 * Author: Nirbheek Chauhan <nirbheek@centricular.com>
 */

// These can be overridden via the web UI or set here for defaults
var ws_server;
var ws_port;
// Set this to use a specific peer id instead of a random one
var default_peer_id;

// Get server address from UI input
function getServerAddress() {
    var input = document.getElementById('server-address');
    if (input && input.value.trim() !== '') {
        return input.value.trim();
    }
    return null; // Will auto-detect
}

// Get server port from UI input
function getServerPort() {
    var input = document.getElementById('server-port');
    if (input && input.value.trim() !== '') {
        return input.value.trim();
    }
    return '8443'; // Default port
}

// Get SSL setting from the checkbox on the page
function getUseSsl() {
    var checkbox = document.getElementById('use-ssl');
    return checkbox ? checkbox.checked : true; // Default to true (use SSL)
}

// Update SSL status display
function onSslChanged() {
    var checkbox = document.getElementById('use-ssl');
    var statusSpan = document.getElementById('ssl-status');
    if (checkbox && statusSpan) {
        if (checkbox.checked) {
            statusSpan.textContent = 'WSS';
            statusSpan.style.background = 'rgba(0, 212, 170, 0.1)';
            statusSpan.style.borderColor = 'rgba(0, 212, 170, 0.3)';
            statusSpan.style.color = '#00d4aa';
        } else {
            statusSpan.textContent = 'WS';
            statusSpan.style.background = 'rgba(255, 168, 0, 0.1)';
            statusSpan.style.borderColor = 'rgba(255, 168, 0, 0.3)';
            statusSpan.style.color = '#ffa800';
        }
    }
    // If already connected, reconnect with new setting
    if (typeof ws_conn !== 'undefined' && ws_conn && ws_conn.readyState === WebSocket.OPEN) {
        console.log("SSL setting changed, reconnecting...");
        ssl_auto_switched = false; // Reset auto-switch flag when user manually changes
        resetState();
    } else {
        // Reset auto-switch flag when user manually changes (even if not connected)
        ssl_auto_switched = false;
    }
}
// Override with your own STUN servers if you want
var rtc_configuration = {iceServers: [{urls: "stun:stun.l.google.com:19302"}]};
// The default constraints that will be attempted. Can be overriden by the user.
var default_constraints = {video: true, audio: true};

// Stream mode: 'sendrecv', 'sendonly', 'recvonly'
function getStreamMode() {
    var select = document.getElementById('stream-mode');
    return select ? select.value : 'sendrecv';
}

function isSendOnly() {
    return getStreamMode() === 'sendonly';
}

function isRecvOnly() {
    return getStreamMode() === 'recvonly';
}

var connect_attempts = 0;
var ssl_auto_switched = false; // Track if we've auto-switched from SSL to non-SSL
var peer_connection = new RTCPeerConnection(rtc_configuration);
var send_channel;
var ws_conn;
// Local stream after constraints are approved by the user
var local_stream = null;

// keep track of some negotiation state to prevent races and errors
var callCreateTriggered = false;
var makingOffer = false;
var isSettingRemoteAnswerPending = false;

// SDP negotiation attempt tracking to prevent infinite loops
var negotiation_attempts = 0;
var MAX_NEGOTIATION_ATTEMPTS = 5;

function setConnectButtonState(value) {
    var btn = document.getElementById("peer-connect-button");
    btn.value = value;
    if (value === "Disconnect") {
        btn.className = "btn btn-danger";
    } else {
        btn.className = "btn btn-primary";
    }
}

function wantRemoteOfferer() {
   return document.getElementById("remote-offerer").checked;
}

function onConnectClicked() {
    if (document.getElementById("peer-connect-button").value == "Disconnect") {
        resetState();
        return;
    }

    var id = document.getElementById("peer-connect").value;
    if (id == "") {
        alert("Peer id must be filled out");
        return;
    }

    ws_conn.send("SESSION " + id);
    setConnectButtonState("Disconnect");
}

function onTextKeyPress(e) {
    e = e ? e : window.event;
    if (e.code == "Enter") {
        onConnectClicked();
        return false;
    }
    return true;
}

function getOurId() {
    return Math.floor(Math.random() * (9000 - 10) + 10).toString();
}

function resetState() {
    // This will call onServerClose()
    ws_conn.close();
}

function onReconnectClicked() {
    // Reset connection attempts and auto-switch flag for fresh connection
    connect_attempts = 0;
    ssl_auto_switched = false;

    // Close existing connection if open
    if (ws_conn && ws_conn.readyState === WebSocket.OPEN) {
        ws_conn.close();
        // onServerClose will be called, which will auto-reconnect
    } else {
        // No active connection, just connect directly
        websocketServerConnect();
    }
}

function handleIncomingError(error) {
    setError("ERROR: " + error);
    resetState();
}

function getVideoElement() {
    var div = document.getElementById("video");
    var video_tag = document.createElement("video");
    video_tag.textContent = "Your browser doesn't support video";
    video_tag.autoplay = true;
    video_tag.playsinline = true;
    div.appendChild(video_tag);
    return video_tag
}

function setStatus(text) {
    console.log(text);
    var span = document.getElementById("status")
    // Don't set the status if it already contains an error
    if (!span.classList.contains('error'))
        span.textContent = text;
}

function setError(text) {
    console.error(text);
    var span = document.getElementById("status")
    span.textContent = text;
    span.classList.add('error');
}

function resetVideo() {
    // Release the webcam and mic
    if (local_stream) {
        local_stream.then(stream => {
            if (stream) {
                stream.getTracks().forEach(function (track) { track.stop(); });
            }
        });
        local_stream = null;
    }

    // Remove all video players
    document.getElementById("video").innerHTML = "";

    // Reset pause state
    streamPaused = false;
    var pauseBtn = document.getElementById('pause-btn');
    if (pauseBtn) {
        pauseBtn.textContent = '⏸ Pause';
        pauseBtn.classList.remove('btn-secondary');
        pauseBtn.classList.add('btn-primary');
    }

    // Reset mute state
    audioMuted = false;
    var muteBtn = document.getElementById('mute-btn');
    if (muteBtn) {
        muteBtn.textContent = '🔊 Mute';
        muteBtn.classList.remove('btn-warning');
        muteBtn.classList.add('btn-secondary');
    }
}

// Stream pause/resume control
var streamPaused = false;

function toggleStreamPause() {
    var btn = document.getElementById('pause-btn');

    if (!peer_connection) {
        console.log("No peer connection");
        return;
    }

    // Use track.enabled to pause/resume - more reliable for WebRTC
    var receivers = peer_connection.getReceivers();
    if (receivers.length === 0) {
        console.log("No receivers found");
        return;
    }

    streamPaused = !streamPaused;

    receivers.forEach(receiver => {
        if (receiver.track) {
            receiver.track.enabled = !streamPaused;
        }
    });

    if (streamPaused) {
        btn.textContent = '▶ Resume';
        btn.classList.remove('btn-primary');
        btn.classList.add('btn-secondary');
        setStatus("Stream paused");
    } else {
        btn.textContent = '⏸ Pause';
        btn.classList.remove('btn-secondary');
        btn.classList.add('btn-primary');
        setStatus("Stream resumed");
    }
}

// Audio mute state
var audioMuted = false;

function toggleMute() {
    audioMuted = !audioMuted;

    // Mute/unmute audio without affecting video
    var videos = document.querySelectorAll('#video video');
    videos.forEach(v => v.muted = audioMuted);

    var btn = document.getElementById('mute-btn');
    if (audioMuted) {
        btn.textContent = '🔇 Unmute';
        btn.classList.remove('btn-secondary');
        btn.classList.add('btn-warning');
    } else {
        btn.textContent = '🔊 Mute';
        btn.classList.remove('btn-warning');
        btn.classList.add('btn-secondary');
    }
}

function onIncomingSDP(sdp) {
    try {
        // An offer may come in while we are busy processing SRD(answer).
        // In this case, we will be in "stable" by the time the offer is processed
        // so it is safe to chain it on our Operations Chain now.
        const readyForOffer =
            !makingOffer &&
            (peer_connection.signalingState == "stable" || isSettingRemoteAnswerPending);
        const offerCollision = sdp.type == "offer" && !readyForOffer;

        if (offerCollision) {
            return;
        }
        isSettingRemoteAnswerPending = sdp.type == "answer";
        peer_connection.setRemoteDescription(sdp).then(() => {
            setStatus("Remote SDP set");
            isSettingRemoteAnswerPending = false;
            if (sdp.type == "offer") {
                setStatus("Got SDP offer, waiting for getUserMedia to complete");
                local_stream.then((stream) => {
                    setStatus("getUserMedia to completed, setting local description");
                    peer_connection.setLocalDescription().then(() => {
                        let desc = peer_connection.localDescription;
                        console.log("Got local description: " + JSON.stringify(desc));
                        setStatus("Sending SDP " + desc.type);
                        ws_conn.send(JSON.stringify({'sdp': desc}));
                        if (peer_connection.iceConnectionState == "connected") {
                            setStatus("SDP " + desc.type + " sent, ICE connected, all looks OK");
                        }
                    });
                });
            }
        });
    } catch (err) {
        handleIncomingError(err);
    }
}

// Local description was set by incoming SDP offer, send answer to peer
function onLocalDescription(desc) {
    if (desc.type != "answer") {
        console.warn("Expected SDP answer, received: " + desc.type);
    }
    console.log("Got local description: " + JSON.stringify(desc));
    peer_connection.setLocalDescription(desc).then(() => {
        var dsc = peer_connection.localDescription;
        setStatus("Sending SDP " + desc.type);
        ws_conn.send(JSON.stringify({'sdp': desc}));
    });
}

// ICE candidate received from peer, add it to the peer connection
function onIncomingICE(ice) {
    var candidate = new RTCIceCandidate(ice);
    peer_connection.addIceCandidate(candidate).catch(setError);
}

function onServerMessage(event) {
    console.log("Received " + event.data);

    // Handle PEER_DISCONNECTED message
    if (event.data.startsWith("PEER_DISCONNECTED")) {
        var disconnected_id = event.data.split(" ")[1];
        console.log("Peer " + disconnected_id + " disconnected");
        setStatus("Peer disconnected. Ready for new connection.");

        // Clean up current call but stay connected to server
        if (peer_connection) {
            peer_connection.close();
            peer_connection = new RTCPeerConnection(rtc_configuration);
        }
        resetVideo();
        callCreateTriggered = false;
        negotiation_attempts = 0;
        setConnectButtonState("Connect");
        return;
    }

    switch (event.data) {
        case "HELLO":
            setStatus("Registered with server, waiting for call");
            return;
        case "SESSION_OK":
            setStatus("Starting negotiation");
            if (wantRemoteOfferer()) {
                ws_conn.send("OFFER_REQUEST");
                setStatus("Sent OFFER_REQUEST, waiting for offer");
                return;
            }
            if (!callCreateTriggered) {
                createCall();
                setStatus("Created peer connection for call, waiting for SDP");
            }
            return;
        case "OFFER_REQUEST":
            // The peer wants us to set up and then send an offer
            if (!callCreateTriggered)
                createCall();
            return;
        default:
            if (event.data.startsWith("ERROR")) {
                handleIncomingError(event.data);
                return;
            }
            // Handle incoming JSON SDP and ICE messages
            try {
                msg = JSON.parse(event.data);
            } catch (e) {
                if (e instanceof SyntaxError) {
                    handleIncomingError("Error parsing incoming JSON: " + event.data);
                } else {
                    handleIncomingError("Unknown error parsing response: " + event.data);
                }
                return;
            }

            // Incoming JSON signals the beginning of a call
            if (!callCreateTriggered)
                createCall(msg);

            if (msg.sdp != null) {
                onIncomingSDP(msg.sdp);
            } else if (msg.ice != null) {
                onIncomingICE(msg.ice);
            } else {
                handleIncomingError("Unknown incoming JSON: " + msg);
            }
    }
}

function onServerClose(event) {
    setStatus('Disconnected from server');
    resetVideo();

    if (peer_connection) {
        peer_connection.close();
        peer_connection = new RTCPeerConnection(rtc_configuration);
    }
    callCreateTriggered = false;
    negotiation_attempts = 0; // Reset negotiation attempts on disconnect

    // Check if we should auto-switch from SSL to non-SSL
    // Close code 1006 usually indicates connection failed (could be SSL issue)
    var use_ssl = getUseSsl();
    if ((event.code === 1006 || event.code === 0) && use_ssl && !ssl_auto_switched) {
        console.log("Connection closed unexpectedly with SSL, automatically switching to non-SSL mode");
        ssl_auto_switched = true;
        connect_attempts = 0; // Reset attempts when switching protocol
        var checkbox = document.getElementById('use-ssl');
        if (checkbox) {
            checkbox.checked = false;
            onSslChanged();
        }
        // Retry immediately with non-SSL
        window.setTimeout(websocketServerConnect, 500);
        return;
    }
    // Reset after a second
    window.setTimeout(websocketServerConnect, 1000);
}

function onServerError(event) {
    var use_ssl = getUseSsl();
    // If using SSL and haven't tried non-SSL yet, auto-switch
    if (use_ssl && !ssl_auto_switched) {
        console.log("SSL connection error, automatically switching to non-SSL mode");
        ssl_auto_switched = true;
        connect_attempts = 0; // Reset attempts when switching protocol
        var checkbox = document.getElementById('use-ssl');
        if (checkbox) {
            checkbox.checked = false;
            onSslChanged();
        }
        // Retry immediately with non-SSL
        window.setTimeout(websocketServerConnect, 500);
        return;
    }
    // Otherwise show error and retry
    if (use_ssl) {
        setError("Unable to connect to server, did you add an exception for the certificate?")
    } else {
        setError("Unable to connect to server. Is the server running?")
    }
    // Retry after 3 seconds
    window.setTimeout(websocketServerConnect, 3000);
}

function getLocalStream() {
    var constraints;
    var textarea = document.getElementById('constraints');
    try {
        constraints = JSON.parse(textarea.value);
    } catch (e) {
        console.error(e);
        setError('ERROR parsing constraints: ' + e.message + ', using default constraints');
        constraints = default_constraints;
    }
    console.log(JSON.stringify(constraints));

    // Add local stream
    if (navigator.mediaDevices.getUserMedia) {
        return navigator.mediaDevices.getUserMedia(constraints);
    } else {
        errorUserMediaHandler();
    }
}

function websocketServerConnect() {
    connect_attempts++;
    if (connect_attempts > 3) {
        // If using SSL and haven't tried non-SSL yet, auto-switch and retry
        var use_ssl = getUseSsl();
        if (use_ssl && !ssl_auto_switched) {
            console.log("SSL connection failed, automatically switching to non-SSL mode");
            ssl_auto_switched = true;
            connect_attempts = 0; // Reset attempts when switching protocol
            var checkbox = document.getElementById('use-ssl');
            if (checkbox) {
                checkbox.checked = false;
                onSslChanged();
            }
            // Retry immediately with non-SSL
            window.setTimeout(websocketServerConnect, 500);
            return;
        }
        setError("Too many connection attempts, aborting. Refresh page to try again");
        return;
    }
    // Clear errors in the status span
    var span = document.getElementById("status");
    span.classList.remove('error');
    span.textContent = '';
    // Populate constraints
    var textarea = document.getElementById('constraints');
    if (textarea.value == '')
        textarea.value = JSON.stringify(default_constraints);
    // Fetch the peer id to use
    peer_id = default_peer_id || getOurId();

    // Get server settings from UI or use defaults
    var server = getServerAddress() || ws_server;
    var port = getServerPort() || ws_port || '8443';

    // Auto-detect server address if not specified
    if (!server) {
        if (window.location.protocol.startsWith("file")) {
            server = "127.0.0.1";
        } else if (window.location.protocol.startsWith("http")) {
            server = window.location.hostname;
        } else {
            throw new Error("Don't know how to connect to the signalling server with uri " + window.location);
        }
    }

    // Use ws:// or wss:// based on checkbox setting
    var use_ssl = getUseSsl();
    var ws_protocol = use_ssl ? 'wss://' : 'ws://';
    var ws_url = ws_protocol + server + ':' + port
    setStatus("Connecting to server " + ws_url);
    ws_conn = new WebSocket(ws_url);
    /* When connected, immediately register with the server */
    ws_conn.addEventListener('open', (event) => {
        document.getElementById("peer-id").textContent = peer_id;
        ws_conn.send('HELLO ' + peer_id);
        setStatus("Registering with server");
        setConnectButtonState("Connect");
        // Reset connection attempts because we connected successfully
        connect_attempts = 0;
        ssl_auto_switched = false; // Reset auto-switch flag on successful connection
    });
    ws_conn.addEventListener('error', onServerError);
    ws_conn.addEventListener('message', onServerMessage);
    ws_conn.addEventListener('close', onServerClose);
}

function errorUserMediaHandler() {
    setError("Browser doesn't support getUserMedia!");
}

const handleDataChannelOpen = (event) =>{
    console.log("dataChannel.OnOpen", event);
};

const handleDataChannelMessageReceived = (event) =>{
    console.log("dataChannel.OnMessage:", event, event.data.type);

    setStatus("Received data channel message");
    if (typeof event.data === 'string' || event.data instanceof String) {
        console.log('Incoming string message: ' + event.data);
        textarea = document.getElementById("text")
        textarea.value = textarea.value + '\n' + event.data
    } else {
        console.log('Incoming data message');
    }
    send_channel.send("Hi! (from browser)");
};

const handleDataChannelError = (error) =>{
    console.log("dataChannel.OnError:", error);
};

const handleDataChannelClose = (event) =>{
    console.log("dataChannel.OnClose", event);
};

function onDataChannel(event) {
    setStatus("Data channel created");
    let receiveChannel = event.channel;
    receiveChannel.onopen = handleDataChannelOpen;
    receiveChannel.onmessage = handleDataChannelMessageReceived;
    receiveChannel.onerror = handleDataChannelError;
    receiveChannel.onclose = handleDataChannelClose;
}

function createCall() {
    callCreateTriggered = true;
    var mode = getStreamMode();
    console.log('Configuring RTCPeerConnection in ' + mode + ' mode');
    send_channel = peer_connection.createDataChannel('label', null);
    send_channel.onopen = handleDataChannelOpen;
    send_channel.onmessage = handleDataChannelMessageReceived;
    send_channel.onerror = handleDataChannelError;
    send_channel.onclose = handleDataChannelClose;
    peer_connection.ondatachannel = onDataChannel;

    /* Set up ontrack handler only if we're receiving media */
    if (!isSendOnly()) {
        peer_connection.ontrack = ({track, streams}) => {
            console.log("ontrack triggered");
            var videoElem = getVideoElement();
            if (track.kind === 'audio')
                videoElem.style.display = 'none';

            videoElem.srcObject = streams[0];
            videoElem.srcObject.addEventListener('mute', (e) => {
                console.log("track muted, hiding video element");
                videoElem.style.display = 'none';
            });
            videoElem.srcObject.addEventListener('unmute', (e) => {
                console.log("track unmuted, showing video element");
                videoElem.style.display = 'block';
            });
            videoElem.srcObject.addEventListener('removetrack', (e) => {
                console.log("track removed, removing video element");
                videoElem.remove();
            });
        };
    }

    peer_connection.onicecandidate = (event) => {
        // We have a candidate, send it to the remote party with the
        // same uuid
        if (event.candidate == null) {
                console.log("ICE Candidate was null, done");
                return;
        }
        ws_conn.send(JSON.stringify({'ice': event.candidate}));
    };
    peer_connection.oniceconnectionstatechange = (event) => {
        console.log("ICE connection state: " + peer_connection.iceConnectionState);
        if (peer_connection.iceConnectionState == "connected") {
            setStatus("ICE connected, streaming");
            negotiation_attempts = 0; // Reset on successful connection
        } else if (peer_connection.iceConnectionState == "failed") {
            setError("ICE connection failed. Check network or codec compatibility.");
        } else if (peer_connection.iceConnectionState == "disconnected") {
            setStatus("ICE disconnected, attempting to reconnect...");
        }
    };

    /*
     * In recv-only mode, we should let the sender create the offer,
     * because the sender knows what media formats it will send.
     * This is more reliable than trying to guess codec capabilities.
     */
    var shouldBeRemoteOfferer = wantRemoteOfferer() || isRecvOnly();

    // let the "negotiationneeded" event trigger offer generation
    peer_connection.onnegotiationneeded = async () => {
        negotiation_attempts++;
        console.log('Negotiation attempt ' + negotiation_attempts + ' of ' + MAX_NEGOTIATION_ATTEMPTS);

        if (negotiation_attempts > MAX_NEGOTIATION_ATTEMPTS) {
            setError("Too many negotiation attempts (" + negotiation_attempts + "). " +
                     "Possible codec incompatibility. Please refresh and try again.");
            console.error("Max negotiation attempts exceeded, stopping to prevent infinite loop");
            return;
        }

        setStatus("Negotiation needed (attempt " + negotiation_attempts + ")");
        if (shouldBeRemoteOfferer) {
            console.log('Waiting for remote to create offer (recv-only or remote-offerer mode)');
            return;
        }
        try {
            makingOffer = true;
            await peer_connection.setLocalDescription();
            let desc = peer_connection.localDescription;
            setStatus("Sending SDP " + desc.type);
            ws_conn.send(JSON.stringify({'sdp': desc}));
        } catch (err) {
            handleIncomingError(err);
        } finally {
            makingOffer = false;
        }
    };

    /* Handle different stream modes */
    if (isRecvOnly()) {
        /*
         * In recv-only mode, we don't add transceivers here.
         * When we receive the remote offer, the browser will automatically
         * create transceivers based on the offer's media descriptions.
         * We just need to request the remote to send an offer.
         */
        console.log('Recv-only mode: will request remote to create offer');
        local_stream = Promise.resolve(null);
        /* Send OFFER_REQUEST to ask the remote peer to create the offer */
        ws_conn.send("OFFER_REQUEST");
    } else {
        /* Send our video/audio to the other peer (sendrecv or sendonly mode) */
        local_stream = getLocalStream().then((stream) => {
            console.log('Adding local stream');
            for (const track of stream.getTracks()) {
                var transceiver = peer_connection.addTransceiver(track, {
                    streams: [stream],
                    direction: isSendOnly() ? 'sendonly' : 'sendrecv'
                });
                console.log('Added track with direction: ' + transceiver.direction);
            }
            return stream;
        }).catch(setError);
    }

    setConnectButtonState("Disconnect");
}
