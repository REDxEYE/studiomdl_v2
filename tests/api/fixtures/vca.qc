// Vertex cache animation: $model vcafile (the frames of a VTA played by one n-way controller)
$modelname "api/vca.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"

$model "face" "src/face.smd" {
	vcafile "src/face.vta"
}

$animation "a_idle" "src/anims/face_idle.smd" fps 30 loop
$sequence "idle" "a_idle"
