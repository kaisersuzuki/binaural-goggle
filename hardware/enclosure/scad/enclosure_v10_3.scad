// ============================================================
// Binaural Goggle Enclosure — v10.3 PRODUCTION
// ============================================================
// CHANGES vs v10.2:
//  * Goggle connector is now a KEYSTONE RJ45 coupler (snap-in, no
//    screws) instead of a D-Type. Standard keystone face is
//    14.5 x 16.0mm; a typical coupler is 14.6 x 16.2 x 32.6mm.
//  * Because a keystone is only 16.2mm tall (vs 31mm for D-Type), the
//    whole connector + patch plug clears the PCB components entirely.
//    The front bay no longer has to be deep -> box reverts to the
//    v10.1 footprint: outer_y 125->115, body_z 62->56, pcb_offset 0.
//  * hole_z 35->36 so the keystone body sits above the tallest parts.
//  * Keystone latches want a ~2.5mm panel; the 4mm wall is relieved
//    to 2.5mm locally on the inside around the opening.
//
// CHANGES vs v10.1:
//  * Sized for the ACTUAL D-Type coupler (28mm deep x 31mm tall) plus
//    the RJ45 patch plug that mates to its inner face. v10.1 left only
//    1.5-1.9mm clearance at three points; this gives 4.5-9mm.
//    outer_y 115->125, body_z 56->62, hole_z 32->35, and the PCB is
//    shifted 6mm rearward (pcb_offset_y) to open up the front bay.
//
// CHANGES vs v10:
//  * Goggle connector: flanged RJ45 panel-mount coupler (was
//    mini-DIN 8). Rectangular cutout + 2x M3 screw holes.
//  * Headphone jack: 3.5mm panel-mount stereo (was Neutrik 1/4"
//    D-series). Simple 6.5mm round hole. Neutrik geometry removed.
//  * NEW: render_test_panel() — a flat coupon carrying every panel
//    cutout at true scale. Print this first (~20 min), push the
//    real parts through it, adjust parameters, THEN print the box.
//
// !! PRINT THE COUPON BEFORE THE BODY !!
// The goggle connector is a D-Type (D-Series) RJ45 coupler. The main
// hole is a Neutrik-standard 24mm — that part is certain. The two M3
// screw-hole centres are NOT published in text form and vary a little
// between third-party makers, so render_test_panel() prints THREE
// candidate spacings side by side. Push the real coupler against each,
// see which pair lines up, then set rj45_screw_spacing to that value
// before printing the body. No calipers required.
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
pcb_offset_y = 0;     // shift PCB rearward to open the front cable bay

// ── Lid ──────────────────────────────────────────────────────
lid_top_t     = 3;
lip_h         = 7;
lip_t         = 4;
lid_clearance = 0.25;  // per side; raise if too tight on test fit
nub_r         = 1.1;
nub_embed     = 0.55;
divot_r       = 1.35;

// ── Panel cutouts. hole_z from outside floor. Sizes incl. comp.
hole_z = 36;

// FRONT (+Y)
// Goggle cable: KEYSTONE RJ45 coupler, snap-in (no screws)
ks_x        = -34;    // centre on front wall
ks_w        = 14.7;   // opening W (std 14.5 + 0.2 print comp)
ks_h        = 16.2;   // opening H (std 16.0 + 0.2 print comp)
ks_relief_d = 1.5;    // inner pocket depth -> leaves 2.5mm at the latch
ks_relief_m = 5;      // pocket margin around the opening
// candidate opening sizes printed on the test coupon [w,h]:
coupon_ks   = [[14.5,16.0],[14.7,16.2],[14.9,16.4]];
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

// keystone opening in the +Y wall, with an inner relief pocket so the
// latch has a ~2.5mm panel to grab instead of the full 4mm wall
module keystone_cutout(x, z, w, h) {
    translate([x, outer_y/2 - wall/2, z])
        cube([w, wall*3, h], center=true);
    translate([x, (outer_y/2 - wall) + ks_relief_d/2, z])
        cube([w + 2*ks_relief_m, ks_relief_d, h + 2*ks_relief_m], center=true);
}

// all front/rear cutouts, reused by body and coupon
module front_cutouts() {
    keystone_cutout(ks_x, hole_z, ks_w, ks_h);
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
            translate([sx, sy + pcb_offset_y, floor_t]) standoff();
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
// Flat plate at true wall thickness, prints lying down (~20 min).
// TOP ROW  : three keystone openings — 14.5x16.0 / 14.7x16.2 /
//            14.9x16.4 — each with the inner latch-relief pocket.
//            Snap the real coupler into each. Use the smallest one it
//            seats into with a firm click and no rattle, then set
//            ks_w / ks_h to that pair before printing the body.
// BOTTOM ROW: LED holder, 3.5mm jack, barrel jack, toggle switch.
coupon_w = 155;
coupon_h = 80;

module render_test_panel() {
    t = wall;
    difference() {
        translate([-coupon_w/2, -coupon_h/2, 0])
            cube([coupon_w, coupon_h, t]);

        // --- top row: keystone opening candidates ---
        for (i = [0 : len(coupon_ks)-1]) {
            w  = coupon_ks[i][0];
            h  = coupon_ks[i][1];
            cx = -50 + i*50;
            cy = 19;
            // through opening
            translate([cx - w/2, cy - h/2, -1]) cube([w, h, t+2]);
            // latch relief pocket, opened from the BACK face of the plate
            translate([cx - (w+2*ks_relief_m)/2,
                       cy - (h+2*ks_relief_m)/2,
                       t - ks_relief_d])
                cube([w + 2*ks_relief_m, h + 2*ks_relief_m,
                      ks_relief_d + 1]);
        }

        // --- bottom row: the other four panel parts ---
        translate([-52, -22, -1]) cylinder(d=led_d,    h=t+2);
        translate([-24, -22, -1]) cylinder(d=trs_d,    h=t+2);
        translate([  4, -22, -1]) cylinder(d=barrel_d, h=t+2);
        translate([ 36, -22, -1]) cylinder(d=switch_d, h=t+2);
    }
}

// ── Export controls ──────────────────────────────────────────
// body -> STL:        leave render_body() active
// lid  -> STL:        swap comments
// coupon -> STL:      enable render_test_panel() alone
render_body();
// render_lid();
// render_test_panel();
