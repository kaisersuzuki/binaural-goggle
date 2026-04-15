// ============================================================
// Binaural Goggle Controller — Enclosure Rev 3
// Brad Beidler · 2026
// ============================================================
// FIX: cavity subtraction PRESERVES standoff and boss positions
// by subtracting (cavity - standoffs) rather than cavity alone.
// Single monolithic difference — valid manifold, no multi-shell.
//
// PCB STANDOFFS:  (±38, ±27)  h=10mm  PCB drops straight in
// LID BOSSES:     (±43, ±32)  h=30mm  fused to wall corners
// ============================================================

$fn = 48;
RENDER_PART = "body";  // "body" | "lid" | "both"

// ── Core dimensions ──────────────────────────────────────────
INT_W = 92;  INT_L = 72;  INT_H = 30;
WALL  = 3.5;
EXT_W = INT_W + WALL*2;
EXT_L = INT_L + WALL*2;
EXT_H = INT_H + WALL;
OUTER_R = 6.0;  INNER_R = OUTER_R - WALL;

// ── Lid ──────────────────────────────────────────────────────
LID_T = 10.0;  LID_LIP_D = 2.0;  LID_LIP_W = 2.5;  LID_GAP = 0.25;

// ── Inserts ──────────────────────────────────────────────────
INS_D = 3.6;  INS_DEPTH = 7.0;

// ── PCB standoffs ─────────────────────────────────────────────
PCB_OD = 10.0;  PCB_H = 10.0;
PCB_BX = 38.0;  PCB_BY = 27.0;
PCB_BOSSES=[[-PCB_BX,-PCB_BY],[PCB_BX,-PCB_BY],[-PCB_BX,PCB_BY],[PCB_BX,PCB_BY]];

// ── Lid bosses ────────────────────────────────────────────────
LID_OD = 8.0;   LID_BX = 43.0;  LID_BY = 32.0;
LID_BOSSES=[[-LID_BX,-LID_BY],[LID_BX,-LID_BY],[-LID_BX,LID_BY],[LID_BX,LID_BY]];

// ── Panel connectors ─────────────────────────────────────────
GX16_X=-22; GX16_Z=18; GX16_D=16.2; GX16_ND=20.5; GX16_NH=2.5;
TRS_X=24;   TRS_Z=18;  TRS_D=9.5;
DTAB_W=2.2; DTAB_H=1.6;
LED_X=0;    LED_Z=9;   LED_D=5.2;
DC_X=20;    DC_Z=16;   DC_D=8.2;   DC_ND=12.5;  DC_NH=2.0;
SW_X=-18;   SW_Z=18;   SW_D=12.2;

// ── Feet ─────────────────────────────────────────────────────
FOOT_IN=9.0; FOOT_D=3.5; FOOT_CS_D=6.5; FOOT_CS_H=2.0;
FEET=[
  [-(EXT_W/2-FOOT_IN),-(EXT_L/2-FOOT_IN)],
  [ (EXT_W/2-FOOT_IN),-(EXT_L/2-FOOT_IN)],
  [-(EXT_W/2-FOOT_IN), (EXT_L/2-FOOT_IN)],
  [ (EXT_W/2-FOOT_IN), (EXT_L/2-FOOT_IN)],
];

// ── Helpers ──────────────────────────────────────────────────
module rrect(w,l,h,r) {
    r=max(0.1,min(r,min(w,l)/2-0.1));
    hull() for(xi=[-(w/2-r),(w/2-r)]) for(yi=[-(l/2-r),(l/2-r)])
        translate([xi,yi,0]) cylinder(r=r,h=h,$fn=32);
}

module ins_hole() {
    translate([0,0,-0.01]) {
        cylinder(d1=INS_D+0.6, d2=INS_D, h=0.8, $fn=24);
        cylinder(d=INS_D, h=INS_DEPTH+0.01, $fn=24);
    }
}

