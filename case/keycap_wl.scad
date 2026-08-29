// keycap_wl.scad — a Work-Louder-flavoured keycap, generated with KeyV2
// (rsheldiii/KeyV2, the open-source parametric keycap library).
//
// Character we're matching from Work Louder's Creator/Codex pads: uniform
// (non-sculpted) caps, chunky with generous plan corner radii, a gentle taper,
// a nearly-flat top with just a hint of spherical dish, satin finish.
//
// Export:
//   /Applications/OpenSCAD-2021.01.app/Contents/MacOS/OpenSCAD \
//     -o keycap_wl.stl keycap_wl.scad

include <KeyV2/includes.scad>

$key_shape_type = "rounded_square";
$corner_radius = 3.2;      // big, soft plan corners (WL signature)

$bottom_key_width = 18.3;  // MX-compatible footprint
$bottom_key_height = 18.3;
$width_difference = 2.2;   // gentle taper (DSA is 6 — too cone-like for WL)
$height_difference = 2.2;

$total_depth = 6.8;        // real cap height, not a chiclet
$top_tilt = 0;             // uniform profile: no row sculpting
$top_tilt_y = 0;
$top_skew = 0;

$dish_type = "spherical";
$dish_depth = 0.45;         // barely dished — reads flat, feels finished
$height_slices = 12;       // smooth side walls

$rounded_key = true;       // minkowski edge softening
$minkowski_radius = 0.55;

$stem_type = "disable";    // render-only: no stem needed
$stem_support_type = "disable";
$legends = [];

key();
