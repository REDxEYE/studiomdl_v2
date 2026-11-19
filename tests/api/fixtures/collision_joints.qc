// Ragdoll: $collisionjoints with a 3-bone physics SMD, $mass, $jointconstrain, $rootbone
$modelname "api/collision_joints.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"

$body "ragdoll" "src/ragdoll_ref.smd"

$sequence "idle" "src/anims/ragdoll_idle.smd" fps 30 loop

$collisionjoints "src/ragdoll_phys.smd"
{
	$mass 40
	$rootbone "pelvis"
	$jointconstrain "spine" x limit -20 20 0.1
	$jointconstrain "spine" y limit -30 30 0.1
	$jointconstrain "spine" z fixed 0 0 0
	$jointconstrain "skull" x limit -40 40 0.2
	$jointconstrain "skull" y free 0 0 0.2
	$jointconstrain "skull" z limit -45 45 0.2
	$jointmassbias "skull" 1.5
	$jointdamping "spine" 0.2
}
