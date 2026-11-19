// Error path: a missing source file must abort with exit code 255 and leave no output
$modelname "golden/missing_source.mdl"
$model "Body" "src/does_not_exist.dmx"
$sequence "idle" "src/does_not_exist.dmx"
