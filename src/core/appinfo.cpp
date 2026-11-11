#include "core/appinfo.h"

namespace bifrons {

QString appDisplayName()
{
    return QStringLiteral("Bifrons");
}

QString appOrganisation()
{
    return QStringLiteral("bifrons");
}

QString appVersion()
{
    return QStringLiteral(BIFRONS_VERSION);
}

} // namespace bifrons
