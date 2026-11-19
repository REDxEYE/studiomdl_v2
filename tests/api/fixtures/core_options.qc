// Core API test: the options of studio: reverse (flip triangles) and scale
$modelname "api/core_options.mdl"
$cdmaterials "models/api"
$surfaceprop "wood"
$staticprop

$body "crate" "src/crate" reverse scale 2.0

$sequence "BindPose" "src/crate.smd"
