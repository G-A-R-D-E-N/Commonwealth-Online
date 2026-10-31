import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(name):
    return (ROOT / name).read_text(encoding="utf-8")


EXPECTED_CENTERS = {
    1: (-76967.41, 67777.91, 7529.0),
    2: (46842.82, -4646.61, 575.99),
    3: (17321.81, 13151.21, 1191.91),
    4: (69546.16, 88423.57, 1795.57),
    5: (-6392.31, 46395.61, 1105.74),
    6: (38521.23, 31611.44, 898.89),
    7: (90857.59, 37769.87, 992.44),
    8: (-30583.13, -64083.1, 1463.82),
    9: (55392.0, 47605.3, 731.47),
    10: (-47565.0, 16621.47, 2671.99),
    11: (29790.29, 66834.8, 2156.95),
    12: (-20706.63, -18532.13, 715.8),
    13: (8712.39, -65830.34, 532.82),
    14: (90560.85, 60331.67, 1246.82),
    15: (5143.5, -95302.77, 624.72),
    16: (64136.28, 3531.74, 669.35),
    17: (-45666.39, -4482.37, 1583.14),
    18: (-10832.56, 100275.74, 3856.0),
    19: (-70384.33, 79519.95, 7360.03),
    20: (-79047.53, 89586.51, 7846.0),
    21: (50747.33, 70409.78, 1392.68),
    22: (-35732.77, -91515.23, 1831.88),
    23: (76494.48, -64030.71, 1464.63),
    24: (-40650.17, 60418.43, 3906.36),
    25: (-85924.73, 38877.23, 6650.6),
    26: (6169.99, 46319.42, 499.52),
    27: (-22190.01, 89346.05, 6139.54),
    28: (49563.08, -46795.05, 831.99),
    29: (56640.95, -76524.32, 519.22),
    30: (4067.42, 61930.87, 602.6),
    31: (55640.21, 42610.6, 1061.82),
    32: (-21793.06, 4657.16, 782.0),
    33: (24161.47, 44910.05, 1560.15),
    34: (21882.12, 31539.97, 949.63),
    35: (1666.9, 8097.7, 96.0),
    36: (-2600.0, 1920.0, -128.0),
}

EXPECTED_SPACES = {
    1: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    2: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    3: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    4: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    5: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    6: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    7: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    8: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    9: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    10: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    11: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    12: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    13: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    14: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    15: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    16: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    17: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    18: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    19: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    20: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    21: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    22: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    23: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    24: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    25: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    26: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    27: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    28: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    29: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    30: ("DLCCoast.esm", 2831, (-30000.0, 70000.0, -10000.0, 75000.0, -5000.0, 12000.0)),
    31: ("DLCCoast.esm", 2831, (-30000.0, 70000.0, -10000.0, 75000.0, -5000.0, 12000.0)),
    32: ("DLCCoast.esm", 2831, (-30000.0, 70000.0, -10000.0, 75000.0, -5000.0, 12000.0)),
    33: ("DLCCoast.esm", 2831, (-30000.0, 70000.0, -10000.0, 75000.0, -5000.0, 12000.0)),
    34: ("DLCNukaWorld.esm", 10511, (-10000.0, 35000.0, -10000.0, 45000.0, -5000.0, 12000.0)),
    35: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
    36: ("Fallout4.esm", 60, (-110000.0, 110000.0, -120000.0, 120000.0, -20000.0, 20000.0)),
}


def check_inside(label, point, bounds):
    assert bounds[0] <= point[0] <= bounds[1], label
    assert bounds[2] <= point[1] <= bounds[3], label
    assert bounds[4] <= point[2] <= bounds[5], label


sample = json.loads(read("CO-SERVER/server-cpp/workshop-zones.sample.json"))
assert sample["version"] == 1
zones = sample["zones"]
assert len(zones) == len(EXPECTED_CENTERS) == 36

ids = [zone["buildZoneId"] for zone in zones]
assert len(set(ids)) == len(ids), "duplicate buildZoneId"
assert set(ids) == set(EXPECTED_CENTERS), "zone id set changed"

