#define VERMEER_HOST_TEST 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/vermeer/vermeer_migration_transaction.h"
struct file { int exists; unsigned size; char bytes[512]; };
struct fs { struct file f[VMB_SLOTS]; unsigned count,fail; int reject_root; };
static int fault(struct fs *fs) { return ++fs->count==fs->fail; }
static int prepare(void *opaque) { struct fs *fs=opaque;return fs->reject_root ? EPERM : (fault(fs) ? EIO : 0); }
static int stat_file(void *opaque,enum vmb_slot s) { struct fs *fs=opaque;return fault(fs) ? EIO : fs->f[s].exists ? 0 : ENOENT; }
static int matches(void *opaque,enum vmb_slot s,const char *data,unsigned size)
{
    struct fs *fs=opaque;if (fault(fs)) return EIO;
    return fs->f[s].exists && fs->f[s].size==size && !memcmp(fs->f[s].bytes,data,size) ? 0 : VMB_DIFFERENT;
}
static int create(void *opaque,enum vmb_slot s,const char *data,unsigned size)
{
    struct fs *fs=opaque;
    assert(s==VMB_STAGE || s==VMB_DONE);assert(size<sizeof(fs->f[s].bytes));
    if (fs->f[s].exists) return EEXIST;
    fs->f[s].exists=1;
    if (fault(fs)) { fs->f[s].size=size/2;memcpy(fs->f[s].bytes,data,size/2);return EIO; }
    fs->f[s].size=size;memcpy(fs->f[s].bytes,data,size);return 0;
}
static int rename_exclusive(void *opaque,enum vmb_slot from,enum vmb_slot to)
{
    struct fs *fs=opaque;if (fault(fs)) return EIO;
    if (fs->f[to].exists) return EEXIST;
    if (!fs->f[from].exists) return ENOENT;
    fs->f[to]=fs->f[from];memset(&fs->f[from],0,sizeof(fs->f[from]));return 0;
}
static const struct vmb_ops ops={prepare,stat_file,matches,create,rename_exclusive};
static void put(struct fs *fs,enum vmb_slot slot,const char *data)
{
    fs->f[slot].exists=1;fs->f[slot].size=(unsigned)strlen(data);memcpy(fs->f[slot].bytes,data,strlen(data));
}
static int is_original(struct fs *fs,enum vmb_slot slot)
{
    return fs->f[slot].exists && fs->f[slot].size==8 && !memcmp(fs->f[slot].bytes,"original",8);
}
int main(void)
{
    struct fs ok={0};put(&ok,VMB_CURRENT,"original");
    assert(vmb_initialize(&ops,&ok)==0 && is_original(&ok,VMB_BACKUP));
    unsigned calls=ok.count;
    assert(matches(&ok,VMB_CURRENT,vmb_plist,sizeof(vmb_plist)-1)==0);
    struct file saved=ok.f[VMB_BACKUP];
    assert(vmb_initialize(&ops,&ok)==0 && !memcmp(&saved,&ok.f[VMB_BACKUP],sizeof(saved)));
    /* Inject a failure at every read/create/atomic-rename step. Partial writes
     * may touch staging/marker files, but never lose the original inode.
     */
    for (unsigned fail=1;fail<=calls;fail++) {
        struct fs fs={.fail=fail};put(&fs,VMB_CURRENT,"original");
        int result=vmb_initialize(&ops,&fs);
        assert(is_original(&fs,VMB_CURRENT) || is_original(&fs,VMB_BACKUP));
        if (!result) assert(matches(&fs,VMB_CURRENT,vmb_plist,sizeof(vmb_plist)-1)==0);
        fs.fail=0;
        (void)vmb_initialize(&ops,&fs);
        assert(is_original(&fs,VMB_CURRENT) || is_original(&fs,VMB_BACKUP));
    }
    struct fs fresh={0};assert(vmb_initialize(&ops,&fresh)==0 && !fresh.f[VMB_BACKUP].exists);
    struct fs interrupted={0};put(&interrupted,VMB_BACKUP,"original");put(&interrupted,VMB_STAGE,vmb_plist);
    assert(vmb_initialize(&ops,&interrupted)==0 && is_original(&interrupted,VMB_BACKUP));
    struct fs ambiguous={0};put(&ambiguous,VMB_BACKUP,"original");put(&ambiguous,VMB_CURRENT,"changed state");
    assert(vmb_initialize(&ops,&ambiguous)==EEXIST && is_original(&ambiguous,VMB_BACKUP));
    struct fs rejected={.reject_root=1};put(&rejected,VMB_CURRENT,"original");
    assert(vmb_initialize(&ops,&rejected)==EPERM && is_original(&rejected,VMB_CURRENT) && !rejected.f[VMB_STAGE].exists);
    struct fs marker={0};put(&marker,VMB_DONE,"foreign marker");
    assert(vmb_initialize(&ops,&marker)==EINVAL && !marker.f[VMB_CURRENT].exists);
    puts("Migration transaction: failure injection, preserved original, exclusive backup, interrupted recovery and idempotence PASS");
}
