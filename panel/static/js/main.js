// =================================================================
// Event Listeners
// =================================================================
document.addEventListener('DOMContentLoaded', () => {
    // Click outside to close menu
    document.addEventListener('click', (e) => {
        const controllerArea = document.getElementById('controllerArea');
        if (menuOpen && controllerArea && !controllerArea.contains(e.target)) {
            closeMenu();
        }
    });

    // Click outside to close modals
    document.querySelectorAll('.modal-overlay').forEach(modal => {
        modal.addEventListener('click', (e) => {
            if (e.target === modal) {
                modal.classList.remove('active');
            }
        });
    });

    // Keyboard shortcuts
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') {
            closeRenameModal();
            closeCreateProfileModal();
            closeDeleteProfileModal();
            closeDeleteMacroModal();
            closeDeleteRemapModal();
            closeGlobalMacroSelector();
            
            // Close creator views
            const remapCreator = document.getElementById('remapCreatorView');
            if (remapCreator && remapCreator.style.display !== 'none') {
                closeRemapCreator();
            }
        }
        
        if (e.key === 'Enter' && !e.shiftKey) {
            const renameModal = document.getElementById('renameModal');
            const createProfileModal = document.getElementById('createProfileModal');
            
            if (renameModal?.classList.contains('active')) {
                saveRename();
            } else if (createProfileModal?.classList.contains('active')) {
                createProfile();
            }
        }
    });

    // Initialize
    fetchStatus();
    fetchProfiles();
    
    // Poll for status updates
    setInterval(fetchStatus, 1000);
});