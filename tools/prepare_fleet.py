"""Build the game's engine library and audit the active Battlecity roster."""

import argparse
import copy
import json
from pathlib import Path
import re

import numpy as np

from prepare_recordings import RATE, decode, loop, write_clean_pcm, video_loop


ROOT = Path(__file__).resolve().parents[1]
PRESETS = ROOT / "presets"
RECORDINGS = ROOT / "assets" / "recordings"

# Each row describes the bench model. An @ bank reuses one of our microphone
# recordings; other bank names are the source profiles in Battlecity. Stroke,
# inertia and full-load behaviour are sound-design settings for these variants.
# key | display name | engine | bank | geometry | litres | stroke mm | compression |
# idle | max | kW | Nm | sources as described to the listener | active vehicle IDs
ROWS = """
oshkosh_het|Oshkosh M1070A1 HET|Caterpillar C18|bigdiesel|I6T|18.1|183|16.3|650|2100|522|2570|Модель C18 с записью большого дизеля из игры|recovery_transporter rook_launcher
kamaz_5320|KamAZ-5320|КамАЗ-740|kamaz|V8|10.85|120|17|600|2600|154|637|Записи КамАЗа-740 и КамАЗа-5320|
btr82a|BTR-82A|КамАЗ-740.14-300|kamaz740|V8T|10.85|120|16.5|650|2600|221|1020|Банк КамАЗ-740 из Vehicle Sound Pack|east_btr82a
btr90|BTR-90|2В-06-2С|2v06|F6T|17.0|155|15.5|800|2600|375|1540|Банк семейства 2В-06 из Vehicle Sound Pack|east_btr90
ural4320|Ural-4320-31|ЯМЗ-238М2|yamz238|V8|14.86|140|16.5|650|2100|177|883|Запись семейства ЯМЗ-238, грузовой выпуск|army_truck
kraz250|KrAZ-250|ЯМЗ-238М2|yamz238|V8|14.86|140|16.5|600|2100|177|883|Банк ЯМЗ-238, тяжёлый самосвал|dump_truck
fuel_truck|Fuel tractor OM 457|Mercedes-Benz OM 457 LA|bigdiesel2|I6T|11.97|155|17.5|600|2100|295|1850|Модель тягача с родственным дизельным банком|oil_convoy_truck
cargo|Cargo truck|Грузовой турбодизель|diesel|I6T|8.3|135|17.0|680|2400|206|1080|Грузовой дизель из материалов игры|cargo_truck
t55|T-55A|В-55|v55|V12|38.88|180|15.0|650|2000|426|2250|Банк В-55 из комплекта игры|east_t55 east_btrt
t72a|T-72A|В-46|@t72|V12S|38.88|180|14.0|650|2000|574|3050|Родственная запись Т-72 по оборотам|east_t72a
bmpt72|BMPT-72|В-92С2|v92s2|V12T|38.88|180|14.0|750|2000|736|3920|Банк В-92С2 из комплекта игры|east_bmpt72
brem1|BREM-1|В-84|v84|V12S|38.88|180|14.0|750|2000|618|3310|Банк В-84 из комплекта игры|east_brem1
pt76|PT-76B|В-6Б|v6b|I6|19.1|180|15|600|1800|177|1100|Рядный дизель из банка В-6|east_pt76
sprut|2S25 Sprut-SD|2В-06-2С|2v06|F6T|17.0|155|15.5|800|2600|375|1540|Банк семейства 2В-06, гусеничный корпус|east_sprut_sd
akatsia|2S3M Akatsiya|В-59|v2_34|V12S|38.88|180|15|650|2000|382|2150|Модель В-59 по родственному банку В-2|east_akatsia
nona|2S9 Nona-S|5Д20|@bmp12|V6|15.9|150|15.8|750|2400|177|830|Модель 5Д20 по родственной записи УТД-20|east_nona_s
bumerang|K-17 Bumerang|Тяжёлый V8|yamz846|V8T|17.2|140|16|650|2100|552|2780|Игровая версия на банке ЯМЗ-846|east_bumerang
kurganets|Kurganets-25|Дизель 800 л. с.|@bmp3|V10T|26.5|150|15.8|750|2600|588|2480|Модель по родственной записи УТД-29|east_kurganets25
mtlb|MT-LB|ЯМЗ-238В|yamz238|V8|14.86|140|16.5|650|2100|177|883|Банк ЯМЗ-238, резонанс гусеничного корпуса|east_mtlb
tigr|GAZ Tigr|ЯМЗ-534|yamz534|I4T|4.43|128|17.5|750|2300|158|735|Банк ЯМЗ-534 из комплекта игры|east_tigr east_tigr_kornet
uaz452|UAZ-452|УМЗ-451|@uaz|I4G|2.445|92|6.7|650|4000|55|167|Запись родственной четвёрки УАЗ-469, закрытый кузов|east_uaz452
brdm2|BRDM-2|ГАЗ-41|synth|V8G|5.53|88|6.7|600|3400|103|350|Синтез бензинового ГАЗ V8|east_brdm2 east_brdm2_9p133
ural375|Ural-375D|ЗИЛ-375|zil|V8G|7.0|95|6.5|600|3200|132|465|Бензиновый V8 по игровому банку ЗИЛ|east_bm21
t14|T-14|А-85-3А|a85|V12T|34.6|160|16|800|2400|1103|4900|Банк X12 А-85, схема стенда приближена двумя рядами|east_t14
m41|M41A1 Walker Bulldog|Continental AOS-895|synth|F6GS|14.67|130|6.5|700|2800|373|1420|Синтез бензиновой оппозитной шестёрки с нагнетателем|west_m41a1
sheridan|M551 Sheridan|Detroit Diesel 6V53T|@m113|V6DT|5.21|114.3|17|650|2800|224|820|Стояночная запись 6V53 и модель турбонаддува|west_m551 west_lav25
leclerc|Leclerc Serie 2|V8X-1500 Hyperbar|hyperbar|V8T|16.5|142|15|900|2500|1103|4850|Банк дизеля Hyperbar из комплекта игры|west_leclerc
m60|M60A3|Continental AVDS-1790|avds1790|V12T|29.36|146|16.0|650|2400|559|2300|Банк AVDS-1790 из комплекта игры|west_m60a3
boxer|Boxer CRV|MTU 8V199 TE20|8v199|V8T|15.9|150|16.5|700|2700|530|2400|Банк MTU 8V199, также для Boxer ARV|west_boxer_crv west_boxer_arv
matv|Oshkosh M-ATV|Caterpillar C7|@stryker|I6T|7.2|127|16.5|750|2450|276|1250|Семейство C7 из пресета Stryker, колёсный кузов|west_matv
centauro|Centauro I|IVECO V6|bf6m1015|V6T|20.0|142|16|700|2300|382|1850|Модель турбо-V6 по родственному банку Deutz|west_centauro
m1117|M1117 Guardian|Cummins 6CTA|cummins_6b|I6T|8.3|135|17.3|700|2500|194|900|Модель 6CTA по банку Cummins B|west_m1117
wiesel|Wiesel 1|Audi 2.0 TD|audi_r5|I5T|1.99|86.4|21|850|4500|64|172|Банк пятицилиндрового Audi|west_wiesel_tow
amx10|AMX-10 RC|Baudouin 6F11 SRX|6f11|V6T|11.2|140|16|900|3000|206|840|Банк Baudouin 6F11 из комплекта игры|west_amx10rc
m109|M109A6 Paladin|Detroit Diesel 8V71T|8v71t|V8DT|9.32|127|17|600|2300|328|1350|Банк двухтактного 8V71T из комплекта игры|west_m109a6
m270|M270|Cummins VTA-903|@bradley|V8T|14.8|121|15.5|780|2600|373|1700|Очищенная запись родственного VTA-903 из Bradley|west_m270
pzh2000|PzH 2000|MTU MT 881 Ka-500|mt881|V8T|18.3|139|16|800|3000|735|3000|Банк MTU 881 из комплекта игры|west_pzh2000
lars|MAN 7t LARS 2|Deutz F8L 413|f8l413|V8|12.76|125|17|700|2500|235|1130|Банк воздушного дизеля Deutz из комплекта игры|west_lars2
hilux|RL-28 Tailgater|ЗМЗ-2424 V8|zmz2424|V8G|5.53|88|8.5|700|4000|145|420|Игровая версия пикапа с бензиновым V8|west_hilux
vito|Mercedes Vito|Дизель фургона|@dci23|I4T|2.15|88.3|16.2|825|4200|120|360|Игровая версия по записи 2.3 dCi из банка фургона|west_vito
civil_car|Workshop car|Легковой I4|compact|I4G|1.6|80|10|800|5900|70|148|Один из гражданских двигателей игры|civilian_car
""".strip().splitlines()

