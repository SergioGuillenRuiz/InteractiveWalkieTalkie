# ============================================================
#  Prueba automatizada MULTI-DISPOSITIVO sobre el "aire" compartido.
#
#  Lanza dispositivos REALES concurrentes (procesos independientes, firmware +
#  EEPROM propios) que se comunican por radio simulada en tiempo real.
#
#  Escena A (3 equipos, difusion):  #1 emite -> #2 y #3 reciben y confirman (ACK);
#                                   #1 recibe la confirmacion; #1 NO se oye a si
#                                   mismo (half-duplex).
#  Escena B (2 equipos, bidireccional): #1 y #2 se envian mensajes a la vez; cada
#                                   uno recibe el del otro, lo confirma y NO oye
#                                   el propio. Prueba ambos sentidos del enlace.
#
#  Plantilla para testear futuras funciones de comunicacion multi-dispositivo.
#  Uso:  powershell -ExecutionPolicy Bypass -File net_test.ps1
# ============================================================
$ErrorActionPreference = "Stop"
$sim  = "$PSScriptRoot\out\walkie_sim.exe"
$work = "$PSScriptRoot\out"

# --- Compilar ---
& "$env:ComSpec" /c "$PSScriptRoot\build.bat" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "build fallo" }

function Clear-Air($dir) {
    if (Test-Path $dir) { Get-ChildItem $dir -File | Remove-Item -Force -ErrorAction SilentlyContinue }
    else { New-Item -ItemType Directory -Path $dir | Out-Null }
}

