param([Parameter(Mandatory = $true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'

# Test-only, self-signed localhost certificate. No certificate-store changes.
$rsa = [System.Security.Cryptography.RSA]::Create(2048)
$request = [System.Security.Cryptography.X509Certificates.CertificateRequest]::new(
    'CN=localhost', $rsa,
    [System.Security.Cryptography.HashAlgorithmName]::SHA256,
    [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)
$san = [System.Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder]::new()
$san.AddDnsName('localhost')
$san.AddIpAddress([System.Net.IPAddress]::Parse('127.0.0.1'))
$request.CertificateExtensions.Add($san.Build())
$request.CertificateExtensions.Add(
    [System.Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($true, $false, 0, $true))
$certificate = $request.CreateSelfSigned([DateTimeOffset]::UtcNow.AddMinutes(-2), [DateTimeOffset]::UtcNow.AddMinutes(10))
[System.IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
[System.IO.File]::WriteAllBytes((Join-Path $OutputDirectory 'server.cer'), $certificate.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert))
[System.IO.File]::WriteAllBytes((Join-Path $OutputDirectory 'server.pfx'), $certificate.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Pfx))
$certificate.Dispose()
$rsa.Dispose()
