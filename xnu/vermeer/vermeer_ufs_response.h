/* UFS SCSI response decoding shared with host-side protocol tests. */
#ifndef VERMEER_UFS_RESPONSE_H
#define VERMEER_UFS_RESPONSE_H
#include <sys/types.h>

struct ufs_sense {
    uint8_t response, key, asc, ascq;
};

static inline int ufs_decode_response_lun(volatile const uint8_t *r, uint8_t tag,
                                         uint8_t lun, uint32_t ocs, struct ufs_sense *sense)
{
    sense->response = r[6];
    sense->key = sense->asc = sense->ascq = 0xff;
    if (ocs) return 2;
    if (r[0] != 0x21 || r[2] != lun || r[3] != tag) return 3;
    /* A SCSI CHECK CONDITION is handled through its auto-sense, as in Linux
     * ufshcd_transfer_rsp_status. Do not reject it before status decoding.
     */
    if (r[7]) {
        unsigned n = (unsigned)r[32] << 8 | r[33];
        unsigned segment = (unsigned)r[10] << 8 | r[11];
        if (r[7] == 2 && n <= 18 && segment >= n + 2) {
            unsigned format = r[34] & 0x7f;
            if ((format == 0x70 || format == 0x71) && n >= 14) {
                sense->key = r[36] & 15;
                sense->asc = r[46];
                sense->ascq = r[47];
            } else if ((format == 0x72 || format == 0x73) && n >= 4) {
                sense->key = r[35] & 15;
                sense->asc = r[36];
                sense->ascq = r[37];
            }
        }
        return 5;
    }
    if (r[6]) return 4;
    if ((r[1] & 0x60) || r[12] || r[13] || r[14] || r[15]) return 6;
    return 0;
}

static inline int ufs_decode_response(volatile const uint8_t *r, uint8_t tag,
                                     uint32_t ocs, struct ufs_sense *sense)
{
    return ufs_decode_response_lun(r, tag, 0, ocs, sense);
}

static inline int ufs_retry_tur(int rc, uint32_t status, const struct ufs_sense *s)
{
    return rc == 5 && status == 2 &&
        (s->key == 6 || (s->key == 2 && s->asc == 4 && s->ascq == 1));
}
#endif
