// 3-bone chain tube (+ end bone) with 1/2/3/4 weights per vertex, bone rotations and offsets
$modelname "api/skinned.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"

$body "tube" "src/chain_ref.smd"

$sequence "idle" "src/chain_ref.smd" fps 30
$sequence "wiggle" "src/anims/chain_idle.smd" fps 30 loop
