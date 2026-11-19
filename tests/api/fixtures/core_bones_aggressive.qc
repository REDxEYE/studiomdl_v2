// Core API test: $collapsebonesaggressive with $unlockdefinebones, which comes before the $definebone it changes
// (the others say `locked` themselves).
$modelname "api/core_bones_aggressive.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"
$includemodel "api/none.mdl"

$unlockdefinebones
$collapsebonesaggressive
$definebone "root" "" locked 0 0 0 0 90 0
$definebone "pelvis" "root" locked 0 0 34 0 0 0
$definebone "spine" "pelvis" locked 0 0 10 0 0 30
$definebone "l_arm" "spine" 0 8 14 0 0 -90

$body "biped" "src/biped.smd"
