#include "version/version.h"

#include "platform/platform.h"

namespace apogee::version {

std::string_view semantic() {
    return APOGEE_VERSION;
}

std::string_view git_commit() {
    return APOGEE_GIT_COMMIT;
}

std::string_view build_date() {
    return APOGEE_BUILD_DATE;
}

std::string full() {
    std::string out{"apogee "};
    out += semantic();
    out += " (";
    out += git_commit();
    out += ", ";
    out += build_date();
    out += ", ";
    out += platform::host_target();
    out += ")";
    return out;
}

}  // namespace apogee::version
