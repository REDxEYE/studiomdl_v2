// Core API test: per-bone options in the order the builder applies them ($controller, $screenalign, $worldalign,
// $bonemerge, $bonealwayssetup, $limitrotation, $bonesaveframe) plus $jointsurfaceprop / $jointcontents.
$modelname "api/core_bones_flags.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"
$jointsurfaceprop "head" "metal"
$jointsurfaceprop "l_arm" "glass"
$jointcontents "head" grate
$jointcontents "r_arm" monster
$includemodel "api/none.mdl"

$controller 0 "head" YR -30 30
$controller 1 "head" XR -20 20
$controller mouth "l_arm" Z 0 4
$controller 2 "r_arm" ZR 0 360
$screenalign "head" sphere
$screenalign "l_arm" cylinder
$worldalign "l_foot"
$bonemerge "spine"
$bonemerge "head"
$bonealwayssetup "r_foot"
$limitrotation "l_shin"
$limitrotation "r_shin" "idle" "walk"
$bonesaveframe "root" position rotation
$bonesaveframe "pelvis" rotation64
$bonesaveframe "spine"

$body "biped" "src/biped.smd"
