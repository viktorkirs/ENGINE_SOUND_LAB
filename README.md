# Engine Sound Lab

**English** · [Русский](README.ru.md)

[Download the portable Windows build](https://github.com/viktorkirs/ENGINE_SOUND_LAB/releases/latest)

Engine Sound Lab is an offline Windows synthesizer for creating engine sound effects for Iron Sector and other games. It models piston firing, intake and exhaust pressure, mechanical vibration, and independently rotating turbine shafts. The source tree includes 64 built-in presets, 44 recording banks and 331 WAV examples. Click the current engine name or press **Ctrl+K** to open the searchable preset library. [Battlecity vehicle index](docs/battlecity-engine-library.md)

The T-90M now combines the low end of the T-72B3 preset with an exterior T-90M recording across five RPM anchors. The turbo whistle rises with load. Selected vehicle and truck loops also have less high-frequency hiss and steadier levels at cruise. [Recording notes](docs/recording-calibration.md)

## Build and run on Windows

Install Qt 6 for MinGW x64, its matching MinGW-w64 toolchain, CMake 3.24 or later, and Ninja. Make CMake available on `PATH`. The tested toolchain is Qt 6.11.1, MinGW 13.1, and C++20.

```powershell
pwsh -File .\Build-Windows.ps1
```

The script builds the Release configuration, runs CTest, and collects the runtime libraries in `dist/Engine Sound Lab`. It also creates `dist/Engine-Sound-Lab-Windows-x64.zip`. Extract the entire archive and launch `EngineSoundLab.exe`. The portable app runs offline on Windows 10 and 11 x64 without the Qt SDK.

Pass `-QtRoot`, `-MinGWRoot`, and `-NinjaDirectory` for a custom SDK installation. Use `-SkipPackage` for a build with tests. Set `-DESL_BUILD_APP=OFF -DESL_BUILD_CLI=OFF` in CMake to build the standalone DSP core without Qt.

For Godot and automation, run `EngineSoundLabCLI.exe` from the portable folder. Its `catalog`, `show-preset` and `export` commands select game vehicles or preset files and return machine-readable JSON. [CLI commands and Godot import](docs/agent-cli.md) · [Agent instructions](AGENTS.md)

## Using the bench

1. Click the engine name in the toolbar. Scroll the library or type a model, engine or in-game vehicle name. Use the arrow keys and Enter to choose a preset, or Escape to cancel. Press **Start** (`Пуск`). Audio plays through the default Windows output device.
2. Set throttle and load independently. Use **RPM Hold** (`Удержание`) to hear how load changes the tone at a fixed speed. Gear changes affect reflected inertia and gearbox noise. The bench briefly cuts drive between gears.
3. Use the **Construction** (`Конструкция`) and **Dynamics** (`Динамика`) tabs to edit the model. Inline, V and flat engines support 1–24 cylinders. A four-stroke radial uses one row with an odd cylinder count, or two equal rows with odd cylinder counts. The firing order follows crank geometry. Turbine presets expose separate gas generator and output shaft parameters. [FAA Powerplant Handbook](https://www.faa.gov/documentLibrary/media/Advisory_Circular/AC_65-12A.pdf)
4. Use the mixer faders to set layer gains in dB. **S** solos a layer and **M** mutes it. Presets with a microphone bank have a seventh **Recording** layer. Select and run **Automatic test** (`Автоматическое испытание`) to cycle through startup, acceleration, gear shifts, full load, lift-off, engine braking and shutdown.

Raise **Transmission** (Трансмиссия) and select gear 1–6 to add drivetrain sound to any engine. The mesh pitch follows the selected gear and shaft RPM. Load, shifts and lift-off change its level and harmonics. Click the channel name to adjust whine, bearing noise and coast tone. The **Oshkosh M1070A1 Drive** preset has the channel enabled. Press **Run** (Запустить) in the scenario row to hear the fifteen-second drive. [Mix, engine and isolated transmission](examples/drive)

The **Listener** selector switches between direct, interior, nearby, third-person and distant sound. Adjust exterior distance or cabin insulation below the tachometer. Playback and WAV export use the same settings. Normalization defaults off in the spatial views to retain relative levels. [Listening examples](examples/perspectives)

Use the sliders or enter numeric values directly. Changing bore or stroke updates displacement. Entering a displacement updates bore at the current stroke. Compression ratio affects combustion attack. The animated engine diagram follows the cylinder firing order or the two turbine shafts and offers a 1/30-speed view.

**Dynamics** includes an optional rev limiter with separate cut and return RPM. The I4 and V8 presets enable it. At full throttle the V8 cuts at 6,000 RPM and resumes at 5,800. [V8 rev limiter sample](examples/v8-rev-limiter_single/steady.wav)

The diagram highlights the chamber that fired, its bank and its place in the firing order. A missed spark or a limiter cut is marked separately. Slow motion replays the actual results of a completed engine cycle.

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

- **Drive with shifts** renders one 15-second take with three forward gears, lift-off and a return to neutral. All export modes respect solo, mute and channel levels.
- **Single sound** captures the bench's RPM, load and throttle as a loop or a faded one-shot.
- **RPM Sound Pack** produces five steady loops from idle to maximum RPM, plus startup, acceleration, deceleration and shutdown.
- **RPM × load grid** renders loops for up to 16 RPM points and 10 load points. Each axis is evenly spaced over the chosen range. With one point, the renderer uses the lower bound.

Every mode supports 44.1 or 48 kHz, 16- or 24-bit PCM, mono or stereo, and optional −1 dBFS peak normalization. A pack or grid uses one gain factor to preserve the relative levels of its clips. Rendering runs in the background and can be cancelled from the status bar.

Each export directory contains the WAV files, `preset.json` and `manifest.json`. The manifest records the engine, seed, PCM format, clip names, RPM, throttle, load and loop bounds. Throttle and load range from 0 to 1. `loop.startFrame` is inclusive and `loop.endFrameExclusive` is exclusive. WAV files also carry a standard `smpl` chunk for loops. In Godot 4, use **Import → Edit → Loop Mode → Detect from WAV**. [Godot audio import guide](https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/importing_audio_samples.html)

| Demo | Contents |
| --- | --- |
| [T-72B3](examples/t-72b3_pack) | Nine diesel V12 clips |
| [T-90M](examples/t-90m_pack) | Nine diesel V12 clips from the five-anchor T-90M bank |
| [Leopard 1](examples/leopard-1_pack) | Supercharged V10 with separate powered and trailing-throttle recordings |
| [Leopard 2](examples/leopard-2_pack) and [Leopard 2 Field](examples/leopard-2-field_pack) | The same turbocharged V12 under power and on lift, at 8 m and 4 m |
| [Marder 1](examples/marder-1_pack) | V6 with three field-recorded RPM layers |
| [Puma](examples/puma_pack) | Compact 10V 890 turbo diesel, three recorded RPM layers |
| [Bradley](examples/bradley_pack) | Cummins V903 turbo diesel, four recorded RPM layers |
| [Hummer H2](examples/hummer-h2_pack) | Naturally aspirated gasoline V8, five recorded RPM layers |
| [UAZ-469](examples/uaz-469_pack) | Carbureted inline-four, five recorded layers from idle to a loaded climb |
| [Renault 2.3 dCi](examples/renault-2-3-dci_pack) | Recorded idle plus separate on-throttle and lift-off layers |
| [BMP-1/2](examples/bmp-1-2_pack) | V6 diesel with recorded idle, loaded passes and mechanical texture from an exposed engine on a stand |
| [BMP-3](examples/bmp-3_pack) | V10 diesel with three recorded RPM layers |
| [Gasoline I4](examples/i4_pack) | Turbocharged 2.0-liter sound with under-hood Golf idle and Audi exhaust under load |
| [Stryker](examples/stryker_pack) | Inline-six diesel with the driving clips cleaned of speech and transmission whine |
| [C1 Ariete](examples/c1-ariete_pack) | V12 diesel with idle and throttle layers from the supplied video |
| [M113](examples/m113_pack) | Two-stroke Detroit Diesel V6, sampled with the tracks stationary |
| [T-72 RPM grid](examples/t-72b3_grid) | Nine mono loops from 1,000 to 2,600 RPM |
| [Listening positions](examples/perspectives) | Nineteen presets at 1,600 RPM from four perspectives |
| [Oshkosh M1070A1 HET](examples/oshkosh-m1070a1-het_pack), [KamAZ-5320](examples/kamaz-5320_pack), [BTR-82A](examples/btr-82a_pack), [BTR-90](examples/btr-90_pack) | Nine engine clips per vehicle |
| [Fleet](examples/library) | 37 other engine loops from the vehicle library |
| [Oshkosh drive](examples/drive) | Three synchronized takes at the same mixer settings |
| [V8 rev limiter](examples/v8-rev-limiter_single) | V8 bouncing between the cut and return thresholds |
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