// ── ENCLOSURE BODY ───────────────────────────────────────────
module enclosure_body() {
    difference() {
        // ── Starting solid ────────────────────────────────────
        rrect(EXT_W, EXT_L, EXT_H, OUTER_R);

        // ── Cavity — with standoffs protected ─────────────────
        // Subtract (cavity MINUS standoff cylinders).
        // The standoff cylinders are removed from the subtraction
        // volume, so those areas are NOT hollowed out → pillars remain.
        difference() {
            // Full interior cavity
            translate([0, 0, WALL])
                rrect(INT_W, INT_L, INT_H + 0.1, INNER_R);
            // Protect PCB standoff positions (cylinder slightly larger/deeper)
            for(p = PCB_BOSSES)
                translate([p[0], p[1], WALL - 0.1])
                    cylinder(d = PCB_OD + 0.1, h = PCB_H + 0.2, $fn = 36);
            // Protect lid boss positions
            for(p = LID_BOSSES)
                translate([p[0], p[1], WALL - 0.1])
                    cylinder(d = LID_OD + 0.1, h = INT_H + 0.2, $fn = 36);
        }

        // ── Insert holes ──────────────────────────────────────
        // Drilled after cavity so they go into the preserved pillars
        for(p = PCB_BOSSES)
            translate([p[0], p[1], WALL + PCB_H])
                mirror([0, 0, 1]) ins_hole();
        for(p = LID_BOSSES)
            translate([p[0], p[1], WALL + INT_H])
                mirror([0, 0, 1]) ins_hole();

        // ── Front panel (+Y face) ─────────────────────────────
        translate([GX16_X, EXT_L/2+0.01, GX16_Z]) rotate([90,0,0]) {
            cylinder(d=GX16_D, h=WALL+0.02, $fn=36);
            translate([0,0,WALL]) cylinder(d=GX16_ND, h=GX16_NH, $fn=36);
        }
        translate([TRS_X, EXT_L/2+0.01, TRS_Z]) rotate([90,0,0]) {
            cylinder(d=TRS_D, h=WALL+0.02, $fn=36);
            translate([0, TRS_D/2+DTAB_H/2-0.01, 0])
                cube([DTAB_W, DTAB_H+0.02, WALL*0.6], center=true);
        }
        translate([LED_X, EXT_L/2+0.01, LED_Z]) rotate([90,0,0])
            cylinder(d=LED_D, h=WALL+0.02, $fn=24);

        // ── Rear panel (−Y face) ──────────────────────────────
        translate([DC_X, -(EXT_L/2)-0.01, DC_Z]) rotate([-90,0,0]) {
            cylinder(d=DC_D, h=WALL+0.02, $fn=32);
            translate([0,0,WALL]) cylinder(d=DC_ND, h=DC_NH, $fn=32);
        }
        translate([SW_X, -(EXT_L/2)-0.01, SW_Z]) rotate([-90,0,0])
            cylinder(d=SW_D, h=WALL+0.02, $fn=36);

        // ── Bottom feet ───────────────────────────────────────
        for(p = FEET) translate([p[0],p[1],-0.01]) {
            cylinder(d=FOOT_CS_D, h=FOOT_CS_H+0.01, $fn=24);
            cylinder(d=FOOT_D,    h=WALL+0.02,       $fn=24);
        }

        // ── Side vents (3 per side) ───────────────────────────
        for(sx=[-1,1]) for(vy=[-22,0,22])
            translate([sx*(EXT_W/2+0.01), vy, EXT_H/2]) rotate([0,90,0])
                hull() for(dz=[-11,11]) translate([dz,0,0])
                    cylinder(d=2.5, h=WALL+0.02, $fn=18);

        // ── Front label (debossed) ────────────────────────────
        translate([0, EXT_L/2-0.55, 5.5]) rotate([90,0,0])
        linear_extrude(0.6)
            text("BINAURAL GOGGLE", size=3.8,
                 font="Liberation Mono:style=Bold",
                 halign="center", valign="center");
    }
}

// ── ENCLOSURE LID ────────────────────────────────────────────
module enclosure_lid() {
    difference() {
        union() {
            rrect(EXT_W, EXT_L, LID_T, OUTER_R);
            difference() {
                rrect(INT_W-LID_GAP*2, INT_L-LID_GAP*2, LID_LIP_D+0.01,
                      max(0.5, INNER_R-LID_GAP));
                translate([0,0,-0.01])
                    rrect(INT_W-LID_GAP*2-LID_LIP_W*2,
                          INT_L-LID_GAP*2-LID_LIP_W*2,
                          LID_LIP_D+0.1,
                          max(0.5, INNER_R-LID_GAP-LID_LIP_W));
            }
        }
        for(p = LID_BOSSES) translate([p[0],p[1],-0.01]) {
            cylinder(d=3.4, h=LID_T+0.02, $fn=24);
            translate([0,0,LID_T-2.2]) cylinder(d1=3.4,d2=6.5, h=2.3, $fn=24);
        }
    }
}

// ── Render ───────────────────────────────────────────────────
if(RENDER_PART=="body") {
    enclosure_body();
} else if(RENDER_PART=="lid") {
    enclosure_lid();
} else {
    color("#3a5a4a",0.85) enclosure_body();
    translate([0,0,EXT_H+10]) color("#4a6a5a",0.75) enclosure_lid();
}
