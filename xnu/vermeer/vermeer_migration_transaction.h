/* One-time migration baseline for the disposable, clean laboratory root.
 * Fixed slots only. Stage the complete plist before preserving/publishing;
 * never replace the original backup. Native callbacks validate filesystem,
 * ownership and regular-file type, use exclusive creation/rename and fsync.
 */
#ifndef VERMEER_MIGRATION_TRANSACTION_H
#define VERMEER_MIGRATION_TRANSACTION_H
#ifdef VERMEER_HOST_TEST
#include <errno.h>
#else
#include <sys/errno.h>
#endif
enum vmb_slot { VMB_CURRENT, VMB_BACKUP, VMB_STAGE, VMB_DONE, VMB_SLOTS };
#define VMB_DIFFERENT (-1)
static const char vmb_plist[] =
    "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
    "<key>LastSystemVersion</key><string>20H392</string>"
    "<key>DMUserDataDisposition</key><dict>"
    "<key>SyntheticDidUpgrade</key><false/>"
    "<key>buildVersion</key><string>20H392</string>"
    "</dict></dict></plist>\n";
static const char vmb_marker[] = "VERMEER_CLEAN_ROOT_MIGRATION_BASELINE_20H392_V1\n";
struct vmb_ops {
    int (*prepare)(void *);
    int (*stat)(void *,enum vmb_slot);
    int (*matches)(void *,enum vmb_slot,const char *,unsigned);
    int (*create)(void *,enum vmb_slot,const char *,unsigned);
    int (*rename_exclusive)(void *,enum vmb_slot,enum vmb_slot);
};
static inline int vmb_initialize(const struct vmb_ops *ops,void *context)
{
    int error=ops->prepare(context);
    if (error) return error;
    error=ops->stat(context,VMB_DONE);
    if (!error) {
        error=ops->matches(context,VMB_DONE,vmb_marker,sizeof(vmb_marker)-1);
        return error==VMB_DIFFERENT ? EINVAL : error;
    }
    if (error!=ENOENT) return error;
    int current=ops->stat(context,VMB_CURRENT),backup=ops->stat(context,VMB_BACKUP);
    if (current && current!=ENOENT) return current;
    if (backup && backup!=ENOENT) return backup;
    if (!current) {
        error=ops->matches(context,VMB_CURRENT,vmb_plist,sizeof(vmb_plist)-1);
        if (!error) return ops->create(context,VMB_DONE,vmb_marker,sizeof(vmb_marker)-1);
        if (error!=VMB_DIFFERENT) return error;
        /* A different current file plus an existing backup is ambiguous.
         * Preserve both; never silently reapply on a running system's state.
         */
        if (!backup) return EEXIST;
    }
    error=ops->stat(context,VMB_STAGE);
    if (!error) error=ops->matches(context,VMB_STAGE,vmb_plist,sizeof(vmb_plist)-1);
    else if (error==ENOENT) error=ops->create(context,VMB_STAGE,vmb_plist,sizeof(vmb_plist)-1);
    if (error) return error==VMB_DIFFERENT ? EINVAL : error;
    int moved=0;
    if (!current) {
        error=ops->rename_exclusive(context,VMB_CURRENT,VMB_BACKUP);
        if (error) return error;
        moved=1;
    }
    error=ops->rename_exclusive(context,VMB_STAGE,VMB_CURRENT);
    if (error) {
        if (moved) (void)ops->rename_exclusive(context,VMB_BACKUP,VMB_CURRENT);
        /* Even if rollback fails, the original inode remains in BACKUP. */
        return error;
    }
    return ops->create(context,VMB_DONE,vmb_marker,sizeof(vmb_marker)-1);
}
#endif
