#!/bin/bash

echo "=== Performance Comparison: aiSocksHttpServer vs nginx ==="
echo "Each server tested 10 times with curl"
echo ""

# Test aiSocksHttpServer
echo "Testing aiSocksHttpServer..."
aiSocks_times=()
for i in {1..10}; do
    start_time=$(date +%s%N)
    curl -s -o /dev/null http://localhost:8080/
    end_time=$(date +%s%N)
    
    duration_ms=$(( (end_time - start_time) / 1000000 ))
    aiSocks_times+=($duration_ms)
    echo "  Test $i: ${duration_ms}ms"
done

# Test nginx
echo ""
echo "Testing nginx..."
nginx_times=()
for i in {1..10}; do
    start_time=$(date +%s%N)
    curl -s -o /dev/null http://localhost:8082/
    end_time=$(date +%s%N)
    
    duration_ms=$(( (end_time - start_time) / 1000000 ))
    nginx_times+=($duration_ms)
    echo "  Test $i: ${duration_ms}ms"
done

# Calculate statistics
aiSocks_sum=0
aiSocks_min=${aiSocks_times[0]}
aiSocks_max=${aiSocks_times[0]}
for time in "${aiSocks_times[@]}"; do
    aiSocks_sum=$((aiSocks_sum + time))
    if [ $time -lt $aiSocks_min ]; then aiSocks_min=$time; fi
    if [ $time -gt $aiSocks_max ]; then aiSocks_max=$time; fi
done
aiSocks_avg=$((aiSocks_sum / 10))

nginx_sum=0
nginx_min=${nginx_times[0]}
nginx_max=${nginx_times[0]}
for time in "${nginx_times[@]}"; do
    nginx_sum=$((nginx_sum + time))
    if [ $time -lt $nginx_min ]; then nginx_min=$time; fi
    if [ $time -gt $nginx_max ]; then nginx_max=$time; fi
done
nginx_avg=$((nginx_sum / 10))

# Calculate speeds (100MB * 8 bits / seconds)
aiSocks_speed=$(( (100 * 8 * 1000) / aiSocks_avg ))
nginx_speed=$(( (100 * 8 * 1000) / nginx_avg ))

echo ""
echo "=== RESULTS (10 tests) ==="
echo "aiSocksHttpServer:"
echo "  Times: ${aiSocks_times[@]} ms"
echo "  Average: ${aiSocks_avg}ms (min: ${aiSocks_min}ms, max: ${aiSocks_max}ms)"
echo "  Speed: ${aiSocks_speed} Mbps"

echo ""
echo "nginx:"
echo "  Times: ${nginx_times[@]} ms"
echo "  Average: ${nginx_avg}ms (min: ${nginx_min}ms, max: ${nginx_max}ms)"
echo "  Speed: ${nginx_speed} Mbps"

echo ""
if [ $aiSocks_avg -lt $nginx_avg ]; then
    improvement=$(( (nginx_avg * 100) / aiSocks_avg - 100 ))
    echo "🎉 aiSocksHttpServer is ${improvement}% faster than nginx!"
else
    slower=$(( (aiSocks_avg * 100) / nginx_avg - 100 ))
    echo "nginx is ${slower}% faster than aiSocksHttpServer"
fi
