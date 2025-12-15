// =================================================================
// Lightbar State
// =================================================================
let lightbarConfig = {
    mode: 'static',
    color: { r: 0, g: 0, b: 255 },
    brightness: 1.0,
    breathing_speed_ms: 2000,
    breathing_min_brightness: 0.1,
    breathing_color2: null,
    rainbow_speed_ms: 3000,
    rainbow_saturation: 1.0,
    wave_speed_ms: 2000,
    wave_colors: [],
    custom_animation_id: null,
    player_leds: 0,
    player_led_brightness: 1.0
};

let lightbarAnimations = [];
let selectedAnimationId = null;

const COLOR_PRESETS = {
    red: { r: 255, g: 0, b: 0 },
    green: { r: 0, g: 255, b: 0 },
    blue: { r: 0, g: 0, b: 255 },
    cyan: { r: 0, g: 255, b: 255 },
    magenta: { r: 255, g: 0, b: 255 },
    yellow: { r: 255, g: 255, b: 0 },
    orange: { r: 255, g: 128, b: 0 },
    purple: { r: 128, g: 0, b: 255 },
    pink: { r: 255, g: 105, b: 180 },
    white: { r: 255, g: 255, b: 255 },
    ps_blue: { r: 0, g: 48, b: 135 },
    ps_light: { r: 0, g: 195, b: 227 }
};

// =================================================================
// Utility Functions
// =================================================================
function rgbToHex(r, g, b) {
    return '#' + [r, g, b].map(x => x.toString(16).padStart(2, '0')).join('');
}

function hexToRgb(hex) {
    const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
    return result ? {
        r: parseInt(result[1], 16),
        g: parseInt(result[2], 16),
        b: parseInt(result[3], 16)
    } : null;
}

// =================================================================
// Lightbar API
// =================================================================
async function fetchLightbarState() {
    try {
        const r = await fetch('/api/lightbar');
        const data = await r.json();
        lightbarConfig = data.config;
        updateLightbarUI();
    } catch (e) {
        console.error('Failed to fetch lightbar state:', e);
    }
}

async function setLightbarColor(r, g, b, brightness = null) {
    try {
        const body = { r, g, b };
        if (brightness !== null) body.brightness = brightness;
        
        await fetch('/api/lightbar/color', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body)
        });
        
        lightbarConfig.color = { r, g, b };
        lightbarConfig.mode = 'static';
        updateLightbarPreview();
    } catch (e) {
        console.error('Failed to set lightbar color:', e);
    }
}

async function setLightbarMode(mode, params = {}) {
    try {
        const body = { mode, ...params };
        
        await fetch('/api/lightbar/mode', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body)
        });
        
        lightbarConfig.mode = mode;
        Object.assign(lightbarConfig, params);
        updateLightbarUI();
    } catch (e) {
        console.error('Failed to set lightbar mode:', e);
    }
}

async function setLightbarBrightness(brightness) {
    try {
        await fetch('/api/lightbar/mode', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ 
                mode: lightbarConfig.mode,
                brightness 
            })
        });
        
        lightbarConfig.brightness = brightness;
        updateLightbarPreview();
    } catch (e) {
        console.error('Failed to set brightness:', e);
    }
}

async function turnOffLightbar() {
    try {
        await fetch('/api/lightbar/off', { method: 'POST' });
        lightbarConfig.mode = 'off';
        updateLightbarUI();
    } catch (e) {
        console.error('Failed to turn off lightbar:', e);
    }
}

async function setPlayerLEDs(ledMask) {
    try {
        await fetch('/api/lightbar/player-leds', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ leds: ledMask })
        });
        
        lightbarConfig.player_leds = ledMask;
        updatePlayerLEDsUI();
    } catch (e) {
        console.error('Failed to set player LEDs:', e);
    }
}

async function fetchAnimations() {
    try {
        const r = await fetch('/api/lightbar/animations');
        const data = await r.json();
        lightbarAnimations = data.animations;
        renderAnimationList();
    } catch (e) {
        console.error('Failed to fetch animations:', e);
    }
}

async function applyPreset(presetName) {
    try {
        await fetch(`/api/lightbar/presets/${presetName}`, { method: 'POST' });
        await fetchLightbarState();
    } catch (e) {
        console.error('Failed to apply preset:', e);
    }
}

