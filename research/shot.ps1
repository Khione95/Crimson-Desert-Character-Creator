# Takes a screenshot of the primary screen (half size) into C:\temp\cc_research\shots\<name>.png
param([string]$name = "shot")

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$dir = "C:\temp\cc_research\shots"
New-Item -ItemType Directory -Force $dir | Out-Null

$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
$small = New-Object System.Drawing.Bitmap $bmp, ([int]($b.Width / 2)), ([int]($b.Height / 2))
$small.Save("$dir\$name.png", [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose(); $small.Dispose()
"$dir\$name.png"
