// MsvcCompat.h - force-included into every translation unit.
//
// Qt 5 defines, unconditionally on MSVC:
//
//     #define QT_MAKE_CHECKED_ARRAY_ITERATOR(x, N) \
//         stdext::make_checked_array_iterator(x, size_t(N))
//     #define QT_MAKE_UNCHECKED_ARRAY_ITERATOR(x)  \
//         stdext::make_unchecked_array_iterator(x)
//
// and uses them inside QVector, QList, QVarLengthArray and friends. On every
// other platform the same macros expand to just (x), and Qt 6 gates the MSVC
// spelling behind a compiler version check.
//
// stdext::checked_array_iterator and its factory functions were a Microsoft
// extension, deprecated in VS 2019 and deleted outright from the headers in
// the toolset that ships with Visual Studio 2026. Any header that instantiates
// QVector<T>::operator== or QList<T>::operator== then fails with
//
//     error C2065: 'stdext': undeclared identifier
//     error C3861: 'stdext': identifier not found
//     error C2039: 'make_checked_array_iterator': is not a member of 'stdext'
//
// Qt 5.14.2 will never be patched for this, and because Qt's own #define is
// not wrapped in #ifndef, predefining the macro on the command line does not
// help: qglobal.h simply overwrites it. So pull qglobal.h in first, then put
// the macros back to the portable no-op form Qt uses everywhere else.
//
// This is a compile-time-only change. The checked iterators only ever added
// bounds assertions in debug builds; the pointer arithmetic Qt performs is
// identical either way.

#pragma once

#if defined(_MSC_VER)

#  include <QtCore/qglobal.h>

#  ifdef QT_MAKE_CHECKED_ARRAY_ITERATOR
#    undef QT_MAKE_CHECKED_ARRAY_ITERATOR
#  endif
#  define QT_MAKE_CHECKED_ARRAY_ITERATOR(x, N) (x)

#  ifdef QT_MAKE_UNCHECKED_ARRAY_ITERATOR
#    undef QT_MAKE_UNCHECKED_ARRAY_ITERATOR
#  endif
#  define QT_MAKE_UNCHECKED_ARRAY_ITERATOR(x) (x)

#endif // _MSC_VER
