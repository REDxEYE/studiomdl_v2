// $body + two $bodygroups (2 studios; studio + blank) + $bodygrouppreset + 2-family $texturegroup
$modelname "api/bodygroups.mdl"
$cdmaterials "models/api"
$surfaceprop "flesh"

$body "base" "src/bg_base.smd"

$bodygroup "hat"
{
	studio "src/bg_hat.smd"
	studio "src/bg_hat_tall.smd"
}

$bodygroup "gloves"
{
	studio "src/bg_gloves.smd"
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

$sequence "idle" "src/bg_base.smd" fps 30
