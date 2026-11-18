# Engine Sound Lab

**English** · [Русский](README.ru.md)

Engine Sound Lab is an offline Windows synthesizer for creating engine sound effects for Iron Sector and other games. It models piston firing, intake and exhaust pressure, mechanical vibration, and independently rotating turbine shafts. The repository includes nine JSON presets and 25 synthesized WAV examples.

## Build and run on Windows

Install Qt 6 for MinGW x64, its matching MinGW-w64 toolchain, CMake 3.24 or later, and Ninja. Make CMake available on `PATH`. The tested toolchain is Qt 6.11.1, MinGW 13.1, and C++20.

```powershell
pwsh -File .\Build-Windows.ps1
```

The script builds the Release configuration, runs CTest, and collects the runtime libraries in `dist/Engine Sound Lab`. It also creates `dist/Engine-Sound-Lab-Windows-x64.zip`. Extract the entire archive and launch `EngineSoundLab.exe`. The portable app runs offline on Windows 10 and 11 x64 without the Qt SDK.

Pass `-QtRoot`, `-MinGWRoot`, and `-NinjaDirectory` for a custom SDK installation. Use `-SkipPackage` for a build with tests. Set `-DESL_BUILD_APP=OFF` in CMake to build the standalone DSP core without Qt.

## Using the bench

1. Pick an engine preset and press **Start** (`Пуск`). Audio plays through the default Windows output device.
2. Set throttle and load independently. Use **RPM Hold** (`Удержание`) to hear how load changes the tone at a fixed speed. Gear changes affect reflected inertia and gearbox noise. The bench briefly cuts drive between gears.
3. Use the **Construction** (`Конструкция`) and **Dynamics** (`Динамика`) tabs to edit the model. Inline, V and flat engines support 1–24 cylinders. A four-stroke radial uses one row with an odd cylinder count, or two equal rows with odd cylinder counts. The firing order follows crank geometry. Turbine presets expose separate gas generator and output shaft parameters. [FAA Powerplant Handbook](https://www.faa.gov/documentLibrary/media/Advisory_Circular/AC_65-12A.pdf)
4. Mix the six layers independently and adjust their gain, resonance, low-pass filtering and saturation. **S** solos a layer and **M** mutes it. Select and run **Automatic test** (`Автоматическое испытание`) to cycle through startup, acceleration, gear shifts, full load, lift-off, engine braking and shutdown.

Use **Save** and **Open** to write and restore versioned JSON presets. Each preset stores the model, mix, dynamics, fault settings and random seed.

### Engine faults

The **Faults** (`Сбои`) tab applies changes during playback and export.

| Control | Effect |
| --- | --- |
| Spark misfire | Sets the chance that the selected gasoline cylinder misses each firing event. |
| Injector wear | Makes fueling uneven and adds a late injection pulse to the selected diesel cylinder. |
| Fuel bias | Negative values starve the engine and lower torque. Positive values overfuel it and can cause uneven combustion and exhaust afterfire in piston models. |
| Overload | Adds resistance above the normal load setting and accentuates low-frequency vibration. |
| Mechanical wear | Increases valve clatter and gearbox rattle. On turbines it affects the gearbox. |

Compare the [healthy V8](examples/v8_single/steady.wav) with the [V8 with faults](examples/v8-misfire_single/steady.wav). Both examples use 1,900 RPM and 58% load. The [fault preset](examples/v8-misfire_single/preset.json) is included alongside the audio.

## WAV export for games

- **Single sound** captures the bench's RPM, load and throttle as a loop or a faded one-shot.
- **RPM Sound Pack** produces five steady loops from idle to maximum RPM, plus startup, acceleration, deceleration and shutdown.
- **RPM × load grid** renders loops for up to 16 RPM points and 10 load points. Each axis is evenly spaced over the chosen range. With one point, the renderer uses the lower bound.

Every mode supports 44.1 or 48 kHz, 16- or 24-bit PCM, mono or stereo, and optional −1 dBFS peak normalization. A pack or grid uses one gain factor to preserve the relative levels of its clips. Rendering runs in the background and can be cancelled from the status bar.

Each export directory contains the WAV files, `preset.json` and `manifest.json`. The manifest records the engine, seed, PCM format, clip names, RPM, throttle, load and loop bounds. Throttle and load range from 0 to 1. `loop.startFrame` is inclusive and `loop.endFrameExclusive` is exclusive. WAV files also carry a standard `smpl` chunk for loops. In Godot 4, use **Import → Edit → Loop Mode → Detect from WAV**. [Godot audio import guide](https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/importing_audio_samples.html)

| Demo | Contents |
| --- | --- |
| [T-72B3](examples/t-72b3_pack) | Nine diesel V12 clips |
| [M1 Abrams](examples/m1-abrams_pack) | Nine gas turbine clips |
| [BMP-2](examples/bmp-2_grid) | Four mono V6 loops at 1,100 and 2,000 RPM and 20% and 80% load |
| [Leopard 2](examples/leopard-2_single) | One stereo diesel V12 idle loop |
| [Gasoline V8](examples/v8_single) and [V8 with faults](examples/v8-misfire_single) | Two loops at matching RPM and load |

To render another copy of the examples from the packaged build, run this from the repository root.

```powershell
$exe = Join-Path (Get-Location) 'dist\Engine Sound Lab\EngineSoundLab.exe'
$output = Join-Path (Get-Location) 'examples'
$process = Start-Process -FilePath $exe -ArgumentList '--export-demo', ('"' + $output + '"') -WindowStyle Hidden -PassThru -Wait
if ($process.ExitCode -ne 0) { throw 'Example rendering failed.' }
```

## Architecture and verification

- [`src/core`](src/core) contains the engine geometry, simulation controller, DSP, mixer and offline renderer. Audio is synthesized at twice the output rate and low-pass filtered before downsampling.
- [`src/app`](src/app) contains the Qt Widgets interface, JSON and WAV handling, and the dedicated WinMM playback thread.
- [`tests`](tests) covers firing order, dynamics, RPM hold, layer isolation, faults, deterministic rendering, and WAV headers and loop boundaries. PCM 16- and 24-bit assets were imported in Godot 4.7.2.

Vehicle presets use published engine specifications where available. The modeled shaft speeds, inertia and acoustic settings are tuned for sound design. Leopard 2, Abrams and gasoline V8 spectra were compared against open field recordings. [Preset references](README.ru.md#конструктивные-данные-пресетов) · [Third-party components and recordings](THIRD-PARTY.md)
