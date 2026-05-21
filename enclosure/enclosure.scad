// IMU Extendor Board enclosure
// Bottom tray + lid with M2 self-tapping screws (washers recommended; PCB holes are 2.7mm)
// PCB outline: ~55x55 mm rounded rectangle, mounting holes at 47x47 mm grid

// --- Parameters ---
pcb_x      = 26.097;   // PCB outline min X (from kicad_pcb)
pcb_y      = 27.097;   // PCB outline min Y
pcb_w      = 81.0 - 26.097;   // ~54.9
pcb_h      = 81.903 - 27.097; // ~54.8
pcb_r      = 5.0;      // corner radius
pcb_thk    = 1.6;

// Mounting holes in board coords (from kicad_pcb)
holes      = [[30,31],[77,31],[77,78],[30,78]];

wall       = 2.0;      // outer wall thickness
floor_thk  = 2.0;      // bottom plate thickness
lid_thk    = 2.0;      // lid plate thickness
gap        = 0.4;      // clearance between PCB edge and inner wall
standoff_h = 1.0;      // floor-top to PCB-bottom
clear_top  = 7.2;      // PCB-top to lid-inner; keeps USB-C cutout vertically centered in wall

post_od    = 5.5;      // standoff/screw post outer diameter
pilot_d    = 1.7;      // M2 self-tap pilot hole diameter
lid_clear  = 2.3;      // lid screw clearance hole (M2 loose)
pcb_pilot_bottom_skin = 0.4; // leave this much material under PCB screw pilot holes

// Lid drops into a rebate so it self-aligns
rebate_depth = 1.2;
rebate_clear = 0.3;

$fn = 64;

// Inner cavity footprint (PCB + gap)
inner_x = pcb_x - gap;
inner_y = pcb_y - gap;
inner_w = pcb_w + 2*gap;
inner_h = pcb_h + 2*gap;
inner_r = pcb_r + gap;

// Outer shell footprint
outer_x = inner_x - wall;
outer_y = inner_y - wall;
outer_w = inner_w + 2*wall;
outer_h = inner_h + 2*wall;
outer_r = inner_r + wall;
box_cx = pcb_x + pcb_w/2;
box_cy = pcb_y + pcb_h/2;

function mirror_y(y) = 2*box_cy - y;

// Total inner height (floor inner to lid-rebate level)
post_total = standoff_h + pcb_thk + clear_top;

// --- Lid-attachment "ears" at outer corners ---
// Each ear is a vertical cylinder overlapping the outer wall corner
// to provide material for an M2 self-tap pilot for the lid screw.
ear_od     = 5.5;     // OD of corner ear (matches PCB standoff)
ear_bulge  = 4.0;     // diagonal outward offset so ear clears the PCB standoffs.
                      // PCB holes sit 4mm from PCB corner; with this bulge the ear
                      // ends up ~1.5mm clear of the PCB standoff.

// Outer/inner corner-arc center (shared)
corner_cx = [pcb_x + pcb_r, pcb_x + pcb_w - pcb_r, pcb_x + pcb_w - pcb_r, pcb_x + pcb_r];
corner_cy = [pcb_y + pcb_r, pcb_y + pcb_r,         pcb_y + pcb_h - pcb_r, pcb_y + pcb_h - pcb_r];
corner_dx = [-1,  1,  1, -1] / sqrt(2);
corner_dy = [-1, -1,  1,  1] / sqrt(2);

// Place ear center so it just bulges past the original outer arc (outer_r) by ear_bulge,
// with ear_od/2 of the ear extending outward.
ear_radial = outer_r + ear_bulge - ear_od/2;
ear_positions = [
    for (i = [0:3]) [corner_cx[i] + corner_dx[i]*ear_radial,
                     corner_cy[i] + corner_dy[i]*ear_radial]
];

// --- Lid cutouts: [center_x, center_y, width_x, width_y] in PCB coords ---
// All connectors are on F.Cu (top of PCB) and accessed through the lid.
// Sized from actual pad bounding boxes + ~3mm margin for connector body / cable.
swd_uart_shift = 11.5; // move SWD opening toward the UART header

