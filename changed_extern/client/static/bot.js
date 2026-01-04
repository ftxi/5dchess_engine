'use strict';

// Bot control variables
let botEnabled = false;
let botGameMode = 'human_vs_human';
let botDepth = 4;
let botTimeLimit = 3.0;
let botColor = 'white';
let botThinking = false;

// Initialize bot panel
function initBotPanel() {
    // Panel toggle
    const panelToggle = document.getElementById('bot-panel-toggle');
    const panelContent = document.getElementById('bot-panel-content');
    
    panelToggle.addEventListener('click', () => {
        if (panelContent.classList.contains('collapsed')) {
            panelContent.classList.remove('collapsed');
            panelToggle.textContent = '−';
        } else {
            panelContent.classList.add('collapsed');
            panelToggle.textContent = '+';
        }
    });
    
    // Game mode selection
    const gameModeSelect = document.getElementById('bot-game-mode');
    gameModeSelect.addEventListener('change', (e) => {
        botGameMode = e.target.value;
        updateBotUI();
        
        // Handle auto-play for bot vs bot mode
        if (e.target.value === 'bot_vs_bot' && botEnabled) {
            startAutoPlay();
        } else {
            stopAutoPlay();
        }
    });
    
    // Depth selection
    const depthSelect = document.getElementById('bot-depth');
    depthSelect.addEventListener('change', (e) => {
        botDepth = parseInt(e.target.value);
        if (botEnabled) {
            socket.emit('bot_set_config', {depth: botDepth, time_limit: botTimeLimit});
            updateBotStatus(`Bot: Enabled (Depth: ${botDepth}, Time: ${botTimeLimit}s)`);
        }
    });
    
    // Time limit selection
    const timeLimitSelect = document.getElementById('bot-time-limit');
    timeLimitSelect.addEventListener('change', (e) => {
        botTimeLimit = parseFloat(e.target.value);
        if (botEnabled) {
            socket.emit('bot_set_config', {depth: botDepth, time_limit: botTimeLimit});
            updateBotStatus(`Bot: Enabled (Depth: ${botDepth}, Time: ${botTimeLimit}s)`);
        }
    });
    
    // Color selection
    const colorSelect = document.getElementById('bot-color');
    colorSelect.addEventListener('change', (e) => {
        botColor = e.target.value;
        updateBotUI();
    });
    
    // Toggle bot
    const toggleBtn = document.getElementById('bot-toggle-btn');
    toggleBtn.addEventListener('click', () => {
        if (botEnabled) {
            disableBot();
        } else {
            enableBot();
        }
    });
    
    // Bot move
    const moveBtn = document.getElementById('bot-move-btn');
    moveBtn.addEventListener('click', () => {
        if (!botThinking) {
            requestBotMove();
        }
    });
    
    // Analyze
    const analyzeBtn = document.getElementById('bot-analyze-btn');
    analyzeBtn.addEventListener('click', () => {
        requestBotAnalysis();
    });
    
    // Stats
    const statsBtn = document.getElementById('bot-stats-btn');
    statsBtn.addEventListener('click', () => {
        requestBotStats();
    });
    
    // Update initial UI state
    updateBotUI();
}

// Enable bot
function enableBot() {
    const depth = parseInt(document.getElementById('bot-depth').value);
    const timeLimit = parseFloat(document.getElementById('bot-time-limit').value);
    socket.emit('bot_toggle', {enabled: true, depth: depth, time_limit: timeLimit});
    botEnabled = true;
    botDepth = depth;
    botTimeLimit = timeLimit;
    updateBotUI();
    updateBotStatus(`Bot: Enabled (Depth: ${depth}, Time: ${timeLimit}s)`);
}

// Disable bot
function disableBot() {
    socket.emit('bot_toggle', {enabled: false});
    botEnabled = false;
    updateBotUI();
    updateBotStatus('Bot: Disabled');
}

// Request bot move
function requestBotMove() {
    if (!botEnabled) {
        alert('Please enable the bot first');
        return;
    }
    
    botThinking = true;
    updateBotUI();
    
    socket.emit('bot_move');
}

// Request bot analysis
function requestBotAnalysis() {
    socket.emit('bot_analyze');
}

// Request bot stats
function requestBotStats() {
    socket.emit('bot_get_stats');
}

