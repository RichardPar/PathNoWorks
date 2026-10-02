<#
decnetd-task.ps1 -- the Task Scheduler task that runs decnetd when you log
in.  Installed with PathNoWorks; setup and the Start menu use it.

    decnetd-task.ps1 -Action Register -Exe decnetd.exe -Conf decnetd.conf -Log decnetd.log
    decnetd-task.ps1 -Action Restart
    decnetd-task.ps1 -Action Remove

Register makes (or replaces) the task, disables any other task that starts
decnetd and stops any other decnetd -- one node, once, on the network --
and starts it.  The same task as cppdecnet's tools\install-decnetd.ps1.
#>

param (
    [ValidateSet ("Register", "Restart", "Remove")]
    [string] $Action = "Restart",
    [string] $Exe = "",
    [string] $Conf = "",
    [string] $Log = ""
)

$ErrorActionPreference = "Stop"
$taskname = "cppdecnet decnetd"

function Our-Exe {
    if ($Exe) { return $Exe }
    $t = Get-ScheduledTask -TaskName $taskname -ErrorAction SilentlyContinue
    if ($t -and $t.Actions[0].Arguments -match '"([^"]*decnetd\.exe)"') { return $Matches[1] }
    return ""
}

function Stop-Ours {
    if (Get-ScheduledTask -TaskName $taskname -ErrorAction SilentlyContinue) {
        Stop-ScheduledTask -TaskName $taskname -ErrorAction SilentlyContinue
    }
    $exe = Our-Exe
    if ($exe) {
        Get-Process decnetd -ErrorAction SilentlyContinue |
            Where-Object { $_.Path -eq $exe } | Stop-Process -Force
    }
    Start-Sleep -Seconds 1
}

switch ($Action) {
    "Register" {
        if (-not ($Exe -and $Conf -and $Log)) { throw "Register needs -Exe, -Conf and -Log" }
        Stop-Ours
        # Another decnetd -- one started by hand, or by another task -- would
        # be the same node on the network twice.
        Get-ScheduledTask -ErrorAction SilentlyContinue | Where-Object {
            $_.TaskName -ne $taskname -and $_.State -ne "Disabled" -and
            ($_.Actions | Where-Object { "$($_.Execute) $($_.Arguments)" -match 'decnetd' })
        } | ForEach-Object {
            Write-Output "disabling the task `"$($_.TaskName)`", which also starts decnetd"
            Stop-ScheduledTask -TaskName $_.TaskName -ErrorAction SilentlyContinue
            Disable-ScheduledTask -TaskName $_.TaskName | Out-Null
        }
        Get-Process decnetd -ErrorAction SilentlyContinue |
            Where-Object { $_.Path -ne $Exe } | ForEach-Object {
                Write-Output "stopping decnetd $($_.Id) ($($_.Path))"
                Stop-Process -Id $_.Id -Force
            }

        # At logon, as this user, with no window (conhost --headless runs a
        # console program without one), no time limit, and a few retries.
        $user = "$env:USERDOMAIN\$env:USERNAME"
        $taskArgs = "--headless `"$Exe`" --log-level info --log-file `"$Log`" `"$Conf`""
        $action = New-ScheduledTaskAction -Execute "$env:SystemRoot\System32\conhost.exe" `
                      -Argument $taskArgs -WorkingDirectory (Split-Path -Parent $Conf)
        $trigger = New-ScheduledTaskTrigger -AtLogOn -User $user
        $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) `
                        -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) `
                        -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
                        -MultipleInstances IgnoreNew
        $principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited
        Register-ScheduledTask -TaskName $taskname -Force -Action $action -Trigger $trigger `
            -Settings $settings -Principal $principal `
            -Description "DECnet node (cppdecnet decnetd), for PathNoWorks" | Out-Null
        Start-ScheduledTask -TaskName $taskname
        Write-Output "decnetd started"
    }
    "Restart" {
        Stop-Ours
        Start-ScheduledTask -TaskName $taskname
        Write-Output "decnetd restarted"
    }
    "Remove" {
        Stop-Ours
        if (Get-ScheduledTask -TaskName $taskname -ErrorAction SilentlyContinue) {
            Unregister-ScheduledTask -TaskName $taskname -Confirm:$false
        }
        Write-Output "decnetd task removed"
    }
}
