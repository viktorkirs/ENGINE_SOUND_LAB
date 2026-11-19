# Сторонние компоненты и источники

Переносимый пакет использует динамические библиотеки Qt 6.11.1 (Qt Core, GUI, Widgets и платформенный плагин Windows) из официальной сборки для MinGW-w64. Qt предоставляется под LGPL v3. Тексты лицензий и перечень компонентов Qt находятся в `licenses/Qt`. Пакет использует отдельные DLL и поддерживает их замену на совместимую сборку, включая отладку такой замены. [Исходный код Qt Base 6.11.1](https://download.qt.io/archive/qt/6.11/6.11.1/submodules/qtbase-everywhere-src-6.11.1.zip) · [Qt и LGPL](https://doc.qt.io/qt-6/lgpl.html)

Лицензии среды выполнения GCC 13.1 и winpthreads находятся в `licenses/MinGW`. [Исходный код GCC 13.1](https://gcc.gnu.org/pub/gcc/releases/gcc-13.1.0/) · [Исходный код MinGW-w64](https://sourceforge.net/projects/mingw-w64/files/mingw-w64/)

Перед разработкой изучены поршневая симуляция, поток выхлопа и последующая обработка звука в **Engine Simulator** Ange Yaghi (MIT), а также модель труб и отражений в **enginesound** DasEtwas (MIT). В Engine Sound Lab используются самостоятельно написанные C++-модели импульсов, труб и резонаторов. [Engine Simulator](https://github.com/ange-yaghi/engine-sim) · [enginesound](https://github.com/DasEtwas/enginesound)

Для слоя **Запись** использована предоставленная для проекта серия WAV Т-72 по оборотам. В Т-90М она совмещена с материалом из `Recording_2026-11-18_22-28-05.mp4` и `T-90m_engine_sound.mp3`. Копии MP3 из материалов игры и из загрузок совпадают. Примеры для обоих пресетов включают этот слой. [Подготовка и измерения](docs/recording-calibration.md)

В **Leopard 1**, **Leopard 2**, **Leopard 2 Field**, **Marder 1**, **Puma**, **Bradley** и **Hummer H2** низкочастотные фрагменты и спектры шумов взяты из предоставленных для проекта видеозаписей YouTube от 18 ноября 2026 года. Для H2 использован ролик `20-34-21` с чёрным внедорожником. Исходные MP4 нужны для повторной подготовки материалов. [Файлы и обработка](docs/recording-calibration.md#восемь-видеозаписей) · [Puma, Bradley и Hummer H2](docs/recording-calibration.md#puma-bradley-и-hummer-h2)

Для пресетов **Renault 2.3 dCi** и **BMP-3** использованы предоставленные для проекта WAV. Для **UAZ-469**, **Stryker**, **C1 Ariete** и **M113** использованы обработанные фрагменты предоставленных видеозаписей от 18 ноября 2026 года. Для **BMP-1/2**, **BMP-2** и **Бензинового I4** использованы пульсации и спектры пяти предоставленных видеозаписей от 19 ноября 2026 года. [Записи версии 1.2](docs/recording-calibration.md#записи-версии-12) · [БМП и бензиновый I4](docs/recording-calibration.md#бмп-12-и-бензиновый-i4)

Для библиотеки Battlecity подготовлены 28 дополнительных банков из предоставленных игровых материалов в `assets/audio/ext`. У КамАЗа-5320 верхний режим также использует фрагменты `materials/sound/Камаз_5320_звук.mp3`. Источники игровых банков описаны в `docs/audio/vehicle_sound_pack_engines.json` и `docs/VEHICLE_SOUND_PACK_RU.md` проекта Battlecity. Трансмиссия в поездке Oshkosh синтезируется процедурно. [Каталог и происхождение пресетов](docs/battlecity-engine-library.md)

Для спектрального сравнения Leopard 2, Abrams и бензинового V8 использованы следующие открытые полевые записи.

- JoniHeinonen, **WAR-TANK-ENGINE RUNNING–Leopard 2A4 engine running–0001.wav**, CC BY 3.0. [Запись на Freesound](https://freesound.org/people/JoniHeinonen/sounds/161895/)
- adr1911, **Start-up Sound of a U.S. Army M1A1 Tank – Gas Turbine Engine**, CC0. [Запись на Freesound](https://freesound.org/people/adr1911/sounds/542582/)
- overmedium, **v8 engine rev.wav**, CC0. [Запись на Freesound](https://freesound.org/people/overmedium/sounds/651534/)
