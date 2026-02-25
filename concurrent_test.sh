#!/bin/bash

# Concurrent connection test using shell processes
PORT=$1
MAX_CONNECTIONS=${2:-100000}
BATCH_SIZE=1000

echo "Testing port $PORT with up to $MAX_CONNECTIONS concurrent connections..."

# Function to run a batch of requests
run_batch() {
    local batch_num=$1
    local batch_size=$2
    local port=$3
    local success=0
    local failed=0
    
    for i in $(seq 1 $batch_size); do
        if curl -s --max-time 10 "http://localhost:$port/" > /dev/null 2>&1; then
            ((success++))
        else
            ((failed++))
        fi
    done
    
    echo "$success $failed"
}

# Test progressively larger batches
for total in 1000 5000 10000 25000 50000 100000; do
    if [ $total -gt $MAX_CONNECTIONS ]; then
        break
    fi
    
    echo ""
    echo "Testing $total concurrent connections..."
    start_time=$(date +%s.%N)
    
    # Calculate number of batches
    batches=$(( (total + BATCH_SIZE - 1) / BATCH_SIZE ))
    
    # Run batches in parallel
    pids=()
    for b in $(seq 1 $batches); do
        # Last batch might be smaller
        if [ $b -eq $batches ]; then
            current_batch=$((total - (batches - 1) * BATCH_SIZE))
        else
            current_batch=$BATCH_SIZE
        fi
        
        run_batch $b $current_batch $PORT > "/tmp/batch_${PORT}_${b}.txt" &
        pids+=($!)
    done
    
    # Wait for all batches to complete
    total_success=0
    total_failed=0
    
    for pid in "${pids[@]}"; do
        wait $pid
    done
    
    # Collect results
    for b in $(seq 1 $batches); do
        if [ -f "/tmp/batch_${PORT}_${b}.txt" ]; then
            read success failed < "/tmp/batch_${PORT}_${b}.txt"
            total_success=$((total_success + success))
            total_failed=$((total_failed + failed))
            rm -f "/tmp/batch_${PORT}_${b}.txt"
        fi
    done
    
    end_time=$(date +%s.%N)
    duration=$(echo "$end_time - $start_time" | bc -l)
    
    echo "Results for $total connections:"
    echo "  Successful: $total_success"
    echo "  Failed: $total_failed"
    echo "  Success rate: $(echo "scale=2; $total_success * 100 / $total" | bc -l)%"
    echo "  Duration: ${duration}s"
    echo "  Requests/sec: $(echo "scale=0; $total / $duration" | bc -l)"
    
    # If failure rate is too high, stop testing
    failure_rate=$(echo "scale=2; $total_failed * 100 / $total" | bc -l)
    if (( $(echo "$failure_rate > 10" | bc -l) )); then
        echo "High failure rate (${failure_rate}%), stopping test"
        break
    fi
done

echo ""
echo "Test completed for port $PORT"
