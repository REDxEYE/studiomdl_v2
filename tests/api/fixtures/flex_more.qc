// Flex options not in flex.qc: position/decay/split, spherenormals, noautodmxrules, lower eyelid, $boneflexdriver, functions in rules
$modelname "api/flex_more.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"

$model "face" "src/face.smd" {
	flexfile "src/face.vta"
	{
		flex "squeeze" frame 1 position 0.5 decay 0.5
		flex "squeeze" frame 2
		flexpair "wide" 0.2 frame 3 decay 0.5
		flex "narrow" frame 3 split 0.3 position 0.75
	}
	spherenormals "face_skin" 0 60 0
	noautodmxrules
}

$model "eyes" "src/eyes.smd" {
	eyeball "right_eye" "r_eye" 2.5 4 1.5 "eyeball_r" 2.4 4 "iris_r" 0.9
	eyeball "left_eye" "l_eye" -2.5 4 1.5 "eyeball_l" 2.4 4 "iris_l" 0.9
	eyelid lower_right "src/eyes.vta" lowerer 1 -0.6 neutral 0 0 raiser 2 0.4 eyeball "right_eye"
	eyelid lower_left "src/eyes.vta" lowerer 3 -0.6 neutral 0 0 raiser 4 0.4

	flexcontroller default range 0 1 squeeze_ctl wide_ctl
	flexcontroller eyes range -1 1 look_ctl

	%squeeze = max(squeeze_ctl, wide_ctl * 0.5)
	%wideR = min(wide_ctl, 0.8) + -look_ctl
	%wideL = (wide_ctl - 0.1) / 2
	%narrow = squeeze_ctl * (1 - wide_ctl)
}

$boneflexdriver "jaw" tz squeeze_ctl 0 1.5
$boneflexdriver "jaw" tx wide_ctl -1 1

$animation "a_idle" "src/anims/face_idle.smd" fps 30 loop
$sequence "idle" "a_idle"
