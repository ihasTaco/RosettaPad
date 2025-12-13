// =================================================================
// Macro Creator - View Management
// =================================================================
function openMacroCreator() {
    editingMacroId = null;
    resetMacroData();
    document.getElementById('macroCreatorTitle').textContent = 'Create Macro';
    openMacroCreatorView();
}

function openMacroCreatorView() {
    document.getElementById('profileEditorView').style.display = 'none';
    document.getElementById('macroCreatorView').style.display = 'block';
    currentStep = 1;
    updateStepUI();
    
    // Restore selections if editing
    if (macroData.type) {
        document.querySelectorAll('.macro-type-card').forEach(c => {
            c.classList.toggle('selected', c.dataset.type === macroData.type);
        });
    }
    if (macroData.triggerButton) {
        document.querySelectorAll('.button-option').forEach(b => {
            b.classList.toggle('selected', b.dataset.button === macroData.triggerButton);
        });
    }
}

function closeMacroCreator() {
    document.getElementById('macroCreatorView').style.display = 'none';
    document.getElementById('profileEditorView').style.display = 'block';
    resetMacroData();
}

function resetMacroData() {
    macroData = {
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
    currentStep = 1;
    
    // Reset UI
    document.querySelectorAll('.macro-type-card').forEach(c => c.classList.remove('selected'));
    document.querySelectorAll('.button-option').forEach(b => b.classList.remove('selected'));
    
    const onPressRadio = document.querySelector('input[name="activationMode"][value="on_press"]');
    if (onPressRadio) onPressRadio.checked = true;
    
    const useModifier = document.getElementById('useModifier');
    if (useModifier) useModifier.checked = false;
    
    const modifierButton = document.getElementById('modifierButton');
    if (modifierButton) modifierButton.disabled = true;
    
    const rapidFireRate = document.getElementById('rapidFireRate');
    if (rapidFireRate) rapidFireRate.value = '10';
    
    const macroName = document.getElementById('macroName');
    if (macroName) macroName.value = '';
    
    const macroDescription = document.getElementById('macroDescription');
    if (macroDescription) macroDescription.value = '';
    
    const makeGlobal = document.getElementById('makeGlobal');
    if (makeGlobal) makeGlobal.checked = false;
    
    const sequenceSteps = document.getElementById('sequenceSteps');
    if (sequenceSteps) {
        sequenceSteps.innerHTML = '<div class="empty-sequence"><p>Click "Add Step" to build your sequence.</p></div>';
    }
}

// =================================================================
// Step Navigation
// =================================================================
function updateStepUI() {
    // Update step indicators
    document.querySelectorAll('.step').forEach(s => {
        const n = parseInt(s.dataset.step);
        s.classList.toggle('active', n === currentStep);
        s.classList.toggle('completed', n < currentStep);
    });
    
    // Show correct step content
    document.querySelectorAll('.creator-step-content').forEach((c, i) => {
        c.classList.toggle('active', i + 1 === currentStep);
    });
    
    // Update navigation buttons
    document.getElementById('prevStepBtn').style.visibility = currentStep === 1 ? 'hidden' : 'visible';
    
    const nextBtn = document.getElementById('nextStepBtn');
    const saveBtn = document.getElementById('saveMacroBtn');
    
    if (currentStep === 4) {
        nextBtn.style.display = 'none';
        saveBtn.style.visibility = 'visible';
        updateMacroSummary();
    } else {
        nextBtn.style.display = 'flex';
        saveBtn.style.visibility = 'hidden';
    }
    
    // Show correct config for step 3
    if (currentStep === 3) {
        document.querySelectorAll('.action-config').forEach(c => c.style.display = 'none');
        const cfg = document.getElementById(`config-${macroData.type}`);
        if (cfg) cfg.style.display = 'block';
    }
}

function nextStep() {
    if (!validateStep(currentStep)) return;
    if (currentStep < 4) {
        currentStep++;
        updateStepUI();
    }
}

function prevStep() {
    if (currentStep > 1) {
        currentStep--;
        updateStepUI();
    }
}

function validateStep(step) {
    if (step === 1 && !macroData.type) {
        alert('Please select a macro type');
        return false;
    }
    if (step === 2 && !macroData.triggerButton) {
        alert('Please select a trigger button');
        return false;
    }
    if (step === 3 && macroData.type === 'sequence' && !macroData.sequenceSteps.length) {
        alert('Please add at least one step to the sequence');
        return false;
    }
    return true;
}

// =================================================================
// Step 1: Type Selection
// =================================================================
function selectMacroType(type) {
    macroData.type = type;
    document.querySelectorAll('.macro-type-card').forEach(c => {
        c.classList.toggle('selected', c.dataset.type === type);
    });
}

// =================================================================
// Step 2: Trigger Configuration
// =================================================================
function selectTriggerButton(btn) {
    macroData.triggerButton = btn;
    document.querySelectorAll('.button-option').forEach(b => {
        b.classList.toggle('selected', b.dataset.button === btn);
    });
}

function toggleModifier() {
    const cb = document.getElementById('useModifier');
    document.getElementById('modifierButton').disabled = !cb.checked;
    if (!cb.checked) macroData.modifier = null;
}

// =================================================================
// Step 3: Action Configuration
// =================================================================
function updateRapidFirePreview() {
    const rate = parseInt(document.getElementById('rapidFireRate').value) || 10;
    const interval = Math.round(1000 / rate);
    document.getElementById('rapidFirePreview').textContent = 
        `Button will press ${rate} times per second (${interval}ms interval)`;
    macroData.rate = rate;
}

// Sequence Builder
function addSequenceStep() {
    macroData.sequenceSteps.push({
        type: 'press',
        button: 'cross',
        duration_ms: 50
    });
    renderSequenceSteps();
}

function removeSequenceStep(index) {
    macroData.sequenceSteps.splice(index, 1);
    renderSequenceSteps();
}

function updateSequenceStep(index, field, value) {
    if (macroData.sequenceSteps[index]) {
        macroData.sequenceSteps[index][field] = field === 'duration_ms' ? parseInt(value) || 0 : value;
    }
}

function renderSequenceSteps() {
    const container = document.getElementById('sequenceSteps');
    
    if (!macroData.sequenceSteps.length) {
        container.innerHTML = '<div class="empty-sequence"><p>Click "Add Step" to build your sequence.</p></div>';
        return;
    }
    
    container.innerHTML = macroData.sequenceSteps.map((s, i) => `
        <div class="sequence-step">
            <div class="step-num">${i + 1}</div>
            <select class="form-select small" onchange="updateSequenceStep(${i}, 'type', this.value)">
                <option value="press" ${s.type === 'press' ? 'selected' : ''}>Press</option>
                <option value="hold" ${s.type === 'hold' ? 'selected' : ''}>Hold</option>
                <option value="release" ${s.type === 'release' ? 'selected' : ''}>Release</option>
                <option value="wait" ${s.type === 'wait' ? 'selected' : ''}>Wait</option>
            </select>
            ${s.type !== 'wait' ? `
                <select class="form-select small" onchange="updateSequenceStep(${i}, 'button', this.value)">
                    ${Object.entries(BUTTONS).map(([k, v]) => 
                        `<option value="${k}" ${s.button === k ? 'selected' : ''}>${v.name}</option>`
                    ).join('')}
                </select>
            ` : '<div class="step-spacer"></div>'}
            <div class="step-duration">
                <input type="number" class="form-input tiny" value="${s.duration_ms}" 
                       min="0" max="5000" onchange="updateSequenceStep(${i}, 'duration_ms', this.value)">
                <span>ms</span>
            </div>
            <button class="icon-btn small danger" onclick="removeSequenceStep(${i})">
                <svg viewBox="0 0 24 24"><path d="M19 6.41L17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z"/></svg>
            </button>
        </div>
    `).join('');
}

// =================================================================
// Step 4: Details & Summary
// =================================================================
function updateMacroSummary() {
    const trigger = BUTTONS[macroData.triggerButton] || { name: '?' };
    const mode = ACTIVATION_MODES[macroData.activationMode];
    const type = MACRO_TYPES[macroData.type];
    
    let actionDesc = '';
    if (macroData.type === 'rapid_fire') {
        const target = macroData.targetButton ? BUTTONS[macroData.targetButton]?.name : trigger.name;
        actionDesc = `Rapid fire ${target} at ${macroData.rate} presses/sec`;
    } else if (macroData.type === 'toggle') {
        const target = macroData.targetButton ? BUTTONS[macroData.targetButton]?.name : trigger.name;
        actionDesc = `Toggle ${target} on/off`;
    } else if (macroData.type === 'turbo') {
        const target = macroData.targetButton ? BUTTONS[macroData.targetButton]?.name : trigger.name;
        actionDesc = `Turbo ${target} while held`;
    } else if (macroData.type === 'sequence') {
        actionDesc = `${macroData.sequenceSteps.length} step sequence`;
    }
    
    document.getElementById('macroSummary').innerHTML = `
        <div class="summary-item">
            <span class="summary-label">Type</span>
            <span class="summary-value">${type}</span>
        </div>
        <div class="summary-item">
            <span class="summary-label">Trigger</span>
            <span class="summary-value">${trigger.name}</span>
        </div>
        <div class="summary-item">
            <span class="summary-label">Mode</span>
            <span class="summary-value">${mode}</span>
        </div>
        <div class="summary-item">
            <span class="summary-label">Action</span>
            <span class="summary-value">${actionDesc}</span>
        </div>
    `;
}

// =================================================================
// Save Macro
// =================================================================
async function saveMacro() {
    macroData.name = document.getElementById('macroName').value.trim();
    macroData.description = document.getElementById('macroDescription').value.trim();
    macroData.isGlobal = document.getElementById('makeGlobal').checked;
    macroData.activationMode = document.querySelector('input[name="activationMode"]:checked')?.value || 'on_press';
    
    if (!macroData.name) {
        document.getElementById('macroName').focus();
        return;
    }
    
    // Build actions array based on type
    let actions = [];
    
    if (macroData.type === 'rapid_fire') {
        const target = document.getElementById('rapidFireTarget')?.value || macroData.triggerButton;
        const rate = parseInt(document.getElementById('rapidFireRate').value) || 10;
        actions = [{ type: 'rapid_fire', button: target, duration_ms: Math.round(1000 / rate) }];
    } else if (macroData.type === 'toggle') {
        const target = document.getElementById('toggleTarget')?.value || macroData.triggerButton;
        actions = [{ type: 'toggle', button: target, duration_ms: 0 }];
    } else if (macroData.type === 'turbo') {
        const target = document.getElementById('turboTarget')?.value || macroData.triggerButton;
        const rate = parseInt(document.getElementById('turboRate').value) || 15;
        actions = [{ type: 'turbo', button: target, duration_ms: Math.round(1000 / rate) }];
    } else if (macroData.type === 'sequence') {
        actions = macroData.sequenceSteps;
    }
    
    const data = {
        name: macroData.name,
        trigger_button: macroData.triggerButton,
        trigger_mode: macroData.activationMode,
        actions,
        enabled: true
    };
    
    if (editingMacroId) {
        await updateMacroAPI(editingProfileId, editingMacroId, data);
    } else {
        await createMacroAPI(editingProfileId, data);
        
        // Save to global macros if requested
        if (macroData.isGlobal) {
            globalMacros.push({
                ...data,
                id: `global_${Date.now()}`,
                description: macroData.description
            });
        }
    }
    
    closeMacroCreator();
    renderMacrosList();
}