// Reference + 10 frame idle + 25 frame walk with LX LY motion extraction, $animation + $sequence pairs
$modelname "api/anims.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"

$body "biped" "src/biped.smd"

$animation "a_idle" "src/anims/biped_idle.smd" fps 15 loop
$animation "@walk" "src/anims/biped_walk.smd" fps 30 loop LX LY
$animation "@ref" "src/biped.smd" fps 30

$sequence "idle" "a_idle" fadein 0.2 fadeout 0.3 snap
$sequence "walk" "@walk"
$sequence "ref" "@ref"
