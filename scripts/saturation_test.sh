#!/bin/bash

# HTTP Server Saturation Testing Script

SERVER_URL="http://127.0.0.1:49667"
CONCURRENT_CONNECTIONS=50
REQUESTS_PER_CONNECTION=100
TOTAL_REQUESTS=$((CONCURRENT_CONNECTIONS * REQUESTS_PER_CONNECTION))

echo "=== HTTP Server Saturation Test ==="
echo "Server URL: $SERVER_URL"
echo "Concurrent connections: $CONCURRENT_CONNECTIONS"
echo "Requests per connection: $REQUESTS_PER_CONNECTION"
echo "Total requests: $TOTAL_REQUESTS"
echo "Starting at: $(date)"
echo.

# Function to make HTTP request
make_request() {
    local conn_id=$1
    local req_id=$2
    local url=$3
    
    curl -s -w "HTTP Status: %{http_code}\nTime: %{time_total}\n" \
         -H "Connection: keep-alive" \
         -o /dev/null \
         "$url" 2>/dev/null
    echo "Connection $conn_id, Request $req_id: $?"
}

# Function to run concurrent connections
run_concurrent_test() {
    local conn_id=$1
    local start_time=$(date +%s)
    
    echo "Starting connection $conn_id with $REQUESTS_PER_CONNECTION requests..."
    
    for ((i=1; i<=REQUESTS_PER_CONNECTION; i++)); do
        make_request $conn_id $i "$SERVER_URL"
    done
    
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    echo "Connection $conn_id completed in ${duration}s"
}

# Main test execution
echo "Starting saturation test..."
start_time=$(date +%s)

# Launch concurrent connections in background
for ((i=1; i<=CONCURRENT_CONNECTIONS; i++)); do
    run_concurrent_test $i &
done

# Wait for all background jobs to complete
wait

end_time=$(date +%s)
total_duration=$((end_time - start_time))

echo.
echo "=== Test Results ==="
echo "Total duration: ${total_duration}s"
echo "Total requests attempted: $TOTAL_REQUESTS"
echo "Concurrent connections: $CONCURRENT_CONNECTIONS"
echo "Requests per connection: $REQUESTS_PER_CONNECTION"
echo "Requests per second: $((TOTAL_REQUESTS / total_duration))"
echo "Test completed at: $(date)"
