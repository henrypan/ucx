/**
* Copyright (C) Mellanox Technologies Ltd. 2001-2014.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#include "ib_mlx5.h"
#include "ib_mlx5_log.h"

#include <uct/ib/base/ib_verbs.h>
#include <uct/ib/base/ib_device.h>
#include <ucs/arch/bitops.h>
#include <ucs/debug/log.h>
#include <ucs/sys/compiler.h>
#include <ucs/sys/sys.h>
#include <string.h>


ucs_status_t uct_ib_mlx5_get_qp_info(struct ibv_qp *qp, uct_ib_mlx5_qp_info_t *qp_info)
{
#if HAVE_DECL_IBV_MLX5_EXP_GET_QP_INFO
    struct ibv_mlx5_qp_info ibv_qp_info;
    int ret;

    ret = ibv_mlx5_exp_get_qp_info(qp, &ibv_qp_info);
    if (ret != 0) {
        return UCS_ERR_NO_DEVICE;
    }

    qp_info->qpn        = ibv_qp_info.qpn;
    qp_info->dbrec      = ibv_qp_info.dbrec;
    qp_info->sq.buf     = ibv_qp_info.sq.buf;
    qp_info->sq.wqe_cnt = ibv_qp_info.sq.wqe_cnt;
    qp_info->sq.stride  = ibv_qp_info.sq.stride;
    qp_info->rq.buf     = ibv_qp_info.rq.buf;
    qp_info->rq.wqe_cnt = ibv_qp_info.rq.wqe_cnt;
    qp_info->rq.stride  = ibv_qp_info.rq.stride;
    qp_info->bf.reg     = ibv_qp_info.bf.reg;
    qp_info->bf.size    = ibv_qp_info.bf.size;
#else
    struct mlx5_qp *mqp = ucs_container_of(qp, struct mlx5_qp, verbs_qp.qp);

    if ((mqp->sq.cur_post != 0) || (mqp->rq.head != 0)) {
        ucs_warn("cur_post=%d head=%d need_lock=%d", mqp->sq.cur_post,
                 mqp->rq.head, mqp->bf->need_lock);
        return UCS_ERR_NO_DEVICE;
    }

    qp_info->qpn        = qp->qp_num;
    qp_info->dbrec      = mqp->db;
    qp_info->sq.buf     = mqp->buf.buf + mqp->sq.offset;
    qp_info->sq.wqe_cnt = mqp->sq.wqe_cnt;
    qp_info->sq.stride  = 1 << mqp->sq.wqe_shift;
    qp_info->rq.buf     = mqp->buf.buf + mqp->rq.offset;
    qp_info->rq.wqe_cnt = mqp->rq.wqe_cnt;
    qp_info->rq.stride  = 1 << mqp->rq.wqe_shift;
    qp_info->bf.reg     = mqp->bf->reg;

    if (mqp->bf->uuarn > 0) {
        qp_info->bf.size = mqp->bf->buf_size;
    } else {
        qp_info->bf.size = 0; /* No BF */
    }
#endif
    return UCS_OK;
}

ucs_status_t uct_ib_mlx5_get_srq_info(struct ibv_srq *srq, uct_ib_mlx5_srq_info_t *srq_info)
{
#if HAVE_DECL_IBV_MLX5_EXP_GET_SRQ_INFO
    struct ibv_mlx5_srq_info ibv_srq_info;
    int ret;

    ret = ibv_mlx5_exp_get_srq_info(srq, &ibv_srq_info);
    if (ret != 0) {
        return UCS_ERR_NO_DEVICE;
    }

    srq_info->buf    = ibv_srq_info.buf;
    srq_info->dbrec  = ibv_srq_info.dbrec;
    srq_info->stride = ibv_srq_info.stride;
    srq_info->head   = ibv_srq_info.head;
    srq_info->tail   = ibv_srq_info.tail;
#else
    struct mlx5_srq *msrq;

    if (srq->handle == LEGACY_XRC_SRQ_HANDLE) {
        srq = (struct ibv_srq *)(((struct ibv_srq_legacy *)srq)->ibv_srq);
    }

    msrq = ucs_container_of(srq, struct mlx5_srq, vsrq.srq);

    if (msrq->counter != 0) {
        ucs_error("SRQ counter is not 0 (%d)", msrq->counter);
        return UCS_ERR_NO_DEVICE;
    }

    srq_info->buf    = msrq->buf.buf;
    srq_info->dbrec  = msrq->db;
    srq_info->stride = 1 << msrq->wqe_shift;
    srq_info->head   = msrq->head;
    srq_info->tail   = msrq->tail;
#endif
    return UCS_OK;
}

