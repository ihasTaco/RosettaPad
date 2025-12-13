// =================================================================
// Remap State
// =================================================================
let editingRemapId = null;
let deletingRemapId = null;

// =================================================================
// Remap API
// =================================================================
async function createRemapAPI(profileId, data) {
    try {
        const r = await fetch(`/api/profiles/${profileId}/remaps`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });
        if ((await r.json()).success) await fetchProfiles();
        return true;
    } catch (e) {
        console.error('Failed to create remap:', e);
        return false;
    }
}

async function updateRemapAPI(profileId, remapId, data) {
    try {
        const r = await fetch(`/api/profiles/${profileId}/remaps/${remapId}`, {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });
        if ((await r.json()).success) await fetchProfiles();
        return true;
    } catch (e) {
        console.error('Failed to update remap:', e);
        return false;
    }
}

async function deleteRemapAPI(profileId, remapId) {
    try {
        const r = await fetch(`/api/profiles/${profileId}/remaps/${remapId}`, {
            method: 'DELETE'
        });
        if ((await r.json()).success) await fetchProfiles();
        return true;
    } catch (e) {
        console.error('Failed to delete remap:', e);
        return false;
    }
}

// =================================================================
// Remap List Rendering
// =================================================================
function renderRemapsList() {
    const p = profiles.find(x => x.id === editingProfileId);
    if (!p) return;
    
    const remaps = p.button_remaps || [];
    document.getElementById('remapNavCount').textContent = remaps.length;
    document.getElementById('remapCount').textContent = `${remaps.length} remap${remaps.length !== 1 ? 's' : ''}`;
    
    const container = document.getElementById('remapsContainer');
    
    if (!remaps.length) {
        container.innerHTML = `
            <div class="empty-state-large">
                <svg viewBox="0 0 24 24"><path d="M6.99 11L3 15l3.99 4v-3H14v-2H6.99v-3zM21 9l-3.99-4v3H10v2h7.01v3L21 9z"/></svg>
                <h4>No Button Remaps</h4>
                <p>Create a remap to change what a button does when pressed.</p>
                <div class="empty-actions">
                    <button class="empty-btn primary" onclick="openRemapCreator()">Create Remap</button>
                </div>
            </div>
        `;
        return;
    }
    
    container.innerHTML = `
        <div class="remaps-grid">
            ${remaps.map(r => {
                const fromBtn = BUTTONS[r.from_button] || { symbol: '?', name: r.from_button };
                const toBtn = BUTTONS[r.to_button] || { symbol: '?', name: r.to_button };
                
                return `
                    <div class="remap-card ${!r.enabled ? 'disabled' : ''}" onclick="openEditRemap('${r.id}')">
                        <div class="remap-card-header">
                            <div class="remap-visual">
                                <div class="remap-button from">${fromBtn.symbol}</div>
                                <svg class="remap-arrow" viewBox="0 0 24 24"><path d="M12 4l-1.41 1.41L16.17 11H4v2h12.17l-5.58 5.59L12 20l8-8z"/></svg>
                                <div class="remap-button to">${toBtn.symbol}</div>
                            </div>
                            <div class="remap-card-actions">
                                <button class="icon-btn small" onclick="event.stopPropagation(); toggleRemap('${r.id}')" title="${r.enabled ? 'Disable' : 'Enable'}">
                                    <svg viewBox="0 0 24 24">
                                        ${r.enabled 
                                            ? '<path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-2 15l-5-5 1.41-1.41L10 14.17l7.59-7.59L19 8l-9 9z"/>'
                                            : '<path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 18c-4.42 0-8-3.58-8-8s3.58-8 8-8 8 3.58 8 8-3.58 8-8 8z"/>'
                                        }
                                    </svg>
                                </button>
                                <button class="icon-btn small danger" onclick="event.stopPropagation(); openDeleteRemapModal('${r.id}')" title="Delete">
                                    <svg viewBox="0 0 24 24"><path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>
                                </button>
                            </div>
                        </div>
                        <div class="remap-card-body">
                            <div class="remap-description">
                                <span class="remap-from-name">${fromBtn.name}</span>
                                <span class="remap-arrow-text">→</span>
                                <span class="remap-to-name">${toBtn.name}</span>
                            </div>
                            ${r.bidirectional ? '<span class="remap-tag">Bidirectional</span>' : ''}
                        </div>
                    </div>
                `;
            }).join('')}
        </div>
    `;
}

// =================================================================
// Remap Actions
// =================================================================
async function toggleRemap(id) {
    const p = profiles.find(x => x.id === editingProfileId);
    if (!p) return;
    
    const r = p.button_remaps.find(x => x.id === id);
    if (!r) return;
    
    await updateRemapAPI(editingProfileId, id, { ...r, enabled: !r.enabled });
    renderRemapsList();
}

function openEditRemap(id) {
    const p = profiles.find(x => x.id === editingProfileId);
    if (!p) return;
    
    const r = p.button_remaps.find(x => x.id === id);
    if (!r) return;
    
    editingRemapId = id;
    
    // Set form values
    document.getElementById('remapFromButton').value = r.from_button;
    document.getElementById('remapToButton').value = r.to_button;
    document.getElementById('remapBidirectional').checked = r.bidirectional || false;
    
    // Update visual
    updateRemapPreview();
    
    document.getElementById('remapCreatorTitle').textContent = 'Edit Remap';
    openRemapCreatorView();
}

