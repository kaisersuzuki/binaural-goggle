// ============================================================
// Binaural Goggle Enclosure — v10.1 PRODUCTION
// ============================================================
// CHANGES vs v10:
//  * Goggle connector: flanged RJ45 panel-mount coupler (was
//    mini-DIN 8). Rectangular cutout + 2x M3 screw holes.
//  * Headphone jack: 3.5mm panel-mount stereo (was Neutrik 1/4"
//    D-series). Simple 6.5mm round hole. Neutrik geometry removed.
//  * NEW: render_test_panel() — a flat coupon carrying every panel
//    cutout at true scale. Print this first (~20 min), push the
//    real parts through it, adjust parameters, THEN print the box.
//
// !! MEASURE BEFORE PRINTING THE BODY !!
// RJ45 flanged couplers are not standardised. The defaults below fit
// the common screw-ear type (flange ~28x16, 1" screw spacing) but
// verify with calipers when yours arrives:
//   rj45_cut_w / rj45_cut_h  = body cutout (add ~0.4 over measured)
//   rj45_screw_spacing       = ear hole centres
//
// PRINT: ABS/ASA, 25-30% infill, 0.2mm layers, no supports.
// Body opening-up; lid top-face-down; coupon flat.
// ============================================================

// ── Shell ────────────────────────────────────────────────────
outer_x   = 135;
outer_y   = 115;
body_z    = 56;
wall      = 4;
floor_t   = 3;
corner_r  = 4;

// ── PCB standoffs (board 84.4 x 62.4, holes at ±38, ±27) ─────
standoff_h   = 8;
standoff_od  = 8;
insert_d     = 4.0;    // pilot for M3x4x5 heat-set insert
insert_depth = 6.6;
standoff_x   = 38;
standoff_y   = 27;

// ── Lid ──────────────────────────────────────────────────────
lid_top_t     = 3;
lip_h         = 7;
lip_t         = 4;
lid_clearance = 0.25;  // per side; raise if too tight on test fit
nub_r         = 1.1;
nub_embed     = 0.55;
divot_r       = 1.35;

// ── Panel cutouts. hole_z from outside floor. Sizes incl. comp.
hole_z = 32;

// FRONT (+Y)
rj45_x             = -30;   // goggle cable: RJ45 flanged coupler
rj45_cut_w         = 17.0;  // cutout width  — VERIFY vs your part
rj45_cut_h         = 15.0;  // cutout height — VERIFY vs your part
rj45_screw_spacing = 25.4;  // ear-hole centres (1" typical) — VERIFY
rj45_screw_d       = 3.4;   // M3
led_x   = 0;     led_d    = 8.2;   // 5mm LED holder bushing
trs_x   = +30;   trs_d    = 6.5;   // 3.5mm stereo panel jack (M6)

// REAR (-Y)
barrel_x = +30;  barrel_d = 8.4;   // right-angle 5.5x2.1, M7 nut
switch_x = -30;  switch_d = 12.6;  // 12mm metal toggle

// ── Vents ────────────────────────────────────────────────────
vent_n = 4;  vent_w = 4;  vent_h = 20;  vent_pitch = 11;  vent_z = 18;

$fn = 48;

// ── Utility ──────────────────────────────────────────────────
module rounded_box(x, y, z, r) {
    hull() {
        translate([ x/2-r,  y/2-r, 0]) cylinder(r=r, h=z);
        translate([-x/2+r,  y/2-r, 0]) cylinder(r=r, h=z);
        translate([ x/2-r, -y/2+r, 0]) cylinder(r=r, h=z);
        translate([-x/2+r, -y/2+r, 0]) cylinder(r=r, h=z);
    }
}

inner_x = outer_x - 2*wall;
inner_y = outer_y - 2*wall;
inner_r = max(corner_r - wall, 1);

module standoff() {
    difference() {
        cylinder(d=standoff_od, h=standoff_h);
        translate([0, 0, standoff_h - insert_depth])
            cylinder(d=insert_d, h=insert_depth + 0.1);
    }
}

