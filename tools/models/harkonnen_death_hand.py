"""Death Hand rocket (the Harkonnen palace missile in flight) - the ORIGINAL design rebuilt as a modern hard-surface
model (kit: atreides_vehicle_kit.py).

    blender -b --factory-startup --python tools/models/harkonnen_death_hand.py -- [--out DIR] [--no-bake] ...
    D2K_UNIT=harkonnen_death_hand FACINGS=0,16 ... tools/models/vehicle_check/run.sh harkonnen_death_hand TAG

Templates.bin DEATH HAND ROCKET: owner mask 2 (Harkonnen), unit art 21 = DATA.R16 frames 2382-2383: TWO frames only
(directions 0-7 and 24-31 -> frame 0 "nose up the screen", 8-23 -> frame 1 "nose down"), no barrel art, no ground
shadow; model key DEATH_HAND_ROCKET. The pack renders facings 0 and 16 only (FACINGS=0,16).

How the original reads: the rocket is drawn at its full length (48 px, no foreshortening) - the model is rendered from
straight above (RENDER elev = 90), lying along u:
  * a slim grey steel body 6 px wide with a dark side, a short nose cone with a house-colour tip;
  * two thin swept-back wings from the shoulders (18 px span);
  * the tail flares to 14 px and carries three engine pods; the middle one burns (white-orange flame, part of the
    original frames).
House colour 2 % of the sprite (the tip and a few marker lights).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atreides_vehicle_kit as K  # noqa: E402

RENDER = dict(house_grey=0.3, sun_elevation=58.0, shadow_azimuth=30.0, contour=0.5, contour_azimuth=-45.0, sharpen=0.6, elev=90.0,
              tone=((0.0, 0.0), (0.12, 0.12), (0.25, 0.26), (0.5, 0.52), (0.75, 0.79), (1.0, 1.0)))
KEY = "DEATH_HAND_ROCKET"
K.PREVIEW_HOUSE = (0.78, 0.22, 0.20)
K.ELEV = 90.0

P = dict(
    v0=-1.2,                        # the frames sit a little left of the hotspot
    zc=8.0,
    body=dict(r=3.0, u=(-4.0, 19.5)),
    nose=dict(u=24.0, r=1.2),
    wing=dict(root=(13.5, 20.0), tip=(10.0, 12.2), v=9.0, t=0.7),
    tail=dict(u=-11.5, r=6.6),
    pod=dict(v=5.6, u=(-22.5, -11.0), r=1.9),
    flame=dict(u=(-22.5, -12.0), r=2.7),
)
COL = dict(dh_steel=(0.680, 0.670, 0.670), dh_dk=(0.420, 0.410, 0.410), dh_lt=(0.860, 0.850, 0.840), flame_w=(1.000, 0.960, 0.860),
           flame_o=(1.000, 0.520, 0.120))
SPEC = dict(dh_steel=(0.0, 0.45, "paint"), dh_dk=(0.0, 0.5, "paint"), dh_lt=(0.0, 0.4, "paint"), flame_w=(0.0, 0.3, "dark"),
            flame_o=(0.0, 0.3, "dark"))
FILM = dict(dh_steel=(0.600, 0.580, 0.570), dh_dk=(0.400, 0.390, 0.380), dh_lt=(0.740, 0.720, 0.700))
K.palette(COL, SPEC, FILM, uv_scale=dict(flame_w=0.2, flame_o=0.2))


def ring(u, r, zc, segs=18, v=0.0):
    return [(v + r * math.cos(2 * math.pi * i / segs), u, zc + r * math.sin(2 * math.pi * i / segs)) for i in range(segs)]


def build():
    root = K.root(KEY)
    parts = []
    bd, ns, wg, tl, pd, fl = (P[k] for k in ("body", "nose", "wing", "tail", "pod", "flame"))
    zc = P["zc"]
    b0, b1 = bd["u"]
    r = bd["r"]
    # body: flared tail, straight tube, nose cone
    parts.append(K.loft("body", [ring(tl["u"], tl["r"], zc), ring(b0, r + 0.6, zc), ring(b0 + 5.0, r + 0.3, zc), ring(b1, r, zc),
                                 ring(b1 + 2.6, r * 0.62, zc), ring(ns["u"] - 1.2, ns["r"], zc)], "dh_steel"))
    parts.append(K.house(K.loft("tip", [ring(ns["u"] - 1.4, ns["r"] + 0.25, zc, 12), ring(ns["u"], 0.35, zc, 12)], "HOUSE"), 0.6, 0.5))
    for uu in (b0 + 5.0, 3.0, 11.0, b1 - 0.4):          # collars along the tube
        parts.append(K.loft("collar", [ring(uu - 0.3, r + 0.22, zc), ring(uu + 0.3, r + 0.22, zc)], "dh_dk"))
    parts.append(K.box("spine", -0.45, 0.45, b0 + 1.0, b1 - 1.0, zc + r - 0.1, zc + r + 0.5, "dh_lt", 0.15))
    for uu in (-2.0, 7.0, 15.0):                         # marker lights
        parts.append(K.house(K.box("marker", r - 0.2, r + 0.35, uu - 0.5, uu + 0.5, zc - 0.5, zc + 0.5, "HOUSE"), 0.7, 0.6))
    # swept wings from the shoulders, tail fins
    w0, w1 = wg["root"]
    t0, t1 = wg["tip"]
    parts.append(K.plan_prism("wing", [(r - 0.6, w0), (wg["v"], t0), (wg["v"], t1), (r - 0.6, w1)], zc - wg["t"] / 2, zc + wg["t"] / 2, "dh_steel", 0.15))
    parts.append(K.plan_prism("wing_edge", [(r + 0.5, w1 - 1.0), (wg["v"] - 0.2, t1 - 0.3), (wg["v"], t1), (r - 0.6, w1)], zc - wg["t"] / 2 - 0.05,
                              zc + wg["t"] / 2 + 0.15, "dh_lt", 0.05))
    # engine pods
    p0, p1 = pd["u"]
    parts.append(K.loft("pod", [ring(p0, pd["r"] * 0.8, zc, 14, pd["v"]), ring(p0 + 2.0, pd["r"], zc, 14, pd["v"]), ring(p1 - 2.0, pd["r"], zc, 14, pd["v"]),
                                ring(p1 + 1.5, pd["r"] * 0.5, zc, 14, pd["v"])], "dh_steel"))
    parts.append(K.cyl("pod_bore", pd["r"] * 0.6, p0 - 0.03, p0 + 0.3, pd["v"], zc, "recess", segs=12))
    parts.append(K.plan_prism("fin", [(tl["r"] - 1.0, tl["u"] + 3.0), (pd["v"] + pd["r"] + 1.0, p0 + 4.0), (pd["v"] + pd["r"] + 1.0, p0 + 1.0), (pd["v"], p0 + 3.0)],
                              zc - 0.35, zc + 0.35, "dh_dk", 0.1))
    K.mirror_all(parts)
    # the burning middle engine
    f0, f1 = fl["u"]
    parts.append(K.loft("nozzle", [ring(tl["u"] + 0.6, 2.6, zc, 14), ring(tl["u"] - 1.6, 3.0, zc, 14)], "dh_dk"))
    parts.append(K.loft("flame_o", [ring(f1 + 0.6, fl["r"], zc, 12), ring((f0 + f1) / 2, fl["r"] * 0.85, zc, 12), ring(f0, 0.3, zc, 12)], "flame_o"))
    parts.append(K.loft("flame_w", [ring(f1 + 0.5, fl["r"] * 0.8, zc + 1.2, 12), ring(f1 - 3.2, fl["r"] * 0.7, zc + 1.2, 12), ring(f1 - 5.5, 0.2, zc + 1.2, 12)], "flame_w"))
    body = K.node(parts, "body", (0, 0, 0), root, (P["v0"], 0, 0))
    K.anchor("fx_exhaust_0", (P["v0"], tl["u"] - 1.6, zc), body)
    return root


if __name__ == "__main__":
    K.main(KEY, "harkonnen_death_hand", build, P)
