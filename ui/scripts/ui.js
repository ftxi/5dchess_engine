// ui.js

import { setGameMetadata as renderGameMetadata } from './metadata.js';
import { compactViewport } from './mobile.js';
import { getExportFilename } from './filename.js';
import { createKeyboardShortcuts } from './keyboard.js';
import { readCommentText } from './comment.js';
import { createImportDiagnostics } from './import-diagnostics.js';

export const UI = (() => {
    /* ================= SVG Icons ================= */
    const ICON_UP = `
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#555" aria-hidden="true" focusable="false">
<path d="M12 4.83582L5.79291 11.0429L7.20712 12.4571L12 7.66424L16.7929 12.4571L18.2071 11.0429L12 4.83582ZM12 10.4857L5.79291 16.6928L7.20712 18.107L12 13.3141L16.7929 18.107L18.2071 16.6928L12 10.4857Z"/>
</svg>`;

    const ICON_DOWN = `
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#555" aria-hidden="true" focusable="false">
<path d="M12 19.1642L18.2071 12.9571L16.7929 11.5429L12 16.3358L7.20712 11.5429L5.79291 12.9571L12 19.1642ZM12 13.5143L18.2071 7.30722L16.7929 5.89301L12 10.6859L7.20712 5.89301L5.79291 7.30722L12 13.5143Z"/>
</svg>`;

    /* ================= HUD Toggle ================= */
    const hud = document.getElementById("hud");
    const toggle = document.getElementById("toggle");
    const hudText = document.querySelector('.hud-text');
    const hudTitle = document.querySelector('.hud-title');
    const boardCanvas = document.getElementById('canvas');
    const hudCommentActions = document.getElementById('hudCommentActions');
    const hudUpdateComment = document.getElementById('hudUpdateComment');
    const hudDiscardChanges = document.getElementById('hudDiscardChanges');
    const AUTO_TOGGLE_KEY = 'auto-toggle-comments';
    let autoToggleComments = true;
    try {
        const storedAutoToggle = localStorage.getItem(AUTO_TOGGLE_KEY);
        autoToggleComments = storedAutoToggle !== null ? storedAutoToggle === 'true' : true;
    } catch {
        autoToggleComments = true;
    }

    function updateToggle(expanded) {
        if (!toggle) return;
        toggle.innerHTML = expanded ? ICON_UP : ICON_DOWN;
        toggle.setAttribute('aria-expanded', String(expanded));
        toggle.setAttribute('aria-label', expanded ? 'Collapse comment' : 'Expand comment');
    }

    function setHudExpanded(expanded) {
        if (!hud) return;
        hud.classList.toggle("expanded", expanded);
        hud.classList.toggle("collapsed", !expanded);
        if (!expanded) hud.scrollTop = 0;
        updateToggle(expanded);
    }

    function autoToggleHudForText(text) {
        if (!autoToggleComments || compactViewport.matches) return;
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
    let hudTextOriginal = hudText ? readCommentText(hudText) : '';

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
        const hasChanges = readCommentText(hudText) !== hudTextOriginal;
        hudCommentActions.classList.toggle('visible', hasChanges);
        
        if (hasChanges) {
            const text = readCommentText(hudText).trim();
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
            hudTextChangeCallback(readCommentText(hudText));
        }
        updateHudCommentActions();
    });

    if (hudUpdateComment) {
        hudUpdateComment.addEventListener('click', () => {
            if (commentsEditCallback) {
                commentsEditCallback(readCommentText(hudText));
            }
            setHudTextOriginal(readCommentText(hudText));
        });
    }

    if (hudDiscardChanges) {
        hudDiscardChanges.addEventListener('click', () => {
            hudText.textContent = hudTextOriginal;
            updateHudCommentActions();
        });
    }
    // Clicking the canvas transfers real keyboard focus away from the editor.
    boardCanvas.addEventListener('pointerdown', () => boardCanvas.focus({ preventScroll: true }));
    let commentSelection = null;
    let pointerFocusingComment = false;
    hudText.addEventListener('pointerdown', () => { pointerFocusingComment = true; });
    document.addEventListener('pointerup', () => { pointerFocusingComment = false; });
    document.addEventListener('pointercancel', () => { pointerFocusingComment = false; });
    hudText.addEventListener('blur', () => {
        const selection = window.getSelection();
        if (selection.rangeCount && hudText.contains(selection.anchorNode) && hudText.contains(selection.focusNode)) {
            commentSelection = selection.getRangeAt(0).cloneRange();
            selection.removeAllRanges();
        }
    });
    hudText.addEventListener('focus', () => {
        if (!pointerFocusingComment && commentSelection && hudText.contains(commentSelection.startContainer)) {
            const selection = window.getSelection();
            selection.removeAllRanges();
            selection.addRange(commentSelection);
        }
    });

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
    hintBtn.disabled = true;

    const options = [];
    let selectedIndex = -1;
    let deletionCallback = null;
    let selectionCallback = null;
    let exportOpenCallback = null;
    const pendingPanel = document.getElementById('pendingBranches');
    let pendingPanelTimer = null;
    document.getElementById('restoreAllBranches').onclick = () => deletionCallback?.('restore_all');
    document.getElementById('confirmBranches').onclick = () => deletionCallback?.('commit_deletions');

    function calculateCollapasedPanelHeight()
    {
        const remoteHeight = document.querySelector('.remote-control').offsetHeight;
        const selectedDisplayHeight = selectedDisplay.offsetHeight;
        const toggleHeight = 28;
        const collapsedHeight = remoteHeight + selectedDisplayHeight + toggleHeight;
        return collapsedHeight + 'px';
    }

    function renderOptions(preserveFocus = true) {
        const active = preserveFocus ? document.activeElement : null;
        const focusedAction = active?.dataset.branchAction;
        const focusedOption = active?.dataset.branchOption;
        const focusedHint = active === hintBtn;
        selectContainer.replaceChildren();
        options.forEach((opt, idx) => {
            const row = document.createElement('div');
            row.className = 'continuation-row';
            row.classList.toggle('pending-deletion', Boolean(opt.deleted));
            const option = document.createElement('button');
            option.type = 'button';
            option.className = 'select-option';
            option.classList.toggle('illegal-history', Boolean(opt.is_illegal));
            if (opt.is_illegal) {
                option.title = 'Contains illegal history';
                option.setAttribute('aria-label', `${opt.pgn} — contains illegal history`);
            }
            option.dataset.branchOption = String(idx);
            option.classList.toggle('selected', idx === selectedIndex);
            option.setAttribute('aria-pressed', String(idx === selectedIndex));
            option.setAttribute('aria-disabled', String(Boolean(opt.deleted)));
            const notation = document.createElement('span');
            notation.className = 'continuation-notation';
            notation.textContent = opt.pgn;
            option.append(notation);
            option.onclick = () => {
                if (opt.deleted) return;
                selectedIndex = idx;
                renderOptions();
                selectContainer.querySelectorAll('.select-option')[idx]?.focus();
            };
            const action = document.createElement('button');
            action.type = 'button';
            action.className = 'branch-action';
            action.dataset.branchAction = String(idx);
            const label = opt.deleted ? 'Restore branch' : 'Delete branch';
            action.title = label;
            action.setAttribute('aria-label', `${label}: ${opt.pgn}`);
            const symbol = opt.deleted ? 'rotate-ccw' : 'trash';
            action.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" aria-hidden="true"><use href="./icons/lucide.svg#${symbol}"></use></svg>`;
            action.onclick = () => deletionCallback?.('toggle_deletion', idx);
            row.append(option, action);
            selectContainer.append(row);
        });
        selectContainer.append(hintBtn);
        selectedDisplay.textContent = options[selectedIndex]?.pgn
            || (options.length ? 'Choose continuation' : 'No continuations');
        selectionCallback?.(selectedIndex);
        const focusTarget = focusedHint ? hintBtn
            : focusedAction !== undefined ? selectContainer.querySelector(`[data-branch-action="${focusedAction}"]`)
            : focusedOption !== undefined ? selectContainer.querySelector(`[data-branch-option="${focusedOption}"]`)
            : null;
        focusTarget?.focus({ preventScroll: true });
        if (!compactViewport.matches && controlPanel.classList.contains('collapsed')) {
            controlPanel.style.height = calculateCollapasedPanelHeight();
        }
    }

    function updatePanelToggle(expanded) {
        panelToggle.innerHTML = expanded ? ICON_UP : ICON_DOWN;
        panelToggle.setAttribute('aria-expanded', String(expanded));
        panelToggle.setAttribute('aria-label', expanded ? 'Collapse continuations' : 'Expand continuations');
    }

    panelToggle.onclick = () => {
        const isExpanded = controlPanel.classList.toggle('expanded');
        controlPanel.classList.toggle('collapsed', !isExpanded);
        if (!isExpanded) controlPanel.scrollTop = 0;
        
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


    window.addEventListener('resize', () => {
        if (!compactViewport.matches && controlPanel.classList.contains('collapsed')) {
            controlPanel.style.height = calculateCollapasedPanelHeight();
        }
    });


    /* ================= Buttons Panel ================= */
    // Focus button callback
    let focusCallback = null;
    let canvasNavigationCallback = null;

    document.getElementById('btnFocus').onclick = () => {
        if (focusCallback) focusCallback();
    };

    document.getElementById('btnScreenshot').onclick = () => {
        console.log('Screenshot button clicked');
    };

    document.getElementById('btnEdit').onclick = () => {
        console.log('Edit button clicked');
    };

    let flipped = false;

    document.getElementById('btnFlip').onclick = () => {
        flipped = !flipped;
        if(settingsChangeCallback) {
            settingsChangeCallback({ flipped });
        }
    };

    /* ================= Pop-up System ================= */
    const popupOverlay = document.getElementById('popupOverlay');
    let activeButton = null;

    // Callbacks for import/export
    let importCallback = null;
    let exportCallback = null;
    let exportRequestId = 0;
    let gameMetadata = {};
    const popupStack = [];

    function showPopup(button, popupName) {
        // Keep older panels open underneath newer panels. Avoid duplicate entries
        // when a trigger is clicked repeatedly.
        const existingIndex = popupStack.findIndex(entry => entry.popupName === popupName);
        if (existingIndex !== -1) {
            popupStack.splice(existingIndex, 1);
        }
        if (activeButton) activeButton.classList.remove('active');
        popupStack.push({ button, popupName });
        popupStack.forEach((entry, index) => {
            document.getElementById(entry.popupName).style.zIndex = String(index + 1);
        });
        popupOverlay.classList.add('show');
        document.getElementById(popupName).classList.add('show');
        if (compactViewport.matches) document.querySelector(`#${popupName} .mobile-popup-close`)?.focus();
        if (button) {
            button.classList.add('active');
            activeButton = button;
        } else {
            activeButton = null;
        }
    }

    function closePopup() {
        const entry = popupStack.pop();
        if (!entry) return;
        if (entry.popupName === 'popupImport') importDiagnostics.cancelPending();
        document.getElementById(entry.popupName).classList.remove('show');
        if (entry.button) entry.button.classList.remove('active');
        const previous = popupStack[popupStack.length - 1];
        activeButton = previous ? previous.button : null;
        if (activeButton) activeButton.classList.add('active');
        if (popupStack.length === 0) popupOverlay.classList.remove('show');
        if (compactViewport.matches) {
            const target = previous ? (entry.button || document.querySelector(`#${previous.popupName} .mobile-popup-close`)) : entry.button;
            target?.focus();
            if (entry.popupName === 'popupExportOptions' && exportSettingsChanged()) saveExportSettings();
        }
    }

    function togglePopup(button, popupName) {
        const topPopup = popupStack[popupStack.length - 1];
        if (topPopup && topPopup.popupName === popupName) {
            closePopup();
            return;
        }
        showPopup(button, popupName);
    }

    // Full-screen phone dialogs need an explicit, keyboard-accessible close action.
    document.querySelectorAll('.popup-window').forEach(popup => {
        const header = document.createElement('div');
        header.className = 'dialog-header';
        const title = popup.querySelector('.popup-title');
        title.id = popup.id + 'Title';
        popup.setAttribute('role', 'dialog');
        popup.setAttribute('aria-labelledby', title.id);
        header.appendChild(title);
        const close = document.createElement('button');
        close.type = 'button';
        close.className = 'mobile-popup-close';
        close.textContent = 'Close';
        close.setAttribute('aria-label', 'Close dialog');
        close.addEventListener('click', closePopup);
        header.appendChild(close);
        popup.prepend(header);
    });

    // Close popup when clicking overlay (but not the window)
    popupOverlay.onclick = (e) => {
        if (e.target === popupOverlay) {
            closePopup();
        }
    };

    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' && popupStack.length > 0) {
            e.preventDefault();
            closePopup();
        }
    });

    document.getElementById('btnInfo').onclick = (event) => {
        showPopup(event.currentTarget, 'popupInfo');
    };

    document.getElementById('btnImport').onclick = (event) => {
        showPopup(event.currentTarget, 'popupImport');
    };

    let importCancelCallback = null;
    const importDiagnostics = createImportDiagnostics({
        textarea: document.getElementById('importTextarea'),
        overlay: document.getElementById('importHighlight'),
        mirror: document.getElementById('importMirror'),
        row: document.getElementById('importDiagnostic'),
        location: document.getElementById('importDiagnosticLocation'),
        message: document.getElementById('importDiagnosticMessage'),
        heading: document.getElementById('importDiagnosticHeading'),
        cancel: id => importCancelCallback?.(id),
    });
    document.getElementById('loadButton').onclick = () => {
        const request = importDiagnostics.begin();
        importCallback?.(request.pgn, request.requestId);
    };

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
                importDiagnostics.edit();
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
            importDiagnostics.edit();
        };
        reader.onerror = () => {
            alert('Failed to read file.');
        };
        reader.readAsText(file);
    };

    function requestExport() {
        document.getElementById('copyButton').disabled = true;
        document.getElementById('saveButton').disabled = true;
        document.getElementById('exportTextarea').setAttribute('aria-busy', 'true');
        document.getElementById('exportPreviewStatus').hidden = false;
        if (exportCallback) exportCallback(++exportRequestId);
    }

    function openExport() {
        setExportControls(appliedExportSettings);
        const defaultFilename = getExportFilename(gameMetadata);
        document.getElementById('exportFilename').value = defaultFilename.slice(0, -'.5dpgn'.length);
        document.getElementById('exportExtension').value = '.5dpgn';
        showPopup(document.getElementById('btnExport'), 'popupExport');
        if (settingsChangeCallback) settingsChangeCallback(appliedExportSettings);
        requestExport();
    };

    document.getElementById('btnExport').onclick = () => exportOpenCallback ? exportOpenCallback() : openExport();

    document.getElementById('exportMoreOptionsButton').onclick = (event) => {
        togglePopup(event.currentTarget, 'popupExportOptions');
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
            const button = document.getElementById('copyButton');
            const mobileLabel = button.querySelector('.mobile-export-label');
            const label = document.getElementById('copiedLabel');
            mobileLabel.textContent = 'Copied!';
            button.classList.add('active');
            label.classList.add('visible');
            setTimeout(() => {
                mobileLabel.textContent = 'Copy';
                button.classList.remove('active');
                label.classList.remove('visible');
            }, 2000);
        }).catch(() => {
            alert('Failed to copy to clipboard.');
        });
    };

    // Download using the loaded game headers, independent of export options.
    document.getElementById('saveButton').onclick = () => {
        const textarea = document.getElementById('exportTextarea');
        const text = textarea.value;
        if (!text) {
            alert('Nothing to save.');
            return;
        }
        
        let fullFilename = getExportFilename(gameMetadata);
        if (!compactViewport.matches) {
            const filename = document.getElementById('exportFilename').value.trim();
            if (!filename) {
                alert('Please enter a filename.');
                return;
            }
            fullFilename = filename + document.getElementById('exportExtension').value.trim();
        }

        // Create blob and download
        const blob = new Blob([text], { type: 'text/plain' });
        const url = URL.createObjectURL(blob);
        const link = document.createElement('a');
        link.href = url;
        link.download = fullFilename;
        
        document.body.appendChild(link);
        link.click();
        document.body.removeChild(link);
        setTimeout(() => URL.revokeObjectURL(url), 1000);
    };

    // Export options checkboxes
    const exportMateCheckbox = document.getElementById('exportMate');
    const exportShortNotationCheckbox = document.getElementById('exportShortNotation');
    const exportRelativeNotationCheckbox = document.getElementById('exportRelativeNotation');
    const exportOutcomeCheckbox = document.getElementById('exportOutcome');
    const exportStandardMetadataOnlyCheckbox = document.getElementById('exportStandardMetadataOnly');
    const exportCompleteGameTreeRadio = document.getElementById('exportCompleteGameTree');
    const exportGameHistoryRadio = document.getElementById('exportGameHistory');
    const EXPORT_MATE_KEY = 'export-mate';
    const EXPORT_SHORT_NOTATION_KEY = 'export-short-notation';
    const EXPORT_RELATIVE_NOTATION_KEY = 'export-relative-notation';
    const EXPORT_OUTCOME_KEY = 'export-outcome';
    const EXPORT_STANDARD_METADATA_ONLY_KEY = 'export-standard-metadata-only';
    const EXPORT_COMPLETE_TREE_KEY = 'export-complete-game-tree';
    let appliedExportSettings = null;

    const getExportSettings = () => ({
        exportMate: exportMateCheckbox.checked,
        exportShortNotation: exportShortNotationCheckbox.checked,
        exportRelativeNotation: exportRelativeNotationCheckbox.checked,
        exportOutcome: exportOutcomeCheckbox.checked,
        exportStandardMetadataOnly: exportStandardMetadataOnlyCheckbox.checked,
        exportCompleteGameTree: exportCompleteGameTreeRadio.checked
    });

    const setExportControls = (settings) => {
        exportMateCheckbox.checked = settings.exportMate;
        exportShortNotationCheckbox.checked = settings.exportShortNotation;
        exportRelativeNotationCheckbox.checked = settings.exportRelativeNotation;
        exportOutcomeCheckbox.checked = settings.exportOutcome;
        exportStandardMetadataOnlyCheckbox.checked = settings.exportStandardMetadataOnly;
        exportCompleteGameTreeRadio.checked = settings.exportCompleteGameTree;
        exportGameHistoryRadio.checked = !settings.exportCompleteGameTree;
    };

    const saveExportSettings = () => {
        const settings = getExportSettings();
        try {
            localStorage.setItem(EXPORT_MATE_KEY, settings.exportMate.toString());
            localStorage.setItem(EXPORT_SHORT_NOTATION_KEY, settings.exportShortNotation.toString());
            localStorage.setItem(EXPORT_RELATIVE_NOTATION_KEY, settings.exportRelativeNotation.toString());
            localStorage.setItem(EXPORT_OUTCOME_KEY, settings.exportOutcome.toString());
            localStorage.setItem(EXPORT_STANDARD_METADATA_ONLY_KEY, settings.exportStandardMetadataOnly.toString());
            localStorage.setItem(EXPORT_COMPLETE_TREE_KEY, settings.exportCompleteGameTree.toString());
        } catch {
            // Ignore storage errors
        }
        appliedExportSettings = settings;
        if (settingsChangeCallback) settingsChangeCallback(settings);
        requestExport();
    };

    // Initialize export options from localStorage
    try {
        const storedMate = localStorage.getItem(EXPORT_MATE_KEY);
        const storedShortNotation = localStorage.getItem(EXPORT_SHORT_NOTATION_KEY);
        const storedRelativeNotation = localStorage.getItem(EXPORT_RELATIVE_NOTATION_KEY);
        const storedOutcome = localStorage.getItem(EXPORT_OUTCOME_KEY);
        const storedStandardMetadataOnly = localStorage.getItem(EXPORT_STANDARD_METADATA_ONLY_KEY);
        const storedCompleteTree = localStorage.getItem(EXPORT_COMPLETE_TREE_KEY);
        exportMateCheckbox.checked = storedMate !== null ? storedMate === 'true' : true;
        exportShortNotationCheckbox.checked = storedShortNotation !== null ? storedShortNotation === 'true' : false;
        exportRelativeNotationCheckbox.checked = storedRelativeNotation !== null ? storedRelativeNotation === 'true' : false;
        exportOutcomeCheckbox.checked = storedOutcome !== null ? storedOutcome === 'true' : false;
        exportStandardMetadataOnlyCheckbox.checked = storedStandardMetadataOnly !== null ? storedStandardMetadataOnly === 'true' : false;
        exportCompleteGameTreeRadio.checked = storedCompleteTree !== 'false';
        exportGameHistoryRadio.checked = storedCompleteTree === 'false';
    } catch {
        // Fallback to defaults if localStorage is not available (e.g., Safari private mode)
        exportMateCheckbox.checked = true;
        exportShortNotationCheckbox.checked = false;
        exportRelativeNotationCheckbox.checked = false;
        exportOutcomeCheckbox.checked = false;
        exportStandardMetadataOnlyCheckbox.checked = false;
        exportCompleteGameTreeRadio.checked = true;
        exportGameHistoryRadio.checked = false;
    }
    appliedExportSettings = getExportSettings();

    const exportSettingsChanged = () => Object.entries(getExportSettings())
        .some(([key, value]) => value !== appliedExportSettings[key]);

    // Desktop keeps explicit Refresh; mobile commits once when Options closes.
    document.getElementById('refreshButton').onclick = saveExportSettings;

    // Move the same controls on resize so checked values and edited filenames survive.
    const exportPlacements = [
        [document.getElementById('exportMoreOptionsButton'), 'mobileExportFooter'],
        [document.getElementById('copyButton'), 'mobileExportFooter'],
        [document.getElementById('saveButton'), 'mobileExportFooter'],
        [exportMateCheckbox.closest('label'), 'mobileExportBasicOptions'],
        [exportShortNotationCheckbox.closest('label'), 'mobileExportBasicOptions']
    ].map(([element, destination]) => {
        const marker = document.createComment('desktop export control');
        element.before(marker);
        return { element, destination, marker };
    });
    function updateExportLayout() {
        const compact = compactViewport.matches;
        for (const { element, destination, marker } of exportPlacements) {
            if (compact) document.getElementById(destination).appendChild(element);
            else marker.after(element);
        }
        for (const checkbox of [exportMateCheckbox, exportShortNotationCheckbox]) {
            checkbox.closest('label').classList.toggle('popup-control', compact);
        }
        document.getElementById('popupExportOptions').querySelector('.popup-title').textContent = compact ? 'Export Options' : 'More Options';
    }
    compactViewport.addEventListener('change', updateExportLayout);
    updateExportLayout();

    document.getElementById('btnSettings').onclick = (event) => {
        showPopup(event.currentTarget, 'popupSettings');
    };

    // Settings change callback wiring
    let settingsChangeCallback = null;
    const doNotReverseFilesCheckbox = document.getElementById('doNotReverseFilesWhenFlipped');
    const DO_NOT_REVERSE_FILES_KEY = 'do-not-reverse-files-when-flipped';
    try {
        doNotReverseFilesCheckbox.checked = localStorage.getItem(DO_NOT_REVERSE_FILES_KEY) === 'true';
    } catch {
        // Keep the default when storage is unavailable.
    }
    doNotReverseFilesCheckbox.addEventListener('change', () => {
        const doNotReverseFilesWhenFlipped = doNotReverseFilesCheckbox.checked;
        try {
            localStorage.setItem(DO_NOT_REVERSE_FILES_KEY, String(doNotReverseFilesWhenFlipped));
        } catch {
            // The preference still applies for this session.
        }
        if (settingsChangeCallback) settingsChangeCallback({ doNotReverseFilesWhenFlipped });
    });
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

    const SHORT_MOVE_NOTATION_KEY = 'use-short-move-notation';
    const shortMoveNotationCheckbox = document.getElementById('useShortMoveNotation');
    try {
        shortMoveNotationCheckbox.checked = localStorage.getItem(SHORT_MOVE_NOTATION_KEY) !== 'false';
    } catch {
        // Keep the default when storage is unavailable.
    }
    shortMoveNotationCheckbox.addEventListener('change', () => {
        const useShortMoveNotation = shortMoveNotationCheckbox.checked;
        try {
            localStorage.setItem(SHORT_MOVE_NOTATION_KEY, String(useShortMoveNotation));
        } catch {
            // The preference still applies for this session.
        }
        if (settingsChangeCallback) settingsChangeCallback({ useShortMoveNotation });
    });

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
            } catch {
                // Ignore storage errors (e.g. Safari private mode)
            }
            if (settingsChangeCallback) {
                settingsChangeCallback({ autoToggleComments });
            }
            autoToggleHudForText(hudText ? readCommentText(hudText) : '');
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

    // The guide follows the initial browser layout; a manual choice lasts until reload.
    const interfaceGuideChoices = document.querySelectorAll('input[name="interfaceGuide"]');
    const interfaceGuideDesktop = document.getElementById('interfaceGuideDesktop');
    const interfaceGuideMobile = document.getElementById('interfaceGuideMobile');
    function updateInterfaceGuide() {
        const mobile = document.querySelector('input[name="interfaceGuide"]:checked').value === 'mobile';
        interfaceGuideDesktop.hidden = mobile;
        interfaceGuideMobile.hidden = !mobile;
    }
    interfaceGuideChoices.forEach(choice => {
        choice.checked = choice.value === (compactViewport.matches ? 'mobile' : 'desktop');
        choice.addEventListener('change', updateInterfaceGuide);
    });
    updateInterfaceGuide();

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

            btn.onclick = (event) => {
                const callback = this.buttonDefinitions[action].callback;
                if (callback) callback();
                // Pointer users can navigate immediately; keyboard users keep their button focus.
                if (event.detail > 0) boardCanvas.focus({ preventScroll: true });
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
        /**
         * Simulate button press with visual feedback
         * @private
         * @param {string} action - Button action name
         */
        _pressButton(action, options) {
            const btn = buttonControls.buttons[action];
            if (!btn)
            {
                if(action === 'hint' && hintCallback && !hintBtn.disabled)
                {
                    hintCallback();
                }
                else if(action === 'focus' && focusCallback)
                {
                    focusCallback();
                }
                else if (canvasNavigationCallback && ['nearby', 'navigate', 'up', 'left', 'down', 'right'].includes(action)) {
                    canvasNavigationCallback(action, options);
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
            const shortcuts = createKeyboardShortcuts({
                onAction: (action, options) => this._pressButton(action, options),
                isBlocked: () => Boolean(document.querySelector('dialog[open]')) || popupStack.length > 0 || (compactViewport.matches
                    && document.getElementById('mobileLayout').classList.contains('has-mobile-panel')
                    && !document.querySelector('[data-mobile-pane="tools"]').hidden)
            });
            document.addEventListener('keydown', shortcuts.handleKeyDown);
            document.addEventListener('keyup', shortcuts.handleKeyUp);
            for (const event of ['focusin', 'pointerdown', 'wheel', 'visibilitychange']) {
                document.addEventListener(event, shortcuts.reset, { capture: true });
            }
            window.addEventListener('blur', shortcuts.reset);
        }
    };

    // Initialize keyboard shortcuts
    keyboardShortcuts.init();
    const selectControls = {
        /**
         * Set the options in the select container
         * @param {Array<{pgn: string, deleted?: boolean, is_illegal?: boolean}>} newOptions - Continuation rows
         */
        setChangeCallback(callback) { selectionCallback = callback; },
        setOptions(newOptions, { selection, preserveFocus = false, scrollToBottom = false } = {}) {
            options.length = 0;
            options.push(...newOptions);
            selectedIndex = selection === undefined ? options.findIndex(option => !option.deleted)
                : selection >= 0 && selection < options.length && !options[selection].deleted ? selection : -1;
            renderOptions(preserveFocus);
            if (scrollToBottom) {
                const scroller = compactViewport.matches
                    ? selectContainer.closest('.mobile-sheet-content') : selectContainer;
                if (scroller) scroller.scrollTop = scroller.scrollHeight;
            }
        },

        /**
         * Get the currently selected option value
         * @returns {string} The selected option text
         */
        getSelectedValue() {
            return options[selectedIndex]?.pgn;
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
            if (index >= -1 && index < options.length) {
                selectedIndex = options[index]?.deleted ? -1 : index;
                renderOptions();
            }
        },

        /**
         * Add a new option
         * @param {string} optionText - The text for the new option
         */
        addOption(optionText) {
            options.push({ pgn: optionText });
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
        setDeletionCallback(callback) { deletionCallback = callback; },
        setExportOpenCallback(callback) { exportOpenCallback = callback; },
        openExport,
        setPendingDeletions(count) {
            clearTimeout(pendingPanelTimer);
            pendingPanel.inert = count === 0;
            if (count > 0) {
                pendingPanel.hidden = false;
                // Establish the initial opacity before starting the shared fade.
                void pendingPanel.offsetHeight;
                pendingPanel.classList.add('visible');
            } else {
                pendingPanel.classList.remove('visible');
                const delay = window.matchMedia('(prefers-reduced-motion: reduce)').matches ? 0 : 250;
                pendingPanelTimer = setTimeout(() => { pendingPanel.hidden = true; }, delay);
            }
            document.dispatchEvent(new CustomEvent('pending-branches-change', { detail: { count } }));
        },
        confirmDeletions(message, callback) {
            const dialog = document.getElementById('deletionDialog');
            const names = { prev: 'Viewing the previous move', next: 'Viewing the next move',
                submit: 'Submitting', open_export: 'Opening export', export: 'Exporting' };
            document.getElementById('deletionReason').textContent =
                `${names[message.operation]} will permanently delete these branches, including their descendants and comments.`;
            const list = document.getElementById('deletionActions');
            list.replaceChildren(...message.actions.map(text => {
                const item = document.createElement('li');
                item.textContent = text;
                return item;
            }));
            dialog.returnValue = '';
            dialog.onclose = () => callback(dialog.returnValue === 'confirm');
            dialog.showModal();
        },
        setGameMetadata(metadata = {}) {
            gameMetadata = { ...metadata };
            renderGameMetadata(metadata);
        },
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
            return readCommentText(hudText);
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
        receiveImportResult(result) { return importDiagnostics.receive(result); },
        setImportPreparing(requestId, preparing) { importDiagnostics.setPreparing(requestId, preparing); },
        completeImport(requestId) {
            if (!importDiagnostics.complete(requestId)) return;
            if (popupStack.at(-1)?.popupName === 'popupImport') closePopup();
        },
        setImportCancelCallback(callback) { importCancelCallback = callback; },

        setImportCallback(callback) {
            importCallback = callback;
        },

        /**
         * Set the callback function for when export button is pressed
         * @param {Function} callback - Receives the request ID to return with the preview
         */
        setExportCallback(callback) {
            exportCallback = callback;
        },

        /**
         * Set the content of exportTextarea
         * @param {string} data
         * @param {number} requestId - ID of the preview request that produced this data
         */
        setExportData(data, requestId) {
            // A superseded preview must not enable downloading stale options.
            if (requestId !== exportRequestId) return;
            const textarea = document.getElementById('exportTextarea');
            textarea.value = data;
            textarea.setAttribute('aria-busy', 'false');
            document.getElementById('exportPreviewStatus').hidden = true;
            document.getElementById('copyButton').disabled = !data;
            document.getElementById('saveButton').disabled = !data;
        },

        /**
         * Set the callback function for when focus button is pressed
         * @param {Function} callback - Function to call when focus button is clicked
         */
        setFocusCallback(callback) {
            focusCallback = callback;
        },

        setCanvasNavigationCallback(callback) {
            canvasNavigationCallback = callback;
        },

        setAllowSubmitWithChecks(checked, locked) {
            allowSubmitCheckbox.checked = checked;
            allowSubmitCheckbox.disabled = locked;
            allowSubmitCheckbox.title = locked
                ? 'Return to a position with legal history to turn this off.' : '';
        },

        setHintAvailable(available) {
            hintBtn.disabled = !available;
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
            callback({ doNotReverseFilesWhenFlipped: doNotReverseFilesCheckbox.checked });
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
        },

        /**
         * Get the settings needed by the worker at startup.
         * @returns {Object} Export options, notation preference and illegal-history permission
         */
        getSettings() {
            return {
                ...getExportSettings(),
                useShortMoveNotation: shortMoveNotationCheckbox.checked,
                allowSubmitWithChecks: allowSubmitCheckbox.checked,
            };
        }
    };
})();

export function setCommentsEditCallback(callback) {
    UI.setCommentsEditCallback(callback);
}
