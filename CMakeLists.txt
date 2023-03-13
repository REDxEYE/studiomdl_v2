cmake_minimum_required(VERSION 3.22)
project(studiomdl_v2)

set(CMAKE_CXX_STANDARD 14)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLATFORM_DEFINES COMPILER_MSVC64 COMPILER_MSVC)
elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PLATFORM_DEFINES COMPILER_MSVC32 COMPILER_MSVC _X86_)
endif ()

add_compile_definitions(${PLATFORM_DEFINES} RAD_TELEMETRY_DISABLED _CRT_SECURE_NO_WARNINGS USE_MEM_DEBUG=0 _DLL_EXT=.dll)

add_library(interfaces
        libs/interfaces/interfaces.cpp
        )
target_include_directories(interfaces PUBLIC include)
target_include_directories(interfaces PUBLIC include/interfaces)
target_compile_definitions(interfaces PUBLIC -DSTATIC_LINK -DSTATIC_TIER0)

add_library(nvtristriplib
        libs/nvtristriplib/nvtristrip.cpp
        libs/nvtristriplib/nvtristripobjects.cpp
        )
target_include_directories(nvtristriplib PUBLIC include)
target_include_directories(nvtristriplib PUBLIC libs/nvtristriplib)
target_compile_definitions(nvtristriplib PUBLIC)

add_library(fbxutils
        libs/fbxutils/dmfbxserializer.cpp
        )
target_include_directories(fbxutils PUBLIC include)
target_include_directories(fbxutils PUBLIC include/fbxutils include/fbx_sdk)
target_compile_definitions(fbxutils PUBLIC)


add_library(tier0_static
        libs/tier0/platform.cpp
        libs/tier0/perfstats.cpp
        libs/tier0/pch_tier0.cpp
        libs/tier0/mem.cpp
        libs/tier0/vprof.cpp
        libs/tier0/logging.cpp
        libs/tier0/dbg.cpp
        libs/tier0/assert_dialog.cpp
        libs/tier0/threadtools.cpp
        libs/tier0/memstd.cpp
        libs/tier0/mem_helpers.cpp
        libs/tier0/tier0_strtools.cpp
        libs/tier0/commandline.cpp
        libs/tier0/stackstats.cpp
        libs/tier0/stacktools.cpp
        libs/tier0/tslist.cpp
        libs/tier0/fasttimer.cpp
        libs/tier0/minidump.cpp
        libs/tier0/dynfunction.cpp
        libs/tier0/memprocessheap.cpp
        libs/tier0/memvirt.cpp
        libs/tier0/cpu.cpp
        libs/tier0/platwindow.cpp
        libs/tier0/pme.cpp
        libs/tier0/pmelib.cpp
        libs/tier0/cputopology.cpp
        libs/tier0/dlmalloc/malloc.cpp
        )
target_include_directories(tier0_static PUBLIC include)
target_include_directories(tier0_static PUBLIC include/tier0)
target_compile_definitions(tier0_static PRIVATE -DTIER0_DLL_EXPORT -DSTATIC_LINK -DSTATIC_TIER0)

add_library(vstdlib_static
        libs/vstdlib/coroutine.cpp
        libs/vstdlib/cvar.cpp
        libs/vstdlib/eventsystem.cpp
        libs/vstdlib/keyvaluessystem.cpp
        libs/vstdlib/random.cpp
        libs/vstdlib/vcover.cpp
        libs/vstdlib/vstrtools.cpp
        libs/vstdlib/jobthread.cpp
        libs/vstdlib/vstdlib_strtools.cpp
        )

add_library(vstdlib_v2
        SHARED
        libs/vstdlib/cvar.cpp
        libs/vstdlib/eventsystem.cpp
        libs/vstdlib/keyvaluessystem.cpp
        libs/vstdlib/random.cpp
        libs/vstdlib/vcover.cpp
        libs/vstdlib/vstrtools.cpp
        libs/vstdlib/jobthread.cpp
        libs/vstdlib/vstdlib_strtools.cpp
        libs/vstdlib/processutils.cpp
        )

