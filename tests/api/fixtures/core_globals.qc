// Core API test: model globals. Commands that come before the sources they affect ($scale, $origin, $upaxis).
$modelname "api/core_globals.mdl"
$internalname "api/core_globals_internal"
$cdmaterials "models/api"
$cdmaterials "models/api/extra"
$scale 1.5
$origin 1 2 3 10
$surfaceprop "metal"
$contents grate ladder
$jointsurfaceprop "root" "glass"
$jointcontents "root" monster
$ambientboost
$noforcedfade
$donotcastshadows
$casttextureshadows
$constantdirectionallight 0.5
$obsolete
$mostlyopaque
$eyeposition 1 2 3
$illumposition 4 5 6
$maxeyedeflection 40
$gamma 2.0
$bbox -10 -10 0 10 10 20
$cbox -5 -5 0 5 5 10
$bboxonlyverts
$skipboneinbbox
$collisionprecision 0.5
$keyvalues
{
	prop_data
	{
		base Wooden.Large
	}
}
$keyvalues
{
	second
	{
		key value
	}
}
$preservetriangleorder
$cliptotextures
$defaultfadein 0.2
$defaultfadeout 0.3
$motionrollback 0.5
$clampworldspace -1000 -1000 -1000 1000 1000 1000
$sectionframes 20 10
$allowactivityname "ACT_CORE_TEST"
$includemodel "api/none.mdl"

$body "crate" "src/crate"
