# Route one-shot local setup.
# Usage (PowerShell):  .\setup.ps1 -Password 'YOUR_POSTGRES_PASSWORD'
# Optional:            .\setup.ps1 -Password 'pw' -JwtSecret 'my-long-secret'
param(
  [Parameter(Mandatory = $true)][string]$Password,
  [string]$JwtSecret
)
$ErrorActionPreference = 'Stop'
$base = 'E:\certificates\Projects\Cpp project'
$pg   = 'C:\Program Files\PostgreSQL\16\bin'
if (-not (Test-Path "$pg\psql.exe")) { $pg = 'C:\Program Files\PostgreSQL\17\bin' }
$env:PGPASSWORD = $Password

if (-not $JwtSecret) {
  $bytes = New-Object 'System.Byte[]' 48
  (New-Object System.Random).NextBytes($bytes)
  $JwtSecret = [Convert]::ToBase64String($bytes)
}

Write-Host '1) Creating database "route" (ok if it already exists)...'
$eap = $ErrorActionPreference; $ErrorActionPreference = 'SilentlyContinue'
& "$pg\createdb.exe" -U postgres route 2>&1 | Out-Null
$ErrorActionPreference = $eap
Write-Host '   (database ready)'

$migrations = '001_init.sql', 'seed.sql', '002_expand.sql', '003_ride_coords.sql', '004_demo_users.sql', '006_improvements.sql', '007_drivers.sql', '008_bidding.sql', '009_safety.sql', '010_payments.sql'
foreach ($m in $migrations) {
  Write-Host "2) Applying $m ..."
  & "$pg\psql.exe" -U postgres -d route -v ON_ERROR_STOP=1 -f "$base\migrations\$m"
  if (-not $?) { throw "Migration $m failed - check the Postgres password and try again." }
}

Write-Host '3) Writing password + JWT secret into config.json ...'
foreach ($cfg in @("$base\config.json", "$base\build\Release\config.json")) {
  if (Test-Path $cfg) {
    $j = Get-Content $cfg -Raw | ConvertFrom-Json
    $j.db_clients[0].passwd = $Password
    $j.custom_config.jwt_secret = $JwtSecret
    ($j | ConvertTo-Json -Depth 30) | Set-Content $cfg -Encoding utf8
    Write-Host "   patched $cfg"
  }
}

Write-Host ''
Write-Host 'DONE. Now restart route.exe (stop the current one, then run build\Release\route.exe).'
Write-Host 'Log in at http://localhost:8080 :'
Write-Host '   rider@route.org   / Ride12345'
Write-Host '   manager@route.org / Manage12345   (operator console)'
