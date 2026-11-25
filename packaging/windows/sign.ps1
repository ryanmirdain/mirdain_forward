# Signs Windows binaries with Azure Artifact Signing (formerly Trusted Signing), as Mirdain Systems.
#   pwsh packaging/windows/sign.ps1 <file> [<file> ...]
# CI signs build/Release before packaging, and Inno Setup calls this for the installer and its
# uninstaller (see installer.iss.in). Files that already carry a valid signature, such as
# Microsoft's runtime DLLs, are left as they are.
#
# The account comes from FORWARD_SIGNING_ENDPOINT (the account's regional endpoint, such as
# https://eus.codesigning.azure.net), FORWARD_SIGNING_ACCOUNT and FORWARD_SIGNING_PROFILE.
# Azure credentials come from DefaultAzureCredential: AZURE_TENANT_ID, AZURE_CLIENT_ID and
# AZURE_CLIENT_SECRET in CI, or `az login` locally.
param([Parameter(Mandatory, ValueFromRemainingArguments)][string[]]$Files)
$ErrorActionPreference = 'Stop'

# The signing client signtool loads, pinned. It needs the .NET 8 runtime, which GitHub's Windows
# runners include.
$version = '1.0.146'
$hash = '7761E5D8F31E54B764DA21D33CD8B0A2940E1B05526C7DFCB5E94B0431426F70'
$client = Join-Path $PSScriptRoot "../../build-deps/artifact-signing-$version"
$dlib = Join-Path $client 'bin/x64/Azure.CodeSigning.Dlib.dll'
if (-not (Test-Path $dlib)) {
    $package = "$client.nupkg"
    Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/microsoft.artifactsigning.client/$version/microsoft.artifactsigning.client.$version.nupkg" -OutFile $package
    if ((Get-FileHash $package -Algorithm SHA256).Hash -ne $hash) { throw "Unexpected hash for $package" }
    Expand-Archive $package $client -Force
    Remove-Item $package
}

$metadata = Join-Path $client 'metadata.json'
@{
    Endpoint = $env:FORWARD_SIGNING_ENDPOINT
    CodeSigningAccountName = $env:FORWARD_SIGNING_ACCOUNT
    CertificateProfileName = $env:FORWARD_SIGNING_PROFILE
} | ConvertTo-Json | Set-Content $metadata

# The dlib needs x64 signtool from Windows SDK 10.0.22621.755 or later.
$signtool = Get-ChildItem "${env:ProgramFiles(x86)}/Windows Kits/10/bin/*/x64/signtool.exe" |
    Sort-Object { [version]$_.Directory.Parent.Name } | Select-Object -Last 1

$unsigned = $Files | Where-Object { (Get-AuthenticodeSignature $_).Status -ne 'Valid' }
if (-not $unsigned) { return }
& $signtool.FullName sign /fd SHA256 /tr http://timestamp.acs.microsoft.com /td SHA256 /dlib $dlib /dmdf $metadata @unsigned
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
