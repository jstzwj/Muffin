#pragma once

#include <QtGlobal>

// Symbol visibility for the MuffinCore / MuffinUi shared libraries.
//
// CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS exports every *function* symbol from the
// DLLs, but its linker-generated .def cannot carry *data* symbols — most
// notably the moc-generated `staticMetaObject`, which every cross-boundary
// QObject::connect (pointer-to-member) or QMetaObject use references. The
// Q_OBJECT classes therefore carry an explicit class-level export attribute.
// Under a STATIC MUFFIN_LIB_TYPE build (MUFFIN_SHARED_LIBS undefined) the
// macros expand to nothing.
#if !defined(MUFFIN_SHARED_LIBS)
#  define MUFFIN_CORE_EXPORT
#  define MUFFIN_UI_EXPORT
#elif defined(MUFFIN_CORE_LIBRARY)
#  define MUFFIN_CORE_EXPORT Q_DECL_EXPORT
#  define MUFFIN_UI_EXPORT Q_DECL_IMPORT
#elif defined(MUFFIN_UI_LIBRARY)
#  define MUFFIN_CORE_EXPORT Q_DECL_IMPORT
#  define MUFFIN_UI_EXPORT Q_DECL_EXPORT
#else
#  define MUFFIN_CORE_EXPORT Q_DECL_IMPORT
#  define MUFFIN_UI_EXPORT Q_DECL_IMPORT
#endif
