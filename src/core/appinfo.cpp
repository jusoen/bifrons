#include "core/appinfo.h"

namespace dcpp {

QString appDisplayName()
{
    return QStringLiteral("DC++");
}

QString appOrganisation()
{
    return QStringLiteral("dcpp");
}

QString appVersion()
{
    return QStringLiteral(DCPP_VERSION);
}

} // namespace dcpp
