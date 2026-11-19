// $collisionjoints with $jointmerge
$modelname "api/collision_joints_merge.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"

$body "ragdoll" "src/ragdoll_ref.smd"

$sequence "idle" "src/anims/ragdoll_idle.smd" fps 30 loop

$collisionjoints "src/ragdoll_phys.smd"
{
	$mass 40
	$jointmerge "spine" "skull"
	$jointconstrain "spine" x limit -20 20 0.1
}