target_include_directories(vstdlib_static PUBLIC include)
target_include_directories(vstdlib_static PUBLIC include/vstdlib)
target_compile_definitions(vstdlib_static PRIVATE -DVSTDLIB_DLL_EXPORT -DSTATIC_TIER0)
target_link_libraries(vstdlib_static PUBLIC tier0_static)

target_include_directories(vstdlib_v2 PUBLIC include)
target_include_directories(vstdlib_v2 PUBLIC include/vstdlib)
target_compile_definitions(vstdlib_v2 PRIVATE -DVSTDLIB_DLL_EXPORT -DSTATIC_TIER0)
target_link_libraries(vstdlib_v2 PUBLIC tier0_static tier1)


add_library(tier1
        libs/tier1/byteswap.cpp
        libs/tier1/exprevaluator.cpp
        libs/tier1/characterset.cpp
        libs/tier1/checksum_crc.cpp
        libs/tier1/checksum_md5.cpp
        libs/tier1/checksum_sha1.cpp
        libs/tier1/circularbuffer.cpp
        libs/tier1/commandbuffer.cpp
        libs/tier1/convar.cpp
        libs/tier1/datamanager.cpp
        libs/tier1/fileio.cpp
        libs/tier1/generichash.cpp
        libs/tier1/interface.cpp
        libs/tier1/keyvalues.cpp
        libs/tier1/keyvaluesjson.cpp
        libs/tier1/kvpacker.cpp
        libs/tier1/lzmaDecoder.cpp
        libs/tier1/lzss.cpp
        libs/tier1/sparsematrix.cpp
        libs/tier1/splitstring.cpp
        libs/tier1/stringpool.cpp
        libs/tier1/strtools.cpp
        libs/tier1/strtools_unicode.cpp
        libs/tier1/tier1.cpp
        libs/tier1/tier1_logging.cpp
        libs/tier1/timeutils.cpp
        libs/tier1/undiff.cpp
        libs/tier1/memstack.cpp
        libs/tier1/mempool.cpp
        libs/tier1/uniqueid.cpp
        libs/tier1/utlbuffer.cpp
        libs/tier1/utlbufferutil.cpp
        libs/tier1/utlsoacontainer.cpp
        libs/tier1/utlstring.cpp
        libs/tier1/utlstringtoken.cpp
        libs/tier1/utlsymbol.cpp
        )
target_include_directories(tier1 PUBLIC include)
target_include_directories(tier1 PUBLIC include/tier1)
target_compile_definitions(tier1 PRIVATE -DSTATIC_TIER0)
target_link_libraries(tier1 PUBLIC tier0_static interfaces vstdlib_static mathlib rpcrt4)

add_library(tier2
        libs/tier2/beamsegdraw.cpp
        libs/tier2/defaultfilesystem.cpp
        libs/tier2/fileutils.cpp
        libs/tier2/interval.cpp
        libs/tier2/keybindings.cpp
        libs/tier2/meshutils.cpp
        libs/tier2/resourceprecacher.cpp
        libs/tier2/riff.cpp
        libs/tier2/schemaclassbinding.cpp
        libs/tier2/socketcreator.cpp
        libs/tier2/soundutils.cpp
        libs/tier2/tier2.cpp
        libs/tier2/tier2_logging.cpp
        libs/tier2/tokenreader.cpp
        libs/tier2/util_init.cpp
        libs/tier2/utlstreambuffer.cpp
        libs/tier2/vconfig.cpp
        )
target_include_directories(tier2 PUBLIC include)
target_include_directories(tier2 PUBLIC include/tier2)
target_compile_definitions(tier2 PRIVATE -DSTATIC_TIER0)
target_link_libraries(tier2 PUBLIC tier0_static tier1)

add_library(tier3
        libs/tier3/mdlutils.cpp
        libs/tier3/tier3.cpp
        libs/tier3/studiohdrstub.cpp
        )
