// Sequence options: activity, modifiers, events, tags, fades, flags, nodes/transitions, layers, 2D blend with pose parameters, pose cycle, blendref/comp/center, iklock, keyvalues
$modelname "api/anim_seq.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"

$body "biped" "src/biped.smd"

$poseparameter "move_x" -1 1
$poseparameter "turn" -180 180 loop 360
$poseparameter "aim" 0 90 wrap
$attachment "muzzle" "r_arm" 0 0 10
$ikchain "l_leg" "l_foot" knee 1 0 0 height 18 floor 0 pad 8

$animation "base" "src/anims/lean_center.smd" fps 30 loop
$animation "g0" "src/anims/lean_left.smd" fps 30 loop
$animation "g1" "src/anims/lean_right.smd" fps 30 loop
$animation "g2" "src/anims/lean_center.smd" fps 30 loop
$animation "g3" "src/anims/lean_left.smd" fps 30 loop
$animation "single" "src/anims/biped_walk.smd" fps 30 loop

$sequence "walk" {
	"single"
	activity ACT_WALK 2
	activitymodifier "crouch"
	actmod "sneak"
	event AE_FOOTSTEP 4 "left"
	event 5004 10
	animtag "step" 0.5
	fadein 0.3
	fadeout 0.4
	node "stand"
	exitphase 0.5
	hidden
	realtime
	snap
	post
	iklock "l_leg" 1 0.5
	keyvalues {
		"bounds" { "min" "0 0 0" "max" "1 1 1" }
	}
}

$sequence "grid" {
	"g0"
	"g1"
	"g2"
	"g3"
	blendwidth 2
	blend move_x -1 1
	blend turn -180 180
	blendref base
	blendcomp base
	blendcenter g2
}

$sequence "calc" {
	"g0"
	"g1"
	"g2"
	blendwidth 3
	blend aim 0 90
	posecycle move_x
}

$sequence "trans" {
	"base"
	transition "stand" "crouch"
	autoplay
}
$sequence "rtrans" {
	"base"
	rtransition "crouch" "stand"
	delta
}
$sequence "world" {
	"base"
	worldspace
	rootdriver "root"
}
$sequence "worldrel" {
	"base"
	worldrelative
	predelta
	addlayer "walk" local
	addlayer "trans"
	blendlayer "walk" 0 2 6 8 spline noblend local poseparameter move_x
	blendlayer "trans" 1 2 3 4
}
