# Generates 16 kHz mono WAV words with the Windows speech synthesizer.
# The words are only used to calibrate the binary feature layer of the
# voice learning model (tools/make_learning_models.py --speech <folder>).
#
# Usage (PowerShell):  .\make_tts_calibration.ps1 -OutDir .\speech
param([string]$OutDir = ".\speech")

Add-Type -AssemblyName System.Speech
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$words = @('down','go','left','no','off','on','right','stop','up','yes','house','tree','cat','hello')
$format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000,
    [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,
    [System.Speech.AudioFormat.AudioChannel]::Mono)
$voices = (New-Object System.Speech.Synthesis.SpeechSynthesizer).GetInstalledVoices() |
    ForEach-Object { $_.VoiceInfo.Name }
foreach ($voice in $voices) {
  foreach ($rate in @(-2, 0, 2)) {
    foreach ($word in $words) {
      $synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
      $synth.SelectVoice($voice)
      $synth.Rate = $rate
      $tag = ($voice -split ' ')[1].ToLower()
      $file = Join-Path (Resolve-Path $OutDir) ("{0}_{1}_{2}.wav" -f $tag, $rate, $word)
      $synth.SetOutputToWaveFile($file, $format)
      $synth.Speak($word)
      $synth.Dispose()
    }
  }
}