EXISTING = {
    "east_bmp1": "BMP-1/2", "east_bmp2": "BMP-1/2", "east_bmp3": "BMP-3",
    "east_t80bvm": "T-80BVM", "east_t90m": "T-90M", "west_leopard1a5": "Leopard 1",
    "west_leopard2a7v": "Leopard 2", "west_m1a2": "M1 Abrams", "west_m3a3": "Bradley",
    "west_marder1a3": "Marder 1", "west_puma": "Puma", "west_m1128": "Stryker",
}
EXCLUDED = {"west_hmmwv_mk19", "west_m1025_javelin"}


def save_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


# Take number and start in seconds for cruise sections with a steady upper band.
CRUISE_TAKES = {"bigdiesel2": (3, 4.20), "diesel": (2, 1.20), "kamaz": (3, 4.20)}


def make_bank(game, key, label, idle, maximum, reports, ffmpeg):
    report = reports.get(key, {})
    idle = report.get("idle_rpm", idle)
    cruise = report.get("cruise_rpm", round(maximum * .6 / 10) * 10)
    inputs = game / "assets/audio/ext"
    layers = [(idle, "idle", .085), (cruise, "run", .140)]
    if (inputs / f"sfx_engine_{key}_high_1.ogg").exists():
        layers.append((round(cruise * 1.6), "high", .180))
    folder = RECORDINGS / ("fleet_" + key)
    folder.mkdir(parents=True, exist_ok=True)
    for rpm, role, level in layers:
        take_number, start = CRUISE_TAKES.get(key, (1, None)) if role == "run" else (1, None)
        source = decode(inputs / f"sfx_engine_{key}_{role}_{take_number}.ogg", ffmpeg)
        seconds = min(4.2, len(source) / RATE - .70)
        if seconds < 1.0:
            raise ValueError(f"Short {key} {role} recording")
        if start is None:
            start = (len(source) / RATE - seconds) * .45
        take = loop(source, start, seconds, steady_air=True)
        take *= min(level / np.sqrt(np.mean(take**2)), .91 / np.max(np.abs(take)))
        write_clean_pcm(folder / f"{rpm}.pcm", take)
    if key == "kamaz":
        # The 5320 roll-by and the 740 idle are the original sources of this
        # game's two lower layers. The separate upper layer comes from the pulls.
        source = decode(game / "materials/sound/Камаз_5320_звук.mp3", ffmpeg)
        cues = (18.43, 20.15, 26.52, 63.25, 73.62, 75.11)
        data = video_loop([source], [cues], 1950, 5320, .175, 130, 113, smooth_air=True)
        write_clean_pcm(folder / "1950.pcm", data)
        layers.append((1950, "pull", .175))
    return {"key": "fleet_" + key, "label": label, "directory": folder.name, "rpm": [row[0] for row in layers]}


