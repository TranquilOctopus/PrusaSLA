#pragma once

#include <string>

namespace Slic3r::Biz::ResultExport {

class IResultExportFailedListener {
public:
    virtual ~IResultExportFailedListener() = default;
    virtual void on_result_export_failed(const std::string& message) = 0;
};

} // namespace Slic3r::Biz::ResultExport