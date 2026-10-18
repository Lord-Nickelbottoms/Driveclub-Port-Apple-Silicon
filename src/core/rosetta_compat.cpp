#include "core/rosetta_compat.h"

#include "common/log.h"

#include <sys/sysctl.h>

namespace Core::Rosetta
{

    bool IsRosetta()
    {
        int translated = 0;
        size_t size = sizeof(translated);
        if (sysctlbyname("sysctl.proc_translated", &translated, &size, nullptr, 0) == 0)
        {
            return translated == 1;
        }
        return false;
    }

    bool ApplyCompatLayout()
    {
        if (!IsRosetta())
            return true;

        // Rosetta reserves [0xFC0000000, 0x7000000000). Avoid mapping there.
        // The DC_NO_GUEST_USER compile flag already handles this, but we can
        // also log a warning if the user built without it.
#if !defined(DC_NO_GUEST_USER)
        // Emit a runtime warning through the existing logging system.
        DC_WARN("Rosetta detected: rebuild with -DDC_GUEST_USER_RANGE=OFF "
                "to avoid exec-time SIGKILL.");
#endif
        return true;
    }

} // namespace Core::Rosetta