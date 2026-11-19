// Core API test: $overridematerial after the body
$modelname "api/core_override.mdl"
$cdmaterials "models/api"
$surfaceprop "wood"
$staticprop

$body "crate" "src/crate"
$overridematerial "crate_override"

$sequence "BindPose" "src/crate.smd"
