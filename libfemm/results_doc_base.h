// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// results_doc_base.h — Common base for all results document types
#ifndef RESULTS_DOC_BASE_H
#define RESULTS_DOC_BASE_H

#include <string>

class ResultsDocBase {
public:
    virtual ~ResultsDocBase() = default;
    std::string filePath;
};

#endif // RESULTS_DOC_BASE_H
