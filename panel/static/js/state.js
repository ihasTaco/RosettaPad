// =================================================================
// Global State
// =================================================================
let isScanning = false;
let menuOpen = false;
let connectedAddress = null;
let renameAddress = null;
let profiles = [];
let activeProfileId = 'default';
let editingProfileId = null;
let deletingProfileId = null;
let globalMacros = [];
let currentStep = 1;
let macroData = {
    type: null,
    triggerButton: null,
    activationMode: 'on_press',
    modifier: null,
    targetButton: null,
    rate: 10,
    sequenceSteps: [],
    loopCount: 1,
    loopDelay: 0,
    name: '',
    description: '',
    isGlobal: false
};
let editingMacroId = null;
let deletingMacroId = null;

// =================================================================
// Constants
// =================================================================
const BUTTONS = {
    cross: { symbol: '✕', name: 'Cross' },
    circle: { symbol: '○', name: 'Circle' },
    square: { symbol: '□', name: 'Square' },
    triangle: { symbol: '△', name: 'Triangle' },
    l1: { symbol: 'L1', name: 'L1' },
    r1: { symbol: 'R1', name: 'R1' },
    l2: { symbol: 'L2', name: 'L2' },
    r2: { symbol: 'R2', name: 'R2' },
    l3: { symbol: 'L3', name: 'L3' },
    r3: { symbol: 'R3', name: 'R3' },
    dpad_up: { symbol: '↑', name: 'D-Up' },
    dpad_down: { symbol: '↓', name: 'D-Down' },
    dpad_left: { symbol: '←', name: 'D-Left' },
    dpad_right: { symbol: '→', name: 'D-Right' },
    start: { symbol: '⏵', name: 'Start' },
    select: { symbol: '⏸', name: 'Select' },
    options: { symbol: '☰', name: 'Options' },
    create: { symbol: '⊞', name: 'Create' },
    ps: { symbol: 'PS', name: 'PS' },
    touchpad: { symbol: '▭', name: 'Touchpad' },
    mute: { symbol: '🔇', name: 'Mute' }
};

const MACRO_TYPES = {
    rapid_fire: 'Rapid Fire',
    toggle: 'Toggle Hold',
    sequence: 'Sequence',
    turbo: 'Turbo'
};

const ACTIVATION_MODES = {
    on_press: 'On Press',
    on_hold: 'While Held',
    on_release: 'On Release',
    toggle: 'Toggle'
};

// =================================================================
// Utility Functions
// =================================================================
function esc(t) {
    const d = document.createElement('div');
    d.textContent = t;
    return d.innerHTML;
}