// SMD reference + old-style VTA flexes
$modelname "golden/smd_vta.mdl"
$cdmaterials "models/golden"

$model "Body" "src/smd/Body.smd" {
    flexfile "src/smd/Body.vta" {
        defaultflex frame 0
        flex "Bulge" frame 1
        flex "Pinch" frame 2
    }
    flexcontroller default range 0 1 "Bulge" "Pinch"
    %Bulge = Bulge
    %Pinch = Pinch
}

$sequence "idle" "src/smd/Body.smd" fps 30
$sequence "wave" "src/smd/anims/Rig.smd" fps 30
