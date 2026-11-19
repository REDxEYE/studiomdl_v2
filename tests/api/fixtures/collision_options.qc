// $collisionmodel with every whole-model option
$modelname "api/collision_options.mdl"
$cdmaterials "models/api"
$surfaceprop "wood"

$body "crate" "src/crate.smd"

$sequence "idle" "src/crate.smd" fps 30

$collisionmodel "src/phys_box.smd"
{
	$mass 25
	$inertia 2.5
	$damping 0.2
	$rotdamping 0.7
	$drag 0.3
	$rollingDrag 0.5
	$maxconvexpieces 4
	$remove2d
	$weldposition 0.02
	$weldnormal 0.95
	$concave
	$convexhullcountoverride 1
	$masscenter 0 0 8
	$assumeworldspace
}
