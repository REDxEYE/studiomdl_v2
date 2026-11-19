// Animation options and commands: loop/fixup/compress, subtract, align, ik rules, rotate/counterrotate, forceboneposrot, bonedriver, localhierarchy, weight lists
$modelname "api/anim_cmds.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"

$body "biped" "src/biped.smd"

$weightlist "upper" {
	"root" 0
	"spine" 1 posweight 0.5
	"head" 1
}
$defaultweightlist {
	"root" 1
	"pelvis" 1
}

$attachment "muzzle" "r_arm" 0 0 10
$ikchain "l_leg" "l_foot" knee 1 0 0 height 18 floor 0 pad 8
$ikchain "r_leg" "r_foot" knee 1 0 0 center 0 0 0

$animation "ref_pose" "src/biped.smd" fps 30
$animation "a_idle" "src/anims/biped_idle.smd" fps 24 loop fixuploop 8 9 noautoik
$animation "a_walk" "src/anims/biped_walk.smd" fps 30 loop LX LY origin 1 2 3 rotate 90 scale 1.5 motionrollback 0.5 walkframe 12 X Y post
$animation "a_wave" "src/anims/biped_wave.smd" fps 30 subtract ref_pose 0 weightlist "upper" frame 2 12
$animation "a_aligned" "src/anims/lean_center.smd" fps 30 alignto a_walk alignbone "pelvis" a_walk X Y 0 5 snap
$animation "a_ik" "src/anims/lean_left.smd" fps 30 startloop 3 nostallframes 4 noanimblock noanimblockstall angles 10 20 30
$animation "a_ik_rules" "src/anims/lean_right.smd" fps 30 percentstartloop 0.25 fudgeloop blockname BindPose framestart 1 ikrule "l_leg" footstep range 0 3 7 11 floor 1 height 10 pad 6 contact 2 target 1 usesequence ikrule "r_leg" touch "l_foot" range 0 2 4 9 radius 3 usesource ikrule "l_leg" release range 4 5 6 7 ikfixup "r_leg" attachment "muzzle" range 0 1 2 3 fakeorigin 1 2 3 fakerotate 10 20 30 ikrule "r_leg" unlatch range 1 2 3 4
$animation "a_bones" "src/anims/lean_center.smd" fps 30 rotateto 45 counterrotate "head" counterrotateto 0 90 0 "spine" forceboneposrot "head" pos 0 0 1 rot 0 30 0 local bonedriver "head" axis y value 1 range 0 1 2 3 bonedriver "spine" localhierarchy "l_arm" "spine" range 1 2 3 4 localhierarchy "r_arm" "spine"
$animation "a_misc" "src/anims/lean_left.smd" fps 30 derivative 2 lineardelta
$animation "a_spline" "src/anims/lean_right.smd" fps 30 splinedelta numframes 6 reverse
$animation "a_appended" "src/anims/lean_center.smd" fps 30 appendanim a_idle matchblend a_walk 0 0 0 3 match a_walk worldspaceblend a_walk worldspaceblendloop a_walk 2 walkalignto 6 a_walk LX walkalign 8 a_walk LX LY 0 4 noanimation noanim_keepduration

$sequence "idle" "a_idle"
$sequence "walk" "a_walk"
$sequence "wave" "a_wave"
$sequence "aligned" "a_aligned"
$sequence "ik" "a_ik"
$sequence "ik_rules" "a_ik_rules"
$sequence "bones" "a_bones"
$sequence "misc" "a_misc"
$sequence "spline" "a_spline"
$sequence "appended" "a_appended"
$sequence "ref" "ref_pose"
