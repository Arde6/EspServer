document.addEventListener('DOMContentLoaded', () => {
    // --- Live Webpage Uptime Counter ---
    // const uptimeDisplay = document.getElementById('uptime');
    // if (uptimeDisplay) {
    //     let seconds = 0;
    //     setInterval(() => {
    //         seconds++;
    //         const mins = Math.floor(seconds / 60);
    //         const secs = seconds % 60;
    //         uptimeDisplay.textContent = `${mins.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
    //     }, 1000);
    // }

    // // --- Interactive Theme Switcher ---
    // const toggleBtn = document.getElementById('theme-btn');
    // if (toggleBtn) {
    //     toggleBtn.addEventListener('click', () => {
    //         document.body.classList.toggle('light-mode');

    //         if (document.body.classList.contains('light-mode')) {
    //             toggleBtn.textContent = "Switch to Dark Mode";
    //         } else {
    //             toggleBtn.textContent = "Switch to Light Mode";
    //         }
    //     });
    // }

    // --- Chat Room Logic ---
    const chatWindow = document.getElementById('chat-window');
    if (chatWindow) {
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
            const data = JSON.parse(event.data);

            if (Array.isArray(data)) {
                chatWindow.innerHTML = '';
                data.forEach(msg => displayMessage(msg.user, msg.text));
            } else {
                displayMessage(data.user, data.text);
            }
        }

        function displayMessage(user, text) {
            const msgDiv = document.createElement('div');
            msgDiv.innerHTML = `<strong>${user}:</strong> ${text}`;
            chatWindow.appendChild(msgDiv);
            chatWindow.scrollTop = chatWindow.scrollHeight;
        }

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
    }

    // --- Fetch topbar for pages ---
    fetch('/topbar.html')
        .then(response => response.text())
        .then(data => {
            document.getElementById('topbar-placeholder').innerHTML = data;
        });
});