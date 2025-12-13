// =================================================================
// Profile List Rendering
// =================================================================
function renderProfiles() {
    const grid = document.getElementById('profilesGrid');
    console.log('renderProfiles called, grid element:', grid, 'profiles:', profiles);
    
    if (!grid) {
        console.error('profilesGrid element not found!');
        return;
    }
    
    if (!profiles || !profiles.length) {
        grid.innerHTML = '<div class="empty-state">No profiles yet. Create one to get started!</div>';
        return;
    }
    
    grid.innerHTML = profiles.map(p => {
        const isActive = p.id === activeProfileId;
        const macroCount = p.macros?.length || 0;
        const remapCount = p.button_remaps?.length || 0;
        
        return `
            <div class="profile-card ${isActive ? 'active' : ''}" onclick="activateProfile('${p.id}')">
                <div class="profile-actions">
                    <button class="profile-action-btn" onclick="event.stopPropagation(); openProfileEditor('${p.id}')" title="Edit">
                        <svg viewBox="0 0 24 24"><path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zM20.71 7.04c.39-.39.39-1.02 0-1.41l-2.34-2.34c-.39-.39-1.02-.39-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/></svg>
                    </button>
                    <button class="profile-action-btn" onclick="event.stopPropagation(); duplicateProfile('${p.id}')" title="Duplicate">
                        <svg viewBox="0 0 24 24"><path d="M16 1H4c-1.1 0-2 .9-2 2v14h2V3h12V1zm3 4H8c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h11c1.1 0 2-.9 2-2V7c0-1.1-.9-2-2-2zm0 16H8V7h11v14z"/></svg>
                    </button>
                    ${!p.is_default ? `
                        <button class="profile-action-btn danger" onclick="event.stopPropagation(); openDeleteProfileModal('${p.id}')" title="Delete">
                            <svg viewBox="0 0 24 24"><path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>
                        </button>
                    ` : ''}
                </div>
                <div class="profile-header">
                    <div class="profile-icon">${p.name.charAt(0).toUpperCase()}</div>
                    ${isActive ? '<span class="profile-badge active">Active</span>' : ''}
                    ${p.is_default && !isActive ? '<span class="profile-badge default">Default</span>' : ''}
                </div>
                <div class="profile-name">${esc(p.name)}</div>
                <div class="profile-description">${esc(p.description) || 'No description'}</div>
                <div class="profile-stats">
                    <div class="profile-stat">
                        <svg viewBox="0 0 24 24"><path d="M17 4h3v16h-3V4zM5 14h3v6H5v-6zm6-5h3v11h-3V9z"/></svg>
                        <span>${macroCount} macros</span>
                    </div>
                    <div class="profile-stat">
                        <svg viewBox="0 0 24 24"><path d="M6.99 11L3 15l3.99 4v-3H14v-2H6.99v-3zM21 9l-3.99-4v3H10v2h7.01v3L21 9z"/></svg>
                        <span>${remapCount} remaps</span>
                    </div>
                </div>
            </div>
        `;
    }).join('');
}

// =================================================================
// Profile Editor
// =================================================================
function openProfileEditor(id) {
    const p = profiles.find(x => x.id === id);
    if (!p) return;
    
    editingProfileId = id;
    document.getElementById('mainView').style.display = 'none';
    document.getElementById('profileEditorView').style.display = 'block';
    document.getElementById('editorProfileName').textContent = p.name;
    document.getElementById('editorProfileId').textContent = `ID: ${id}`;
    document.getElementById('editorNameInput').value = p.name;
    document.getElementById('editorDescInput').value = p.description || '';
    document.getElementById('dangerZone').style.display = p.is_default ? 'none' : 'block';
    
    switchEditorSection('macros');
    renderMacrosList();
    renderRemapsList();
}

function closeProfileEditor() {
    document.getElementById('profileEditorView').style.display = 'none';
    document.getElementById('mainView').style.display = 'block';
    editingProfileId = null;
}

async function saveProfileChanges() {
    if (!editingProfileId) return;
    
    const name = document.getElementById('editorNameInput').value.trim();
    const desc = document.getElementById('editorDescInput').value.trim();
    
    if (!name) {
        document.getElementById('editorNameInput').focus();
        return;
    }
    
    await updateProfileAPI(editingProfileId, name, desc);
    
    const p = profiles.find(x => x.id === editingProfileId);
    if (p) {
        document.getElementById('editorProfileName').textContent = p.name;
    }
}

function switchEditorSection(section) {
    document.querySelectorAll('.sidebar-nav-item').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.section === section);
    });
    document.querySelectorAll('.editor-section').forEach(sec => {
        sec.classList.toggle('active', sec.id === `section-${section}`);
    });
}

// =================================================================
// Profile Modals
// =================================================================
function openCreateProfileModal() {
    document.getElementById('profileNameInput').value = '';
    document.getElementById('profileDescInput').value = '';
    document.getElementById('createProfileModal').classList.add('active');
    document.getElementById('profileNameInput').focus();
}

function closeCreateProfileModal() {
    document.getElementById('createProfileModal').classList.remove('active');
}

async function createProfile() {
    const name = document.getElementById('profileNameInput').value.trim();
    const desc = document.getElementById('profileDescInput').value.trim();
    
    if (!name) {
        document.getElementById('profileNameInput').focus();
        return;
    }
    
    await createProfileAPI(name, desc);
    closeCreateProfileModal();
}

function openDeleteProfileModal(id) {
    const p = profiles.find(x => x.id === id);
    if (!p) return;
    
    deletingProfileId = id;
    document.getElementById('deleteProfileName').textContent = p.name;
    document.getElementById('deleteProfileModal').classList.add('active');
}

function closeDeleteProfileModal() {
    document.getElementById('deleteProfileModal').classList.remove('active');
    deletingProfileId = null;
}

async function confirmDeleteProfile() {
    if (!deletingProfileId) return;
    
    await deleteProfileAPI(deletingProfileId);
    closeDeleteProfileModal();
    
    if (editingProfileId === deletingProfileId) {
        closeProfileEditor();
    }
}

async function duplicateProfile(id) {
    const p = profiles.find(x => x.id === id);
    if (p) {
        await duplicateProfileAPI(id, `${p.name} (Copy)`);
    }
}