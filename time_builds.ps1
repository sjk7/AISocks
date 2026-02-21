param(
    [string]$Root = $PSScriptRoot
)

function Time-Build {
    param([string]$Label, [scriptblock]$Block)
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & $Block 2>&1 | Out-Null
    $sw.Stop()
    [pscustomobject]@{
        Label   = $Label
        Seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1)
        OK      = $LASTEXITCODE -eq 0
    }
}

$results = @()

# ── Ninja Debug ─────────────────────────────────────────────────────────────
Remove-Item -Recurse -Force "$Root\build-ninja-debug"   -ErrorAction SilentlyContinue
$results += Time-Build "Ninja  Debug  (configure)" {
    cmake -S $Root -B "$Root\build-ninja-debug" -G Ninja `
          -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
}
$results += Time-Build "Ninja  Debug  (build)    " {
    cmake --build "$Root\build-ninja-debug"
}

# ── Ninja Release ────────────────────────────────────────────────────────────
Remove-Item -Recurse -Force "$Root\build-ninja-release" -ErrorAction SilentlyContinue
$results += Time-Build "Ninja  Release(configure)" {
    cmake -S $Root -B "$Root\build-ninja-release" -G Ninja `
          -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
}
$results += Time-Build "Ninja  Release(build)    " {
    cmake --build "$Root\build-ninja-release"
}

# ── MSBuild Debug ────────────────────────────────────────────────────────────
Remove-Item -Recurse -Force "$Root\build-msbuild-debug" -ErrorAction SilentlyContinue
$results += Time-Build "MSBuild Debug  (configure)" {
    cmake -S $Root -B "$Root\build-msbuild-debug" `
          -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
}
$results += Time-Build "MSBuild Debug  (build)    " {
    cmake --build "$Root\build-msbuild-debug" --config Debug
}

# ── MSBuild Release ──────────────────────────────────────────────────────────
Remove-Item -Recurse -Force "$Root\build-msbuild-release" -ErrorAction SilentlyContinue
$results += Time-Build "MSBuild Release(configure)" {
    cmake -S $Root -B "$Root\build-msbuild-release" `
          -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
}
$results += Time-Build "MSBuild Release(build)    " {
    cmake --build "$Root\build-msbuild-release" --config Release
}

# ── Results ──────────────────────────────────────────────────────────────────
""
"=" * 52
"{0,-30} {1,8}  {2}" -f "Step", "Seconds", "Status"
"=" * 52
foreach ($r in $results) {
    "{0,-30} {1,7}s  {2}" -f $r.Label, $r.Seconds, $(if ($r.OK) {"OK"} else {"FAILED"})
}
"=" * 52

$results | Export-Csv -NoTypeInformation "$Root\build_times.csv"
"Results saved to build_times.csv"
