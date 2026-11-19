// Core API test: bone collapsing. core_bones_extra.smd has the bones root, mid, extra_a, extra_b (below extra_a) and
// side; the vertices use mid and extra_a. The compiler only collapses bones that have no vertex use anywhere below
// them, and the bones table of the compile only holds bones something below uses, so for this model $collapsebones and
// $alwayscollapse leave the output as it is (the test checks that the options reach the compile unchanged).
$modelname "api/core_bones_collapse.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"
$includemodel "api/none.mdl"

$collapsebones
$alwayscollapse "side"

$body "biped" "src/core_bones_extra.smd"
