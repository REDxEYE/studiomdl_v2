// Binary DMX (Blender export, old SourceIO test file), backslash in $modelname
$modelname "golden\suzanne.mdl"

$model "Suzanne" "src/Suzanne"


$surfaceprop "default"

$contents "solid"


$mostlyopaque

$sequence "idle" {
	"src/Suzanne"
	fadein 0.2
	fadeout 0.2
	fps 30
}
$sequence "aaa" {
	"src/anims/ArmatureAction"
	fadein 0.2
	fadeout 0.2
	fps 30
}
