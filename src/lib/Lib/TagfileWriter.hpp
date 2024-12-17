//
// This is a derivative work. originally part of the LLVM Project.
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2024 Fernando Pelliccioni (fpelliccioni@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdocs
//

#ifndef MRDOCS_LIB_TAGFILEWRITER_HPP
#define MRDOCS_LIB_TAGFILEWRITER_HPP

#include "../Gen/xml/XMLTags.hpp"
#include <mrdocs/Metadata.hpp>
#include "lib/Gen/hbs/HandlebarsCorpus.hpp"
#include <mrdocs/Support/Error.hpp>

#include <fstream>
#include <string>

namespace clang {
namespace mrdocs {

struct SimpleWriterTag {}; //Tag dispatch for simple writers

class jit_indenter;

/** A writer which outputs Tagfiles.
*/
class TagfileWriter
{
    using os_ptr = std::unique_ptr<llvm::raw_fd_ostream>;

    hbs::HandlebarsCorpus const& corpus_;
    os_ptr os_;
    xml::XMLTags tags_;
    std::string defaultFilename_;

    TagfileWriter(
        hbs::HandlebarsCorpus const& corpus,
        os_ptr os,
        std::string_view defaultFilename) noexcept;

public:
    /** Create a TagfileWriter instance.

        This static function creates a TagfileWriter instance using the provided
        HandlebarsCorpus, tagfile, and default filename.

        @param corpus The HandlebarsCorpus to use for the writer.
        @param tagfile The name of the tagfile to write to.
        @param defaultFilename The default filename to use for a symbol if none
        is provided. Typically, the relative path to a single page output file.
        This parameter is ignored in multipage mode.
        @return An Expected object containing the TagfileWriter instance or an error.
     */
    static
    Expected<TagfileWriter>
    create(
        hbs::HandlebarsCorpus const& corpus,
        std::string_view tagfile,
        std::string_view defaultFilename);

    /** Build the tagfile.

        This function builds the tagfile by initializing the output,
        traversing the global namespace of the corpus, and finalizing
        the output.
     */
    void
    build();

private:
    // ==================================================
    // Build
    // ==================================================
    void
    initialize();

    template<class T>
    void
    operator()(T const&);

    void
    finalize();

    // ==================================================
    // Write
    // ==================================================
    void
    writeNamespace(NamespaceInfo const&);

    template<class T>
    void
    writeClassLike(T const& I);

    void
    writeFunctionMember(FunctionInfo const& I);

    // ==================================================
    // URLs
    // ==================================================

    template<class T>
    std::string
    generateFilename(T const& I);

    template<class T>
    std::pair<std::string, std::string>
    generateFileAndAnchor(T const& I);
};

} // mrdocs
} // clang

#endif