def make_preset(row, templates):
    key, name, engine, bank, geometry, liters, stroke, compression, idle, maximum, kw, torque, source, ids = row
    liters, stroke, compression, kw, torque = map(float, (liters, stroke, compression, kw, torque))
    idle, maximum = int(idle), int(maximum)
    shape, count, options = re.fullmatch(r"([IVF])(\d+)([A-Z]*)", geometry).groups()
    count = int(count)
    gasoline = "G" in options
    clean = bank == "synth"
    p = copy.deepcopy(templates["synth" if clean else "gasoline" if gasoline else "diesel"])
    p.update(name=name, description=engine + " · " + source)
    model = p["model"]
    boost = "turbocharger" if "T" in options else "supercharger" if "S" in options else "none"
    model.update(kind="gasoline" if gasoline else "diesel", layout={"I": "inline", "V": "v", "F": "boxer"}[shape],
                 cylinders=count, strokes=2 if "D" in options else 4, liters=liters, strokeMm=stroke,
                 compressionRatio=compression, idleRpm=idle, maxRpm=maximum,
                 bankAngle=180 if shape == "F" else 60 if count == 12 else 90,
                 firingOrder=[], powerKw=kw, torqueNm=torque, boost=boost, exhaustTracts=2 if shape != "I" else 1,
                 inertia=round(.47 * liters**.86 + .24, 2), overrunDrag=.43 if liters > 10 else .51,
                 seed=int.from_bytes(key.encode("utf-8")[-4:], "little"),
                 recording="none" if clean else bank.removeprefix("@") if bank.startswith("@") else "fleet_" + bank)
    layers = p["layers"]
    layers["mechanical"]["shape"] = 0
    if boost == "none":
        layers["turbo"]["gain"] = 0
    if not clean:
        layers["exhaust"]["resonance"] = round(155 / (liters / count)**.28)
        layers["body"]["resonance"] = round(105 / (liters / count)**.24)
        layers["recording"]["gain"] = 1.10
    p.pop("revLimiter", None)
    p["behavior"].update(holdRpm=round((idle + .45 * (maximum-idle))/10)*10, program="autoTest")
    truck = key in {"oshkosh_het", "kamaz_5320", "ural4320", "kraz250", "fuel_truck", "cargo", "ural375"}
    p["listener"] = {"position": "thirdPerson", "distanceMeters": 8, "insulation": .54 if truck else .70}
    if key in {"oshkosh_het", "kraz250"}:
        model["inertia"] = round(model["inertia"] * 1.22, 2)
    if key in {"brdm2", "m41"}:
        layers["exhaust"]["gain"] = .90 if key == "brdm2" else .76
        layers["exhaust"]["resonance"] = 130 if key == "brdm2" else 91
        layers["body"]["resonance"] = 83 if key == "brdm2" else 70
    if key == "uaz452":
        model["firingOrder"] = [1, 2, 4, 3]
    return p


