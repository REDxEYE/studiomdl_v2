// DMX character: two materials, skins, bodygroup with blank, LOD, flexes (incl. a corrective), bone animation, attachments
$modelname "golden/character.mdl"
$cdmaterials "models/golden"
$surfaceprop "flesh"

$model "Body" "src/Body.dmx"
$bodygroup "hat" {
    blank
    studio "src/Hat.dmx"
}

$texturegroup "skinfamilies" {
    { "body" "body2" }
    { "body_alt" "body2_alt" }
}

$lod 20 {
    replacemodel "src/Body" "src/Body_lod1.dmx"
}

// attachments: every $attachment option (SFM studiomdl knows them all; $redefineattachment: attachment_redefine.qc)
$attachment "head_tip" "head" 0 0 4 rotate 0 0 0
$attachment "head_rot" "head" 1 2 3 rotate 10 20 30
$attachment "spine_abs" "spine" 0 5 0 absolute
$attachment "root_rigid" "root" 0 0 1 rigid world_align
$attachment "spine_axes" "spine" 0 0 1 x_and_z_axes 0 1 0 1 0 1

$sequence "idle" "src/Body.dmx" fps 30
$sequence "wave" "src/anims/Rig.dmx" fps 30 loop
