# Regenerate the spoken-alert WAV clips (offline, Windows SAPI — no cloud).
#   pwsh -File gen_clips.ps1 [VoiceName]
# Default voice = Microsoft Zira Desktop (en-US). 16 kHz / 16-bit / mono PCM WAV.
# Then run wav_to_c.py to embed them into main/speak_clips.c.
param([string]$Voice = 'Microsoft Zira Desktop')
Add-Type -AssemblyName System.Speech
$dir = $PSScriptRoot
$fmt = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000,[System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,[System.Speech.AudioFormat.AudioChannel]::Mono)
# 5H usage (announced on rising into 25/50/75/90/100) + battery (falling into 50/30/20/10/5)
$clips = [ordered]@{
  'usage_25' ='Five hour usage, twenty five percent';
  'usage_50' ='Five hour usage, fifty percent, halfway';
  'usage_75' ='Five hour usage, seventy five percent';
  'usage_90' ='Five hour usage, ninety percent, nearing the limit';
  'usage_100'='Five hour usage at one hundred percent, limit reached';
  'batt_50'  ='Battery, fifty percent';
  'batt_30'  ='Battery, thirty percent';
  'batt_20'  ='Battery, twenty percent';
  'batt_10'  ='Battery low, ten percent';
  'batt_5'   ='Battery critical, please charge'
}
$s = New-Object System.Speech.Synthesis.SpeechSynthesizer
$s.SelectVoice($Voice)
$s.Rate = 1
foreach($k in $clips.Keys){
  $p = Join-Path $dir ($k + '.wav')
  $s.SetOutputToWaveFile($p, $fmt); $s.Speak($clips[$k])
}
$s.SetOutputToNull(); $s.Dispose()
Write-Output "Generated $($clips.Count) clips in $dir (voice: $Voice)"
