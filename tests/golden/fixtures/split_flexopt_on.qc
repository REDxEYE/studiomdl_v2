// Mesh over $maxverts with a flex on one corner: vertex-count split, flex optimization on
// sfm_diff: skip (flex optimization intentionally changes the model; SFM has no equivalent)
$modelname "golden/split_flexopt_on.mdl"
$cdmaterials "models/golden"
$maxverts 2000 1500
$flexoptimize on

$model "Big" "src/Big.dmx"
$sequence "idle" "src/Big.dmx" fps 30
