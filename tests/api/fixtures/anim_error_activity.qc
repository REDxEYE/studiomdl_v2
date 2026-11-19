// FAILS ON PURPOSE (zero activity weight): the error must carry the QC file and line
$modelname "api/anim_error_activity.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"

$body "biped" "src/biped.smd"

$sequence "idle" "src/biped.smd" fps 30
$sequence "walk" "src/biped.smd" fps 30 activity ACT_WALK 0
