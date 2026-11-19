// Core API test: unskinned static prop. The builder has no sequence for it: ApplyFinish adds the default sequence
// "BindPose" of a static prop (the one SetupStaticProp creates), here written out. Mesh name = the studio path.
$modelname "api/core_prop_static.mdl"
$cdmaterials "models/api"
$surfaceprop "wood"
$staticprop

$body "crate" "src/crate"

$sequence "BindPose" "src/crate.smd"
