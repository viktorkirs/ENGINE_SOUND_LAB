# Двигатели техники Battlecity

В каталоге на 18 ноября 2026 года 53 машины ангара и 7 машин кампании. Для всех 60 нашлись игровые профили и ожидаемые файлы мотора. Основные варианты содержат по три записи холостого хода, движения и нагрузки. У фургона Vito три дорожки motor, у гражданского автомобиля несколько вариантов двигателя. Несколько машин делят один профиль.

В Engine Sound Lab назначены пресеты для 58 игровых машин. Две версии военного Hummer H1 пропущены по выбору автора. Гражданский H2, КамАЗ-5320 и некоторые другие двигатели также доступны как самостоятельные пресеты. Нажмите на название двигателя в верхней панели или **Ctrl+K**. В библиотеке работает поиск по модели, индексу ресурса, имени двигателя и английскому игровому названию.

## Откуда взят звук

| Пресет | Материал |
| --- | --- |
| Oshkosh M1070A1 HET | Игровой банк большого дизеля bigdiesel и синтез рядного Caterpillar C18. Акустическое приближение для тяжёлого тягача. |
| KamAZ-5320 | Игровые циклы из записей КамАЗа-740 и КамАЗа-5320. Верхний режим дополнен фрагментами исходного КамАЗа-5320. |
| BTR-82A | Отдельный банк семейства КамАЗ-740 и модель V8. |
| BTR-90 | По описанию машины в проекте выбран 2В-06-2С. Использован соответствующий игровой банк и оппозитная шестёрка. |
| BRDM-2 и M41A1 | Процедурные модели бензиновых V8 и оппозитной шестёрки. В игре у них общий исходный банк БТР-70. |
| Прочий автопарк | Обработанные игровые циклы, родственные записи и процедурные компоненты. Источник каждой модели указан в описании пресета. |

У пресетов из пользовательских записей отдельно описаны выбранные участки, очистка и приблизительные обороты. Акустика кабины и наружных позиций задаётся моделью слушателя. [Разбор исходных записей](recording-calibration.md)

## Сопоставление с файлами игры

Проверка использует действующий game_catalog.gd, ресурсы кампаний и выбор звука в effects_component.gd. Ресурсы старых версий техники за пределами этих списков исключены из подсчёта. Слот btr3u в действующем каталоге открывает БТР-90. Таблица повторяет встроенный справочник приложения.