target_include_directories(tier3 PUBLIC include)
target_include_directories(tier3 PUBLIC include/tier3)
target_compile_definitions(tier3 PRIVATE -DSTATIC_TIER0)
target_link_libraries(tier3 PUBLIC tier0_static tier1 tier2 materialsystem)

add_library(bitmap
        libs/bitmap/bitmap.cpp
        libs/bitmap/floatbitmap.cpp
        libs/bitmap/floatbitmap2.cpp
        libs/bitmap/floatbitmap3.cpp
        libs/bitmap/floatbitmap4.cpp
        libs/bitmap/floatbitmap_bilateralfilter.cpp
        libs/bitmap/ImageByteSwap.cpp
        libs/bitmap/imageformat.cpp
        libs/bitmap/psd.cpp
        libs/bitmap/resample.cpp
        libs/bitmap/texturepacker.cpp
        libs/bitmap/tgaloader.cpp
        libs/bitmap/tgawriter.cpp
        )
target_include_directories(bitmap PUBLIC include)
target_include_directories(bitmap PUBLIC include/bitmap)
target_compile_definitions(bitmap PRIVATE -DSTATIC_TIER0)
target_link_libraries(bitmap PUBLIC)

add_library(materialsystem
        libs/materialsystem/MaterialSystemUtil.cpp
        libs/materialsystem/cmaterial.cpp
        libs/materialsystem/ctexture.cpp
        libs/materialsystem/CColorCorrection.cpp
        libs/materialsystem/CMaterialSubRect.cpp
        libs/materialsystem/texturemanager.cpp
        libs/materialsystem/shadersystem.cpp
        libs/materialsystem/cmaterialdict.cpp
        libs/materialsystem/cmaterialsystem.cpp
        libs/materialsystem/cmaterialvar.cpp
        libs/materialsystem/colorspace.cpp
        libs/materialsystem/occlusionquerymgr.cpp
        libs/materialsystem/morph.cpp
        libs/materialsystem/cmatnullrendercontext.cpp
        libs/materialsystem/cmatrendercontext.cpp
        libs/materialsystem/cmatqueuedrendercontext.cpp
        libs/materialsystem/composite_texture.cpp
        libs/materialsystem/custom_material.cpp
        libs/materialsystem/cmatlightmaps.cpp
        libs/materialsystem/imagepacker.cpp
        libs/materialsystem/materialsystem_global.cpp
        )
target_include_directories(materialsystem PUBLIC include)
target_include_directories(materialsystem PUBLIC include/materialsystem)
target_compile_definitions(materialsystem PRIVATE -DSTATIC_TIER0)
target_link_libraries(materialsystem PUBLIC tier0_static tier1 tier2 interfaces bitmap)

add_library(mathlib
        libs/mathlib/sse.cpp
        libs/mathlib/feagglomerator.cpp
        libs/mathlib/disjoint_set_forest.cpp
        libs/mathlib/camera.cpp
        libs/mathlib/bumpvects.cpp
        libs/mathlib/imagequant.cpp
        libs/mathlib/capsule.cpp
        libs/mathlib/lightdesc.cpp
        libs/mathlib/almostequal.cpp
        libs/mathlib/ssenoise.cpp
        libs/mathlib/femodel.cpp
        libs/mathlib/polygon.cpp
        libs/mathlib/femodeldesc.cpp
        libs/mathlib/kdop.cpp
        libs/mathlib/sseconst.cpp
        libs/mathlib/planefit.cpp
        libs/mathlib/color_conversion.cpp
        libs/mathlib/simdvectormatrix.cpp
        libs/mathlib/polyhedron.cpp
        libs/mathlib/eigen.cpp
        libs/mathlib/powsse.cpp
        libs/mathlib/expressioncalculator.cpp
        libs/mathlib/box_buoyancy.cpp
        libs/mathlib/softbody.cpp
        libs/mathlib/sparse_convolution_noise.cpp
        libs/mathlib/svd.cpp
        libs/mathlib/anorms.cpp
        libs/mathlib/mathlib_base.cpp
        libs/mathlib/halton.cpp
        libs/mathlib/spherical.cpp
        libs/mathlib/randsse.cpp
        libs/mathlib/femodelbuilder.cpp
        libs/mathlib/IceKey.cpp
        libs/mathlib/cholesky.cpp
        libs/mathlib/transform.cpp
        libs/mathlib/dynamictree.cpp
        libs/mathlib/softbodyenvironment.cpp
        libs/mathlib/quantize.cpp
        libs/mathlib/simplex.cpp
        libs/mathlib/sphere.cpp
        libs/mathlib/vmatrix.cpp
        libs/mathlib/volumeculler.cpp
        )
