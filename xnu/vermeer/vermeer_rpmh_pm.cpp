/* SPDX-License-Identifier: MIT. The controller owner calls this on a thread.
 * Assertion is deliberately permanent. Demand sleep is covered separately by
 * the mandatory CPU PM terminal guard; this assertion alone cannot block it. */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <IOKit/IOService.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include "../../../bsd/dev/vermeer_rpmh_board.h"
extern "C" int vermeer_rpmh_system_idle_hold(void)
{
    static IOPMrootDomain *root;
    static IOPMDriverAssertionID assertion;
    /* Caller owns the common controller lock; no competing initializers. */
    if(assertion)return 1;
    root=IOService::getPMRootDomain();if(!root)return 0;
    root->retain();
    assertion=root->createPMAssertion(kIOPMDriverAssertionCPUBit|
        kIOPMDriverAssertionPreventSystemIdleSleepBit,kIOPMDriverAssertionLevelOn,
        root,"org.vermeer.rpmh.ACTIVE-votes");
    return assertion!=0;
}
#endif
