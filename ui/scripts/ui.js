
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

// Remote control buttons
document.querySelectorAll('.remote-btn').forEach(btn => {
    btn.onclick = () => {
        console.log('Button clicked:', btn.textContent || btn.className);
    };
});

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
document.getElementById('btnFocus').onclick = () => {
    console.log('Focus button clicked');
};

document.getElementById('btnScreenshot').onclick = () => {
    console.log('Screenshot button clicked');
};

document.getElementById('btnEdit').onclick = () => {
    console.log('Edit button clicked');
};

/* ================= Pop-up System ================= */
const popupOverlay = document.getElementById('popupOverlay');
const popupWindow = document.getElementById('popupWindow');
let activeButton = null;

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

// Close popup when clicking canvas
canvas.onclick = () => {
    if (activeButton) {
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
    console.log('Loading data:', data);
    closePopup();
}

document.getElementById('btnExport').onclick = () => {
    showPopup(this, 'popupExport');
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
