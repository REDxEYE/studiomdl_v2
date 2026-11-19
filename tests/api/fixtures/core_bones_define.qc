// Core API test: bone processing options and $definebone, issued in the order the builder applies them
// ($realignbones, $lockbonelengths, $root, $definebone x5 parents first, $forcerealign). biped.smd has 12 bones,
// all weighted. The angles are degrees whose radians convert back exactly (the API takes radians).
$modelname "api/core_bones_define.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"
$includemodel "api/none.mdl"

$realignbones
$lockbonelengths
$root "root"
$definebone "root" "" 0 0 0 0 90 0
$definebone "pelvis" "root" 0 0 34 0 0 0
$definebone "spine" "pelvis" 0 0 10 0 0 15
$definebone "head" "spine" locked 0 0 18 -30 0 0 0 0 18 -15 0 0
$definebone "l_arm" "spine" unlocked 0 8 14 0 0 -90
$forcerealign "head" rotate 15 0 0

$body "biped" "src/biped.smd"