target_include_directories(mathlib PUBLIC include)
target_include_directories(mathlib PRIVATE include/mathlib)
target_compile_definitions(mathlib PRIVATE -DSTATIC_TIER0)
target_link_libraries(mathlib PRIVATE tier0_static)

add_library(datamodel
        libs/datamodel/clipboardmanager.cpp
        libs/datamodel/datamodel.cpp
        libs/datamodel/dependencygraph.cpp
        libs/datamodel/dmattribute.cpp
        libs/datamodel/dmconnect.cpp
        libs/datamodel/dmelement.cpp
        libs/datamodel/dmelementdictionary.cpp
        libs/datamodel/dmelementfactoryhelper.cpp
        libs/datamodel/DmElementFramework.cpp
        libs/datamodel/dmserializerbinary.cpp
        libs/datamodel/dmserializerkeyvalues.cpp
        libs/datamodel/dmserializerkeyvalues2.cpp
        libs/datamodel/undomanager.cpp
        )
target_include_directories(datamodel PUBLIC include)
target_include_directories(datamodel PRIVATE include/datamodel)
target_compile_definitions(datamodel PRIVATE -DDATAMODEL_LIB -DSTATIC_TIER0)
target_link_libraries(datamodel PRIVATE)

add_library(movieobjects
        libs/movieobjects/dmmeshcomp.cpp
        libs/movieobjects/dmetransform.cpp
        libs/movieobjects/dmetrack.cpp
        libs/movieobjects/dmepackoperators.cpp
        libs/movieobjects/dmefaceset.cpp
        libs/movieobjects/dmemakefileutils.cpp
        libs/movieobjects/dmeusersettings.cpp
        libs/movieobjects/dmematerialoverlayfxclip.cpp
        libs/movieobjects/dmetrackgroup.cpp
        libs/movieobjects/dmegamemodel.cpp
        libs/movieobjects/dmechannel.cpp
        libs/movieobjects/movieobjects_interfaces.cpp
        libs/movieobjects/dmelog.cpp
        libs/movieobjects/dmemodel.cpp
        libs/movieobjects/dmelight.cpp
        libs/movieobjects/dmerigconstraintoperators.cpp
        libs/movieobjects/dmeaxissystem.cpp
        libs/movieobjects/dmecamera.cpp
        libs/movieobjects/dmetimeframe.cpp
        libs/movieobjects/dmecontrolgroup.cpp
        libs/movieobjects/dmeoperator.cpp
        libs/movieobjects/dmeunpackoperators.cpp
        libs/movieobjects/dmeconnectionoperator.cpp
        libs/movieobjects/dmecycle.cpp
        libs/movieobjects/dmeexpressionoperator.cpp
        libs/movieobjects/dmeoverlay.cpp
        libs/movieobjects/dmobjserializer.cpp
        libs/movieobjects/dmetransformoperator.cpp
        libs/movieobjects/dmeexporttags.cpp
        libs/movieobjects/dmemesh.cpp
        libs/movieobjects/dmerig.cpp
        libs/movieobjects/dmerighandle.cpp
        libs/movieobjects/dmephonememapping.cpp
        #        movieobjects/dmeshader.cpp
        libs/movieobjects/exportfacialanimation.cpp
        libs/movieobjects/dmetransformcontrol.cpp
        libs/movieobjects/dmeselection.cpp
        libs/movieobjects/dmeshape.cpp
        libs/movieobjects/dmecombinationoperator.cpp
        libs/movieobjects/dmeattributereference.cpp
        libs/movieobjects/dmedag.cpp
        libs/movieobjects/dmevertexdata.cpp
        libs/movieobjects/dmejoint.cpp
        libs/movieobjects/dmetimeselection.cpp
        libs/movieobjects/dmesound.cpp
        libs/movieobjects/dmeinput.cpp
        libs/movieobjects/dmemdlmakefile.cpp
        #        movieobjects/timeutils.cpp
        libs/movieobjects/dmematerial.cpp
        libs/movieobjects/dmemdl.cpp
        libs/movieobjects/dmemorphoperator.cpp
        libs/movieobjects/dmedccmakefile.cpp
        #        movieobjects/dmekeyboardinput.cpp
        libs/movieobjects/dmeanimationset.cpp
        #        movieobjects/dmemouseinput.cpp
        libs/movieobjects/dmemakefile.cpp
        libs/movieobjects/dmeclip.cpp
        libs/movieobjects/dmeflexrules.cpp
        libs/movieobjects/dmeeditortypedictionary.cpp
        libs/movieobjects/dmeparticlesystemdefinition.cpp
        libs/movieobjects/dmetransforminput.cpp
        libs/movieobjects/dmebookmark.cpp
        libs/movieobjects/dmedrawsettings.cpp
        libs/movieobjects/dmeattachment.cpp
        libs/movieobjects/dmetransformlist.cpp
        libs/movieobjects/dmeanimationlist.cpp
        )