async function previewAnimation(animId) {
    try {
        await fetch(`/api/lightbar/animations/${animId}/preview`, { method: 'POST' });
        selectedAnimationId = animId;
        lightbarConfig.mode = 'custom';
        lightbarConfig.custom_animation_id = animId;
        updateLightbarUI();
    } catch (e) {
        console.error('Failed to preview animation:', e);
    }
}

async function deleteAnimation(animId) {
    if (!confirm('Delete this animation?')) return;
    try {
        await fetch(`/api/lightbar/animations/${animId}`, { method: 'DELETE' });
        await fetchAnimations();
    } catch (e) {
        console.error('Failed to delete animation:', e);
    }
}

// =================================================================
// UI Updates
// =================================================================
function updateLightbarUI() {
    updateLightbarPreview();
    updateModeButtons();
    updateColorSliders();
    updateBrightnessSlider();
    updatePlayerLEDsUI();
    updateModeConfig();
}

function updateLightbarPreview() {
    const preview = document.getElementById('lightbarPreview');
    if (!preview) return;
    
    const { r, g, b } = lightbarConfig.color;
    const brightness = lightbarConfig.brightness;
    
    const adjustedR = Math.round(r * brightness);
    const adjustedG = Math.round(g * brightness);
    const adjustedB = Math.round(b * brightness);
    
    const colorStr = `rgb(${adjustedR}, ${adjustedG}, ${adjustedB})`;
    
    const previewGlow = preview.querySelector('.preview-glow');
    const previewSolid = preview.querySelector('.preview-solid');
    
    if (previewGlow) previewGlow.style.backgroundColor = colorStr;
    if (previewSolid) {
        previewSolid.style.backgroundColor = colorStr;
        previewSolid.style.boxShadow = `0 0 30px ${colorStr}`;
    }
    
    const isAnimated = ['breathing', 'rainbow', 'wave', 'custom'].includes(lightbarConfig.mode);
    preview.classList.toggle('animated', isAnimated);
    
    if (lightbarConfig.mode === 'off') {
        if (previewSolid) {
            previewSolid.style.backgroundColor = '#1a1b24';
            previewSolid.style.boxShadow = 'none';
        }
    }
}

function updateModeButtons() {
    document.querySelectorAll('.lightbar-mode-btn').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.mode === lightbarConfig.mode);
    });
}

function updateColorSliders() {
    const { r, g, b } = lightbarConfig.color;
    
    const redSlider = document.getElementById('redSlider');
    const greenSlider = document.getElementById('greenSlider');
    const blueSlider = document.getElementById('blueSlider');
    const redValue = document.getElementById('redValue');
    const greenValue = document.getElementById('greenValue');
    const blueValue = document.getElementById('blueValue');
    const hexInput = document.getElementById('hexInput');
    
    if (redSlider) redSlider.value = r;
    if (greenSlider) greenSlider.value = g;
    if (blueSlider) blueSlider.value = b;
    if (redValue) redValue.value = r;
    if (greenValue) greenValue.value = g;
    if (blueValue) blueValue.value = b;
    if (hexInput) hexInput.value = rgbToHex(r, g, b);
}

function updateBrightnessSlider() {
    const slider = document.getElementById('brightnessSlider');
    const value = document.getElementById('brightnessValue');
    
    if (slider) slider.value = lightbarConfig.brightness * 100;
    if (value) value.textContent = Math.round(lightbarConfig.brightness * 100) + '%';
}

function updatePlayerLEDsUI() {
    const leds = document.querySelectorAll('.player-led');
    leds.forEach((led, i) => {
        led.classList.toggle('active', (lightbarConfig.player_leds & (1 << i)) !== 0);
    });
}

