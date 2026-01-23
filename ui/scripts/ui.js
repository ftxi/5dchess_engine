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

    function updateToggle(expanded) {
        toggle.innerHTML = expanded ? ICON_UP : ICON_DOWN;
    }

    toggle.onclick = () => {
        const expanded = hud.classList.toggle("expanded");
        hud.classList.toggle("collapsed", !expanded);
        updateToggle(expanded);
    };

    updateToggle(false);
    /* ================= Light Toggle ================= */
    const light = document.getElementById("light");
    light.onclick = () => {
        light.classList.toggle("off");
    };

    /* ================= Control Panel ================= */
    const controlPanel = document.getElementById("controlPanel");
    const panelToggle = document.getElementById("panelToggle");
    const selectContainer = document.getElementById("selectContainer");
    const selectedDisplay = document.getElementById("selectedDisplay");
    const hintBtn = document.getElementById("hintBtn");

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
                controlPanel.classList.remove('expanded');
                controlPanel.classList.add('collapsed');
                updatePanelToggle(false);
                renderOptions();
                controlPanel.style.height = calculateCollapasedPanelHeight();
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

    hintBtn.onclick = () => {
        const newOption = `New entry ${options.length + 1}`;
        options.unshift(newOption);
        selectedIndex++;
        renderOptions();
    };

    // Initialize
    updatePanelToggle(false);
    renderOptions();
    controlPanel.style.height = calculateCollapasedPanelHeight();

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
            hud.style.left = Math.max(18, newLeft) + '%';
        } else {
            hud.style.left = '50%';
        }
        
        // Check if left edge is too close to screen edge
        const hudLeft = parseFloat(hud.style.left) / 100 * screenWidth - hudHalfWidth;
        if (hudLeft < 18) {
            hud.style.left = '18px';
            hud.style.transform = 'translateX(0)';
        } else {
            hud.style.transform = 'translateX(-50%)';
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
        button.classList.add('active');
        activeButton = button;
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

    document.getElementById('btnInfo').onclick = () => {
        showPopup(this, 'popupInfo');
    };

    document.getElementById('btnImport').onclick = () => {
        showPopup(this, 'popupImport');
    };

    document.getElementById('loadButton').onclick = () => {
        const data = document.getElementById('importTextarea').value;
        if (importCallback) {
            importCallback(data);
        }
        closePopup();
    }

    document.getElementById('btnExport').onclick = () => {
        showPopup(this, 'popupExport');
        // Call export callback to populate textarea
        if (exportCallback) {
            const exportData = exportCallback();
            document.getElementById('exportTextarea').value = exportData;
        }
    };

    document.getElementById('btnSettings').onclick = () => {
        showPopup(this, 'popupSettings');
    };

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
            // Panel buttons
            submit: { selector: '.remote-btn.ok', callback: null },
            hint: { element: null, callback: null } // hint button is handled separately
        },

        // References to button elements
        buttons: {},

        /**
         * Initialize all button references and event listeners
         */
        init() {
            // Initialize remote buttons
            const remoteButtons = document.querySelectorAll('.remote-btn');
            remoteButtons.forEach(btn => {
                for (const [action, def] of Object.entries(this.buttonDefinitions)) {
                    if (action === 'hint') continue; // Skip hint, handled separately
                    if (btn.matches(def.selector)) {
                        this.buttons[action] = btn;
                        this._attachListener(action);
                        break;
                    }
                }
            });

            // Initialize hint button
            this.buttons.hint = hintBtn;
            this._attachListener('hint');
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
            console.log(`enabling ${action}`);
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
            console.log(`diabling ${action}`);
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

    /* ================= Select Options Controls ================= */
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
            controlPanel.style.height = calculateCollapasedPanelHeight();
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
         * Set the callback function for when focus button is pressed
         * @param {Function} callback - Function to call when focus button is clicked
         */
        setFocusCallback(callback) {
            focusCallback = callback;
        }
    };
})();