target_include_directories(movieobjects PUBLIC include)
target_include_directories(movieobjects PRIVATE include/movieobjects)
target_compile_definitions(movieobjects PRIVATE -DMOVIEOBJECTS_LIB -DSTATIC_TIER0)
target_link_libraries(movieobjects PRIVATE datamodel)

add_library(dmserializers
        libs/dmserializers/importcommentary.cpp
        libs/dmserializers/importsfmv4.cpp
        libs/dmserializers/importmks.cpp
        libs/dmserializers/importsfmv5.cpp
        libs/dmserializers/dmserializers.cpp
        libs/dmserializers/importsfmv2.cpp
        libs/dmserializers/importsfmv6.cpp
        libs/dmserializers/importtex.cpp
        libs/dmserializers/importsfmv7.cpp
        libs/dmserializers/importsfmv8.cpp
        libs/dmserializers/importactbusy.cpp
        libs/dmserializers/dmebaseimporter.cpp
        libs/dmserializers/importkeyvaluebase.cpp
        libs/dmserializers/importvmf.cpp
        libs/dmserializers/importsfmv9.cpp
        libs/dmserializers/importsfmv3.cpp
        libs/dmserializers/importmks.cpp
        libs/dmserializers/importvmt.cpp
        libs/dmserializers/importsfmv1.cpp
        )
target_include_directories(dmserializers PUBLIC include)
target_include_directories(dmserializers PRIVATE include/dmserializers)
target_compile_definitions(dmserializers PRIVATE -DSTATIC_TIER0)
target_link_libraries(dmserializers PRIVATE)