function updateModeConfig() {
    document.querySelectorAll('.mode-config').forEach(el => el.style.display = 'none');
    
    const currentConfig = document.getElementById(`config-${lightbarConfig.mode}`);
    if (currentConfig) currentConfig.style.display = 'block';
    
    if (lightbarConfig.mode === 'breathing') {
        const speedSlider = document.getElementById('breathingSpeed');
        const speedValue = document.getElementById('breathingSpeedValue');
        if (speedSlider) speedSlider.value = lightbarConfig.breathing_speed_ms;
        if (speedValue) speedValue.textContent = (lightbarConfig.breathing_speed_ms / 1000).toFixed(1) + 's';
    }
    
    if (lightbarConfig.mode === 'rainbow') {
        const speedSlider = document.getElementById('rainbowSpeed');
        const speedValue = document.getElementById('rainbowSpeedValue');
        if (speedSlider) speedSlider.value = lightbarConfig.rainbow_speed_ms;
        if (speedValue) speedValue.textContent = (lightbarConfig.rainbow_speed_ms / 1000).toFixed(1) + 's';
    }
}

// =================================================================
// Event Handlers
// =================================================================
function onColorSliderChange() {
    const r = parseInt(document.getElementById('redSlider')?.value) || 0;
    const g = parseInt(document.getElementById('greenSlider')?.value) || 0;
    const b = parseInt(document.getElementById('blueSlider')?.value) || 0;
    
    lightbarConfig.color = { r, g, b };
    updateLightbarPreview();
    updateColorSliders();
}

function onColorSliderRelease() {
    const { r, g, b } = lightbarConfig.color;
    setLightbarColor(r, g, b);
}

function onColorValueChange(channel) {
    const input = document.getElementById(`${channel}Value`);
    let value = parseInt(input?.value) || 0;
    value = Math.max(0, Math.min(255, value));
    if (input) input.value = value;
    
    const key = channel === 'red' ? 'r' : channel === 'green' ? 'g' : 'b';
    lightbarConfig.color[key] = value;
    updateLightbarPreview();
    updateColorSliders();
    
    const { r, g, b } = lightbarConfig.color;
    setLightbarColor(r, g, b);
}

function onHexInputChange() {
    const input = document.getElementById('hexInput');
    if (!input) return;
    
    let hex = input.value.trim();
    if (!hex.startsWith('#')) hex = '#' + hex;
    
    const rgb = hexToRgb(hex);
    if (rgb) {
        lightbarConfig.color = rgb;
        updateLightbarPreview();
        updateColorSliders();
        setLightbarColor(rgb.r, rgb.g, rgb.b);
    }
}

function onBrightnessChange() {
    const slider = document.getElementById('brightnessSlider');
    const value = document.getElementById('brightnessValue');
    
    if (!slider) return;
    
    const brightness = parseInt(slider.value) / 100;
    if (value) value.textContent = Math.round(brightness * 100) + '%';
    
    lightbarConfig.brightness = brightness;
    updateLightbarPreview();
}

function onBrightnessRelease() {
    setLightbarBrightness(lightbarConfig.brightness);
}

function onModeClick(mode) {
    const params = {};
    
    if (mode === 'static') {
        params.color = lightbarConfig.color;
    }
    
    setLightbarMode(mode, params);
}

function onPresetClick(presetName) {
    if (COLOR_PRESETS[presetName]) {
        const color = COLOR_PRESETS[presetName];
        setLightbarColor(color.r, color.g, color.b);
    } else {
        applyPreset(presetName);
    }
}

function onPlayerLEDClick(index) {
    const currentMask = lightbarConfig.player_leds;
    const newMask = currentMask ^ (1 << index);
    setPlayerLEDs(newMask);
}

function onBreathingSpeedChange() {
    const slider = document.getElementById('breathingSpeed');
    const value = document.getElementById('breathingSpeedValue');
    
    if (!slider) return;
    
    const speed = parseInt(slider.value);
    if (value) value.textContent = (speed / 1000).toFixed(1) + 's';
    
    lightbarConfig.breathing_speed_ms = speed;
}

function onBreathingSpeedRelease() {
    setLightbarMode('breathing', { 
        speed_ms: lightbarConfig.breathing_speed_ms,
        color: lightbarConfig.color
    });
}

function onRainbowSpeedChange() {
    const slider = document.getElementById('rainbowSpeed');
    const value = document.getElementById('rainbowSpeedValue');
    
    if (!slider) return;
    
    const speed = parseInt(slider.value);
    if (value) value.textContent = (speed / 1000).toFixed(1) + 's';
    
    lightbarConfig.rainbow_speed_ms = speed;
}

function onRainbowSpeedRelease() {
    setLightbarMode('rainbow', { speed_ms: lightbarConfig.rainbow_speed_ms });
}

