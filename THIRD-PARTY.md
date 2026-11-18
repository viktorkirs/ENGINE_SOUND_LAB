# Сторонние компоненты и источники

Переносимый пакет использует динамические библиотеки Qt 6.11.1 (Qt Core, GUI, Widgets и платформенный плагин Windows) из официальной сборки для MinGW-w64. Qt предоставляется под LGPL v3. Тексты лицензий и перечень компонентов Qt находятся в `licenses/Qt`. Пакет использует отдельные DLL и поддерживает их замену на совместимую сборку, включая отладку такой замены. [Исходный код Qt Base 6.11.1](https://download.qt.io/archive/qt/6.11/6.11.1/submodules/qtbase-everywhere-src-6.11.1.zip) · [Qt и LGPL](https://doc.qt.io/qt-6/lgpl.html)

Лицензии среды выполнения GCC 13.1 и winpthreads находятся в `licenses/MinGW`. [Исходный код GCC 13.1](https://gcc.gnu.org/pub/gcc/releases/gcc-13.1.0/) · [Исходный код MinGW-w64](https://sourceforge.net/projects/mingw-w64/files/mingw-w64/)

Перед разработкой изучены поршневая симуляция, поток выхлопа и последующая обработка звука в **Engine Simulator** Ange Yaghi (MIT), а также модель труб и отражений в **enginesound** DasEtwas (MIT). В Engine Sound Lab используются самостоятельно написанные C++-модели импульсов, труб и резонаторов. [Engine Simulator](https://github.com/ange-yaghi/engine-sim) · [enginesound](https://github.com/DasEtwas/enginesound)

Для спектрального сравнения использованы открытые полевые записи. Распространяемые WAV заново синтезированы ядром Engine Sound Lab.

- JoniHeinonen, **WAR-TANK-ENGINE RUNNING–Leopard 2A4 engine running–0001.wav**, CC BY 3.0. [Запись на Freesound](https://freesound.org/people/JoniHeinonen/sounds/161895/)
- adr1911, **Start-up Sound of a U.S. Army M1A1 Tank – Gas Turbine Engine**, CC0. [Запись на Freesound](https://freesound.org/people/adr1911/sounds/542582/)
- overmedium, **v8 engine rev.wav**, CC0. [Запись на Freesound](https://freesound.org/people/overmedium/sounds/651534/)
