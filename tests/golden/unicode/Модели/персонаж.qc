// Cyrillic input paths: QC, $include, SMD, VTA, DMX and a $pushd directory (the output name is ASCII)
$include "общее.qci"

$model "Body" "исходники/тело.smd" {
    flexfile "исходники/тело.vta" {
        defaultflex frame 0
        flex "Bulge" frame 1
    }
    flexcontroller default range 0 1 "Bulge"
    %Bulge = Bulge
}

$bodygroup "hat" {
    blank
    studio "исходники/шляпа.dmx"
}

$sequence "idle" "исходники/тело.smd" fps 30
$pushd "исходники"
$sequence "wave" "анимации/движение.smd" fps 30 loop
$popd
