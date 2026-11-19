// $poseparameter + a 3-way blend sequence (blendwidth 3, blend move_x -1 1) with event and activity
$modelname "api/blend.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"

$body "biped" "src/biped.smd"

$poseparameter "move_x" -1 1

$animation "a_left" "src/anims/lean_left.smd" fps 30 loop
$animation "a_center" "src/anims/lean_center.smd" fps 30 loop
$animation "a_right" "src/anims/lean_right.smd" fps 30 loop
$animation "@idle" "src/biped.smd" fps 30

$sequence "lean" {
	"a_left"
	"a_center"
	"a_right"
	blendwidth 3
	blend move_x -1 1
	activity ACT_WALK 1
	event AE_FOOTSTEP 4 "left"
	event AE_FOOTSTEP 10 "right"
	fadein 0.1
}

$sequence "idle" "@idle"
