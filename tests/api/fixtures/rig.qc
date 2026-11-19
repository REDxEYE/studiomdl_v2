// Attachments, $hboxset/$hbox/$hgroup, $ikchain + $ikautoplaylock, $weightlist layers, $bonemerge
$modelname "api/rig.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"

$body "biped" "src/biped.smd"

$attachment "head_top" "head" 0 0 12 rotate 0 90 0
$attachment "eye_pos" "head" 4 0 2 x_and_z_axes 1 0 0 0 0 1
$attachment "hand_abs" "l_arm" 0 0 14 absolute
$attachment "root_rigid" "root" 0 0 0 rigid
$attachment "foot_wa" "l_foot" 0 0 2 rotate 0 0 45 world_align

$hgroup 1 "head"
$hgroup 2 "spine"
$hgroup 3 "pelvis"
$hgroup 4 "l_arm"
$hgroup 5 "r_arm"
$hgroup 6 "l_thigh"
$hgroup 7 "r_thigh"

$hboxset "default"
$hbox 1 "head" -5 -5 0 5 5 10
$hbox 2 "spine" -7 -7 0 7 7 18 0 10 0 -1 "chest_box"
$hbox 3 "pelvis" -7 -7 -2 7 7 10
$hbox 4 "l_arm" -2.5 -2.5 0 2.5 2.5 14
$hbox 5 "r_arm" -2.5 -2.5 0 2.5 2.5 14
$hbox 6 "l_thigh" -3 -3 0 3 3 16
$hbox 7 "r_thigh" -3 -3 0 3 3 16

$hboxset "crouch"
$hbox 1 "head" -5 -5 0 5 5 10
$hbox 3 "pelvis" -7 -7 -2 7 7 10

$bonemerge "head"

$weightlist "upperbody" {
	"root" 0
	"pelvis" 0
	"spine" 1
	"head" 1
	"l_arm" 1
	"r_arm" 1
}

$animation "@idle" "src/biped.smd" fps 30
$animation "@wave" "src/anims/biped_wave.smd" fps 30 weightlist "upperbody"
$animation "@wave_layered" "src/anims/biped_wave.smd" fps 30 weightlist "upperbody"

$sequence "idle" "@idle"
$sequence "wave" "@wave"
$sequence "wave_layered" {
	"@wave_layered"
	addlayer "idle"
	blendlayer "wave" 0 2 10 14 xfade
}

$ikchain "l_leg" "l_foot" knee 1 0 0 height 18 floor 0 pad 8
$ikchain "r_leg" "r_foot" knee 1 0 0 center 0 0 0
$ikautoplaylock "l_leg" 1.0 0.1
$ikautoplaylock "r_leg" 0.9 0.2