add_library(mdlobjects
        libs/mdlobjects/dmehitboxset.cpp
        libs/mdlobjects/dmemdllist.cpp
        libs/mdlobjects/dmematerialgrouplist.cpp
        libs/mdlobjects/dmeelementgroup.cpp
        libs/mdlobjects/dmebodypart.cpp
        libs/mdlobjects/dmehitbox.cpp
        libs/mdlobjects/authphysfx.cpp
        libs/mdlobjects/dmeskinner.cpp
        libs/mdlobjects/dmemouth.cpp
        libs/mdlobjects/dmeanimblocksize.cpp
        libs/mdlobjects/dmemotioncontrol.cpp
        libs/mdlobjects/dmejigglebone.cpp
        libs/mdlobjects/dmeproceduralbone.cpp
        libs/mdlobjects/dmehitboxsetlist.cpp
        libs/mdlobjects/dmematsysroot.cpp
        libs/mdlobjects/dmeincludemodellist.cpp
        libs/mdlobjects/dmesequencelist.cpp
        libs/mdlobjects/dmeboneweight.cpp
        libs/mdlobjects/dmeasset.cpp
        libs/mdlobjects/mpp_utils.cpp
        libs/mdlobjects/dmebodygroup.cpp
        libs/mdlobjects/dmeblankbodypart.cpp
        libs/mdlobjects/dmeassemblycommand.cpp
        libs/mdlobjects/dmeposeparameter.cpp
        libs/mdlobjects/dmeeyeballglobals.cpp
        libs/mdlobjects/dmebonemask.cpp
        libs/mdlobjects/dmeik.cpp
        libs/mdlobjects/dmelod.cpp
        libs/mdlobjects/dmematerialgroup.cpp
        libs/mdlobjects/dmeanimcmd.cpp
        libs/mdlobjects/dmeboneflexdriver.cpp
        libs/mdlobjects/dmecollisionjoints.cpp
        libs/mdlobjects/dmebonemasklist.cpp
        libs/mdlobjects/dmeanimationassemblycommand.cpp
        libs/mdlobjects/dmeposeparameterlist.cpp
        libs/mdlobjects/dmelodlist.cpp
        libs/mdlobjects/dmecollisionmodel.cpp
        libs/mdlobjects/dmebbox.cpp
        libs/mdlobjects/physmodelsource.cpp
        libs/mdlobjects/dmesequence.cpp
        libs/mdlobjects/dmedefinebonelist.cpp
        libs/mdlobjects/dmebodygrouplist.cpp
        libs/mdlobjects/dmeeyelid.cpp
        libs/mdlobjects/dmeeyeball.cpp
        libs/mdlobjects/dmedefinebone.cpp
        )
target_include_directories(mdlobjects PUBLIC include)
target_include_directories(mdlobjects PRIVATE include/mdlobjects)
target_compile_definitions(mdlobjects PRIVATE -DSTATIC_TIER0)
target_link_libraries(mdlobjects PRIVATE)

add_library(dmeutils
        libs/dmeutils/savetodmx.cpp
        libs/dmeutils/dmanimutils.cpp
        libs/dmeutils/dmmeshutils.cpp
        libs/dmeutils/loadfromdmx.cpp
        )
target_include_directories(dmeutils PUBLIC include)
target_include_directories(dmeutils PRIVATE include/dmeutils)
target_compile_definitions(dmeutils PRIVATE -DSTATIC_TIER0)
target_link_libraries(dmeutils PRIVATE tier0_static)

add_library(utils
        libs/utils/cmdlib.cpp
        libs/utils/datalinker.cpp
        libs/utils/filesystem_tools.cpp
        libs/utils/filesystem_helpers.cpp
        libs/utils/scriplib.cpp
        libs/utils/studio.cpp
        libs/utils/collisionutils.cpp
        libs/utils/phonemeconverter.cpp
        libs/utils/interpolatortypes.cpp
        libs/utils/physdll.cpp
        libs/utils/zip_utils.cpp
        )
target_include_directories(utils PUBLIC include)
target_include_directories(utils PRIVATE include/common)
target_compile_definitions(utils PRIVATE -DSTATIC_TIER0)
target_link_libraries(utils PRIVATE tier0_static)

add_library(bonesetup
        libs/bonesetup/bone_constraints.cpp
        libs/bonesetup/bone_decode.cpp
        libs/bonesetup/bone_ik.cpp
        libs/bonesetup/bone_setup.cpp
        libs/bonesetup/bone_utils.cpp
        )
target_include_directories(bonesetup PUBLIC include)
target_include_directories(bonesetup PRIVATE include/studiomdl)
target_compile_definitions(bonesetup PRIVATE -DSTATIC_TIER0)
target_link_libraries(bonesetup PRIVATE tier0_static)

