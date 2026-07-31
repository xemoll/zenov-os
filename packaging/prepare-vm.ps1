[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('Qemu', 'VirtualBox', 'VMware')]
    [string]$Hypervisor = 'Qemu',

    [string]$VmName = 'ZenovOS 0.1.1',

    [switch]$PrepareOnly,

    [switch]$ResetDisk
)

$ErrorActionPreference = 'Stop'
$Version = '0.1.1'
$BaseDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Iso = Join-Path $BaseDir "ZenovOS-$Version-x86.iso"
$Raw = Join-Path $BaseDir "ZenovOS-$Version-data.img"
$Qcow2 = Join-Path $BaseDir "ZenovOS-$Version-data.qcow2"
$Vdi = Join-Path $BaseDir "ZenovOS-$Version-data.vdi"
$Vmdk = Join-Path $BaseDir "ZenovOS-$Version-data.vmdk"
$Vmx = Join-Path $BaseDir "ZenovOS-$Version.vmx"

function Require-Command {
    param([Parameter(Mandatory)][string]$Name)
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $command) {
        throw "Required command was not found: $Name"
    }
    return $command.Source
}

function Assert-File {
    param([Parameter(Mandatory)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file was not found: $Path"
    }
    if ((Get-Item -LiteralPath $Path).Length -le 0) {
        throw "Required file is empty: $Path"
    }
}

Assert-File $Iso
Assert-File $Raw

switch ($Hypervisor) {
    'Qemu' {
        $QemuImg = Require-Command 'qemu-img'
        $Qemu = Require-Command 'qemu-system-i386'
        if ($ResetDisk -and (Test-Path -LiteralPath $Qcow2)) {
            Remove-Item -LiteralPath $Qcow2 -Force
        }
        if (-not (Test-Path -LiteralPath $Qcow2)) {
            & $QemuImg convert -q -f raw -O qcow2 -o 'compat=1.1,cluster_size=65536' $Raw $Qcow2
            if ($LASTEXITCODE -ne 0) { throw 'qemu-img conversion failed' }
        }
        & $QemuImg check -q -f qcow2 $Qcow2
        if ($LASTEXITCODE -ne 0) { throw 'qcow2 consistency check failed' }
        if ($PrepareOnly) {
            Write-Host "QEMU appliance prepared: $Qcow2"
            break
        }
        & $Qemu '-machine' 'pc,vmport=off' '-m' '64M' '-vga' 'std' `
            '-drive' "file=$Qcow2,format=qcow2,if=ide,index=0,media=disk" `
            '-drive' "file=$Iso,format=raw,if=ide,index=2,media=cdrom,readonly=on" `
            '-boot' 'order=d,strict=on'
        exit $LASTEXITCODE
    }

    'VirtualBox' {
        $VBoxManage = Require-Command 'VBoxManage'
        if ($ResetDisk -and (Test-Path -LiteralPath $Vdi)) {
            Remove-Item -LiteralPath $Vdi -Force
        }
        if (-not (Test-Path -LiteralPath $Vdi)) {
            & $VBoxManage convertfromraw $Raw $Vdi --format VDI | Out-Null
            if ($LASTEXITCODE -ne 0) { throw 'VirtualBox VDI conversion failed' }
        }
        & $VBoxManage showvminfo $VmName *> $null
        if ($LASTEXITCODE -eq 0) {
            throw "VirtualBox VM already exists: $VmName. Choose another -VmName or remove it explicitly."
        }
        & $VBoxManage createvm --name $VmName --ostype Other --register | Out-Null
        try {
            & $VBoxManage modifyvm $VmName --memory 64 --firmware bios `
                --boot1 dvd --boot2 disk --boot3 none --boot4 none `
                --acpi on --ioapic off --nic1 none | Out-Null
            & $VBoxManage storagectl $VmName --name 'IDE Controller' --add ide `
                --controller PIIX4 --bootable on | Out-Null
            & $VBoxManage storageattach $VmName --storagectl 'IDE Controller' `
                --port 0 --device 0 --type hdd --medium $Vdi | Out-Null
            & $VBoxManage storageattach $VmName --storagectl 'IDE Controller' `
                --port 1 --device 0 --type dvddrive --medium $Iso | Out-Null
        }
        catch {
            & $VBoxManage unregistervm $VmName *> $null
            throw
        }
        if ($PrepareOnly) {
            Write-Host "VirtualBox appliance prepared: $VmName"
            break
        }
        & $VBoxManage startvm $VmName
        exit $LASTEXITCODE
    }

    'VMware' {
        if ($ResetDisk -and (Test-Path -LiteralPath $Vmdk)) {
            Remove-Item -LiteralPath $Vmdk -Force
        }
        if (-not (Test-Path -LiteralPath $Vmdk)) {
            $VdiskManager = Get-Command 'vmware-vdiskmanager' -ErrorAction SilentlyContinue
            if ($VdiskManager) {
                & $VdiskManager.Source -r $Raw -t 0 $Vmdk | Out-Null
            }
            else {
                $QemuImg = Require-Command 'qemu-img'
                & $QemuImg convert -q -f raw -O vmdk `
                    -o 'subformat=monolithicSparse,compat6' $Raw $Vmdk
            }
            if ($LASTEXITCODE -ne 0) { throw 'VMware VMDK conversion failed' }
        }
        Assert-File $Vmx
        $Vmrun = Get-Command 'vmrun' -ErrorAction SilentlyContinue
        if (-not $PrepareOnly -and $Vmrun) {
            & $Vmrun.Source start $Vmx gui
            exit $LASTEXITCODE
        }
        Write-Host "VMware appliance prepared. Open: $Vmx"
    }
}
