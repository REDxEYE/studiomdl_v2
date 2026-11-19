// $collisionmodel blank with two $addconvexsrc (offset, concave)
$modelname "api/collision_convex.mdl"
$cdmaterials "models/api"
$surfaceprop "wood"

$body "crate" "src/crate.smd"

$sequence "idle" "src/crate.smd" fps 30

$collisionmodel blank
{
	$mass 30
	$addconvexsrc "src/phys_box"
	$addconvexsrc "src/phys_box" "offset pos[ 0 0 20 ] angle[ 0 45 0 ] scale[ 0.5 ]" concave
}
