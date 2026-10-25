param(
    [Parameter(Mandatory = $true, Position = 0)][string]$GameExe,
    [Parameter(Mandatory = $true, Position = 1)][string]$Output
)
$ErrorActionPreference = 'Stop'
$expected = 'C6B4116788B7F68C56860FB9CC8A94BF984E620907031E4BC3DB43623DBE579A'
if ((Get-FileHash -LiteralPath $GameExe -Algorithm SHA256).Hash -ne $expected) {
    Write-Warning 'Unrecognized HOTD2 executable SHA256; attempting font extraction.'
}
$exe = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $GameExe).Path)
$pe = [BitConverter]::ToUInt32($exe, 60)
$sections = $pe + 24 + [BitConverter]::ToUInt16($exe, $pe + 20)
$ranges = for ($i = 0; $i -lt [BitConverter]::ToUInt16($exe, $pe + 6); $i++) {
    $entry = $sections + $i * 40
    @{
        Start = [BitConverter]::ToUInt32($exe, $entry + 12)
        Size = [Math]::Min([BitConverter]::ToUInt32($exe, $entry + 8), [BitConverter]::ToUInt32($exe, $entry + 16))
        Raw = [BitConverter]::ToUInt32($exe, $entry + 20)
    }
}
function Read-GameValue([long]$Address, [int]$Size) {
    $rva = $Address - 0x400000
    foreach ($range in $ranges) {
        if ($rva -ge $range.Start -and $rva + $Size -le $range.Start + $range.Size) {
            $offset = $range.Raw + $rva - $range.Start
            if ($Size -eq 2) { return [BitConverter]::ToUInt16($exe, $offset) }
            return [BitConverter]::ToUInt32($exe, $offset)
        }
    }
    throw ('Invalid font address: 0x{0:X}' -f $Address)
}
$source = [IO.File]::ReadAllBytes((Join-Path (Split-Path -LiteralPath $GameExe) 'tex\scr_opt_moji05.bin'))
if ($source.Length -lt 4 -or [BitConverter]::ToUInt32($source, 0) -ne 64512) {
    throw 'Invalid compressed font size.'
}
$state = @{ Position = 4; Bits = 0; Left = 0 }
function Read-Byte {
    if ($state.Position -ge $source.Length) { throw 'Truncated compressed font.' }
    return [int]$source[$state.Position++]
}
function Read-Bit {
    if (!$state.Left) { $state.Bits = Read-Byte; $state.Left = 8 }
    $bit = $state.Bits -band 1
    $state.Bits = $state.Bits -shr 1
    $state.Left--
    return $bit
}
$pixels = New-Object byte[] 64512
$length = 0
while ($true) {
    if (Read-Bit) {
        if ($length -ge $pixels.Length) { throw 'Decompressed font exceeds expected size.' }
        $pixels[$length++] = Read-Byte
        continue
    }
    if (Read-Bit) {
        $word = (Read-Byte) -bor ((Read-Byte) -shl 8)
        if (!$word) { break }
        $offset = ($word -shr 3) - 8192
        $count = $word -band 7
        if (!$count) { $count = (Read-Byte) + 1 } else { $count += 2 }
    } else {
        $count = (((Read-Bit) -shl 1) -bor (Read-Bit)) + 2
        $offset = (Read-Byte) - 256
    }
    if ($offset -ge 0 -or $offset -lt -$length -or $length + $count -gt $pixels.Length) {
        throw 'Invalid compressed font back-reference.'
    }
    for ($i = 0; $i -lt $count; $i++) {
        $pixels[$length] = $pixels[$length + $offset]
        $length++
    }
}
if ($length -ne $pixels.Length) { throw 'Unexpected decompressed font size.' }
$descriptors = Read-GameValue (0x55B9B8 + 0x15E * 4) 4
$count = Read-GameValue (0x55C0A8 + 0x15E * 2) 2
if ($count -lt 1 -or $count -gt 4096) { throw 'Invalid font descriptor count.' }
$glyphs = New-Object byte[] (4 + 128 * 32 * 32 * 4)
[Text.Encoding]::ASCII.GetBytes('H2PF').CopyTo($glyphs, 0)
foreach ($character in 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789'.ToCharArray()) {
    $glyph = Read-GameValue (0x56AED0 + [int]$character * 2) 2
    if ((Read-GameValue (0x57A5BC + $glyph * 4) 4) -ne 0x15E) { throw 'Unexpected font texture group.' }
    $texture = Read-GameValue (0x57D448 + $glyph * 4) 4
    $matches = @(for ($i = 0; $i -lt $count; $i++) {
        if ((Read-GameValue ($descriptors + $i * 16 + 12) 4) -eq $texture) { $descriptors + $i * 16 }
    })
    if ($matches.Count -ne 1) { throw 'Expected one font texture descriptor.' }
    $descriptor = $matches[0]
    $width = Read-GameValue $descriptor 2
    $height = Read-GameValue ($descriptor + 2) 2
    if ($width -lt 1 -or $width -gt 32 -or $height -lt 1 -or $height -gt 32 -or
        (Read-GameValue ($descriptor + 4) 4) -ne 0x902) { throw 'Invalid ARGB4444 font dimensions or format.' }
    $offset = Read-GameValue ($descriptor + 8) 4
    if ([long]$offset + $width * $height * 2 -gt $pixels.Length) { throw 'Font pixels outside texture.' }
    for ($i = 0; $i -lt $width * $height; $i++) {
        $value = [BitConverter]::ToUInt16($pixels, $offset + $i * 2)
        # Native surface upload reverses the stored rows.
        $target = 4 + ([int]$character * 32 * 32 + ($height - 1 - [Math]::Floor($i / $width)) * 32 + $i % $width) * 4
        $glyphs[$target] = (($value -shr 8) -band 15) * 17
        $glyphs[$target + 1] = (($value -shr 4) -band 15) * 17
        $glyphs[$target + 2] = ($value -band 15) * 17
        $glyphs[$target + 3] = (($value -shr 12) -band 15) * 17
    }
}
$outputPath = [IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outputPath)) | Out-Null
[IO.File]::WriteAllBytes($outputPath, $glyphs)
Write-Output "Native menu font: $outputPath ($($glyphs.Length - 4) RGBA bytes)"