lid_cutouts = [
    // SWD J501 (1x6 vertical, top edge): pads X=27.27..39.97, Y=78.19
    [33.62 + swd_uart_shift, 78.19, 17.0, 7.0],
    // Optical Flow J601 (1x7 vertical, right edge): pads X=76.8, Y=40.1..55.3
    [76.8,   47.71,  7.0, 19.0],
    // UART J703 (1x4 vertical, right edge): pads X=76.8, Y=61.7..69.3
    [76.8,   65.49,  7.0, 11.0],
];

// --- Floor cutouts: same format, cut through the case floor ---
// J702 (1x10 1.27mm, mounted on B.Cu): connector body is on PCB underside,
// cable mates from below -> cut floor under it. Pads X=38.9..50.4, Y=31.5..34.5
// J102 (1x2 power, F.Cu) accessed from below per request.
//   Pads X=30.06, Y=40.08..42.62
floor_cutouts = [
    [44.635, mirror_y(33.0),   14.0, 5.0],   // J702 GPIO breakout (B.Cu: Y mirrored about box centre)
    [30.06,  mirror_y(41.35),   5.0, 6.0],   // J102 5V power     (B.Cu: Y mirrored about box centre)
];

// User LEDs (small viewing holes in lid)
led_positions = [[39, 62.4], [39, 46.6]];
led_hole_d    = 2.0;

// --- Side cutouts in BOTTOM walls ---
// PCB top in outer Z coords: floor_thk + standoff_h + pcb_thk
pcb_top_z = floor_thk + standoff_h + pcb_thk;

// Side openings: [edge, along_axis_center, width_along_edge, z_center, z_height]
//   edge: "xmin"/"xmax"/"ymin"/"ymax" -- which wall to cut through
// USB-C J101 at (29.75, 54.5), rot=-90 -- receptacle mouth faces -X (xmin wall)
//   Mouth centerline ~1.3mm above PCB top => z_center = pcb_top_z + 1.3
//   Generous cutout for plug overmold: 12mm wide along Y, 7mm tall along Z
side_cutouts = [
    ["xmin", 54.5, 12.0, pcb_top_z + 1.3, 7.0],   // USB-C J101
];

module rrect(x, y, w, h, r) {
    translate([x, y])
        hull() {
            translate([r, r])       circle(r=r);
            translate([w-r, r])     circle(r=r);
            translate([r, h-r])     circle(r=r);
            translate([w-r, h-r])   circle(r=r);
        }
}

// Outline shared by bottom outer shell and lid: rounded rect + bulged corner ears
module shell_outline_2d() {
    union() {
        rrect(outer_x, outer_y, outer_w, outer_h, outer_r);
        for (p = ear_positions)
            translate([p[0], p[1]]) circle(d=ear_od + 2*ear_bulge);
    }
}

// --- BOTTOM ---
module bottom() {
    pilot_depth = standoff_h + 1;
    ear_h = floor_thk + post_total - rebate_depth;

    difference() {
        union() {
            // Outer shell
            linear_extrude(floor_thk + post_total)
                shell_outline_2d();
            // Corner ear bosses (flush with rebate floor).
            // Kept inside the main difference so the pilot hole below punches through
            // both the boss AND the surrounding shell material at the ear positions.
            for (p = ear_positions)
                translate([p[0], p[1], 0])
                    cylinder(d=ear_od, h=ear_h);
        }

        // Hollow inside (leave floor)
        translate([0,0,floor_thk])
            linear_extrude(post_total + 1)
                rrect(inner_x, inner_y, inner_w, inner_h, inner_r);

        // Lid rebate
        translate([0,0,floor_thk + post_total - rebate_depth])
            linear_extrude(rebate_depth + 0.1)
                offset(r=-wall/2 + rebate_clear/2)
                    shell_outline_2d();

        // Corner ear pilot holes — full depth through ear so any screw length works
        for (p = ear_positions)
            translate([p[0], p[1], -0.1])
                cylinder(d=pilot_d, h=ear_h + 0.2);

        // PCB mounting screw pilots continue into the floor, leaving a thin bottom skin.
        for (h = holes)
            translate([h[0], h[1], pcb_pilot_bottom_skin])
                cylinder(d=pilot_d, h=floor_thk - pcb_pilot_bottom_skin + 0.2);

        // Floor cutouts
        for (c = floor_cutouts)
            translate([c[0] - c[2]/2, c[1] - c[3]/2, -1])
                linear_extrude(floor_thk + 2)
                    offset(r=1, $fn=24)
                        offset(r=-1)
                            square([c[2], c[3]], center=false);

        // Side wall cutouts
        for (s = side_cutouts) {
            edge = s[0]; ctr = s[1]; w = s[2]; zc = s[3]; zh = s[4];
            cut_depth = wall + 2;
            if (edge == "xmin")
                translate([outer_x - 1, ctr - w/2, zc - zh/2])
                    cube([cut_depth, w, zh]);
            else if (edge == "xmax")
                translate([outer_x + outer_w - wall - 1, ctr - w/2, zc - zh/2])
                    cube([cut_depth, w, zh]);
            else if (edge == "ymin")
                translate([ctr - w/2, outer_y - 1, zc - zh/2])
                    cube([w, cut_depth, zh]);
            else if (edge == "ymax")
                translate([ctr - w/2, outer_y + outer_h - wall - 1, zc - zh/2])
                    cube([w, cut_depth, zh]);
        }
    }

