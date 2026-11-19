// Old-style VTA flexes (flexfile/defaultflex/flex/flexpair/flexcontroller/rules/localvar), eyeballs, eyelids, mouth
$modelname "api/flex.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"

$model "face" "src/face.smd" {
	flexfile "src/face.vta"
	{
		defaultflex frame 0
		flex "blink" frame 1
		flexpair "smile" 0.4 frame 2
		flex "jaw_open" frame 3
	}
}

$model "eyes" "src/eyes.smd" {
	eyeball "right_eye" "r_eye" 2.5 4 1.5 "eyeball_r" 2.4 4 "iris_r" 0.9
	eyeball "left_eye" "l_eye" -2.5 4 1.5 "eyeball_l" 2.4 4 "iris_l" 0.9
	eyelid upper_right "src/eyes.vta" lowerer 1 -0.8 neutral 0 0 raiser 2 0.5 split 0.1 eyeball "right_eye"
	eyelid upper_left "src/eyes.vta" lowerer 3 -0.8 neutral 0 0 raiser 4 0.5 split 0.1 eyeball "left_eye"

	localvar smile_amount

	mouth 0 "mouth_open" "jaw" 0 1 0

	flexcontroller default range 0 1 blink_ctl smile_ctl jaw_ctl
	flexcontroller default range -1 1 brow_ctl

	%blink = blink_ctl
	%smile_amount = smile_ctl * 0.5 + 0.25
	%smileR = smile_ctl
	%smileL = smile_ctl * 0.5 + %smile_amount * 0.5
	%jaw_open = jaw_ctl + 0.25 * %blink
}

$animation "a_idle" "src/anims/face_idle.smd" fps 30 loop
$sequence "idle" "a_idle"
