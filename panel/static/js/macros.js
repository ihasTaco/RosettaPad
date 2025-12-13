// =================================================================
// Macro List Rendering
// =================================================================
function renderMacrosList() {
    const p = profiles.find(x => x.id === editingProfileId);
    if (!p) return;
    
    const macros = p.macros || [];
    document.getElementById('macroNavCount').textContent = macros.length;
    document.getElementById('macroCount').textContent = `${macros.length} macro${macros.length !== 1 ? 's' : ''}`;
    
    const container = document.getElementById('macrosContainer');
    
    if (!macros.length) {
        container.innerHTML = `
            <div class="empty-state-large">
                <svg viewBox="0 0 24 24"><path d="M17 4h3v16h-3V4zM5 14h3v6H5v-6zm6-5h3v11h-3V9z"/></svg>
                <h4>No Macros Yet</h4>
                <p>Create a new macro or add an existing global macro.</p>
                <div class="empty-actions">
                    <button class="empty-btn" onclick="openGlobalMacroSelector()">Add Global</button>
                    <button class="empty-btn primary" onclick="openMacroCreator()">Create Macro</button>
                </div>
            </div>
        `;
        return;
    }
    
    container.innerHTML = `
        <div class="macros-grid">
            ${macros.map(m => {
                const btn = BUTTONS[m.trigger_button] || { symbol: '?', name: m.trigger_button };
                const type = MACRO_TYPES[getMacroType(m)] || 'Custom';
                const mode = ACTIVATION_MODES[m.trigger_mode] || m.trigger_mode;
                
                return `
                    <div class="macro-card ${!m.enabled ? 'disabled' : ''}" onclick="openEditMacro('${m.id}')">
                        <div class="macro-card-header">
                            <div class="macro-trigger-badge">${btn.symbol}</div>
                            <div class="macro-card-actions">
                                <button class="icon-btn small" onclick="event.stopPropagation(); toggleMacro('${m.id}')" title="${m.enabled ? 'Disable' : 'Enable'}">
                                    <svg viewBox="0 0 24 24">
                                        ${m.enabled 
                                            ? '<path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-2 15l-5-5 1.41-1.41L10 14.17l7.59-7.59L19 8l-9 9z"/>'
                                            : '<path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 18c-4.42 0-8-3.58-8-8s3.58-8 8-8 8 3.58 8 8-3.58 8-8 8z"/>'
                                        }
                                    </svg>
                                </button>
                                <button class="icon-btn small danger" onclick="event.stopPropagation(); openDeleteMacroModal('${m.id}', '${esc(m.name)}')" title="Delete">
                                    <svg viewBox="0 0 24 24"><path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>
                                </button>
                            </div>
                        </div>
                        <div class="macro-card-body">
                            <div class="macro-card-name">${esc(m.name)}</div>
                            <div class="macro-card-meta">
                                <span class="macro-tag type">${type}</span>
                                <span class="macro-tag mode">${mode}</span>
                            </div>
                        </div>
                    </div>
                `;
            }).join('')}
        </div>
    `;
}

function getMacroType(m) {
    if (!m.actions?.length) return 'custom';
    const t = m.actions[0].type;
    if (t === 'rapid_fire' || t === 'toggle' || t === 'turbo') return t;
    if (m.actions.length > 1) return 'sequence';
    return 'custom';
}

// =================================================================
// Macro Actions
// =================================================================
async function toggleMacro(id) {
    const p = profiles.find(x => x.id === editingProfileId);
    if (!p) return;
    
    const m = p.macros.find(x => x.id === id);
    if (!m) return;
    
    await updateMacroAPI(editingProfileId, id, { ...m, enabled: !m.enabled });
    renderMacrosList();
}

function openEditMacro(id) {
    const p = profiles.find(x => x.id === editingProfileId);
    if (!p) return;
    
    const m = p.macros.find(x => x.id === id);
    if (!m) return;
    
    editingMacroId = id;
    
    // Populate macroData from existing macro
    macroData.type = getMacroType(m);
    macroData.triggerButton = m.trigger_button;
    macroData.activationMode = m.trigger_mode;
    macroData.name = m.name;
    macroData.description = m.description || '';
    
    if (m.actions?.length) {
        const a = m.actions[0];
        macroData.targetButton = a.button || '';
        macroData.rate = a.duration_ms ? Math.round(1000 / a.duration_ms) : 10;
        if (macroData.type === 'sequence') {
            macroData.sequenceSteps = m.actions.map(x => ({ ...x }));
        }
    }
    
    document.getElementById('macroCreatorTitle').textContent = 'Edit Macro';
    openMacroCreatorView();
}

// =================================================================
// Delete Macro Modal
// =================================================================
function openDeleteMacroModal(id, name) {
    deletingMacroId = id;
    document.getElementById('deleteMacroName').textContent = name;
    document.getElementById('deleteMacroModal').classList.add('active');
}

function closeDeleteMacroModal() {
    document.getElementById('deleteMacroModal').classList.remove('active');
    deletingMacroId = null;
}

async function confirmDeleteMacro() {
    if (!deletingMacroId || !editingProfileId) return;
    
    await deleteMacroAPI(editingProfileId, deletingMacroId);
    closeDeleteMacroModal();
    renderMacrosList();
}

// =================================================================
// Global Macro Selector
// =================================================================
function openGlobalMacroSelector() {
    const list = document.getElementById('globalMacrosList');
    
    if (!globalMacros.length) {
        list.innerHTML = '<div class="empty-state">No global macros available. Create a macro and check "Make Global" to save it.</div>';
    } else {
        list.innerHTML = globalMacros.map(m => `
            <div class="global-macro-item" onclick="addGlobalMacro('${m.id}')">
                <div class="global-macro-name">${esc(m.name)}</div>
                <div class="global-macro-desc">${esc(m.description || '')}</div>
            </div>
        `).join('');
    }
    
    document.getElementById('globalMacroModal').classList.add('active');
}

function closeGlobalMacroSelector() {
    document.getElementById('globalMacroModal').classList.remove('active');
}

async function addGlobalMacro(id) {
    const m = globalMacros.find(x => x.id === id);
    if (m && editingProfileId) {
        await createMacroAPI(editingProfileId, { ...m, id: undefined });
        closeGlobalMacroSelector();
        renderMacrosList();
    }
}