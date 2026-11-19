// Core API test: $appendsource with an offset (merges a second copy of the crate into the first model's source)
$modelname "api/core_appendsource.mdl"
$cdmaterials "models/api"
$surfaceprop "wood"
$staticprop

$body "crate" "src/crate"
$appendsource "src/crate" "offset pos[ 20.0 0.0 0.0 ] angle[ 0.0 0.0 90.0 ] scale[ 1.0 ]"

$sequence "BindPose" "src/crate.smd"
