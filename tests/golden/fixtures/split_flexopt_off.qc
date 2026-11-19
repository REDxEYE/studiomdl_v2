// Mesh over $maxverts with a flex on one corner: vertex-count split, flex optimization off
$modelname "golden/split_flexopt_off.mdl"
$cdmaterials "models/golden"
$maxverts 2000 1500
$flexoptimize off

$model "Big" "src/Big.dmx"
$sequence "idle" "src/Big.dmx" fps 30
