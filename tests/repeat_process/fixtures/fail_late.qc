// Error path late in the compile: the source is loaded and the sequence parsed, SimplifyModel then finds the bad bone
$modelname "repeat/fail_late.mdl"
$cdmaterials "models/golden"
$model "Body" "../../golden/fixtures/src/Body.dmx"
$sequence "idle" "../../golden/fixtures/src/Body.dmx" fps 30
$hierarchy "no_such_child" "no_such_parent"