def game_vehicles(game):
    def gd(path):
        return re.sub(r"#[^\n]*", "", (game / path).read_text(encoding="utf-8"))
    entries = re.findall(r'^\s*"([^"]+)"\s*:\s*"res://(data/tanks/[^"]+\.tres)"',
                         gd("scripts/autoload/game/game_catalog.gd"), re.M)
    effects = gd("scripts/tank/components/effects_component.gd")
    table = effects.split("const ENGINES := {", 1)[1].split("\n}", 1)[0]
    sounds = dict(re.findall(r'"([^"]+)"\s*:\s*\[\s*"(engine_[^"]+)"', table))
    campaigns = [(p.stem, p.relative_to(game).as_posix()) for p in sorted((game/"data/campaigns").glob("*.tres"))
                 if 'script_class="TankData"' in p.read_text(encoding="utf-8")[:500]]
    for key, path in entries + campaigns:
        vehicle = (game/path).read_text(encoding="utf-8")
        name = json.loads(re.search(r'^tank_name\s*=\s*("(?:\\.|[^"\\])*")', vehicle, re.M)[1])
        stem = Path(path).stem
        profiles = [sounds[stem]] if stem in sounds else []
        if stem == "civilian_car":
            profiles = sorted(set(re.findall(r'"(engine_[^"]+)"', effects.split("const CIVILIAN_ENGINES :=", 1)[1].split("\n", 1)[0])))
        missing = []
        for profile in profiles:
            parts = ["motor"] if profile == "engine_van_diesel" else ["idle", "run", "load"]
            for role in parts:
                for take in (1, 2, 3):
                    file = f"sfx_{profile}_{role}_{take}.ogg"
                    if not (game/"assets/audio/ext"/file).is_file():
                        missing.append(file)
        yield {"id": stem, "slot": key, "name": name, "gameBank": ", ".join(profiles),
               "filesPresent": bool(profiles) and not missing}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    args = parser.parse_args()
    game = args.game.resolve()
    report = json.loads((game/"docs/audio/vehicle_sound_pack_engines.json").read_text(encoding="utf-8"))
    templates = {key: json.loads((PRESETS / filename).read_text(encoding="utf-8")) for key, filename in (
        ("diesel", "04g_stryker.json"), ("gasoline", "11_uaz.json"), ("synth", "08_gasoline_v8.json"))}
    bank_file = RECORDINGS / "banks.json"
    base_banks = [row for row in json.loads(bank_file.read_text(encoding="utf-8")) if not row["key"].startswith("fleet_")]
    fleet_banks, mapping = {}, dict(EXISTING)
    for line in ROWS:
        row = line.split("|")
        key, name, engine, bank = row[:4]
        if bank != "synth" and not bank.startswith("@") and bank not in fleet_banks:
            fleet_banks[bank] = make_bank(game, bank, engine, int(row[8]), int(row[9]), report, args.ffmpeg)
        preset = make_preset(row, templates)
        save_json(PRESETS / f"fleet_{key}.json", preset)
        if key == "oshkosh_het":
            ride = copy.deepcopy(preset)
            ride.update(name="Oshkosh M1070A1 Drive",
                        description="Поездка на тяжёлом тягаче. Нажмите «Запустить» в строке сценария. Канал «Трансмиссия» можно послушать отдельно кнопкой S.")
            ride["layers"]["transmission"] = dict(gain=.66, resonance=530, cutoff=3700, shape=.65, amount=.48,
                                                  damping=.58, drive=.05, mute=False, solo=False)
            ride["behavior"].update(gear=1, program="drive")
            save_json(PRESETS / "12_oshkosh_drive.json", ride)
        mapping.update(dict.fromkeys(row[-1].split(), name))
    entries = base_banks + list(fleet_banks.values())
    bank_file.write_text("[\n" + ",\n".join("  " + json.dumps(b, ensure_ascii=False) for b in entries) + "\n]\n", encoding="utf-8")
    vehicles = list(game_vehicles(game))
    for v in vehicles:
        v["preset"] = None if v["id"] in EXCLUDED else mapping[v["id"]]
    save_json(ROOT/"assets"/"fleet.json", vehicles)
    print(f"{len(ROWS)} presets, {len(fleet_banks)} new recording banks, {len(vehicles)} active game vehicles")


if __name__ == "__main__":
    main()
