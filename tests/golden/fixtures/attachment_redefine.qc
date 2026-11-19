// $redefineattachment replaces an earlier $attachment in place (same slot, so "first" stays attachment 0).
// SFM's studiomdl has no $redefineattachment.
// sfm_diff: skip
$modelname "golden/attachment_redefine.mdl"
$cdmaterials "models/golden"

$model "Body" "src/Body.dmx"

$attachment "first" "root" 1 1 1
$attachment "second" "spine" 0 0 1 rotate 0 0 45
$redefineattachment "first" "head" 0 -2 0 rotate 0 90 0

$sequence "idle" "src/Body.dmx" fps 30
