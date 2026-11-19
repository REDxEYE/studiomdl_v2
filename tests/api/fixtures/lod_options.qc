// $lod options: replacebone, bonetreecollapse, replacematerial, removemesh (mesh present), nofacial, use_shadowlod_materials
$modelname "api/lod_options.mdl"
$cdmaterials "models/api"
$surfaceprop "metal"

$body "lod_body" "src/lod_hi.smd"

$lod 15
{
	replacemodel "src/lod_hi" "src/lod_mid.smd" reverse
	replacebone "tip" "mid"
	replacematerial "lod_skin" "lod_skin_far"
	removemesh "lod_trim"
	nofacial
}

$lod 40
{
	replacemodel "src/lod_hi" "src/lod_low.smd"
	bonetreecollapse "mid"
	facial
}

$shadowlod
{
	replacemodel "src/lod_hi" "src/lod_shadow.smd"
	use_shadowlod_materials
}

$sequence "idle" "src/lod_hi.smd" fps 30
