// $collisionjoints with the joint properties, collide pairs, no self collisions, animated friction
$modelname "api/collision_joints_full.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"

$body "ragdoll" "src/ragdoll_ref.smd"

$sequence "idle" "src/anims/ragdoll_idle.smd" fps 30 loop

$collisionjoints "src/ragdoll_phys.smd"
{
	$automass
	$inertia 4
	$damping 0.1
	$rotdamping 0.2
	$drag 0.05
	$maxconvexpieces 8
	$weldposition 0.03
	$concaveperjoint
	$convexhullcountoverride 1
	$noselfcollisions
	$rootbone "pelvis"
	$animatedfriction 10 100 0.5 1.0 0.5
	$jointinertia "spine" 2
	$jointdamping "skull" 0.3
	$jointrotdamping "spine" 0.4
	$jointmassbias "pelvis" 2
	$jointconstrain "spine" x limit -20 20 0.1
	$jointconstrain "spine" y free 0 0 0.2
	$jointconstrain "spine" z fixed 0 0 0
	$jointconstrain "skull" x limit -10 10
	$jointcollide "pelvis" "skull"
	$jointcollidealltoall
	{
		"pelvis"
		"spine"
		"skull"
	}
	$jointnocollide "spine" "skull"
}
