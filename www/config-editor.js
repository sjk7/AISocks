// Config Editor JavaScript
// Handles loading, editing, and saving server configuration

let availableIPs = [];
let currentConfig = {};

// Show status message at top of page
function showStatusTop(message, type) {
    const statusDiv = document.getElementById('status-top');
    statusDiv.textContent = message;
    statusDiv.className = 'status status-top ' + type;
    statusDiv.style.display = 'block';
}

// Show status message next to button
function showStatusButton(message, type) {
    const statusDiv = document.getElementById('status-button');
    statusDiv.textContent = message;
    statusDiv.className = 'status status-button ' + type;
    statusDiv.style.display = 'block';
}

// Hide status messages
function hideStatus() {
    document.getElementById('status-top').style.display = 'none';
    document.getElementById('status-button').style.display = 'none';
}

// Disable all form fields and buttons
function disableForm() {
    const inputs = document.querySelectorAll('input, select, button');
    inputs.forEach(el => el.disabled = true);
}

// Enable all form fields and buttons
function enableForm() {
    const inputs = document.querySelectorAll('input, select, button');
    inputs.forEach(el => el.disabled = false);
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
    showStatusTop('Loading configuration...', 'loading');

    try {
        const response = await fetch('/api/config/current');

        if (response.status === 403) {
            throw new Error('Config API is local only. You must access this page from localhost (127.0.0.1 or ::1).');
        }

        if (!response.ok) {
            throw new Error('Failed to fetch configuration');
        }

        const config = await response.json();
        currentConfig = config;
        populateForm(config);
        hideStatus();
        enableForm();
    } catch (error) {
        console.error('Error fetching config:', error);
        showStatusTop('Failed to load configuration: ' + error.message, 'error');
        disableForm();
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
    if (config.httpPort === '' || config.httpPort === null || config.httpPort === undefined || isNaN(config.httpPort)) {
        throw new Error('HTTP port must be a valid number');
    }
    if (config.httpPort < 1 || config.httpPort > 65535) {
        throw new Error('HTTP port must be between 1 and 65535');
    }
    if (!config.wwwRoot || config.wwwRoot.trim() === '') {
        throw new Error('Document root is required and cannot be empty');
    }
    if (!config.indexFile || config.indexFile.trim() === '') {
        throw new Error('Index file is required and cannot be empty');
    }
    if (config.enableHttps && (!config.cert || config.cert.trim() === '')) {
        throw new Error('Certificate file is required when HTTPS is enabled');
    }
    if (config.enableHttps && (!config.key || config.key.trim() === '')) {
        throw new Error('Key file is required when HTTPS is enabled');
    }
    if (config.logMaxFiles < 2 || config.logMaxFiles > 200) {
        throw new Error('Max log files must be between 2 and 200');
    }
    if (config.logMaxSizeBytes < 1) {
        throw new Error('Max log size must be at least 1');
    }
    return true;
}

// Save configuration
async function saveConfig() {
    console.log('saveConfig() called');
    try {
        const config = getFormValues();
        console.log('Config to save:', config);
        validateConfig(config);

        showStatusButton('Saving configuration and restarting server...', 'loading');

        console.log('Sending POST to /api/config/save');
        const response = await fetch('/api/config/save', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(config)
        });

        console.log('Response received:', response.status, response.statusText);

        if (!response.ok) {
            if (response.status === 405) {
                throw new Error('Method Not Allowed (405): The server does not support this operation. This may be due to authentication requirements or server configuration.');
            } else if (response.status === 403) {
                throw new Error('Forbidden (403): You do not have permission to access this resource. Config editing is restricted to local connections only.');
            } else if (response.status === 404) {
                throw new Error('Not Found (404): The config save endpoint is not available on this server.');
            } else {
                throw new Error('Server returned error: ' + response.status + ' ' + response.statusText);
            }
        }

        const responseText = await response.text();
        console.log('Response text:', responseText);

        let result;
        try {
            result = JSON.parse(responseText);
        } catch (parseError) {
            throw new Error('Invalid JSON response from server: ' + parseError.message + '. Response: ' + responseText);
        }

        if (response.status === 403) {
            throw new Error('Config API is local only. You must access this page from localhost (127.0.0.1 or ::1).');
        }

        if (result.success === false) {
            throw new Error(result.message || 'Failed to save configuration');
        }

        showStatusButton('Configuration saved successfully! Server is restarting...', 'success');

    } catch (error) {
        console.error('Error saving config:', error);
        showStatusButton('Failed to save configuration: ' + error.message, 'error');
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
