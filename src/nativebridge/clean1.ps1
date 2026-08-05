# clean1.ps1 — trim the nativebridge install tree to just the artifacts Unity
# needs (the native lib + public headers). No bin/curl-config, cmake or pkgconfig
# is produced, but strip share/ just in case.
$install_dir = $args[0]

function sremove($path) {
    if (Test-Path $path) { Remove-Item $path -Recurse -Force }
}

if ((Test-Path $install_dir -PathType Container)) {
    Write-Output "Cleaning ${install_dir}..."
    sremove "$install_dir/share"
    sremove "$install_dir/lib/cmake"
    sremove "$install_dir/lib/pkgconfig"
}