// =================================================================
// Animation List
// =================================================================
function renderAnimationList() {
    const container = document.getElementById('animationList');
    if (!container) return;
    
    if (!lightbarAnimations.length) {
        container.innerHTML = '<div class="empty-state">No animations available</div>';
        return;
    }
    
    const presetIds = ['pulse_slow', 'pulse_fast', 'police', 'fire'];
    
    container.innerHTML = lightbarAnimations.map(anim => {
        const isPreset = presetIds.includes(anim.id);
        const isSelected = selectedAnimationId === anim.id;
        
        return `
            <div class="animation-item ${isPreset ? 'preset' : ''} ${isSelected ? 'selected' : ''}"
                 onclick="previewAnimation('${anim.id}')">
                <div class="animation-preview"></div>
                <div class="animation-info">
                    <div class="animation-name">${esc(anim.name)}</div>
                    <div class="animation-meta">
                        ${anim.duration_ms}ms ${anim.loop ? '• Loop' : ''}
                        ${isPreset ? '• Preset' : ''}
                    </div>
                </div>
                ${!isPreset ? `
                    <div class="animation-actions">
                        <button class="icon-btn small danger" onclick="event.stopPropagation(); deleteAnimation('${anim.id}')" title="Delete">
                            <svg viewBox="0 0 24 24"><path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>
                        </button>
                    </div>
                ` : ''}
            </div>
        `;
    }).join('');
}

