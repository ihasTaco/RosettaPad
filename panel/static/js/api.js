// =================================================================
// Bluetooth API
// =================================================================
async function fetchStatus() {
    try {
        const r = await fetch('/api/status');
        updateControllerUI(await r.json());
    } catch (e) {
        console.error('Failed to fetch status:', e);
    }
}

async function startScan() {
    try {
        await fetch('/api/scan/start', { method: 'POST' });
    } catch (e) {
        console.error('Failed to start scan:', e);
    }
}

async function stopScan() {
    try {
        await fetch('/api/scan/stop', { method: 'POST' });
    } catch (e) {
        console.error('Failed to stop scan:', e);
    }
}

async function pairDevice(address) {
    try {
        await fetch('/api/pair', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ address })
        });
    } catch (e) {
        console.error('Failed to pair device:', e);
    }
}

async function connectDevice(address) {
    try {
        await fetch('/api/connect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ address })
        });
    } catch (e) {
        console.error('Failed to connect device:', e);
    }
}

async function disconnectDevice(address) {
    try {
        await fetch('/api/disconnect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ address })
        });
    } catch (e) {
        console.error('Failed to disconnect device:', e);
    }
}

async function forgetDevice(address) {
    try {
        await fetch('/api/forget', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ address })
        });
    } catch (e) {
        console.error('Failed to forget device:', e);
    }
}

async function renameDevice(address, name) {
    try {
        await fetch('/api/rename', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ address, name })
        });
    } catch (e) {
        console.error('Failed to rename device:', e);
    }
}

// =================================================================
// Profile API
// =================================================================
async function fetchProfiles() {
    try {
        console.log('Fetching profiles...');
        const r = await fetch('/api/profiles');
        const d = await r.json();
        console.log('Profiles response:', d);
        profiles = d.profiles;
        activeProfileId = d.active_profile_id;
        console.log('Loaded profiles:', profiles.length, 'active:', activeProfileId);
        renderProfiles();
    } catch (e) {
        console.error('Failed to fetch profiles:', e);
    }
}

async function activateProfile(id) {
    try {
        await fetch(`/api/profiles/${id}/activate`, { method: 'POST' });
        activeProfileId = id;
        renderProfiles();
    } catch (e) {
        console.error('Failed to activate profile:', e);
    }
}

async function createProfileAPI(name, desc) {
    try {
        const r = await fetch('/api/profiles', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ name, description: desc })
        });
        if ((await r.json()).success) await fetchProfiles();
    } catch (e) {
        console.error('Failed to create profile:', e);
    }
}

async function updateProfileAPI(id, name, desc) {
    try {
        const r = await fetch(`/api/profiles/${id}`, {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ name, description: desc })
        });
        if ((await r.json()).success) await fetchProfiles();
    } catch (e) {
        console.error('Failed to update profile:', e);
    }
}

async function deleteProfileAPI(id) {
    try {
        const r = await fetch(`/api/profiles/${id}`, { method: 'DELETE' });
        if ((await r.json()).success) await fetchProfiles();
    } catch (e) {
        console.error('Failed to delete profile:', e);
    }
}

async function duplicateProfileAPI(id, name) {
    try {
        const r = await fetch(`/api/profiles/${id}/duplicate`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ name })
        });
        if ((await r.json()).success) await fetchProfiles();
    } catch (e) {
        console.error('Failed to duplicate profile:', e);
    }
}

// =================================================================
// Macro API
// =================================================================
async function createMacroAPI(profileId, data) {
    try {
        const r = await fetch(`/api/profiles/${profileId}/macros`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });
        if ((await r.json()).success) await fetchProfiles();
        return true;
    } catch (e) {
        console.error('Failed to create macro:', e);
        return false;
    }
}

async function updateMacroAPI(profileId, macroId, data) {
    try {
        const r = await fetch(`/api/profiles/${profileId}/macros/${macroId}`, {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });
        if ((await r.json()).success) await fetchProfiles();
        return true;
    } catch (e) {
        console.error('Failed to update macro:', e);
        return false;
    }
}

async function deleteMacroAPI(profileId, macroId) {
    try {
        const r = await fetch(`/api/profiles/${profileId}/macros/${macroId}`, {
            method: 'DELETE'
        });
        if ((await r.json()).success) await fetchProfiles();
        return true;
    } catch (e) {
        console.error('Failed to delete macro:', e);
        return false;
    }
}