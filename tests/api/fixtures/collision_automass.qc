// $collisionmodel with $automass
$modelname "api/collision_automass.mdl"
$cdmaterials "models/api"
$surfaceprop "wood"

$body "crate" "src/crate.smd"

$sequence "idle" "src/crate.smd" fps 30

$collisionmodel "src/phys_box.smd"
{
	$automass
}
