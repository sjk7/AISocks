// Test JavaScript file
console.log('Hello from HttpFileServer!');

function greet(name) {
    return `Hello, ${name}!`;
}

function testFunction() {
    const output = document.getElementById('output');
    const timestamp = new Date().toLocaleTimeString();
    output.innerHTML = `
        <strong>✅ Test function executed successfully!</strong><br><br>
        Time: ${timestamp}<br>
        Message: ${greet('from test.js')}<br><br>
        <em>The JavaScript file was loaded and executed correctly.</em>
    `;
    console.log('testFunction() executed at', timestamp);
}

// This file should be served with MIME type: application/javascript