// =================================================================
// Render Lightbar Section in Editor
// =================================================================
function renderLightbarSection() {
    const container = document.getElementById('lightbarContainer');
    if (!container) return;
    
    container.innerHTML = `
        <!-- Color Preview -->
        <div class="color-preview-large" id="lightbarPreview">
            <div class="preview-glow"></div>
            <div class="preview-solid"></div>
        </div>
        
        <!-- Color Sliders -->
        <div class="color-sliders">
            <div class="color-slider-group">
                <div class="color-slider-label red">R</div>
                <input type="range" class="color-slider red-slider" id="redSlider" 
                       min="0" max="255" value="${lightbarConfig.color.r}"
                       oninput="onColorSliderChange()" onchange="onColorSliderRelease()">
                <input type="number" class="color-value-input" id="redValue" 
                       min="0" max="255" value="${lightbarConfig.color.r}"
                       onchange="onColorValueChange('red')">
            </div>
            <div class="color-slider-group">
                <div class="color-slider-label green">G</div>
                <input type="range" class="color-slider green-slider" id="greenSlider" 
                       min="0" max="255" value="${lightbarConfig.color.g}"
                       oninput="onColorSliderChange()" onchange="onColorSliderRelease()">
                <input type="number" class="color-value-input" id="greenValue" 
                       min="0" max="255" value="${lightbarConfig.color.g}"
                       onchange="onColorValueChange('green')">
            </div>
            <div class="color-slider-group">
                <div class="color-slider-label blue">B</div>
                <input type="range" class="color-slider blue-slider" id="blueSlider" 
                       min="0" max="255" value="${lightbarConfig.color.b}"
                       oninput="onColorSliderChange()" onchange="onColorSliderRelease()">
                <input type="number" class="color-value-input" id="blueValue" 
                       min="0" max="255" value="${lightbarConfig.color.b}"
                       onchange="onColorValueChange('blue')">
            </div>
            
            <div class="hex-input-row">
                <label>HEX</label>
                <input type="text" class="hex-input" id="hexInput" 
                       value="${rgbToHex(lightbarConfig.color.r, lightbarConfig.color.g, lightbarConfig.color.b)}"
                       onchange="onHexInputChange()">
            </div>
        </div>
        
        <!-- Color Presets -->
        <div class="form-group">
            <label class="form-label">Presets</label>
            <div class="color-presets">
                ${Object.entries(COLOR_PRESETS).map(([name, color]) => `
                    <div class="color-preset" 
                         style="background-color: rgb(${color.r}, ${color.g}, ${color.b})"
                         onclick="onPresetClick('${name}')"
                         title="${name}"></div>
                `).join('')}
            </div>
        </div>
        
        <!-- Mode Selection -->
        <div class="form-group">
            <label class="form-label">Mode</label>
            <div class="lightbar-mode-grid">
                <button class="lightbar-mode-btn ${lightbarConfig.mode === 'off' ? 'active' : ''}" 
                        data-mode="off" onclick="onModeClick('off')">
                    <svg viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 18c-4.42 0-8-3.58-8-8s3.58-8 8-8 8 3.58 8 8-3.58 8-8 8z"/></svg>
                    <span class="mode-name">Off</span>
                </button>
                <button class="lightbar-mode-btn ${lightbarConfig.mode === 'static' ? 'active' : ''}" 
                        data-mode="static" onclick="onModeClick('static')">
                    <svg viewBox="0 0 24 24"><path d="M12 7c-2.76 0-5 2.24-5 5s2.24 5 5 5 5-2.24 5-5-2.24-5-5-5zm0-5C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 18c-4.42 0-8-3.58-8-8s3.58-8 8-8 8 3.58 8 8-3.58 8-8 8z"/></svg>
                    <span class="mode-name">Static</span>
                </button>
                <button class="lightbar-mode-btn ${lightbarConfig.mode === 'breathing' ? 'active' : ''}" 
                        data-mode="breathing" onclick="onModeClick('breathing')">
                    <svg viewBox="0 0 24 24"><path d="M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zM12 17c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5zm0-8c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3z"/></svg>
                    <span class="mode-name">Breathing</span>
                </button>
                <button class="lightbar-mode-btn ${lightbarConfig.mode === 'rainbow' ? 'active' : ''}" 
                        data-mode="rainbow" onclick="onModeClick('rainbow')">
                    <svg viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-1 17.93c-3.95-.49-7-3.85-7-7.93 0-.62.08-1.21.21-1.79L9 15v1c0 1.1.9 2 2 2v1.93zm6.9-2.54c-.26-.81-1-1.39-1.9-1.39h-1v-3c0-.55-.45-1-1-1H8v-2h2c.55 0 1-.45 1-1V7h2c1.1 0 2-.9 2-2v-.41c2.93 1.19 5 4.06 5 7.41 0 2.08-.8 3.97-2.1 5.39z"/></svg>
                    <span class="mode-name">Rainbow</span>
                </button>
                <button class="lightbar-mode-btn ${lightbarConfig.mode === 'wave' ? 'active' : ''}" 
                        data-mode="wave" onclick="onModeClick('wave')">
                    <svg viewBox="0 0 24 24"><path d="M17 16c-1.35 0-2.2.42-2.95.8-.65.33-1.18.6-2.05.6-.9 0-1.4-.25-2.05-.6-.75-.38-1.57-.8-2.95-.8s-2.2.42-2.95.8c-.65.33-1.17.6-2.05.6v2c1.35 0 2.2-.42 2.95-.8.65-.33 1.17-.6 2.05-.6s1.4.25 2.05.6c.75.38 1.57.8 2.95.8s2.2-.42 2.95-.8c.65-.33 1.18-.6 2.05-.6.9 0 1.4.25 2.05.6.75.38 1.58.8 2.95.8v-2c-.9 0-1.4-.25-2.05-.6-.75-.38-1.6-.8-2.95-.8z"/></svg>
                    <span class="mode-name">Wave</span>
                </button>
                <button class="lightbar-mode-btn ${lightbarConfig.mode === 'custom' ? 'active' : ''}" 
                        data-mode="custom" onclick="onModeClick('custom')">
                    <svg viewBox="0 0 24 24"><path d="M12 3c-4.97 0-9 4.03-9 9s4.03 9 9 9c.83 0 1.5-.67 1.5-1.5 0-.39-.15-.74-.39-1.01-.23-.26-.38-.61-.38-.99 0-.83.67-1.5 1.5-1.5H16c2.76 0 5-2.24 5-5 0-4.42-4.03-8-9-8zm-5.5 9c-.83 0-1.5-.67-1.5-1.5S5.67 9 6.5 9 8 9.67 8 10.5 7.33 12 6.5 12zm3-4C8.67 8 8 7.33 8 6.5S8.67 5 9.5 5s1.5.67 1.5 1.5S10.33 8 9.5 8zm5 0c-.83 0-1.5-.67-1.5-1.5S13.67 5 14.5 5s1.5.67 1.5 1.5S15.33 8 14.5 8zm3 4c-.83 0-1.5-.67-1.5-1.5S16.67 9 17.5 9s1.5.67 1.5 1.5-.67 1.5-1.5 1.5z"/></svg>
                    <span class="mode-name">Custom</span>
                </button>
            </div>
        </div>
        
        <!-- Mode-specific configs -->
        <div class="mode-config" id="config-breathing" style="display: ${lightbarConfig.mode === 'breathing' ? 'block' : 'none'}">
            <div class="animation-config">
                <div class="config-row">
                    <label>Speed</label>
                    <input type="range" class="speed-slider" id="breathingSpeed" 
                           min="500" max="5000" value="${lightbarConfig.breathing_speed_ms}"
                           oninput="onBreathingSpeedChange()" onchange="onBreathingSpeedRelease()">
                    <span class="speed-value" id="breathingSpeedValue">${(lightbarConfig.breathing_speed_ms / 1000).toFixed(1)}s</span>
                </div>
            </div>
        </div>
        
        <div class="mode-config" id="config-rainbow" style="display: ${lightbarConfig.mode === 'rainbow' ? 'block' : 'none'}">
            <div class="animation-config">
                <div class="config-row">
                    <label>Speed</label>
                    <input type="range" class="speed-slider" id="rainbowSpeed" 
                           min="500" max="10000" value="${lightbarConfig.rainbow_speed_ms}"
                           oninput="onRainbowSpeedChange()" onchange="onRainbowSpeedRelease()">
                    <span class="speed-value" id="rainbowSpeedValue">${(lightbarConfig.rainbow_speed_ms / 1000).toFixed(1)}s</span>
                </div>
            </div>
        </div>
        
        <div class="mode-config" id="config-custom" style="display: ${lightbarConfig.mode === 'custom' ? 'block' : 'none'}">
            <label class="form-label">Animations</label>
            <div class="animation-list" id="animationList"></div>
        </div>
        
        <!-- Brightness -->
        <div class="brightness-control">
            <svg class="brightness-icon" viewBox="0 0 24 24"><path d="M20 8.69V4h-4.69L12 .69 8.69 4H4v4.69L.69 12 4 15.31V20h4.69L12 23.31 15.31 20H20v-4.69L23.31 12 20 8.69zM12 18c-3.31 0-6-2.69-6-6s2.69-6 6-6 6 2.69 6 6-2.69 6-6 6zm0-10c-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4-1.79-4-4-4z"/></svg>
            <input type="range" class="brightness-slider" id="brightnessSlider"
                   min="0" max="100" value="${Math.round(lightbarConfig.brightness * 100)}"
                   oninput="onBrightnessChange()" onchange="onBrightnessRelease()">
            <span class="brightness-value" id="brightnessValue">${Math.round(lightbarConfig.brightness * 100)}%</span>
        </div>
        
        <!-- Player LEDs -->
        <div class="player-leds-control">
            <div class="player-leds-title">Player LEDs</div>
            <div class="player-leds-row">
                ${[0, 1, 2, 3, 4].map(i => `
                    <div class="player-led ${(lightbarConfig.player_leds & (1 << i)) ? 'active' : ''}"
                         onclick="onPlayerLEDClick(${i})"></div>
                `).join('')}
            </div>
        </div>
        
        <!-- Quick Actions -->
        <div class="lightbar-quick-actions">
            <button class="quick-action-btn danger" onclick="turnOffLightbar()">
                <svg viewBox="0 0 24 24"><path d="M13 3h-2v10h2V3zm4.83 2.17l-1.42 1.42C17.99 7.86 19 9.81 19 12c0 3.87-3.13 7-7 7s-7-3.13-7-7c0-2.19 1.01-4.14 2.58-5.42L6.17 5.17C4.23 6.82 3 9.26 3 12c0 4.97 4.03 9 9 9s9-4.03 9-9c0-2.74-1.23-5.18-3.17-6.83z"/></svg>
                Turn Off
            </button>
        </div>
    `;
    
    updateLightbarPreview();
    
    if (lightbarConfig.mode === 'custom') {
        fetchAnimations();
    }
}

// =================================================================
// Initialize on Profile Editor Open
// =================================================================
function initLightbarSection() {
    fetchLightbarState().then(() => {
        renderLightbarSection();
        fetchAnimations();
    });
}