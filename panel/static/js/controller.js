// =================================================================
// Controller UI
// =================================================================
function updateControllerUI(data) {
    const { connection, trusted_devices, discovered_devices, state } = data;
    const pairBtn = document.getElementById('pairBtn');
    const connectionStatus = document.getElementById('connectionStatus');
    
    // Connection status
    if (connection.connected && connection.controller) {
        pairBtn.style.display = 'none';
        connectionStatus.style.display = 'flex';
        document.getElementById('statusName').textContent = connection.controller.display_name;
        document.getElementById('statusLatency').textContent = connection.latency_ms.toFixed(1);
        connectedAddress = connection.controller.address;
        document.getElementById('connectedSection').style.display = 'block';
        document.getElementById('connectedName').textContent = connection.controller.display_name;
        document.getElementById('connectedLatency').textContent = connection.latency_ms.toFixed(1) + 'ms';
    } else {
        pairBtn.style.display = 'flex';
        connectionStatus.style.display = 'none';
        connectedAddress = null;
        document.getElementById('connectedSection').style.display = 'none';
    }
    
    // Trusted devices list
    const trustedList = document.getElementById('trustedList');
    if (trusted_devices.length > 0) {
        trustedList.innerHTML = trusted_devices.map(d => `
            <div class="trusted-device ${d.connected ? 'connected' : ''}">
                <div class="trusted-icon">
                    <svg viewBox="0 0 24 24">
                        <path d="M17.71 7.71L12 2h-1v7.59L6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 11 14.41V22h1l5.71-5.71-4.3-4.29 4.3-4.29zM13 5.83l1.88 1.88L13 9.59V5.83zm1.88 10.46L13 18.17v-3.76l1.88 1.88z"/>
                    </svg>
                </div>
                <div class="trusted-info">
                    <div class="trusted-name">${esc(d.name)}</div>
                    <div class="trusted-address">${d.address}</div>
                </div>
                <div class="trusted-actions">
                    ${!d.connected ? `<button class="small-btn connect" onclick="connectDevice('${d.address}')">Connect</button>` : ''}
                    <button class="small-btn" onclick="openRenameModalFor('${d.address}', '${esc(d.name)}')">Rename</button>
                    <button class="small-btn" onclick="forgetDevice('${d.address}')">Forget</button>
                </div>
            </div>
        `).join('');
    } else {
        trustedList.innerHTML = '<div class="empty-state">No trusted controllers</div>';
    }
    
    // Scan state
    isScanning = state === 'scanning';
    const scanBtn = document.getElementById('scanBtn');
    scanBtn.classList.toggle('scanning', isScanning);
    document.getElementById('scanBtnText').textContent = isScanning ? 'Stop' : 'Scan';
    
    // Discovered devices
    const discoveredList = document.getElementById('discoveredList');
    if (discovered_devices.length > 0) {
        discoveredList.style.display = 'flex';
        discoveredList.innerHTML = discovered_devices.map(d => `
            <div class="discovered-device ${!d.paired ? 'new' : ''}">
                <div class="discovered-icon">
                    <svg viewBox="0 0 24 24">
                        <path d="M17.71 7.71L12 2h-1v7.59L6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 11 14.41V22h1l5.71-5.71-4.3-4.29 4.3-4.29zM13 5.83l1.88 1.88L13 9.59V5.83zm1.88 10.46L13 18.17v-3.76l1.88 1.88z"/>
                    </svg>
                </div>
                <div class="discovered-info">
                    <div class="discovered-name">${esc(d.name)}</div>
                    <div class="discovered-address">${d.address}</div>
                </div>
                <button class="pair-device-btn" onclick="${d.paired ? 'connectDevice' : 'pairDevice'}('${d.address}')">
                    ${d.paired ? 'Connect' : 'Pair'}
                </button>
            </div>
        `).join('');
    } else {
        discoveredList.style.display = isScanning ? 'flex' : 'none';
        if (isScanning) {
            discoveredList.innerHTML = '<div class="empty-state">Searching...</div>';
        }
    }
}

// =================================================================
// Controller Menu Functions
// =================================================================
function toggleMenu() {
    menuOpen = !menuOpen;
    document.getElementById('controllerArea').classList.toggle('menu-open', menuOpen);
}

function closeMenu() {
    menuOpen = false;
    document.getElementById('controllerArea').classList.remove('menu-open');
}

function toggleScan() {
    isScanning ? stopScan() : startScan();
}

function disconnectCurrent() {
    if (connectedAddress) {
        disconnectDevice(connectedAddress);
    }
}

// =================================================================
// Rename Modal
// =================================================================
function openRenameModal() {
    if (connectedAddress) {
        renameAddress = connectedAddress;
        document.getElementById('renameInput').value = '';
        document.getElementById('renameModal').classList.add('active');
        document.getElementById('renameInput').focus();
    }
}

function openRenameModalFor(address, name) {
    renameAddress = address;
    document.getElementById('renameInput').value = '';
    document.getElementById('renameInput').placeholder = name;
    document.getElementById('renameModal').classList.add('active');
    document.getElementById('renameInput').focus();
}

function closeRenameModal() {
    document.getElementById('renameModal').classList.remove('active');
    renameAddress = null;
}

function saveRename() {
    if (renameAddress) {
        renameDevice(renameAddress, document.getElementById('renameInput').value.trim());
        closeRenameModal();
    }
}