| Игровое название | Ресурс | Игровой банк мотора | Пресет Engine Sound Lab |
| --- | --- | --- | --- |
| LT-44 Hound | `west_m41a1` | btr70 | M41A1 Walker Bulldog |
| AT-12 Stoat | `west_wiesel_tow` | audi_r5 | Wiesel 1 |
| LT-55 Cavalier | `west_m551` | 6v53_tx | M551 Sheridan |
| LT-67 Volna | `east_pt76` | v6b | PT-76B |
| LT-25 Skat | `east_sprut_sd` | 2v06 | 2S25 Sprut-SD |
| T-57 Kremen | `east_t55` | v55 | T-55A |
| T-74M Granit-M | `east_t72a` | v12_field | T-72A |
| L-17 Pardus I | `west_leopard1a5` | mb838 | Leopard 1 |
| C-92 Courser | `west_leclerc` | hyperbar | Leclerc Serie 2 |
| M-63 Marshall | `west_m60a3` | avds1790 | M60A3 |
| T-83 Burya | `east_t80bvm` | gtd1000 | T-80BVM |
| T-94M Grom-M | `east_t90m` | v12_field | T-90M |
| T-17 Korund | `east_t14` | a85 | T-14 |
| M-81 Barron | `west_m1a2` | agt1500 | M1 Abrams |
| L-27 Pardus II | `west_leopard2a7v` | leopard | Leopard 2 |
| BPT-22 Strazh | `east_bmpt72` | v92s2 | BMPT-72 |
| PTR-2 Zhalo | `east_brdm2_9p133` | btr70 | BRDM-2 |
| BK-17 Bars | `east_tigr` | yamz534 | GAZ Tigr |
| PTR-4 Ship | `east_tigr_kornet` | yamz534 | GAZ Tigr |
| H-41 Badger AT | `west_m1025_javelin` | dd62 | Hummer H1 исключён |
| H-40 Badger GL | `west_hmmwv_mk19` | dd62 | Hummer H1 исключён |
| AS-17 Provost | `west_m1117` | cummins_6b | M1117 Guardian |
| MR-50 Pangolin | `west_matv` | cat_c7 | Oshkosh M-ATV |
| RL-12 Tempest | `west_m270` | vta903_hs400 | M270 |
| RL-36 Feierabend | `west_lars2` | f8l413 | MAN 7t LARS 2 |
| RL-28 Tailgater | `west_hilux` | zmz2424 | RL-28 Tailgater |
| SM-90 Lira | `east_nona_s` | utd20 | 2S9 Nona-S |
| SA-31 Veresk | `east_akatsia` | v2_34 | 2S3M Akatsiya |
| RS-24 Liven | `east_bm21` | zil | Ural-375D |
| TM-64 Motolyga | `east_mtlb` | yamz238 | MT-LB |
| UM-45 Bulochka | `east_uaz452` | umz451 | UAZ-452 |
| V-21 Vento | `west_vito` | van_diesel | Mercedes Vito |
| SP-156 Warden | `west_m109a6` | 8v71t | M109A6 Paladin |
| SP-200 Thunderer | `west_pzh2000` | mt881 | PzH 2000 |
| RD-20 Dozor-M | `east_brdm2` | btr70 | BRDM-2 |
| BX-88 Bulwark | `west_boxer_crv` | 8v199 | Boxer CRV |
| TR-86 Berkut | `east_btr82a` | kamaz740 | BTR-82A |
| TR-92 Rassvet | `east_btr90` | 2v06 | BTR-90 |
| BP-47 Perekat | `east_bumerang` | yamz846 | K-17 Bumerang |
| LV-27 Mako | `west_lav25` | 6v53_mt | M551 Sheridan |
| CR-105 Taurus | `west_centauro` | bf6m1015 | Centauro I |
| MG-11 Halberd | `west_m1128` | cat_c7 | Stryker |
| AR-12 Corsair | `west_amx10rc` | 6f11 | AMX-10 RC |
| BP-11 Kedr | `east_bmp1` | bmp | BMP-1/2 |
| BP-12 Kedr-M | `east_bmp2` | bmp | BMP-1/2 |
| BP-33 Ladoga | `east_bmp3` | bmp3 | BMP-3 |
| BP-45 Rubezh | `east_kurganets25` | utd29 | Kurganets-25 |
| IFV-31 Marten | `west_marder1a3` | mb833 | Marder 1 |
| IFV-33 Ranger | `west_m3a3` | vta903_hmpt | Bradley |
| IFV-41 Cougar | `west_puma` | mt892 | Puma |
| TRM-72 Burlak | `east_brem1` | v84 | BREM-1 |
| BX-90 Hauler | `west_boxer_arv` | 8v199 | Boxer CRV |
| TB-75 Bastion | `east_btrt` | v55 | T-55A |
| Army truck | `army_truck` | kamaz | Ural-4320-31 |
| Ammunition truck | `cargo_truck` | diesel | Cargo truck |
| Workshop car | `civilian_car` | city, compact, twin | Workshop car |
| Dump truck | `dump_truck` | kamaz | KrAZ-250 |
| Oil convoy truck | `oil_convoy_truck` | bigdiesel2 | Fuel tractor OM 457 |
| Recovery Transporter | `recovery_transporter` | bigdiesel | Oshkosh M1070A1 HET |
| Rook missile launcher | `rook_launcher` | bigdiesel | Oshkosh M1070A1 HET |
