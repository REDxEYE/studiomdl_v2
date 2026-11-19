// $lod with replacemodel and removemodel of a second body
$modelname "api/lod_models.mdl"
$cdmaterials "models/api"
$surfaceprop "metal"

$body "lod_body" "src/lod_hi.smd"
$body "lod_extra" "src/lod_low.smd"

$lod 25
{
	replacemodel "src/lod_hi" "src/lod_mid.smd"
	removemodel "src/lod_low"
}

$sequence "idle" "src/lod_hi.smd" fps 30
