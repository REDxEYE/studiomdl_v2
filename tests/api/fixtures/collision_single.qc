// Prop with a single convex bone-parented $collisionmodel and $mass
$modelname "api/collision_single.mdl"
$cdmaterials "models/api"
$surfaceprop "wood"

$body "crate" "src/crate.smd"

$sequence "idle" "src/crate.smd" fps 30

$collisionmodel "src/phys_box.smd"
{
	$mass 50
	$inertia 3
	$damping 0.1
	$rotdamping 1.5
}