add_library(meshutils
        libs/meshutils/simplify.cpp
        libs/meshutils/atlas.cpp
        libs/meshutils/clipmesh.cpp
        libs/meshutils/convexhull.cpp
        libs/meshutils/mesh.cpp
        libs/meshutils/uvparam.cpp
        libs/meshutils/vertexops.cpp
        )
target_include_directories(meshutils PUBLIC include)
target_include_directories(meshutils PRIVATE include/meshutils)
target_compile_definitions(meshutils PRIVATE -DSTATIC_TIER0)
target_link_libraries(meshutils PRIVATE tier0_static)

add_library(resourcefile
        libs/resourcefile/resourcedictionary.cpp
        libs/resourcefile/resourcefile.cpp
        libs/resourcefile/resourceintrospection.cpp
        libs/resourcefile/resourcestream.cpp
        )
target_include_directories(resourcefile PUBLIC include)
target_include_directories(resourcefile PRIVATE include/resourcefile)
target_compile_definitions(resourcefile PRIVATE -DSTATIC_TIER0)
target_link_libraries(resourcefile PRIVATE tier0_static)

add_library(appframework
        libs/appframework/AppSystemGroup.cpp
        libs/appframework/materialsystem2app.cpp
        libs/appframework/WinApp.cpp
        )
target_include_directories(appframework PUBLIC include)
target_include_directories(appframework PRIVATE include/appframework)
target_compile_definitions(appframework PRIVATE -DSTATIC_TIER0)
target_link_libraries(appframework PRIVATE tier0_static)

add_executable(studiomdl_v2
        studiomdl/dmxsupport.cpp
        studiomdl/mrmsupport.cpp
        studiomdl/v1support.cpp
        studiomdl/bmpread.cpp
        studiomdl/perfstats.cpp
        studiomdl/collisionmodel.cpp
        studiomdl/collisionmodelsource.cpp
        studiomdl/compileclothproxy.cpp
        studiomdl/hardwarematrixstate.cpp
        studiomdl/hardwarevertexcache.cpp
        studiomdl/objsupport.cpp
        studiomdl/optimize.cpp
        studiomdl/optimize_subd.cpp
        #                studiomdl/physics2collision.cpp
        studiomdl/simplify.cpp
        studiomdl/tristrip.cpp
        studiomdl/UnifyLODs.cpp
        studiomdl/write.cpp
        studiomdl/studiomdl.cpp
        studiomdl/studiobyteswap.cpp
        studiomdl/filesystem_init.cpp
        )
target_compile_definitions(studiomdl_v2 PRIVATE -DSTATIC_TIER0)
target_include_directories(studiomdl_v2 PUBLIC include)
target_link_libraries(studiomdl_v2 PRIVATE
        tier0_static
        tier1
        tier2
        tier3
        mathlib
        utils
        datamodel
        dmserializers
        movieobjects
        bonesetup
        materialsystem
        mdlobjects
        dmeutils
        meshutils
        resourcefile
        appframework
        nvtristriplib
        fbxutils
        shlwapi
        filesystem_stdio_v2
        )

add_library(vpklib
        libs/vpklib/packedstore.cpp
        )

target_compile_definitions(vpklib PRIVATE -DSTATIC_TIER0)
target_include_directories(vpklib PUBLIC include/vpklib)
target_link_libraries(vpklib PRIVATE tier0_static tier1)

add_library(filesystem_stdio_v2
        SHARED
        libs/filesystem/basefilesystem.cpp
        libs/filesystem/basefilesystemasync.cpp
        libs/filesystem/filetracker.cpp
        libs/filesystem/filegroup.cpp
        libs/filesystem/filesystem_stdio.cpp
        )

target_compile_definitions(filesystem_stdio_v2 PRIVATE -DDISABLE_ASYNC -DSTATIC_TIER0)
target_include_directories(filesystem_stdio_v2 PUBLIC include/filesystem)
target_link_libraries(filesystem_stdio_v2 PRIVATE tier0_static tier1 tier2 vstdlib_v2 vpklib utils)