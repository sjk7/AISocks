#!/usr/bin/env python3
import asyncio
import aiohttp
import time
import sys

async def test_concurrent_connections(url, max_connections=100000):
    """Test server with massive concurrent connections"""
    print(f"Testing {url} with {max_connections} concurrent connections...")
    
    connector = aiohttp.TCPConnector(
        limit=max_connections,
        limit_per_host=max_connections,
        keepalive_timeout=300,
        enable_cleanup_closed=True
    )
    
    timeout = aiohttp.ClientTimeout(total=30, connect=10)
    
    async with aiohttp.ClientSession(connector=connector, timeout=timeout) as session:
        start_time = time.time()
        
        # Create tasks for concurrent connections
        tasks = []
        for i in range(max_connections):
            task = asyncio.create_task(make_request(session, url, i))
            tasks.append(task)
            
            # Report progress every 10k connections
            if i % 10000 == 0 and i > 0:
                print(f"  {i:,} connections initiated...")
        
        # Wait for all to complete
        results = await asyncio.gather(*tasks, return_exceptions=True)
        
        end_time = time.time()
        
        # Count successes and failures
        success_count = sum(1 for r in results if not isinstance(r, Exception))
        failed_count = len(results) - success_count
        
        duration = end_time - start_time
        total_requests = len(results)
        
        print(f"\nResults for {url}:")
        print(f"  Total requests: {total_requests:,}")
        print(f"  Successful: {success_count:,}")
        print(f"  Failed: {failed_count:,}")
        print(f"  Success rate: {(success_count/total_requests)*100:.2f}%")
        print(f"  Duration: {duration:.2f}s")
        print(f"  Requests/sec: {total_requests/duration:.0f}")
        print(f"  Avg response time: {duration/total_requests*1000:.2f}ms")
        
        return {
            'url': url,
            'total': total_requests,
            'success': success_count,
            'failed': failed_count,
            'duration': duration,
            'req_per_sec': total_requests/duration
        }

async def make_request(session, url, request_id):
    """Make a single request"""
    try:
        async with session.get(url) as response:
            await response.text()
            return True
    except Exception as e:
        return e

async def main():
    if len(sys.argv) != 2:
        print("Usage: python3 concurrent_test.py <port>")
        sys.exit(1)
    
    port = sys.argv[1]
    url = f"http://localhost:{port}/"
    
    # Test with progressively larger numbers
    test_sizes = [1000, 5000, 10000, 25000, 50000, 100000]
    
    for size in test_sizes:
        print(f"\n{'='*60}")
        try:
            result = await test_concurrent_connections(url, size)
            if result['failed'] > result['total'] * 0.1:  # If >10% fail, stop
                print(f"Too many failures ({result['failed']}/{result['total']}), stopping test")
                break
        except Exception as e:
            print(f"Test failed at {size:,} connections: {e}")
            break

if __name__ == "__main__":
    asyncio.run(main())
