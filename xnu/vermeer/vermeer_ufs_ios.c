/* Integrated root profile only. Diagnostic profiles remain read-only. */
#define VERMEER_UFS_IOS_PROFILE 1
#include "vermeer_ufs_block.c"

int vermeer_ufs_ios_root_ready(void);
int vermeer_ufs_ios_root_ready(void)
{
    return vb.ready && vb.root_valid && vb.writable && !vb.failed && ios_media_writable == 1;
}
