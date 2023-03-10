# SPDX-FileCopyrightText: 2023 Daniel Vrátil <daniel.vratil@gendigital.com>
#
# SPDX-License-Identifier: BSL-1.0

if (${DOXYGEN_FOUND})
    set(DOXYGEN_EXAMPLE_PATH ${CMAKE_SOURCE_DIR}/include)
    set(DOXYGEN_EXTRACT_ANON_NSPACES YES)
    set(DOXYGEN_EXTRACT_PRIVATE YES)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_MAN NO)
    set(DOXYGEN_GENERATE_TREEVIEW YES)
    set(DOXYGEN_HIDE_IN_BODY_DOCS YES)
    set(DOXYGEN_INTERNAL_DOCS YES)
    set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/doc)
    set(DOXYGEN_REFERENCED_BY_RELATION YES)
    set(DOXYGEN_REFERENCES_RELATION YES)
    set(DOXYGEN_SOURCE_BROWSER YES)
    set(DOXYGEN_WARN_AS_ERROR NO)
    set(DOXYGEN_WARN_NO_PARAMDOC YES)
    set(DOXYGEN_ENABLE_PREPROCESSING YES)
    set(DOXYGEN_MACRO_EXPANSION YES)
    set(DOXYGEN_PREDEFINED DOXYGEN)

    doxygen_add_docs(doc
        ${CMAKE_SOURCE_DIR}/README.md
        ${CMAKE_SOURCE_DIR}/doc
        ${CMAKE_SOURCE_DIR}/include
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    message(STATUS "Use 'make doc' do generate documentation.")
else()
    message(STATUS "Doxygen not found, 'doc' target won't be available.")
endif()
