param([int]$N = 5000, [string]$Addr = "127.0.0.1", [int]$Port = 8080, [int]$HoldSecs = 6)

# int[] is a reference type - safe to share across .NET Task closures
$counts = [int[]]::new(2)  # [0]=ok  [1]=fail

$request = [System.Text.Encoding]::ASCII.GetBytes(
    "GET / HTTP/1.1`r`nHost: $Addr`r`nConnection: keep-alive`r`n`r`n")

Write-Host "[$(Get-Date -f 'HH:mm:ss')] Launching $N concurrent connections to ${Addr}:${Port}..."

$tasks = 1..$N | ForEach-Object {
    $h = $Addr; $p = $Port; $hs = $HoldSecs; $req = $request; $cnt = $counts
    [System.Threading.Tasks.Task]::Run([System.Action]{
        try {
            $tcp = [System.Net.Sockets.TcpClient]::new()
            $tcp.SendTimeout    = 10000
            $tcp.ReceiveTimeout = 10000
            $tcp.Connect($h, $p)
            $stream = $tcp.GetStream()
            $stream.Write($req, 0, $req.Length)
            $buf = [byte[]]::new(4096)
            $null = $stream.Read($buf, 0, $buf.Length)
            [System.Threading.Interlocked]::Increment([ref]$cnt[0]) | Out-Null
            Start-Sleep $hs
            $tcp.Close()
        } catch {
            [System.Threading.Interlocked]::Increment([ref]$cnt[1]) | Out-Null
        }
    })
}

[System.Threading.Tasks.Task]::WaitAll($tasks)

Write-Host ""
Write-Host "=== RESULTS ==="
Write-Host "  Success : $($counts[0]) / $N"
Write-Host "  Failed  : $($counts[1]) / $N"
