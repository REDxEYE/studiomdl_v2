//
// Created by red_eye on 11/2/26.
//

#pragma once

#define SRC_FILE_EXT ".qc"

#define IDSTUDIOHEADER            (('T'<<24)+('S'<<16)+('D'<<8)+'I')
// little-endian "IDST"
#define IDSTUDIOANIMGROUPHEADER    (('G'<<24)+('A'<<16)+('D'<<8)+'I')
#include <cstdint>
// little-endian "IDAG"


const uint32_t MAXSTUDIOANIMFRAMES = 5000; // max frames per animation
// [mlowrance] updated total number of animations to give more headroom for new weapons
// bumped up from 2k to 3k
const uint32_t MAXSTUDIOANIMS = 3000; // total animations
const uint32_t MAXSTUDIOSEQUENCES = 1524; // total sequences
const uint32_t MAXSTUDIOSRCBONES = 1024u; // bones allowed at source movement
const uint32_t MAXSTUDIOTEXCOORDS = 8;
const uint32_t MAXSTUDIOMESHES = 256;
const uint32_t MAXSTUDIOEVENTS = 1024;
const uint32_t MAXSTUDIOFLEXKEYS = 512;
const uint32_t MAXSTUDIOFLEXRULES = 1024;
const uint32_t MAXSTUDIOBONEWEIGHTS = 3u; // influences per vertex in the .vvd (the model's limit)
const uint32_t MAXSTUDIOSOURCEBONEWEIGHTS = 16u;
// influences per vertex a source keeps until FitSourceWeights reduces them
const uint32_t MAXSTUDIOCMDS = 64;
const uint32_t MAXSTUDIOMOVEKEYS = 64;
const uint32_t MAXSTUDIOIKRULES = 64;
const uint32_t MAXSTUDIONAME = 128;
const uint32_t MAXSTUDIOACTIVITYMODIFIERS = 128;
const uint32_t MAXSTUDIOTAGS = 1024;

const uint32_t MAXSTUDIOSRCVERTS = (8 * 65536 * 8);

const uint32_t MAXSTUDIOTRIANGLES = 65536; // TODO: tune this
const uint32_t MAXSTUDIOVERTS = 65536; // TODO: tune this

// max number of verts that can be flexed per model (the engine flex cache addresses 65535 verts)
const uint32_t MAXSTUDIOFLEXVERTS = (MAXSTUDIOVERTS - 1);


const uint32_t MAXSTUDIOSKINS = 128; // total textures (compiler side limit; SFM renders models with more than 32)
const uint32_t MAXSTUDIOBONES = 256; // total bones actually used
const uint32_t MAXSTUDIOFLEXDESC = 1024; // maximum number of low level flexes (actual morph targets)
const uint32_t MAXSTUDIOFLEXCTRL = 512; // maximum number of flexcontrollers (input sliders)
const uint32_t MAXSTUDIOPOSEPARAM = 24;
const uint32_t MAXSTUDIOBONECTRLS = 4;
const uint32_t MAXSTUDIOANIMBLOCKS = 256;

const uint32_t MAXSTUDIOBONEBITS = 8; // NOTE: MUST MATCH MAXSTUDIOBONES

// NOTE!!! : Changing this number also changes the vtx file format!!!!!
const uint32_t MAX_NUM_BONES_PER_VERT = 3;
