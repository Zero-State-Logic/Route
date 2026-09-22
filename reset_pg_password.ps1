# Reset the local PostgreSQL 'postgres' password to a known value, then run Route setup.
# MUST be run in an ELEVATED PowerShell (right-click PowerShell > Run as administrator).
# It backs up pg_hba.conf, briefly enables 'trust' auth to set the password, then
# RESTORES the original pg_hba.conf so your DB is secured again.
# Usage:  .\reset_pg_password.ps1 -Password 'your-new-postgres-password'
param([Parameter(Mandatory = $true)][string]$Password)
$ErrorActionPreference = 'Stop'

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)
if (-not $admin) { Write-Host 'Please re-run this in an ELEVATED PowerShell (Run as administrator).' -ForegroundColor Red; exit 1 }

$svc  = 'postgresql-x64-16'
$data = 'C:\Program Files\PostgreSQL\16\data'
$pg   = 'C:\Program Files\PostgreSQL\16\bin'
$hba  = Join-Path $data 'pg_hba.conf'
$bak  = "$hba.route-bak"

Write-Host "Backing up pg_hba.conf -> $bak"
Copy-Item $hba $bak -Force

Write-Host 'Temporarily enabling trust auth (local only)...'
$c = Get-Content $hba -Raw
$c = [regex]::Replace($c, '(?m)^(host\s+all\s+all\s+127\.0\.0\.1/32\s+)[\w-]+', '${1}trust')
$c = [regex]::Replace($c, '(?m)^(host\s+all\s+all\s+::1/128\s+)[\w-]+', '${1}trust')
$c = [regex]::Replace($c, '(?m)^(local\s+all\s+all\s+)[\w-]+', '${1}trust')
Set-Content $hba $c -Encoding ascii

try {
  Write-Host 'Restarting PostgreSQL...'
  Restart-Service $svc
  for ($i = 0; $i -lt 20; $i++) { & "$pg\pg_isready.exe" -h 127.0.0.1 -q; if ($?) { break }; Start-Sleep 1 }

  Write-Host 'Setting the postgres password...'
  & "$pg\psql.exe" -U postgres -d postgres -h 127.0.0.1 -c "ALTER USER postgres PASSWORD '$Password';"
  if (-not $?) { throw 'ALTER USER failed.' }
}
finally {
  Write-Host 'Restoring secure pg_hba.conf...'
  Copy-Item $bak $hba -Force
  Restart-Service $svc
  for ($i = 0; $i -lt 20; $i++) { & "$pg\pg_isready.exe" -h 127.0.0.1 -q; if ($?) { break }; Start-Sleep 1 }
}

Write-Host ''
Write-Host "Password for 'postgres' is now: $Password" -ForegroundColor Green
Write-Host 'Running Route setup (create DB, migrations, config)...'
& "$PSScriptRoot\setup.ps1" -Password $Password
