// Core API test: a second source whose bind pose differs from the model skeleton (rest pose override of the geometry)
$modelname "api/core_restpose.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"
$includemodel "api/none.mdl"

$body "tube" "src/chain_ref"

$bodygroup "alt"
{
	studio "src/core_chain_rest"
}
