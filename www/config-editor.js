// Config Editor JavaScript
// Handles loading, editing, and saving server configuration

let availableIPs = [];
let currentConfig = {};

// Show status message
function showStatus(message, type) {
    const statusDiv = document.getElementById('status');
    statusDiv.textContent = message;
    statusDiv.className = 'status ' + type;
    statusDiv.style.display = 'block';
}

// Hide status message
function hideStatus() {
    const statusDiv = document.getElementById('status');
    statusDiv.style.display = 'none';
}

// Fetch available IP addresses
async function fetchAvailableIPs() {
    try {
        const response = await fetch('/api/config/ips');
        if (!response.ok) {
            throw new Error('Failed to fetch available IPs');
        }
        const data = await response.json();
        availableIPs = data.ips || [];
        populateIPDropdown();
    } catch (error) {
        console.error('Error fetching IPs:', error);
        showStatus('Failed to load available IPs: ' + error.message, 'error');
        // Fallback to common IPs
        availableIPs = ['0.0.0.0', '127.0.0.1', '::', '::1'];
        populateIPDropdown();
    }
}

// Populate IP dropdown
function populateIPDropdown() {
    const select = document.getElementById('bindAddress');
    select.innerHTML = '';

    // Add special options first
    const specialOptions = [
        { value: '0.0.0.0', label: '0.0.0.0 (All IPv4)' },
        { value: '::', label: ':: (All IPv6)' },
        { value: '127.0.0.1', label: '127.0.0.1 (Localhost IPv4)' },
        { value: '::1', label: '::1 (Localhost IPv6)' }
    ];

    specialOptions.forEach(opt => {
        const option = document.createElement('option');
        option.value = opt.value;
        option.textContent = opt.label;
        select.appendChild(option);
    });

    // Add separator
    const separator = document.createElement('option');
    separator.disabled = true;
    separator.textContent = '--- Network Interfaces ---';
    select.appendChild(separator);

    // Add library-enumerated IPs
    availableIPs.forEach(ip => {
        const option = document.createElement('option');
        option.value = ip;
        option.textContent = ip;
        select.appendChild(option);
    });
}

// Fetch current configuration
async function fetchConfig() {
    showStatus('Loading configuration...', 'loading');

    try {
        const response = await fetch('/api/config/current');
        if (!response.ok) {
            throw new Error('Failed to fetch configuration');
        }
        const config = await response.json();
        currentConfig = config;
        populateForm(config);
        hideStatus();
    } catch (error) {
        console.error('Error fetching config:', error);
        showStatus('Failed to load configuration: ' + error.message, 'error');
    }
}

// Populate form with config values
function populateForm(config) {
    // Server Settings
    document.getElementById('bindAddress').value = config.bindAddress || '0.0.0.0';
    document.getElementById('httpPort').value = config.httpPort || 8080;
    document.getElementById('wwwRoot').value = config.wwwRoot || './www';

    // TLS Settings
    document.getElementById('cert').value = config.cert || 'server-cert.pem';
    document.getElementById('key').value = config.key || 'server-key.pem';
    document.getElementById('enableHttps').checked = config.enableHttps || false;

    // Logging Settings
    document.getElementById('logPath').value = config.logPath || 'access.log';
    document.getElementById('enableLogging').checked = config.enableLogging !== false;
    document.getElementById('enableLogRotation').checked = config.enableLogRotation !== false;
    document.getElementById('logMaxSizeBytes').value = config.logMaxSizeBytes || 10485760;
    document.getElementById('logMaxFiles').value = config.logMaxFiles || 5;

    // Display Settings
    document.getElementById('indexFile').value = config.indexFile || 'index.html';
    document.getElementById('directoryListing').checked = config.directoryListing !== false;
}

// Get form values as config object
function getFormValues() {
    return {
        bindAddress: document.getElementById('bindAddress').value,
        httpPort: parseInt(document.getElementById('httpPort').value) || 8080,
        wwwRoot: document.getElementById('wwwRoot').value,
        cert: document.getElementById('cert').value,
        key: document.getElementById('key').value,
        enableHttps: document.getElementById('enableHttps').checked,
        logPath: document.getElementById('logPath').value,
        enableLogging: document.getElementById('enableLogging').checked,
        enableLogRotation: document.getElementById('enableLogRotation').checked,
        logMaxSizeBytes: parseInt(document.getElementById('logMaxSizeBytes').value) || 10485760,
        logMaxFiles: parseInt(document.getElementById('logMaxFiles').value) || 5,
        indexFile: document.getElementById('indexFile').value,
        directoryListing: document.getElementById('directoryListing').checked
    };
}

// Validate config
function validateConfig(config) {
    if (!config.bindAddress) {
        throw new Error('Bind address is required');
    }
    if (config.httpPort < 1 || config.httpPort > 65535) {
        throw new Error('HTTP port must be between 1 and 65535');
    }
    if (!config.wwwRoot) {
        throw new Error('Document root is required');
    }
    if (config.enableHttps && (!config.cert || !config.key)) {
        throw new Error('Certificate and key files are required when HTTPS is enabled');
    }
    if (config.logMaxFiles < 1) {
        throw new Error('Max log files must be at least 1');
    }
    return true;
}

// Save configuration
async function saveConfig() {
    try {
        const config = getFormValues();
        validateConfig(config);

        showStatus('Saving configuration and restarting server...', 'loading');

        const response = await fetch('/api/config/save', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(config)
        });

        if (!response.ok) {
            throw new Error('Server returned error: ' + response.status);
        }

        const result = await response.json();

        if (result.success === false) {
            throw new Error(result.message || 'Failed to save configuration');
        }

        showStatus('Configuration saved successfully! Server is restarting...', 'success');

        // Redirect to testing guide after 3 seconds
        setTimeout(() => {
            window.location.href = '/testing-guide.html';
        }, 3000);

    } catch (error) {
        console.error('Error saving config:', error);
        showStatus('Failed to save configuration: ' + error.message, 'error');
    }
}

// Load config (reload button)
function loadConfig() {
    fetchConfig();
}

// Initialize on page load
document.addEventListener('DOMContentLoaded', () => {
    fetchAvailableIPs();
    fetchConfig();
});
