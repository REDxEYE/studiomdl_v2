//
// Created by red_eye on 11/2/26.
//

#pragma once

#define SRC_FILE_EXT ".qc"

#define IDSTUDIOHEADER            (('T'<<24)+('S'<<16)+('D'<<8)+'I')
// little-endian "IDST"
#define IDSTUDIOANIMGROUPHEADER    (('G'<<24)+('A'<<16)+('D'<<8)+'I')
// little-endian "IDAG"



#define MAXSTUDIOANIMFRAMES        5000    // max frames per animation
// [mlowrance] updated total number of animations to give more headroom for new weapons
// bumped up from 2k to 3k
#define MAXSTUDIOANIMS            3000    // total animations
#define MAXSTUDIOSEQUENCES        1524    // total sequences
#define MAXSTUDIOSRCBONES        1024u        // bones allowed at source movement
#define MAXSTUDIOTEXCOORDS        8
#define MAXSTUDIOMESHES            256
#define MAXSTUDIOEVENTS            1024
#define MAXSTUDIOFLEXKEYS        512
#define MAXSTUDIOFLEXRULES        1024
#define MAXSTUDIOBONEWEIGHTS    3u
#define MAXSTUDIOCMDS            64
#define MAXSTUDIOMOVEKEYS        64
#define MAXSTUDIOIKRULES        64
#define MAXSTUDIONAME            128
#define MAXSTUDIOACTIVITYMODIFIERS    128
#define MAXSTUDIOTAGS            1024

#define MAXSTUDIOSRCVERTS        (8*65536*8)