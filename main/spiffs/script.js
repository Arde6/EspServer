document.addEventListener('DOMContentLoaded', () => {
    console.log("ESP32 Web Dashboard Loaded Successfully!");

    const toggleBtn = document.getElementById('theme-btn');
    const uptimeDisplay = document.getElementById('uptime');
    let seconds = 0;

    // 1. Live Webpage Uptime Counter 
    setInterval(() => {
        seconds++;
        const mins = Math.floor(seconds / 60);
        const secs = seconds % 60;
        uptimeDisplay.textContent = `${mins.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
    }, 1000);

    // 2. Interactive Theme Switcher
    toggleBtn.addEventListener('click', () => {
        document.body.classList.toggle('light-mode');
        
        if (document.body.classList.contains('light-mode')) {
            toggleBtn.textContent = "Switch to Dark Mode";
        } else {
            toggleBtn.textContent = "Switch to Light Mode";
        }
    });

    // 3. Chat room stuff
    const gateway = `ws://${window.location.hostname}/ws`;
    let websocket;

    function initWebSocket() {
        console.log('Trying to open a WebSocket connection...');
        websocket = new WebSocket(gateway);
        websocket.onopen = onOpen;
        websocket.onclose = onClose;
        websocket.onmessage = onMessage;
    }

    function onOpen(event) { console.log('Connection opened'); }
    function onClose(event) { setTimeout(initWebSocket, 2000); }

    function onMessage(event) {
        const chatWindow = document.getElementById('chat-window');
        const data = JSON.parse(event.data);

        if (Array.isArray(data)) {
            chatWindow.innerHTML = '';
            data.forEach(msg => displayMessage(msg.user, msg.text));
        } else {
            displayMessage(data.user, data.text);
        }
    }

    // Helper to display messages in the window
    function displayMessage(user, text) {
        const chatWindow = document.getElementById('chat-window');
        const msgDiv = document.createElement('div');
        msgDiv.innerHTML = `<strong>${user}:</strong> ${text}`;
        chatWindow.appendChild(msgDiv);
        chatWindow.scrollTop = chatWindow.scrollHeight; 
    }

    // This handles sending outbound data packets
    function sendMessage() {
        const userEl = document.getElementById('username');
        const msgEl = document.getElementById('message');
        const user = userEl.value.trim() || "Anonymous";
        const text = msgEl.value.trim();

        if (text === "") return;

        const packet = { user: user, text: text };
        websocket.send(JSON.stringify(packet));
        msgEl.value = '';
    }

    document.getElementById('send-btn').addEventListener('click', sendMessage);
    document.getElementById('message').addEventListener('keypress', (e) => {
        if (e.key === 'Enter') sendMessage();
    });

    window.addEventListener('load', initWebSocket);
});