// $lod with replacemodel (fewer triangles, same skeleton), removemesh, $shadowlod
$modelname "api/lod.mdl"
$cdmaterials "models/api"
$surfaceprop "metal"

$body "lod_body" "src/lod_hi.smd"

$lod 20
{
	replacemodel "src/lod_hi" "src/lod_mid.smd"
}

$lod 50
{
	replacemodel "src/lod_hi" "src/lod_low.smd"
	removemesh "lod_trim"
}

$shadowlod
{
	replacemodel "src/lod_hi" "src/lod_shadow.smd"
}

$sequence "idle" "src/lod_hi.smd" fps 30