// =================================================================
// Delete Remap Modal
// =================================================================
function openDeleteRemapModal(id) {
    deletingRemapId = id;
    const p = profiles.find(x => x.id === editingProfileId);
    const r = p?.button_remaps?.find(x => x.id === id);
    if (r) {
        const fromBtn = BUTTONS[r.from_button] || { name: r.from_button };
        const toBtn = BUTTONS[r.to_button] || { name: r.to_button };
        document.getElementById('deleteRemapDesc').textContent = `${fromBtn.name} → ${toBtn.name}`;
    }
    document.getElementById('deleteRemapModal').classList.add('active');
}

function closeDeleteRemapModal() {
    document.getElementById('deleteRemapModal').classList.remove('active');
    deletingRemapId = null;
}

async function confirmDeleteRemap() {
    if (!deletingRemapId || !editingProfileId) return;
    
    await deleteRemapAPI(editingProfileId, deletingRemapId);
    closeDeleteRemapModal();
    renderRemapsList();
}

// =================================================================
// Remap Creator
// =================================================================
function openRemapCreator() {
    editingRemapId = null;
    resetRemapForm();
    document.getElementById('remapCreatorTitle').textContent = 'Create Remap';
    openRemapCreatorView();
}

function openRemapCreatorView() {
    document.getElementById('profileEditorView').style.display = 'none';
    document.getElementById('remapCreatorView').style.display = 'block';
}

function closeRemapCreator() {
    document.getElementById('remapCreatorView').style.display = 'none';
    document.getElementById('profileEditorView').style.display = 'block';
    resetRemapForm();
}

function resetRemapForm() {
    document.getElementById('remapFromButton').value = '';
    document.getElementById('remapToButton').value = '';
    document.getElementById('remapBidirectional').checked = false;
    updateRemapPreview();
}

function selectRemapFrom(btn) {
    document.getElementById('remapFromButton').value = btn;
    
    // Update button grid selection
    document.querySelectorAll('#remapFromGrid .button-option').forEach(b => {
        b.classList.toggle('selected', b.dataset.button === btn);
    });
    
    updateRemapPreview();
}

function selectRemapTo(btn) {
    document.getElementById('remapToButton').value = btn;
    
    // Update button grid selection
    document.querySelectorAll('#remapToGrid .button-option').forEach(b => {
        b.classList.toggle('selected', b.dataset.button === btn);
    });
    
    updateRemapPreview();
}

function updateRemapPreview() {
    const fromVal = document.getElementById('remapFromButton').value;
    const toVal = document.getElementById('remapToButton').value;
    const bidir = document.getElementById('remapBidirectional').checked;
    
    const fromBtn = BUTTONS[fromVal] || { symbol: '?', name: 'Select' };
    const toBtn = BUTTONS[toVal] || { symbol: '?', name: 'Select' };
    
    document.getElementById('previewFromSymbol').textContent = fromBtn.symbol;
    document.getElementById('previewFromName').textContent = fromBtn.name;
    document.getElementById('previewToSymbol').textContent = toBtn.symbol;
    document.getElementById('previewToName').textContent = toBtn.name;
    
    // Show bidirectional indicator
    const arrowEl = document.getElementById('previewArrow');
    if (bidir) {
        arrowEl.innerHTML = '<path d="M6.99 11L3 15l3.99 4v-3H14v-2H6.99v-3zM21 9l-3.99-4v3H10v2h7.01v3L21 9z"/>';
    } else {
        arrowEl.innerHTML = '<path d="M12 4l-1.41 1.41L16.17 11H4v2h12.17l-5.58 5.59L12 20l8-8z"/>';
    }
}

async function saveRemap() {
    const fromButton = document.getElementById('remapFromButton').value;
    const toButton = document.getElementById('remapToButton').value;
    const bidirectional = document.getElementById('remapBidirectional').checked;
    
    if (!fromButton) {
        alert('Please select a button to remap from');
        return;
    }
    
    if (!toButton) {
        alert('Please select a button to remap to');
        return;
    }
    
    if (fromButton === toButton) {
        alert('Cannot remap a button to itself');
        return;
    }
    
    const data = {
        from_button: fromButton,
        to_button: toButton,
        bidirectional,
        enabled: true
    };
    
    if (editingRemapId) {
        await updateRemapAPI(editingProfileId, editingRemapId, data);
    } else {
        await createRemapAPI(editingProfileId, data);
    }
    
    closeRemapCreator();
    renderRemapsList();
}

// =================================================================
// Swap Buttons Helper
// =================================================================
function swapRemapButtons() {
    const fromVal = document.getElementById('remapFromButton').value;
    const toVal = document.getElementById('remapToButton').value;
    
    document.getElementById('remapFromButton').value = toVal;
    document.getElementById('remapToButton').value = fromVal;
    
    // Update grid selections
    document.querySelectorAll('#remapFromGrid .button-option').forEach(b => {
        b.classList.toggle('selected', b.dataset.button === toVal);
    });
    document.querySelectorAll('#remapToGrid .button-option').forEach(b => {
        b.classList.toggle('selected', b.dataset.button === fromVal);
    });
    
    updateRemapPreview();
}