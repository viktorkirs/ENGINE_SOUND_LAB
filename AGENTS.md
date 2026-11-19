# Engine sound tasks

Use the bundled **EngineSoundLabCLI.exe** to create or update vehicle engine audio for Godot. Run `catalog` to find the exact `vehicles[].id` or `presets[].id`. Run `show-preset` to inspect the RPM range, recorded layers, listener and faults.

Run `export` with `--vehicle ID`, `--preset ID`, or `--preset-file FILE` and an explicit `--output DIR`. Use `pack` for startup, idle, acceleration and shutdown, or `grid` for loops across RPM and load. Read the returned `manifest` path and use its clip roles, RPM, load and loop bounds to integrate the files with the existing game audio code. The tool accepts UTF-8 JSON and returns UTF-8 JSON. [Commands and examples](docs/agent-cli.md)

## Звуки двигателей

Для задачи по звуку техники в Godot используйте **EngineSoundLabCLI.exe**. Команда `catalog` выдаёт идентификаторы машин и пресетов. Команда `show-preset` показывает диапазон оборотов, записанные режимы, позицию слушателя и настройки неисправностей. Команда `export` возвращает путь к манифесту и генерирует WAV. По манифесту привяжите файлы к оборотам и нагрузке в существующей звуковой системе игры. [Справка по экспорту](docs/agent-cli.md)