// Update bot UI based on state
function updateBotUI() {
    const toggleBtn = document.getElementById('bot-toggle-btn');
    const moveBtn = document.getElementById('bot-move-btn');
    const analyzeBtn = document.getElementById('bot-analyze-btn');
    const statsBtn = document.getElementById('bot-stats-btn');
    const colorSelect = document.getElementById('bot-color');
    
    // Update toggle button
    if (botEnabled) {
        toggleBtn.textContent = 'Disable Bot';
        toggleBtn.style.background = 'linear-gradient(135deg, #f44336, #d32f2f)';
    } else {
        toggleBtn.textContent = 'Enable Bot';
        toggleBtn.style.background = 'linear-gradient(135deg, #4CAF50, #45a049)';
    }
    
    // Update move button
    moveBtn.disabled = !botEnabled || botThinking;
    
    // Update analyze and stats buttons (always enabled)
    analyzeBtn.disabled = false;
    statsBtn.disabled = false;
    
    // Update color select based on game mode
    if (botGameMode === 'bot_vs_bot') {
        colorSelect.value = 'both';
        colorSelect.disabled = true;
    } else if (botGameMode === 'human_vs_bot') {
        if (botColor === 'both') {
            colorSelect.value = 'black';
        }
        colorSelect.disabled = false;
    } else {
        colorSelect.disabled = true;
    }
}

// Update bot status text
function updateBotStatus(text) {
    const statusText = document.getElementById('bot-status-text');
    statusText.textContent = text;
}

// Update bot analysis display
function updateBotAnalysis(analysis) {
    document.getElementById('analysis-eval').textContent = analysis.evaluation || '-';
    document.getElementById('analysis-move').textContent = analysis.best_move || '-';
    document.getElementById('analysis-material').textContent = analysis.material_balance || '-';
    document.getElementById('analysis-timeline').textContent = analysis.timeline_advantage || '-';
}

// Update bot stats display
function updateBotStats(stats) {
    document.getElementById('stats-nodes').textContent = stats.nodes_searched ? stats.nodes_searched.toLocaleString() : '0';
    document.getElementById('stats-time').textContent = stats.total_think_time ? stats.total_think_time.toFixed(2) + 's' : '0.00s';
    
    const nps = stats.nodes_searched && stats.total_think_time > 0 
        ? (stats.nodes_searched / stats.total_think_time).toLocaleString(undefined, {maximumFractionDigits: 0})
        : '0';
    document.getElementById('stats-nps').textContent = nps;
    
    document.getElementById('stats-depth').textContent = stats.search_depth || '0';
}

// Socket event handlers for bot
socket.on('bot_toggle_response', function(data) {
    if (data.success) {
        botEnabled = data.enabled;
        updateBotUI();
        if (data.enabled) {
            updateBotStatus(`Bot: Enabled (Depth: ${data.depth}, Time: ${data.time_limit}s)`);
        } else {
            updateBotStatus('Bot: Disabled');
        }
    } else {
        alert('Failed to toggle bot: ' + data.error);
    }
});

socket.on('bot_move_response', function(data) {
    botThinking = false;
    updateBotUI();
    
    if (data.success) {
        console.log('Bot move executed:', data.move);
        // The move will be reflected in the next data update
    } else {
        alert('Bot move failed: ' + data.error);
    }
});

socket.on('bot_analyze_response', function(data) {
    if (data.success) {
        updateBotAnalysis(data.analysis);
    } else {
        alert('Analysis failed: ' + data.error);
    }
});

socket.on('bot_stats_response', function(data) {
    if (data.success) {
        updateBotStats(data.stats);
    } else {
        alert('Failed to get stats: ' + data.error);
    }
});

socket.on('bot_error', function(data) {
    botThinking = false;
    updateBotUI();
    alert('Bot error: ' + data.error);
});

// Auto-play for bot vs bot mode
let autoPlayInterval = null;

function startAutoPlay() {
    if (autoPlayInterval) {
        clearInterval(autoPlayInterval);
    }
    
    autoPlayInterval = setInterval(() => {
        if (botEnabled && botGameMode === 'bot_vs_bot' && !botThinking) {
            requestBotMove();
        }
    }, 1000);
}

function stopAutoPlay() {
    if (autoPlayInterval) {
        clearInterval(autoPlayInterval);
        autoPlayInterval = null;
    }
}

// Initialize when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initBotPanel);
} else {
    initBotPanel();
}