    // PCB standoffs — added outside the main difference so the inner-cavity cut
    // doesn't eat them. They sit inside the hollow, so no shell material to worry about.
    for (h = holes) {
        translate([h[0], h[1], floor_thk])
            difference() {
                cylinder(d=post_od, h=standoff_h);
                translate([0,0,standoff_h - pilot_depth + 0.1])
                    cylinder(d=pilot_d, h=pilot_depth);
            }
    }
}

// --- LID ---
module lid() {
    difference() {
        union() {
            // Top plate matches the OUTER shell footprint (including ear bulges),
            // shrunk slightly to fit into the rebate.
            linear_extrude(lid_thk)
                offset(r=-rebate_clear/2 - wall/2)
                    shell_outline_2d();
            // Alignment lip drops INTO the rebate (slightly smaller)
            translate([0,0,-rebate_depth + 0.05])
                linear_extrude(rebate_depth)
                    offset(r=-wall/2 - 0.2)
                        shell_outline_2d();
        }
        // Lid screw clearance holes (aligned with corner ears)
        for (p = ear_positions)
            translate([p[0], p[1], -rebate_depth - 1])
                cylinder(d=lid_clear, h=lid_thk + rebate_depth + 2);
        // Countersink top
        for (p = ear_positions)
            translate([p[0], p[1], lid_thk - 0.8])
                cylinder(d1=lid_clear, d2=lid_clear+1.6, h=0.9);

        // Connector cutouts (rectangles, rounded corners)
        for (c = lid_cutouts)
            translate([c[0], mirror_y(c[1]), -rebate_depth - 1])
                linear_extrude(lid_thk + rebate_depth + 2)
                    offset(r=1, $fn=24)
                        offset(r=-1)
                            square([c[2], c[3]], center=true);

        // LED viewing holes
        for (p = led_positions)
            translate([p[0], mirror_y(p[1]), -rebate_depth - 1])
                cylinder(d=led_hole_d, h=lid_thk + rebate_depth + 2);
    }
}

// --- Render selection ---
// PART:     "bottom" / "lid" / "all"
// VIEW:     "stl"  -> identity transform (use this when exporting STL)
//           "user" -> preview reorientation: USB-C wall faces DOWN, PCB-low-Y
//                    side on user's LEFT. Reflection -> do not export as STL!
PART = "all";
VIEW = "stl";

module view_transform() {
    if (VIEW == "kicad") {
        // KiCad top view: Y-flip only.
        translate([box_cx, box_cy, 0])
            mirror([0, 1, 0])
                translate([-box_cx, -box_cy, 0])
                    children();
    } else if (VIEW == "kicad_bottom") {
        // KiCad bottom view: both X and Y flipped (= 180 deg about Z).
        // PCB-bottom convention: as if you flipped the case over to look at the floor.
        translate([box_cx, box_cy, 0])
            rotate([0, 0, 180])
                translate([-box_cx, -box_cy, 0])
                    children();
    } else {
        children();
    }
}

view_transform() {
    if (PART == "bottom") bottom();
    else if (PART == "lid") lid();
    else {
        bottom();
        translate([outer_w + 10, 0, 0]) lid();
    }
}
