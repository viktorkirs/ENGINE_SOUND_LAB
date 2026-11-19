# Rendering engine audio from an agent or script

`EngineSoundLabCLI.exe` is included in the portable Windows archive. It uses the same presets, recording banks and WAV exporter as the desktop app. It works offline and needs no audio device. Use PowerShell 7 for the examples below.

## Find the engine and render it

`catalog` returns `presets` and `vehicles`. Each vehicle has a `presetId` that links to `presets[].id`. A null `presetId` means the catalog has no model for that vehicle. Preset entries include idle and maximum RPM and the recorded RPM anchors, if present.

~~~powershell
$engineSoundLab = 'C:\Tools\Engine Sound Lab\EngineSoundLabCLI.exe'
$catalog = & $engineSoundLab catalog | ConvertFrom-Json
$catalog.vehicles | Where-Object id -eq 'east_bmp2'
$details = & $engineSoundLab show-preset --vehicle east_bmp2 | ConvertFrom-Json
$details.preset.model

$result = & $engineSoundLab export --vehicle east_bmp2 --mode pack --output 'D:\Game\audio\engines' | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { throw 'Engine sound export failed.' }
$clips = (Get-Content -LiteralPath $result.manifest -Raw -Encoding utf8 | ConvertFrom-Json).clips
~~~

`show-preset` and `export` take exactly one selector.

| Selector | Source |
| --- | --- |
| `--vehicle east_bmp2` | A game vehicle ID from the catalog |
| `--preset 07_gasoline_i4` | A built-in preset ID from the catalog |
| `--preset-file my-engine.json` | A version 1 preset saved by the app or taken from the `preset` field of `show-preset` |

For a customized mix, save the `preset` object to a UTF-8 JSON file and edit its `listener`, `layers` or `faults`. The `faults` object supports `cylinder`, `sparkMiss`, `fuelBias`, `injectorWear`, `overload` and `mechanicalWear`. A negative `fuelBias` models lean operation and a positive value models rich operation. Render that file with `--preset-file`. Each export also saves the effective `preset.json` for reuse.

## Export options

Pass `--mode single`, `--mode pack`, `--mode grid` or `--mode drive`. The default is `pack`. Pass optional settings as a UTF-8 JSON object in `--options FILE`. Here is a compact mono grid for the BMP-1/2 preset.

~~~json
{
  "sampleRate": 48000,
  "bitsPerSample": 16,
  "channels": 1,
  "seconds": 3,
  "rpmMin": 830,
  "rpmMax": 2460,
  "rpmCount": 6,
  "loadMin": 0.2,
  "loadMax": 0.8,
  "loadCount": 2
}
~~~

~~~powershell
& $engineSoundLab export --vehicle east_bmp2 --mode grid --options bmp-grid.json --output 'D:\Game\audio\engines'
~~~

| Mode | Output and configurable fields |
| --- | --- |
| All | `sampleRate` 44100 or 48000, `bitsPerSample` 16 or 24, `channels` 1 or 2, and `normalize` true or false. Defaults are 48000 Hz, 24-bit stereo and normalization on. |
| `single` | One WAV at `rpm`, `throttle` and `load`. Defaults are idle RPM from the preset, zero throttle and 0.12 load. `singleLoop` defaults to true. |
| `pack` | Nine clips including start, five steady regimes, acceleration, deceleration and shutdown. |
| `grid` | Loops at evenly spaced RPM and load points. The default is four RPM points from idle to maximum and two load points from 0.2 to 0.8. Use `rpmMin`, `rpmMax`, `rpmCount` (1–16), `loadMin`, `loadMax`, `loadCount` (1–10). |
| `drive` | One continuous drive scenario using the chosen preset. |

`seconds` adjusts the duration of the steady clips in `single`, `pack` and `grid`, from 1 to 15 seconds. The default is 4 seconds. Throttle and load use the range 0 to 1. CLI errors include the name of an unknown option or a field with the wrong type.

## Results and Godot import

The three commands return a UTF-8 JSON object on stdout with `ok: true` and `schemaVersion: 1`. `export` also returns `directory`, `manifest`, `presetFile` and `wavCount`. These paths are absolute. Each export creates a fresh subdirectory, so use the actual path in `manifest`. Built-in selections include `presetId`. Vehicle selections also include `vehicleId`. On failure, stdout is empty and stderr contains `ok: false`, `schemaVersion` and an `error` object with `code` and `message`. Exit codes are 2 for arguments or options, 3 for preset selection, 4 for export failures and 5 for an unavailable embedded catalog.

In the manifest, `clips[].file` is relative to the export directory. Use `rpm`, `load` and `role` from each clip to bind it to the game audio system. A null `loop` marks a one-shot clip. Looped WAVs contain a `smpl` chunk. In Godot 4, set **Import → Edit → Loop Mode** to **Detect from WAV**. For a grid, use the WAVs with existing `AudioStreamPlayer3D` nodes and interpolate volume between adjacent RPM and load points. [Godot documentation for importing WAV](https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/importing_audio_samples.html)

## CLI-only build

With CMake 3.24 or later, a C++20 compiler and Qt Core 6.7 or later, configure with `-DESL_BUILD_APP=OFF -DESL_BUILD_CLI=ON`. The CLI target is `EngineSoundLabCLI`.