required = {"buildZoneId", "space", "centerX", "centerY", "centerZ",
            "radius", "workbench", "captureRadius", "maxScale", "maxObjects"}
LABELS = {
    12: "attributed by marker chain (REFR 1F0711, LCTN 1B8C53, marker 16CA1D)",
    22: "inferred (Farm05 REFR 1E81EA by elimination)",
}
EXPECTED_RADII = {
    1: (6946.84, 6946.84),
    2: (3664.17, 3664.17),
    3: (4624.7, 4624.7),
    4: (4472.03, 4472.03),
    5: (3197.94, 3197.94),
    6: (4790.36, 4790.36),
    7: (3866.84, 3866.84),
    8: (4713.99, 4713.99),
    9: (4421.9, 4421.9),
    10: (4013.67, 4013.67),
    11: (4519.43, 4519.43),
    12: (2703.63, 2703.63),
    13: (3021.9, 3021.9),
    14: (4687.26, 4687.26),
    15: (3414.19, 3414.19),
    16: (5090.38, 5090.38),
    17: (3015.88, 3015.88),
    18: (3536.2, 3536.2),
    19: (5288.77, 5288.77),
    20: (9179.26, 9179.26),
    21: (5442.82, 5442.82),
    22: (4735.06, 4735.06),
    23: (14253.3, 14253.3),
    24: (6949.04, 6949.04),
    25: (4878.29, 4878.29),
    26: (4148.75, 4148.75),
    27: (3464.17, 3464.17),
    28: (6434.21, 6434.21),
    29: (5812.6, 5812.6),
    30: (7047.06, 7047.06),
    31: (9164.74, 9164.74),
    32: (5678.65, 5678.65),
    33: (8309.92, 8309.92),
    34: (7636.53, 7636.53),
    35: (23655.05, 23655.05),
    36: (3798.65, 3798.65),
}
for zone in zones:
    assert required <= set(zone), zone["buildZoneId"]
    assert zone["name"] and zone["provenance"], zone["buildZoneId"]
    zone_id = zone["buildZoneId"]
    plugin, local_form_id, bounds = EXPECTED_SPACES[zone_id]
    assert zone["space"]["plugin"] == plugin, zone_id
    assert zone["space"]["localFormId"] == local_form_id, zone_id
    for field in ("centerX", "centerY", "centerZ", "radius", "captureRadius", "maxScale"):
        assert math.isfinite(zone[field]), (zone_id, field)
    assert zone["radius"] > 0, zone_id
    assert zone["radius"] < 60000, zone_id
    assert zone["captureRadius"] >= 0, zone_id
    assert zone["maxScale"] > 0 and zone["maxObjects"] > 0, zone_id
    expected = EXPECTED_CENTERS[zone_id]
    actual = (zone["centerX"], zone["centerY"], zone["centerZ"])
    assert all(math.isclose(a, e, abs_tol=0.005) for a, e in zip(actual, expected)), zone_id
    check_inside(("center", zone_id), actual, bounds)
    bench = zone["workbench"]
    bench_point = (bench["x"], bench["y"], bench["z"])
    assert all(math.isfinite(v) for v in bench_point), zone_id
    check_inside(("workbench", zone_id), bench_point, bounds)
    expected_radii = EXPECTED_RADII[zone_id]
    actual_radii = (zone["radius"], zone["captureRadius"])
    assert all(math.isclose(a, e, abs_tol=0.005) for a, e in zip(actual_radii, expected_radii)), zone_id
    assert all(math.isclose(b, c, abs_tol=0.005) for b, c in zip(bench_point, actual)), zone_id

names = {zone["buildZoneId"]: zone["name"] for zone in zones}
provenance = {zone["buildZoneId"]: zone["provenance"] for zone in zones}
assert names[12] == "HangmanAlley" and names[22] == "SomervillePlace"
assert {zone_id: provenance[zone_id] for zone_id in LABELS} == LABELS
