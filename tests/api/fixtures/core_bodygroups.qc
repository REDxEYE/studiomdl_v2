// Core API test: $body + two $bodygroups (studio + studio, studio + blank), presets, a 2-family $texturegroup
$modelname "api/core_bodygroups.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"
$includemodel "api/none.mdl"

$body "base" "src/bg_base"

$bodygroup "hat"
{
	studio "src/bg_hat"
	studio "src/bg_hat_tall"
}

$bodygroup "gloves"
{
	studio "src/bg_gloves"
	blank
}

$texturegroup "skins"
{
	{ "bg_body" "bg_cloth" "bg_glove" }
	{ "bg_body_alt" "bg_cloth_alt" "bg_glove_alt" }
}

$bodygrouppreset "formal"
{
	"hat" 1
	"gloves" 0
}

$bodygrouppreset "casual"
{
	"hat" 0
	"gloves" 1
}