# --pace 20: tope de velocidad del tiempo virtual (20 ms virtuales por ms real). Sin tope cada
# proceso corre su tiempo casi instantaneo y los equipos dejan de coincidir en el aire.
# --radio ideal: en Windows no esta el modo sincronizado (--nodes, solo Linux/macOS), y con los relojes
# de los procesos desfasados decenas de ms el modelo realista (tiempo en el aire, colisiones) daria
# colisiones espurias: aqui se usa el modelo sin tiempo en el aire. Las colisiones reales se prueban en
# Linux con net_test.sh.
function Start-Node($air, $node, $chip, $keysText, $keysFile, $log) {
    Set-Content "$work\$keysFile" $keysText -Encoding ASCII
    $env:SIM_CHIPID = "$chip"
    $p = Start-Process -FilePath $sim -PassThru -NoNewWindow -RedirectStandardOutput $log `
        -ArgumentList @("--keys","$work\$keysFile","--air",$air,"--node","$node","--pace","20","--radio","ideal",
                        "--eeprom","$work\net_$node.bin","--fresh","--shots",$work)
    Remove-Item Env:\SIM_CHIPID -ErrorAction SilentlyContinue
    return $p
}

$fail = 0
function Check($desc, $cond) {
    if ($cond) { Write-Host "  [PASS] $desc" } else { Write-Host "  [FAIL] $desc"; $script:fail++ }
}

# Guion: navegar IDLE -> Enviar -> Instant -> mensaje <potIdx> -> enviar; seguir vivo.
# pot 80   -> "Enviar"  (idx0 de 3 en IDLE).
# pot 384  -> modo "Instant" (idx1 de 4: Morse/Instant/Rueda/Frase). OJO: antes
#             ponia pot 1023, que cae en "Frase" (idx3) -> no enviaba nada y el
#             test fallaba. 384 selecciona Instant con el reparto en bandas iguales.
# potIdx   -> mensaje del grid Instant (8 opciones): 585->"happy", 460->"kissy".
function SenderKeys($potIdx, $life) {
@"
0 pot 80
3000 mdown
3120 mup
3600 pot 384
4000 mdown
4120 mup
4600 pot $potIdx
5000 mdown
5120 mup
$life q
"@
}

# ============================================================
#  ESCENA A: difusion 1 -> (2,3) + ACK + half-duplex
# ============================================================
Write-Host "`n==== Escena A: difusion 3 equipos (#1 -> #2,#3) ===="
$airA = "$work\air_a"
Clear-Air $airA

# Receptor: en reposo recibe; abre el historial y captura.
$rxKeys = @'
0 pot 512
7000 mdown
7120 mup
9000 shot __SHOT__
10500 q
'@

$a1 = Start-Node $airA 1 100 (SenderKeys 585 10500) "_a1.txt" "$work\_a1.out"   # #1 envia "happy"
$a2 = Start-Node $airA 2 200 ($rxKeys -replace "__SHOT__","net_rx2") "_a2.txt" "$work\_a2.out"
$a3 = Start-Node $airA 3 300 ($rxKeys -replace "__SHOT__","net_rx3") "_a3.txt" "$work\_a3.out"
$a1,$a2,$a3 | Wait-Process -Timeout 60

$o1 = Get-Content "$work\_a1.out" -Raw
$o2 = Get-Content "$work\_a2.out" -Raw
$o3 = Get-Content "$work\_a3.out" -Raw

Check "#1 emite 'happy'"                  ($o1 -match "Chat\] Enviando: happy")
Check "#2 (id 201) recibe 'happy'"        ($o2 -match "Guardado: happy")
Check "#3 (id 47) recibe 'happy'"         ($o3 -match "Guardado: happy")
Check "#2 confirma con ACK a #101"        ($o2 -match "ACK a #101")
Check "#3 confirma con ACK a #101"        ($o3 -match "ACK a #101")
Check "#1 recibe la confirmacion (ACK)"   ($o1 -match "Confirmado: entregado")
Check "half-duplex: #1 NO se oye a si mismo" (-not ($o1 -match "ACK a #101"))

# ============================================================
#  ESCENA B: bidireccional 1 <-> 2 (cada uno emisor y receptor)
# ============================================================
Write-Host "`n==== Escena B: bidireccional 2 equipos (#1 <-> #2) ===="
$airB = "$work\air_b"
Clear-Air $airB

$b1 = Start-Node $airB 1 100 (SenderKeys 585 14000) "_b1.txt" "$work\_b1.out"   # #1 envia "happy"
$b2 = Start-Node $airB 2 200 (SenderKeys 460 14000) "_b2.txt" "$work\_b2.out"   # #2 envia "kissy"
$b1,$b2 | Wait-Process -Timeout 60

$q1 = Get-Content "$work\_b1.out" -Raw
$q2 = Get-Content "$work\_b2.out" -Raw

Check "#1 envia 'happy'"                   ($q1 -match "Chat\] Enviando: happy")
Check "#2 envia 'kissy'"                   ($q2 -match "Chat\] Enviando: kissy")
Check "#1 recibe 'kissy' (de #2)"          ($q1 -match "Guardado: kissy")
Check "#2 recibe 'happy' (de #1)"          ($q2 -match "Guardado: happy")
Check "#1 identifica al emisor #201"       ($q1 -match "ACK a #201")
Check "#2 identifica al emisor #101"       ($q2 -match "ACK a #101")
Check "#1 confirma su envio (ACK de #2)"   ($q1 -match "Confirmado: entregado")
Check "#2 confirma su envio (ACK de #1)"   ($q2 -match "Confirmado: entregado")
Check "half-duplex: #1 NO recibe su 'happy'" (-not ($q1 -match "ACK a #101"))
Check "half-duplex: #2 NO recibe su 'kissy'" (-not ($q2 -match "ACK a #201"))

# --- Capturas a PNG ---
Add-Type -AssemblyName System.Drawing
foreach ($n in @("net_rx2","net_rx3")) {
    if (Test-Path "$work\$n.bmp") {
        $bmp = [System.Drawing.Bitmap]::FromFile("$work\$n.bmp")
        $bmp.Save("$work\$n.png", [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    }
}

Remove-Item "$work\_a1.txt","$work\_a2.txt","$work\_a3.txt","$work\_b1.txt","$work\_b2.txt" -ErrorAction SilentlyContinue
Write-Host ""
if ($fail -eq 0) { Write-Host "MULTI-DISPOSITIVO OK: comunicacion real (difusion + bidireccional) verificada." }
else             { Write-Host "$fail comprobacion(es) fallaron." }
exit $fail