ucs_status_t uct_ib_mlx5_get_cq(struct ibv_cq *cq, uct_ib_mlx5_cq_t *mlx5_cq)
{
    unsigned cqe_size;
#if HAVE_DECL_IBV_MLX5_EXP_GET_CQ_INFO
    struct ibv_mlx5_cq_info ibv_cq_info;
    int ret;

    ret = ibv_mlx5_exp_get_cq_info(cq, &ibv_cq_info);
    if (ret != 0) {
        return UCS_ERR_NO_DEVICE;
    }

    mlx5_cq->cq_buf    = ibv_cq_info.buf;
    mlx5_cq->cq_ci     = 0;
    mlx5_cq->cq_length = ibv_cq_info.cqe_cnt;
    cqe_size           = ibv_cq_info.cqe_size;
#else
    struct mlx5_cq *mcq = ucs_container_of(cq, struct mlx5_cq, ibv_cq);
    int ret;

    if (mcq->cons_index != 0) {
        ucs_error("CQ consumer index is not 0 (%d)", mcq->cons_index);
        return UCS_ERR_NO_DEVICE;
    }

    mlx5_cq->cq_buf      = mcq->active_buf->buf;
    mlx5_cq->cq_ci       = 0;
    mlx5_cq->cq_length   = mcq->ibv_cq.cqe + 1;
    cqe_size             = mcq->cqe_sz;
#endif

    /* Move buffer forward for 128b CQE, so we would get pointer to the 2nd
     * 64b when polling.
     */
    mlx5_cq->cq_buf += cqe_size - sizeof(struct mlx5_cqe64);

    ret = ibv_exp_cq_ignore_overrun(cq);
    if (ret != 0) {
        ucs_error("Failed to modify send CQ to ignore overrun: %s", strerror(ret));
        return UCS_ERR_UNSUPPORTED;
    }

    mlx5_cq->cqe_size_log = ucs_ilog2(cqe_size);
    ucs_assert_always((1<<mlx5_cq->cqe_size_log) == cqe_size);
    return UCS_OK;
}

void uct_ib_mlx5_update_cq_ci(struct ibv_cq *cq, unsigned cq_ci)
{
#if HAVE_DECL_IBV_MLX5_EXP_UPDATE_CQ_CI
    ibv_mlx5_exp_update_cq_ci(cq, cq_ci);
#else
    struct mlx5_cq *mcq = ucs_container_of(cq, struct mlx5_cq, ibv_cq);
    mcq->cons_index = cq_ci;
#endif
}

unsigned uct_ib_mlx5_get_cq_ci(struct ibv_cq *cq)
{
    struct mlx5_cq *mcq = ucs_container_of(cq, struct mlx5_cq, ibv_cq);
    return mcq->cons_index;
}

void uct_ib_mlx5_get_av(struct ibv_ah *ah, struct mlx5_wqe_av *av)
{
    memcpy(av, &ucs_container_of(ah, struct mlx5_ah, ibv_ah)->av, sizeof(*av));
}

struct mlx5_cqe64* uct_ib_mlx5_check_completion(uct_ib_mlx5_cq_t *cq,
                                                struct mlx5_cqe64 *cqe)
{
    switch (cqe->op_own >> 4) {
    case MLX5_CQE_INVALID:
        return NULL; /* No CQE */
    case MLX5_CQE_REQ_ERR:
        uct_ib_mlx5_completion_with_err((void*)cqe);
        /* For send completion, we don't care about the data, only releasing
         * the descriptor and updating QP pi.
         * TODO need to be changed if we have scatter-to-CQE on send. */
        ++cq->cq_ci;
        return cqe;
    case MLX5_CQE_RESP_ERR:
        uct_ib_mlx5_completion_with_err((void*)cqe);
        ++cq->cq_ci;
        return NULL;
    default:
        /* CQE might have been updated by HW. Skip it now, and it would be handled
         * in next polling. */
        return NULL;
    }
}

static int uct_ib_mlx5_bf_cmp(uct_ib_mlx5_bf_t *bf, uintptr_t addr)
{
    return ((bf->reg.addr & ~UCT_IB_MLX5_BF_REG_SIZE) == (addr & ~UCT_IB_MLX5_BF_REG_SIZE));
}

static void uct_ib_mlx5_bf_init(uct_ib_mlx5_bf_t *bf, uintptr_t addr)
{
    bf->reg.addr = addr;
}

static void uct_ib_mlx5_bf_cleanup(uct_ib_mlx5_bf_t *bf)
{
}

