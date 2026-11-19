// $collisionmodel of the model's own SMD
$modelname "api/collision_mesh.mdl"
$cdmaterials "models/api"
$surfaceprop "wood"

$body "crate" "src/crate.smd"

$sequence "idle" "src/crate.smd" fps 30

$collisionmodel "src/crate.smd"
{
	$mass 50
}
