//====== Copyright © 1996-2004, Valve Corporation, All rights reserved. =======
//
// Purpose: Registration of element classes with the datamodel
//
//=============================================================================

#pragma once


#include "datamodel/idatamodel.h"
#include "datamodel/dmelement.h"
#include "datamodel/dmattribute.h"
#include "datamodel/dmattributevar.h"
#include "tier1/utlsymbollarge.h"
#include <cstddef>
#include <string_view>


//-----------------------------------------------------------------------------
// One per element class, defined by IMPLEMENT_ELEMENT_FACTORY / IMPLEMENT_ABSTRACT_ELEMENT. The constructors link
// every type into a static list at static-initialization time; InstallDmElementFactories() hands the list to the
// datamodel, which looks types up by name (case-insensitive) when creating elements.
//-----------------------------------------------------------------------------
struct DmElementTypeInfo {
    using CreateFn = CDmElement *(*)(DmElementHandle_t handle, const char *pElementTypeName, const DmObjectId_t &id,
                                     const char *pElementName, DmFileId_t fileid);
    using SetTypeSymbolFn = void (*)(CUtlSymbolLarge typeSymbol);

    // Takes a string literal only, so m_TypeName.data() is NUL-terminated for the const char* datamodel APIs
    template<size_t N>
    DmElementTypeInfo(const char (&typeName)[N], CreateFn pCreate, SetTypeSymbolFn pSetTypeSymbol)
            : m_TypeName(typeName, N - 1), m_pCreate(pCreate), m_pSetTypeSymbol(pSetTypeSymbol), m_pNext(s_pFirst) {
        s_pFirst = this;
    }

    std::string_view m_TypeName;       // lookup name, e.g. "DmeMesh"
    CreateFn m_pCreate;                // nullptr for abstract types, which can't be instanced
    SetTypeSymbolFn m_pSetTypeSymbol;  // sets the class's static type symbol, used by IsA()
    const DmElementTypeInfo *m_pNext;

    // Most recently constructed first
    static inline const DmElementTypeInfo *s_pFirst = nullptr;
};


//-----------------------------------------------------------------------------
// Element class registration, in the .cpp of the class (className must use DEFINE_ELEMENT)
//-----------------------------------------------------------------------------
#define DM_REGISTER_ELEMENT_TYPE_( lookupName, className, createFn ) \
    DmElementTypeInfo g_##className##_TypeInfo( #lookupName, createFn, &className::SetTypeSymbol );

#define IMPLEMENT_ELEMENT_FACTORY( lookupName, className ) \
    IMPLEMENT_ELEMENT( className )                          \
    DM_REGISTER_ELEMENT_TYPE_( lookupName, className, &className::CreateInstance<> )

// For a className that is an alias of a class template specialization
#define IMPLEMENT_TEMPLATE_ELEMENT_FACTORY( lookupName, className ) \
    IMPLEMENT_TEMPLATE_ELEMENT( className )                          \
    DM_REGISTER_ELEMENT_TYPE_( lookupName, className, &className::CreateInstance<> )

// Known to the datamodel (IsA works) but never instanced
#define IMPLEMENT_ABSTRACT_ELEMENT( lookupName, className ) \
    IMPLEMENT_ELEMENT( className )                           \
    DM_REGISTER_ELEMENT_TYPE_( lookupName, className, nullptr )


//-----------------------------------------------------------------------------
// The element classes live in static libraries: an object file is only linked (and its registration constructed) if
// something references it. USING_ELEMENT_FACTORY( DmeFoo ) references CDmeFoo's registration from the executable.
//-----------------------------------------------------------------------------
#define USING_ELEMENT_FACTORY( className )                       \
    extern DmElementTypeInfo g_C##className##_TypeInfo;            \
    const DmElementTypeInfo *g_C##className##_PullInModule = &g_C##className##_TypeInfo;


//-----------------------------------------------------------------------------
// Registers every linked element class with the datamodel and sets their type symbols
//-----------------------------------------------------------------------------
void InstallDmElementFactories();
