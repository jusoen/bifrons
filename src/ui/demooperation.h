#pragma once

#include "core/fileoperation.h"

namespace bifrons {

// Test operation for watching background progress. It writes a scratch folder of large files under
// the system temp folder, copies them slowly, then deletes the scratch folder, also when cancelled.
// It never touches user files.
class DemoOperation : public FileOperation
{
    Q_OBJECT

public:
    using FileOperation::FileOperation;

protected:
    void execute() override;
};

} // namespace bifrons
