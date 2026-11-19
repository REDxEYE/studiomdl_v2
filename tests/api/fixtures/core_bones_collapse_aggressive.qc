// Core API test: $collapsebonesaggressive filters bones by their own use: "root" has no vertex of its own (the
// vertices use mid and extra_a), so it is collapsed and mid becomes the root of the model.
$modelname "api/core_bones_collapse_aggressive.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"
$includemodel "api/none.mdl"

$collapsebonesaggressive

$body "biped" "src/core_bones_extra.smd"
