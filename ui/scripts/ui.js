// ui.js

export const UI = (() => {
    /* ================= SVG Icons ================= */
    const ICON_UP = `
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#555">
<path d="M12 4.83582L5.79291 11.0429L7.20712 12.4571L12 7.66424L16.7929 12.4571L18.2071 11.0429L12 4.83582ZM12 10.4857L5.79291 16.6928L7.20712 18.107L12 13.3141L16.7929 18.107L18.2071 16.6928L12 10.4857Z"/>
</svg>`;

    const ICON_DOWN = `
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#555">
<path d="M12 19.1642L18.2071 12.9571L16.7929 11.5429L12 16.3358L7.20712 11.5429L5.79291 12.9571L12 19.1642ZM12 13.5143L18.2071 7.30722L16.7929 5.89301L12 10.6859L7.20712 5.89301L5.79291 7.30722L12 13.5143Z"/>
</svg>`;

    /* ================= HUD Toggle ================= */
    const hud = document.getElementById("hud");
    const toggle = document.getElementById("toggle");
    const hudText = document.querySelector('.hud-text');
    const hudTitle = document.querySelector('.hud-title');
    const hudCommentActions = document.getElementById('hudCommentActions');
    const hudUpdateComment = document.getElementById('hudUpdateComment');
    const hudDiscardChanges = document.getElementById('hudDiscardChanges');
    const AUTO_TOGGLE_KEY = 'auto-toggle-comments';
    let autoToggleComments = true;
    try {
        const storedAutoToggle = localStorage.getItem(AUTO_TOGGLE_KEY);
        autoToggleComments = storedAutoToggle !== null ? storedAutoToggle === 'true' : true;
    } catch (error) {
        autoToggleComments = true;
    }

    function updateToggle(expanded) {
        if (!toggle) return;
        toggle.innerHTML = expanded ? ICON_UP : ICON_DOWN;
    }

    function setHudExpanded(expanded) {
        if (!hud) return;
        hud.classList.toggle("expanded", expanded);
        hud.classList.toggle("collapsed", !expanded);
        updateToggle(expanded);
    }

    function autoToggleHudForText(text) {
        if (!autoToggleComments) return;
        const trimmed = (text || '').trim();
        setHudExpanded(trimmed.length > 0);
    }

    toggle.onclick = () => {
        if (!hud) return;
        const isExpanded = hud.classList.contains("expanded");
        setHudExpanded(!isExpanded);
    };

    setHudExpanded(false);

    /* ================= HUD Text Content ================= */
    let hudTextChangeCallback = null;
    let commentsEditCallback = null;
    let hudTextOriginal = hudText ? hudText.textContent : '';

    function setHudTextOriginal(value) {
        hudTextOriginal = value;
        updateHudCommentActions();
    }

    function areBracesBalanced(text) {
        let braceCount = 0;
        for (const char of text) {
            if (char === '{') braceCount++;
            else if (char === '}') braceCount--;
            if (braceCount < 0) return false;
        }
        return braceCount === 0;
    }

    function updateHudCommentActions() {
        if (!hudCommentActions || !hudText || !hudUpdateComment) return;
        const hasChanges = hudText.textContent !== hudTextOriginal;
        hudCommentActions.classList.toggle('visible', hasChanges);
        
        if (hasChanges) {
            const text = hudText.textContent.trim();
            const isEmpty = text.length === 0;
            const isBalanced = areBracesBalanced(text);
            
            // Update button text based on empty state
            hudUpdateComment.textContent = isEmpty ? 'Delete Comment' : 'Update Comment';
            hudUpdateComment.classList.toggle('delete', isEmpty);
            
            // Disable button if braces are unbalanced
            if (!isBalanced) {
                hudUpdateComment.disabled = true;
                hudUpdateComment.title = 'Curly braces must be balanced';
            } else {
                hudUpdateComment.disabled = false;
                hudUpdateComment.title = '';
            }
        }
    }

    // Listen for changes in hud-text
    hudText.addEventListener('input', () => {
        if (hudTextChangeCallback) {
            hudTextChangeCallback(hudText.textContent);
        }
        updateHudCommentActions();
    });

    if (hudUpdateComment) {
        hudUpdateComment.addEventListener('click', () => {
            if (commentsEditCallback) {
                commentsEditCallback(hudText.textContent);
            }
            setHudTextOriginal(hudText.textContent);
        });
    }

    if (hudDiscardChanges) {
        hudDiscardChanges.addEventListener('click', () => {
            hudText.textContent = hudTextOriginal;
            updateHudCommentActions();
        });
    }
    /* ================= Light Toggle ================= */
    const light = document.getElementById("light");
    let lightCallback = null;

    /* ================= Control Panel ================= */
    const controlPanel = document.getElementById("controlPanel");
    const panelToggle = document.getElementById("panelToggle");
    const selectContainer = document.getElementById("selectContainer");
    const selectedDisplay = document.getElementById("selectedDisplay");
    const hintBtn = document.getElementById("hintBtn");
    let hintCallback = null;

    // Sample options
    const options = [
        "Option 1",
        "Option 2",
        "This is a very long option that needs multiple lines to display properly because it contains so much text",
        "Short option",
        "Another extremely long option with lots and lots of text that will definitely wrap to multiple lines in the dropdown menu"
    ];

    let selectedIndex = 0;

    function calculateCollapasedPanelHeight()
    {
        const remoteHeight = document.querySelector('.remote-control').offsetHeight;
        const selectedDisplayHeight = selectedDisplay.offsetHeight;
        const toggleHeight = 28;
        const collapsedHeight = remoteHeight + selectedDisplayHeight + toggleHeight;
        return collapsedHeight + 'px';
    }

    function renderOptions() {
        // Clear existing options (keep hint button)
        selectContainer.innerHTML = '';
        selectContainer.appendChild(hintBtn);
        
        options.forEach((opt, idx) => {
            const optEl = document.createElement('div');
            optEl.className = 'select-option';
            if (idx === selectedIndex) optEl.classList.add('selected');
            optEl.textContent = opt;
            optEl.onclick = () => {
                selectedIndex = idx;
                selectedDisplay.textContent = opt;
                renderOptions();
            };
            selectContainer.appendChild(optEl);
        });
        
        selectedDisplay.textContent = options[selectedIndex];
    }

    function updatePanelToggle(expanded) {
        panelToggle.innerHTML = expanded ? ICON_UP : ICON_DOWN;
    }

    panelToggle.onclick = () => {
        const isExpanded = controlPanel.classList.toggle('expanded');
        controlPanel.classList.toggle('collapsed', !isExpanded);
        
        if (isExpanded) {
            controlPanel.style.height = '500px';
        } else {
            // Calculate collapsed height based on content
            controlPanel.style.height = calculateCollapasedPanelHeight();
        }
        
        updatePanelToggle(isExpanded);
    };


    // Initialize
    updatePanelToggle(false);
    renderOptions();
    controlPanel.style.height = calculateCollapasedPanelHeight();

    hintBtn.onclick = () => {
        if(hintCallback) {
            hintCallback();
        }
    }


    /* ================= HUD Positioning ================= */
    function positionHud() {
        const screenWidth = window.innerWidth;
        const controlPanelWidth = 200;
        const controlPanelRight = 18;
        const minGap = 20;
        const hudWidth = Math.min(440, Math.max(150, screenWidth - controlPanelWidth - 18 - 18 - minGap));
        
        hud.style.width = hudWidth + 'px';
        
        // Calculate if centered HUD would overlap control panel
        const hudHalfWidth = hudWidth / 2;
        const hudRightEdge = screenWidth / 2 + hudHalfWidth;
        const controlPanelLeftEdge = screenWidth - controlPanelRight - controlPanelWidth;
        
        if (hudRightEdge + minGap > controlPanelLeftEdge) {
            // Need to shift left
            const overlap = hudRightEdge + minGap - controlPanelLeftEdge;
            const newLeft = 50 - (overlap / screenWidth * 100);
            const hudWrapper = document.getElementById('hudWrapper');
            hudWrapper.style.left = Math.max(18, newLeft) + '%';
        } else {
            const hudWrapper = document.getElementById('hudWrapper');
            hudWrapper.style.left = '50%';
        }
        
        // Check if left edge is too close to screen edge
        const hudWrapper = document.getElementById('hudWrapper');
        const hudLeft = parseFloat(hudWrapper.style.left) / 100 * screenWidth - hudHalfWidth;
        if (hudLeft < 18) {
            hudWrapper.style.left = '18px';
            hudWrapper.style.transform = 'translateX(0)';
        } else {
            hudWrapper.style.transform = 'translateX(-50%)';
        }
    }

    window.addEventListener('resize', positionHud);
    positionHud();


    /* ================= Buttons Panel ================= */
    // Focus button callback
    let focusCallback = null;

    document.getElementById('btnFocus').onclick = () => {
        if (focusCallback) focusCallback();
    };

    document.getElementById('btnScreenshot').onclick = () => {
        console.log('Screenshot button clicked');
    };

    document.getElementById('btnEdit').onclick = () => {
        console.log('Edit button clicked');
    };

    /* ================= Pop-up System ================= */
    const popupOverlay = document.getElementById('popupOverlay');
    let activeButton = null;

    // Callbacks for import/export
    let importCallback = null;
    let exportCallback = null;

    function showPopup(button, popupName) {
        // Close existing popup if any
        if (activeButton) {
            activeButton.classList.remove('active');
        }
        popupOverlay.classList.add('show');
        document.getElementById(popupName).classList.add('show');
        if (button) {
            button.classList.add('active');
            activeButton = button;
        } else {
            activeButton = null;
        }
    }

    function closePopup() {
        document.querySelectorAll('.popup-window').forEach(popup => {
            popup.classList.remove('show');
        });
        popupOverlay.classList.remove('show');
        if (activeButton) {
            activeButton.classList.remove('active');
            activeButton = null;
        }
    }

    // Close popup when clicking overlay (but not the window)
    popupOverlay.onclick = (e) => {
        if (e.target === popupOverlay) {
            closePopup();
        }
    };

    document.getElementById('btnInfo').onclick = (event) => {
        showPopup(event.currentTarget, 'popupInfo');
    };

    document.getElementById('btnImport').onclick = (event) => {
        showPopup(event.currentTarget, 'popupImport');
    };

    document.getElementById('loadButton').onclick = () => {
        const data = document.getElementById('importTextarea').value;
        if (importCallback) {
            importCallback(data);
        }
        closePopup();
    }

    // Load from File button - opens file picker and loads selected file
    document.getElementById('loadFromFileButton').onclick = () => {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '.5dpgn,.pgn,.txt';
        input.onchange = (e) => {
            const file = e.target.files[0];
            if (!file) return;
            
            // Limit file size to 10MB
            const maxSize = 10 * 1024 * 1024;
            if (file.size > maxSize) {
                alert('File is too large. Maximum size is 10MB.');
                return;
            }
            
            const reader = new FileReader();
            reader.onload = (event) => {
                const content = event.target.result;
                document.getElementById('importTextarea').value = content;
            };
            reader.onerror = () => {
                alert('Failed to read file.');
            };
            reader.readAsText(file);
        };
        input.click();
    };

    // Drag and drop support for import textarea
    const importTextarea = document.getElementById('importTextarea');
    importTextarea.ondragover = (e) => {
        e.preventDefault();
        importTextarea.classList.add('drag-over');
    };
    importTextarea.ondragleave = () => {
        importTextarea.classList.remove('drag-over');
    };
    importTextarea.ondrop = (e) => {
        e.preventDefault();
        importTextarea.classList.remove('drag-over');
        const files = e.dataTransfer.files;
        if (files.length === 0) return;
        
        const file = files[0];
        const maxSize = 10 * 1024 * 1024;
        if (file.size > maxSize) {
            alert('File is too large. Maximum size is 10MB.');
            return;
        }
        
        const reader = new FileReader();
        reader.onload = (event) => {
            const content = event.target.result;
            importTextarea.value = content;
        };
        reader.onerror = () => {
            alert('Failed to read file.');
        };
        reader.readAsText(file);
    };

    document.getElementById('btnExport').onclick = (event) => {
        showPopup(event.currentTarget, 'popupExport');
        // Call export callback to populate textarea
        if (exportCallback) {
            exportCallback();
        }
        // Populate filename and extension fields
        const timestamp = new Date().toISOString().slice(0, 10).replace(/-/g, '');
        document.getElementById('exportFilename').value = `game_${timestamp}`;
        document.getElementById('exportExtension').value = '.5dpgn';
    };

    // Copy to Clipboard button
    document.getElementById('copyButton').onclick = () => {
        const textarea = document.getElementById('exportTextarea');
        const text = textarea.value;
        if (!text) {
            alert('Nothing to copy.');
            return;
        }
        navigator.clipboard.writeText(text).then(() => {
            // Visual feedback - flash the button
            const button = document.getElementById('copyButton');
            const originalText = button.textContent;
            button.textContent = 'Copied!';
            setTimeout(() => {
                button.textContent = originalText;
            }, 2000);
        }).catch(() => {
            alert('Failed to copy to clipboard.');
        });
    };

    // Save as File button
    document.getElementById('saveButton').onclick = () => {
        const textarea = document.getElementById('exportTextarea');
        const text = textarea.value;
        if (!text) {
            alert('Nothing to save.');
            return;
        }
        
        // Get filename and extension from input fields
        const filename = document.getElementById('exportFilename').value.trim();
        const extension = document.getElementById('exportExtension').value.trim();
        
        if (!filename) {
            alert('Please enter a filename.');
            return;
        }
        
        // Construct the full filename
        const fullFilename = extension ? filename + extension : filename;
        
        // Create blob and download
        const blob = new Blob([text], { type: 'text/plain' });
        const url = URL.createObjectURL(blob);
        const link = document.createElement('a');
        link.href = url;
        link.download = fullFilename;
        
        document.body.appendChild(link);
        link.click();
        document.body.removeChild(link);
        URL.revokeObjectURL(url);
    };

    document.getElementById('btnSettings').onclick = (event) => {
        showPopup(event.currentTarget, 'popupSettings');
    };

    // Settings change callback wiring
    let settingsChangeCallback = null;
    const allowSubmitCheckbox = document.getElementById('allowSubmitWithChecks');
    if (allowSubmitCheckbox) {
        allowSubmitCheckbox.addEventListener('change', () => {
            if (settingsChangeCallback) {
                settingsChangeCallback({ allowSubmitWithChecks: allowSubmitCheckbox.checked });
            }
        });
    }

    // Debug window toggle
    const debugCheckbox = document.getElementById('toggleDebugWindow');
    if (debugCheckbox) {
        // Initialize checkbox based on current debug window visibility
        const dbgEl = document.querySelector('.debug-window');
        try {
            const visible = dbgEl && window.getComputedStyle(dbgEl).display !== 'none';
            debugCheckbox.checked = visible;
        } catch(e) { alert(e); }

        debugCheckbox.addEventListener('change', () => {
            const dbgVal = debugCheckbox.checked;
            const dbg = document.querySelector('.debug-window');
            if (dbg) dbg.style.display = dbgVal ? '' : 'none';
            if (settingsChangeCallback) {
                settingsChangeCallback({ debugWindow: dbgVal });
            }
        });
    }

    // Show movable pieces toggle
    const showMovablePiecesCheckbox = document.getElementById('showMovablePieces');
    if (showMovablePiecesCheckbox) {
        showMovablePiecesCheckbox.addEventListener('change', () => {
            if (settingsChangeCallback) {
                settingsChangeCallback({ showMovablePieces: showMovablePiecesCheckbox.checked });
            }
        });
    }

    // Automatically toggle HUD when comments change
    const autoToggleCommentsCheckbox = document.getElementById('autoToggleComments');
    if (autoToggleCommentsCheckbox) {
        autoToggleCommentsCheckbox.checked = autoToggleComments;
        autoToggleCommentsCheckbox.addEventListener('change', () => {
            autoToggleComments = autoToggleCommentsCheckbox.checked;
            try {
                localStorage.setItem(AUTO_TOGGLE_KEY, autoToggleComments.toString());
            } catch (error) {
                // Ignore storage errors (e.g. Safari private mode)
            }
            if (settingsChangeCallback) {
                settingsChangeCallback({ autoToggleComments });
            }
            autoToggleHudForText(hudText ? hudText.textContent : '');
        });
    }

    // ----------------------- Color Scheme / Theme Management -----------------------

    // Populate color scheme select dynamically
    const colorSchemeSelect = document.getElementById('colorSchemeSelect');

    // Apply theme by toggling a class on documentElement and removing others

    function applyTheme(name) {
        for (const t of window.themes) {
            document.documentElement.classList.remove(t.name);
        }
        const found = name && window.themes && window.themes.some(t => t.name === name);
        if (found) {
            colorSchemeSelect.value = name;
            document.documentElement.classList.add(name);
        } else if (name && !found) {
            console.warn(`Theme '${name}' not found; falling back to default`);
            // no class added for root
        }
        localStorage.setItem('color-scheme', name);
        // Notify settings change listeners that the theme changed
        if (settingsChangeCallback) settingsChangeCallback({ theme: name });
    }

    if (colorSchemeSelect) {
        // Initialize selector
        for (const t of window.themes) {
            const opt = document.createElement('option');
            opt.value = t.name;
            opt.textContent = t.option;
            colorSchemeSelect.appendChild(opt);
        }
        // load saved theme
        const saved = localStorage.getItem('color-scheme') || window.defaultTheme;
        applyTheme(saved);

        colorSchemeSelect.addEventListener('change', (e) => {
            const v = e.target.value;
            applyTheme(v);
        });
    }

    // Expose setter for theme change callback


    /* ================= Info Popup Management ================= */

    // Info page preference key for localStorage
    const INFO_PAGE_KEY = 'show-info-page';
    const LEGACY_WELCOME_KEY = 'show-welcome-page';

    // Get the info popup checkbox
    const showInfoPageCheckbox = document.getElementById('showInfoPageCheckbox');

    // Initialize the info page checkbox from localStorage
    if (showInfoPageCheckbox) {
        const stored = localStorage.getItem(INFO_PAGE_KEY);
        const legacy = localStorage.getItem(LEGACY_WELCOME_KEY);
        const shouldShowInfo = stored !== null ? stored !== 'false' : legacy !== 'false';
        showInfoPageCheckbox.checked = shouldShowInfo;

        if (stored === null && legacy !== null) {
            localStorage.setItem(INFO_PAGE_KEY, legacy);
        }

        // Listen for checkbox changes
        showInfoPageCheckbox.addEventListener('change', () => {
            localStorage.setItem(INFO_PAGE_KEY, showInfoPageCheckbox.checked.toString());
            if (settingsChangeCallback) {
                settingsChangeCallback({ showInfoPage: showInfoPageCheckbox.checked });
            }
        });
    }

    // Function to show info popup on page load
    function showInfoPopup() {
        const stored = localStorage.getItem(INFO_PAGE_KEY);
        const legacy = localStorage.getItem(LEGACY_WELCOME_KEY);
        const shouldShowInfo = stored !== null ? stored !== 'false' : legacy !== 'false';
        if (shouldShowInfo) {
            // Small delay to ensure DOM is fully ready
            setTimeout(() => {
                showPopup(null, 'popupInfo');
            }, 500);
        }
    }

    // Store reference to show info popup for external use
    const infoPopupManager = {
        show() {
            showPopup(null, 'popupInfo');
        },
        setShowOnStartup(value) {
            localStorage.setItem(INFO_PAGE_KEY, value.toString());
            if (showInfoPageCheckbox) {
                showInfoPageCheckbox.checked = value;
            }
        }
    };

    // Handle info popup tabs
    const infoTabBtns = document.querySelectorAll('.info-tab-btn');
    const infoTabContents = document.querySelectorAll('.info-tab-content');

    infoTabBtns.forEach(btn => {
        btn.addEventListener('click', () => {
            const tabId = btn.getAttribute('data-tab');
            
            // Remove active class from all buttons and contents
            infoTabBtns.forEach(b => b.classList.remove('active'));
            infoTabContents.forEach(content => content.style.display = 'none');
            
            // Add active class to clicked button
            btn.classList.add('active');
            
            // Show corresponding content
            const contentElement = document.getElementById(`tab-${tabId}`);
            if (contentElement) {
                contentElement.style.display = 'block';
            }
        });
    });

    document.getElementById('btnScreenshot').onclick = () => {
        let canvasImage = document.getElementById('canvas').toDataURL('image/png');
        // this can be used to download any image from webpage to local disk
        let xhr = new XMLHttpRequest();
        xhr.responseType = 'blob';
        xhr.onload = function () {
            let a = document.createElement('a');
            a.href = window.URL.createObjectURL(xhr.response);
            function getTimestamp() {
                const now = new Date();
                return now.toISOString().replace(/\D/g, '').slice(0, 14);
            }
            a.download = `5dc_${getTimestamp()}.png`;
            a.style.display = 'none';
            document.body.appendChild(a);
            a.click();
            a.remove();
        };
        xhr.open('GET', canvasImage);
        xhr.send();
    };

    /* ================= Unified Button Controls ================= */
    const buttonControls = {
        // All button definitions with their selectors
        buttonDefinitions: {
            // Navigation buttons
            prev: { selector: '.remote-btn.up', callback: null },
            next: { selector: '.remote-btn.down', callback: null },
            undo: { selector: '.remote-btn.left', callback: null },
            redo: { selector: '.remote-btn.right', callback: null },
            submit: { selector: '.remote-btn.ok', callback: null },
        },

        // References to button elements
        buttons: {},

        /**
         * Initialize all button references and event listeners
         */
        init() {
            const remoteButtons = document.querySelectorAll('.remote-btn');
            remoteButtons.forEach(btn => {
                for (const [action, def] of Object.entries(this.buttonDefinitions)) {
                    if (btn.matches(def.selector)) {
                        this.buttons[action] = btn;
                        this._attachListener(action);
                        break;
                    }
                }
            })
        },

        /**
         * Attach click listener to a button
         * @private
         * @param {string} action - Button action name
         */
        _attachListener(action) {
            const btn = this.buttons[action];
            if (!btn) return;

            btn.onclick = () => {
                const callback = this.buttonDefinitions[action].callback;
                if (callback) callback();
            };
        },

        /**
         * Set a callback function for a button
         * @param {string} action - Button action name
         * @param {Function} callback - Function to call when button is clicked
         */
        setCallback(action, callback) {
            if (Object.prototype.hasOwnProperty.call(this.buttonDefinitions, action)) {
                this.buttonDefinitions[action].callback = callback;
            }
        },

        /**
         * Enable a button
         * @param {string} action - Button action name
         */
        enable(action) {
            const btn = this.buttons[action];
            if (!btn) return;
            btn.classList.remove('inactive');
            btn.classList.add('active');
            btn.disabled = false;
        },

        /**
         * Disable a button
         * @param {string} action - Button action name
         */
        disable(action) {
            const btn = this.buttons[action];
            if (!btn) return;
            btn.classList.remove('active');
            btn.classList.add('inactive');
            btn.disabled = true;
        },

        /**
         * Check if a button is enabled
         * @param {string} action - Button action name
         * @returns {boolean}
         */
        isEnabled(action) {
            const btn = this.buttons[action];
            if (!btn) return false;
            return !btn.classList.contains('inactive');
        },

        /**
         * Set multiple callbacks at once
         * @param {Object} callbacks - Object with action names as keys and callbacks as values
         */
        setCallbacks(callbacks) {
            Object.entries(callbacks).forEach(([action, callback]) => {
                this.setCallback(action, callback);
            });
        },

        /**
         * Enable multiple buttons at once
         * @param {Array<string>} actions - Array of action names to enable
         */
        enableMultiple(actions) {
            actions.forEach(action => this.enable(action));
        },

        /**
         * Disable multiple buttons at once
         * @param {Array<string>} actions - Array of action names to disable
         */
        disableMultiple(actions) {
            actions.forEach(action => this.disable(action));
        }
    };

    // Initialize button controls
    buttonControls.init();

    /* ================= Keyboard Shortcuts ================= */
    const keyboardShortcuts = {
        // Map of key codes to button actions
        shortcuts: {
            'ArrowUp': 'prev',
            'ArrowDown': 'next',
            'ArrowLeft': 'undo',
            'ArrowRight': 'redo',
            'Enter': 'submit',
            'h': 'hint',
            ' ': 'focus'
        },

        /**
         * Simulate button press with visual feedback
         * @private
         * @param {string} action - Button action name
         */
        _pressButton(action) {
            const btn = buttonControls.buttons[action];
            if (!btn)
            {
                if(action === 'hint' && hintCallback)
                {
                    hintCallback();
                }
                else if(action === 'focus' && focusCallback)
                {
                    focusCallback();
                }
                return;
            }

            // Add pressed effect
            btn.classList.add('pressed');
            
            // Trigger the button's callback
            const callback = buttonControls.buttonDefinitions[action].callback;
            if (callback && !btn.classList.contains('inactive')) {
                callback();
            }
            
            // Remove pressed effect after 100ms
            setTimeout(() => {
                btn.classList.remove('pressed');
            }, 100);
        },

        /**
         * Initialize keyboard event listener
         */
        init() {
            document.addEventListener('keydown', (e) => {
                // Check if the key is a valid shortcut
                let key = e.key;
                
                // For space key, we need to prevent default scrolling only when not in editable elements
                if (key === ' ') {
                    const activeElement = document.activeElement;
                    const isEditable = activeElement && (
                        activeElement.tagName === 'INPUT' ||
                        activeElement.tagName === 'TEXTAREA' ||
                        activeElement.contentEditable === 'true'
                    );
                    if (!isEditable) {
                        e.preventDefault();
                    }
                }
                
                // Check if the key matches any shortcut
                if (Object.prototype.hasOwnProperty.call(this.shortcuts, key)) {
                    const action = this.shortcuts[key];
                    this._pressButton(action);
                }
            });
        }
    };

    // Initialize keyboard shortcuts
    keyboardShortcuts.init();
    const selectControls = {
        /**
         * Set the options in the select container
         * @param {Array<string>} newOptions - Array of option strings
         */
        setOptions(newOptions) {
            options.length = 0;
            options.push(...newOptions);
            selectedIndex = 0;
            renderOptions();
        },

        /**
         * Get the currently selected option value
         * @returns {string} The selected option text
         */
        getSelectedValue() {
            return options[selectedIndex];
        },

        /**
         * Get the index of the selected option
         * @returns {number} The selected index
         */
        getSelectedIndex() {
            return selectedIndex;
        },

        /**
         * Set the selected option by index
         * @param {number} index - The index to select
         */
        setSelectedIndex(index) {
            if (index >= 0 && index < options.length) {
                selectedIndex = index;
                selectedDisplay.textContent = options[selectedIndex];
                renderOptions();
            }
        },

        /**
         * Add a new option
         * @param {string} optionText - The text for the new option
         */
        addOption(optionText) {
            options.push(optionText);
            renderOptions();
            controlPanel.style.height = calculateCollapasedPanelHeight();
        },

        /**
         * Get all options
         * @returns {Array<string>} All options
         */
        getAllOptions() {
            return [...options];
        }
    };

    return {
        buttons: buttonControls,
        select: selectControls,
        
        /**
         * Set the content text of hud-text element
         * @param {string} content - The text content to set
         */
        setHudText(content) {
            hudText.textContent = content;
            setHudTextOriginal(content);
            autoToggleHudForText(content);
        },

        /**
         * Get the current content of hud-text element
         * @returns {string} The current text content
         */
        getHudText() {
            return hudText.textContent;
        },
                /**
         * Set the title text of hud-title element
         * @param {string} title - The title text to set
         */
        setHudTitle(title) {
            hudTitle.textContent = title;
        },

        /**
         * Get the current title of hud-title element
         * @returns {string} The current title text
         */
        getHudTitle() {
            return hudTitle.textContent;
        },

        /**
         * Set the callback function for when hud-text content changes
         * @param {Function} callback - Function that receives the new content
         */
        setHudTextChangeCallback(callback) {
            hudTextChangeCallback = callback;
        },

        /**
         * Set the callback function for when Update Comment is pressed
         * @param {Function} callback - Function that receives the current comment text
         */
        setCommentsEditCallback(callback) {
            commentsEditCallback = callback;
        },
        
        /**
         * Set the callback function for when import load button is pressed
         * @param {Function} callback - Function that receives the imported data
         */
        setImportCallback(callback) {
            importCallback = callback;
        },

        /**
         * Set the callback function for when export button is pressed
         * @param {Function} callback - Function that returns the data to export
         */
        setExportCallback(callback) {
            exportCallback = callback;
        },

        /**
         * Set the content of exportTextarea
         * @param {string} data 
         */
        setExportData (data) {
            document.getElementById('exportTextarea').value = data;
        },

        /**
         * Set the callback function for when focus button is pressed
         * @param {Function} callback - Function to call when focus button is clicked
         */
        setFocusCallback(callback) {
            focusCallback = callback;
        },

        setHintCallback(callback) {
            hintCallback = callback;
        },

        /**
         * Set a callback function that will be called when settings change
         * @param {Function} callback - Function receiving a settings object
         */
        setSettingsChangeCallback(callback) {
            settingsChangeCallback = callback;
        },

        /**
         * Set the Version Number in the Info popup
         */
        setVersionNumber(version) {
            const infoEl = document.getElementById('infoVersionNumber');
            if (infoEl) infoEl.textContent = version;
        },

        /**
         * Set callback for HUD light clicks
         * The callback is invoked when the user clicks the HUD light to toggle its blinking state.
         * @param {Function} callback - Function that receives a boolean: true when stopping blink, false when starting blink
         */
        setHudLightCallback(callback) {
            lightCallback = callback;
        },

        /**
         * Set HUD light state
         * Controls whether the HUD light is visible and interactive. When on, the light blinks and can be
         * clicked to toggle between blinking and steady states. When off, the light is hidden and non-interactive.
         * @param {boolean} on - True to show and enable the light with blinking animation, false to hide it
         */
        setHudLight(on) {
            if (on) {
                if (light.classList.contains("off")) {
                    light.classList.remove("off");
                    light.classList.add("blink");
                }
                light.onclick = () => {
                    if (light.classList.contains("blink")) {
                        light.classList.remove("blink");
                        if (lightCallback) lightCallback(true);
                    } else {
                        light.classList.add("blink");
                        if (lightCallback) lightCallback(false);
                    }
                };
            } else {
                light.classList.add("off");
                light.classList.remove("blink");
                light.onclick = null;
            }
        },

        /**
         * Show the info popup
         */
        showInfoPage() {
            showInfoPopup();
        }
    };
})();

export function setCommentsEditCallback(callback) {
    UI.setCommentsEditCallback(callback);
}
