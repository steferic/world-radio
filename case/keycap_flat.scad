// keycap_flat.scad — flat-slab keycap, authored at FINAL size (no rescaling
// in Blender, so radius/taper/height stay exactly as specified).
//
// Brief: flat square key, slightly rounded edges, generous plan corners, only
// a whisper of taper. Crisp precision hardware — not a tapered Cherry cap.
//
// Export:
//   /Applications/OpenSCAD-2021.01.app/Contents/MacOS/OpenSCAD \
//     -o keycap_flat.stl keycap_flat.scad

include <KeyV2/includes.scad>

$key_shape_type = "rounded_square";
$corner_radius = 4.0;       // soft plan corners at 29mm — WL-ish friendliness

$bottom_key_width = 29.0;   // final size, authored here
$bottom_key_height = 29.0;
$width_difference = 1.6;    // whisper of taper; sides read near-vertical
$height_difference = 1.6;

$total_depth = 5.6;         // slab, not a dome
$top_tilt = 0;
$top_tilt_y = 0;
$top_skew = 0;

$dish_type = "disable";     // FLAT top
$dish_depth = 0;
$height_slices = 4;         // straight walls; no need for slicing

$rounded_key = true;
$minkowski_radius = 0.45;   // fine edge roll only

$stem_type = "disable";
$stem_support_type = "disable";
$legends = [];

key();
