// $jigglebone on a skinned chain: is_flexible (+ has_base_spring) and is_rigid
$modelname "api/jiggle.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"

$body "tube" "src/chain_ref.smd"

$jigglebone "tip"
{
	is_flexible
	{
		length 6
		tip_mass 40
		pitch_stiffness 100
		pitch_damping 7
		yaw_stiffness 100
		yaw_damping 7
		allow_length_flex
		along_stiffness 100
		along_damping 0
		angle_constraint 30
	}
	has_base_spring
	{
		stiffness 100
		damping 5
		left_constraint -10 10
		up_constraint -10 10
		forward_constraint -10 10
		base_mass 5
	}
}

$jigglebone "tip_end"
{
	is_rigid
	{
		length 4
		tip_mass 10
		yaw_constraint -20 20
		yaw_friction 0.3
		yaw_bounce 0.1
		pitch_constraint -25 25
		pitch_friction 0.3
		pitch_bounce 0.1
	}
}

$sequence "idle" "src/chain_ref.smd" fps 30
$sequence "wiggle" "src/anims/chain_idle.smd" fps 30 loop
