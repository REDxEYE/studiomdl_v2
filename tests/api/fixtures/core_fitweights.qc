// Core API test: 5-bone chain, up to 5 weights per vertex with real tails (-fitweights writes the fit).
$modelname "api/core_fitweights.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"
$includemodel "api/none.mdl"

$body "tube" "src/fit_ref"
