# PVS enclosure

The Hammond-based print files are
[PVS-Hammond-1590BB-body.stl](PVS-Hammond-1590BB-body.stl) and
[PVS-Hammond-1590BB-lid.stl](PVS-Hammond-1590BB-lid.stl).
The [STEP assembly](PVS-enclosure-assembly.step) uses Hammond's 1590BB
cast body, lid, and cover screws as the reference for the Tayda case.
The modified body includes the PVS cutouts, PCB, externally mounted
keypad, and panel-mount audio jack. Hammond's model was retrieved from
the [public enclosure CAD mirror](https://github.com/whbeers/enclosure_kicad_footprints/blob/e59b04e1130962d179eb39395b5c85b11a5789ce/Enclosures.pretty/1590BB.stp).
The [editable OpenSCAD design](1590BB.scad) uses the 119.5 × 94 × 34 mm
envelope from the [Hammond 1590BB drawing](https://www.hammfg.com/files/parts/pdf/1590BB.pdf).
Its [straight-wall body](PVS-body.stl) and [lid](PVS-lid.stl) use a
matching screw pattern.

Set `part` to `body` or `lid`, render, and export each as an STL. Both parts
sit flat on the build plate. The lid uses M3 clearance holes and the body
uses adjustable pilot holes. Match the pilot diameter and depth to the
screws and filament you plan to use.

Viewed from outside with the USB wall at the top, the two Ø10 mm pot
holes are 35 mm left of center and 17.965 mm and 63.685 mm down from the
PCB edge. The PCB edge is positioned 1 mm inside the USB wall. The
rectangular USB opening is 13.56 × 8.94 mm, including 3 mm clearance per
side around the STEP model's USB shell. Its height assumes the pot
bushings sit against the inside face of the control panel.

The Ø10 mm audio hole is on the USB wall, 35 mm right of center and
17 mm from the outside face of the control panel. The
[Tayda A-7911 model](Tayda-A-7911-CK635178.step) is a fit model of the
Tuozhi CK635178 based on the supplier drawing and product photo. It
uses the drawing's 3/8-32 bushing and 15.6 mm square housing envelope.
The nut and washer geometry are estimated, and the bushing has a
smooth thread envelope. The model clears the panel and the other
modeled parts. [Tayda product page](https://www.taydaelectronics.com/6-35mm-1-4-stereo-phone-jack-3-pins.html) · [Mechanical drawing](https://www.taydaelectronics.com/datasheets/files/CK635178.pdf)

In the printed design, the lid screw pattern was moved outward to clear
the PCB. The Hammond assembly uses the stock screw pattern. Its PCB
position accounts for the narrower inside of the tapered wall, and the
audio jack and its hole follow the wall angle.

The OLED window is 22.99 × 15.30 mm, centered over the visible display
area in the KiCad model. The keypad sits on the outside of the control
panel, 4 mm from the top and right edges. The 24 × 4 mm cable slot is
centered horizontally under the keypad, with its lower edge 4 mm above
the keypad's bottom edge. The slot center is 6 mm above that edge, at
the ribbon exit. The STEP assembly shows the ribbon through the slot
as a simplified shape.

The body and lid export as closed meshes. Physical fit remains
unverified, including OLED viewing depth, keypad connector passage,
the jack body, and USB cable-plug clearance.
