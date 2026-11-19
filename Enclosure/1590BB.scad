// Hammond 1590BB-inspired FDM enclosure. Dimensions in millimeters.
part = "both"; // [both,body,lid]

length = 119.5;
width = 94;
assembled_height = 34;
corner_radius = 5;
wall = 2.4;
floor_thickness = 2.8;
lid_thickness = 3;

boss_diameter = 8;
boss_inset = 5;  // Keeps the corner bosses clear of the PCB.
pilot_diameter = 2.7;
pilot_depth = 9;
lid_clearance_diameter = 3.4;
layout_gap = 8;

pot_diameter = 10;
pot_x = -35;
pcb_usb_edge_gap = 1;
// From the PCB's USB edge to each shaft center, measured in the STEP assembly.
pot_distances = [17.965, 63.685];

// Visible glass area from the KiCad model, plus 0.3 mm per side.
oled_window_width = 22.99;
oled_window_height = 15.30;
oled_distance = 41.724;

// The ribbon exits the back of the keypad near its bottom edge.
keypad_margin = 4;
keypad_width = 63.5;
keypad_height = 76.2;
keypad_slot_width = 24;
keypad_slot_height = 4;
keypad_slot_bottom_inset = 4;

usb_clearance = 3;
usb_port_width = 7.558;
usb_port_height = 2.940;
usb_x_from_pots = 35.941;
// STEP heights relative to the same datum. Seat the pot bushings at the
// inside of the control panel to determine the PCB's height.
pot_bushing_start = 12.4;
usb_port_center_height = 3.855;

audio_diameter = 10;
audio_x = 35;

$fn = 64;

body_height = assembled_height - lid_thickness;
overlap = 0.02;
// The body prints with the outside of the control panel on the build plate.
usb_wall_y = -width / 2;
pcb_usb_edge_y = usb_wall_y + wall + pcb_usb_edge_gap;
usb_center_z = floor_thickness + pot_bushing_start - usb_port_center_height;
keypad_center_x = length / 2 - keypad_margin - keypad_width / 2;
keypad_bottom_y = usb_wall_y + keypad_margin + keypad_height;
keypad_slot_y = keypad_bottom_y - keypad_slot_bottom_inset - keypad_slot_height / 2;

module rounded_rectangle(x, y, radius) {
    offset(r = radius)
        square([x - 2 * radius, y - 2 * radius], center = true);
}

module screw_positions() {
    for (side_x = [-1, 1], side_y = [-1, 1])
        translate([side_x * (length / 2 - boss_inset),
                   side_y * (width / 2 - boss_inset)])
            children();
}

module panel_openings() {
    for (distance = pot_distances)
        translate([pot_x, pcb_usb_edge_y + distance, -overlap])
            cylinder(h = floor_thickness + 2 * overlap, d = pot_diameter);

    translate([pot_x - oled_window_width / 2,
               pcb_usb_edge_y + oled_distance - oled_window_height / 2, -overlap])
        cube([oled_window_width, oled_window_height, floor_thickness + 2 * overlap]);

    translate([keypad_center_x - keypad_slot_width / 2,
               keypad_slot_y - keypad_slot_height / 2, -overlap])
        cube([keypad_slot_width, keypad_slot_height, floor_thickness + 2 * overlap]);

    translate([pot_x + usb_x_from_pots, usb_wall_y + wall / 2, usb_center_z])
        cube([usb_port_width + 2 * usb_clearance, wall + 2 * overlap,
              usb_port_height + 2 * usb_clearance], center = true);

    translate([audio_x, usb_wall_y + wall / 2, assembled_height / 2])
        rotate([90, 0, 0])
            cylinder(h = wall + 2 * overlap, d = audio_diameter, center = true);
}

module body() {
    difference() {
        union() {
            difference() {
                linear_extrude(height = body_height)
                    rounded_rectangle(length, width, corner_radius);
                translate([0, 0, floor_thickness])
                    linear_extrude(height = body_height - floor_thickness + overlap)
                        rounded_rectangle(length - 2 * wall, width - 2 * wall,
                                          corner_radius - wall);
            }
            screw_positions()
                translate([0, 0, floor_thickness - overlap])
                    cylinder(h = body_height - floor_thickness + overlap,
                             d = boss_diameter);
        }
        screw_positions()
            translate([0, 0, body_height - pilot_depth])
                cylinder(h = pilot_depth + overlap, d = pilot_diameter);
        panel_openings();
    }
}

module lid() {
    difference() {
        linear_extrude(height = lid_thickness)
            rounded_rectangle(length, width, corner_radius);
        screw_positions()
            translate([0, 0, -overlap])
                cylinder(h = lid_thickness + 2 * overlap, d = lid_clearance_diameter);
    }
}

if (part == "body") {
    body();
} else if (part == "lid") {
    lid();
} else if (part == "both") {
    body();
    translate([length + layout_gap, 0, 0]) lid();
} else {
    assert(false, "Select body, lid, or both.");
}