// round hole through +Y wall
module panel_hole(x, z, d) {
    translate([x, outer_y/2 - wall/2, z])
        rotate([90,0,0]) cylinder(d=d, h=wall*3, center=true);
}
// round hole through -Y wall
module rear_hole(x, z, d) {
    translate([x, -outer_y/2 + wall/2, z])
        rotate([90,0,0]) cylinder(d=d, h=wall*3, center=true);
}
// rectangular cutout through +Y wall
module panel_rect(x, z, w, h) {
    translate([x, outer_y/2 - wall/2, z])
        cube([w, wall*3, h], center=true);
}

// all front/rear cutouts, reused by body and coupon
module front_cutouts() {
    panel_rect(rj45_x, hole_z, rj45_cut_w, rj45_cut_h);
    panel_hole(rj45_x - rj45_screw_spacing/2, hole_z, rj45_screw_d);
    panel_hole(rj45_x + rj45_screw_spacing/2, hole_z, rj45_screw_d);
    panel_hole(led_x, hole_z, led_d);
    panel_hole(trs_x, hole_z, trs_d);
}
module rear_cutouts() {
    rear_hole(barrel_x, hole_z, barrel_d);
    rear_hole(switch_x, hole_z, switch_d);
}

// ── Body ─────────────────────────────────────────────────────
module render_body() {
    difference() {
        rounded_box(outer_x, outer_y, body_z, corner_r);

        // cavity — straight to the top, no recess
        translate([0, 0, floor_t])
            rounded_box(inner_x, inner_y, body_z, inner_r);

        front_cutouts();
        rear_cutouts();

        // vents through both X walls
        for (side = [-1, 1])
            for (i = [0 : vent_n-1])
                translate([side*(outer_x/2 - wall/2),
                           (i - (vent_n-1)/2) * vent_pitch,
                           vent_z + vent_h/2])
                    cube([wall*3, vent_w, vent_h], center=true);

        // snap divots for lid nubs
        for (p = [[0,  inner_y/2], [0, -inner_y/2],
                  [ inner_x/2, 0], [-inner_x/2, 0]])
            translate([p[0], p[1], body_z - lip_h/2])
                sphere(r=divot_r);
    }

    for (sx = [-standoff_x, standoff_x])
        for (sy = [-standoff_y, standoff_y])
            translate([sx, sy, floor_t]) standoff();
}

// ── Lid ──────────────────────────────────────────────────────
module render_lid() {
    lip_ox = inner_x - 2*lid_clearance;
    lip_oy = inner_y - 2*lid_clearance;

    rounded_box(outer_x, outer_y, lid_top_t, corner_r);

    translate([0, 0, -lip_h])
        difference() {
            rounded_box(lip_ox, lip_oy, lip_h + 0.1, inner_r);
            translate([0, 0, -0.1])
                rounded_box(lip_ox - 2*lip_t, lip_oy - 2*lip_t,
                            lip_h + 0.3, max(inner_r - lip_t, 1));
        }

    for (p = [[0,  lip_oy/2 + nub_embed], [0, -lip_oy/2 - nub_embed],
              [ lip_ox/2 + nub_embed, 0], [-lip_ox/2 - nub_embed, 0]])
        translate([p[0], p[1], -lip_h/2])
            sphere(r=nub_r);
}

// ── Test-fit coupon ──────────────────────────────────────────
// Flat wall-thickness plate with every cutout at true position
// spacing. Print flat (~20 min), push real hardware through,
// tweak parameters, reprint coupon until everything fits, THEN
// print the body. Front-row cutouts on top half, rear on bottom.
module render_test_panel() {
    plate_w = outer_x;          // same width as the box
    plate_h = 40;               // two rows of 20
    rotate([-90, 0, 0])         // lay the "wall" flat for printing
    difference() {
        translate([-plate_w/2, outer_y/2 - wall, hole_z - plate_h/2])
            cube([plate_w, wall, plate_h]);
        front_cutouts();
        // rear cutouts brought onto the same plate, offset down
        translate([0, outer_y - 2*wall + 0.0, 0]) // map -Y wall onto +Y
            mirror([0, 1, 0]) rear_cutouts();
    }
}

// ── Export controls ──────────────────────────────────────────
// body -> STL:        leave render_body() active
// lid  -> STL:        swap comments
// coupon -> STL:      enable render_test_panel() alone
render_body();
// render_lid();
// render_test_panel();