ucs_status_t uct_ib_mlx5_get_txwq(uct_worker_h worker, struct ibv_qp *qp,
                                  uct_ib_mlx5_txwq_t *wq)
{
    uct_ib_mlx5_qp_info_t qp_info;
    ucs_status_t status;

    status = uct_ib_mlx5_get_qp_info(qp, &qp_info);
    if (status != UCS_OK) {
        ucs_error("Failed to get mlx5 QP information");
        return UCS_ERR_IO_ERROR;
    }

    if ((qp_info.bf.size == 0) || !ucs_is_pow2(qp_info.bf.size) ||
        (qp_info.sq.stride != MLX5_SEND_WQE_BB) ||
        (qp_info.bf.size != UCT_IB_MLX5_BF_REG_SIZE) ||
        !ucs_is_pow2(qp_info.sq.wqe_cnt))
    {
        ucs_error("mlx5 device parameters not suitable for transport");
        return UCS_ERR_IO_ERROR;
    }

    ucs_debug("tx wq %d bytes [bb=%d, nwqe=%d], ud_seg=%lu [ctl=%lu av=%lu] inl=%lu data=%lu", 
              qp_info.sq.stride * qp_info.sq.wqe_cnt,
              qp_info.sq.stride, qp_info.sq.wqe_cnt,
              sizeof(struct mlx5_wqe_ctrl_seg) + sizeof(struct mlx5_wqe_datagram_seg),
              sizeof(struct mlx5_wqe_ctrl_seg),
              sizeof(struct mlx5_wqe_datagram_seg),
              sizeof(struct mlx5_wqe_inl_data_seg),
              sizeof(struct mlx5_wqe_data_seg));

    wq->qstart     = qp_info.sq.buf;
    wq->qend       = qp_info.sq.buf + (qp_info.sq.stride * qp_info.sq.wqe_cnt);
    wq->curr       = wq->qstart;
    wq->sw_pi      = wq->prev_sw_pi = 0;
    wq->bf         = uct_worker_tl_data_get(worker,
                                            UCT_IB_MLX5_WORKER_BF_KEY,
                                            uct_ib_mlx5_bf_t,
                                            uct_ib_mlx5_bf_cmp,
                                            uct_ib_mlx5_bf_init,
                                            (uintptr_t)qp_info.bf.reg);
    wq->dbrec      = &qp_info.dbrec[MLX5_SND_DBR];
    /* need to reserve 2x because:
     *  - on completion we only get the index of last wqe and we do not 
     *    really know how many bb is there (but no more than max bb
     *  - on send we check that there is at least one bb. We know
     *  exact number of bbs once we acually are sending.
     */
    wq->bb_max     = qp_info.sq.wqe_cnt - 2*UCT_IB_MLX5_MAX_BB;
#if ENABLE_ASSERT
    wq->hw_ci      = 0xFFFF;
#endif
    ucs_assert_always(wq->bb_max > 0);
    memset(wq->qstart, 0, wq->qend - wq->qstart); 
    return UCS_OK;
} 

void uct_ib_mlx5_put_txwq(uct_worker_h worker, uct_ib_mlx5_txwq_t *wq)
{
    uct_worker_tl_data_put(wq->bf, uct_ib_mlx5_bf_cleanup);
}

ucs_status_t uct_ib_mlx5_get_rxwq(struct ibv_qp *qp, uct_ib_mlx5_rxwq_t *wq)
{
    uct_ib_mlx5_qp_info_t qp_info;
    ucs_status_t status;

    status = uct_ib_mlx5_get_qp_info(qp, &qp_info);
    if (status != UCS_OK) {
        ucs_error("Failed to get mlx5 QP information");
        return UCS_ERR_IO_ERROR;
    }

    if (!ucs_is_pow2(qp_info.rq.wqe_cnt) ||
        qp_info.rq.stride != sizeof(struct mlx5_wqe_data_seg)) {
        ucs_error("mlx5 rx wq [count=%d stride=%d] has invalid parameters", 
                  qp_info.rq.wqe_cnt,
                  qp_info.rq.stride);
        return UCS_ERR_IO_ERROR;
    }
    wq->wqes            = qp_info.rq.buf;
    wq->rq_wqe_counter  = 0;
    wq->cq_wqe_counter  = 0;
    wq->mask            = qp_info.rq.wqe_cnt - 1;
    wq->dbrec           = &qp_info.dbrec[MLX5_RCV_DBR];
    memset(wq->wqes, 0, qp_info.rq.wqe_cnt * sizeof(struct mlx5_wqe_data_seg)); 

    return UCS_OK;
}

