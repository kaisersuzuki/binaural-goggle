# Enclosure

3D-printed ASA case for the controller PCB. Currently at **v7** — pressure-fit lid, no lid screws.

## Files

| File | Purpose |
|---|---|
| `stl/enclosure_body_v7.stl` | Main body — 99 × 79 × 39.5 mm, open top, internal PCB standoffs |
| `stl/enclosure_lid_v7.stl` | Press-fit lid — 99 × 79 × 11.5 mm |
| `drawings/insert_drawing_v7.pdf` | Heat-set insert location drawing for JLC3DP order |

## Specs

- Material: **ASA**, black
- Process: **FDM** (JLC3DP)
- Surface finish: **yes** (smoother outer face)
- Threaded inserts: **4× M3 × 4mm OD × 5mm long**, all in PCB standoffs at (±38, ±27)
- Lid: pressure-fit, no inserts, no screws
- Mounting: PCB attaches to body via 4× M3 cap-head screws into the heat-set inserts

## Ordering from JLC3DP

1. Upload `enclosure_body_v7.stl` and `enclosure_lid_v7.stl`
2. For the body only, select:
   - Material: ASA
   - Color: Black
   - Surface finish: Yes
   - Threaded insert: M3 × 4 × 5, **quantity 4**
   - Upload `insert_drawing_v7.pdf` for insert locations
3. For the lid: same material/color/finish, no inserts

Cost: ~$25–35 per set. Lead time ~5–7 business days.

## SCAD source (TODO)

The `scad/` folder is a placeholder. Brad has the working SCAD source locally — drop `enclosure_v7.scad` here when convenient so the parametric source lives in version control alongside the STLs.

## History

- **v1–v6**: iterations during early development (not in repo)
- **v7** (current): pressure-fit redesign, removed all 4 lid bosses, 4 PCB inserts only
