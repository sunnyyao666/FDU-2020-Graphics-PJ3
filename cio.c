/** 
 * @file cio.c
 * @brief memory manager and operations for compressing JPEG IO.
 */

#include <string.h>
#include "cjpeg.h"
#include "cio.h"


/*
 * flush input and output of compress IO.
 */


bool
flush_cin_buffer(void *cio) {
    mem_mgr *in = ((compress_io *) cio)->in;
    size_t len = in->end - in->set;
    memset(in->set, 0, len);
    if (fread(in->set, sizeof(UINT8), len, in->fp) != len)
        return false;
    fseek(in->fp, 2 * (-len), SEEK_CUR); // 当前行已经读完，将指针位置置于前一行头部（-2*len)
    in->pos = in->set;
    return true;
}

bool
flush_cout_buffer(void *cio) {
    mem_mgr *out = ((compress_io *) cio)->out;
    size_t len = out->pos - out->set;
    if (fwrite(out->set, sizeof(UINT8), len, out->fp) != len)
        return false;
    memset(out->set, 0, len);
    out->pos = out->set;
    return true;
}


/*
 * init memory manager.
 */

void
init_mem(compress_io *cio,
         FILE *in_fp, int in_size, FILE *out_fp, int out_size) {
    cio->in = (mem_mgr *) malloc(sizeof(mem_mgr));
    if (!cio->in)
        err_exit(BUFFER_ALLOC_ERR);
    cio->in->set = (UINT8 *) malloc(sizeof(UINT8) * in_size);
    if (!cio->in->set)
        err_exit(BUFFER_ALLOC_ERR);
    cio->in->pos = cio->in->set;
    cio->in->end = cio->in->set + in_size;
    cio->in->flush_buffer = flush_cin_buffer;
    cio->in->fp = in_fp;

    cio->out = (mem_mgr *) malloc(sizeof(mem_mgr));
    if (!cio->out)
        err_exit(BUFFER_ALLOC_ERR);
    cio->out->set = (UINT8 *) malloc(sizeof(UINT8) * out_size);
    if (!cio->out->set)
        err_exit(BUFFER_ALLOC_ERR);
    cio->out->pos = cio->out->set;
    cio->out->end = cio->out->set + out_size;
    cio->out->flush_buffer = flush_cout_buffer;
    cio->out->fp = out_fp;

    cio->temp_bits.len = 0;
    cio->temp_bits.val = 0;
}

void
free_mem(compress_io *cio) {
    fflush(cio->out->fp);
    free(cio->in->set);
    free(cio->out->set);
    free(cio->in);
    free(cio->out);
}


/*
 * write operations.
 */

void
write_byte(compress_io *cio, UINT8 val) {
    mem_mgr *out = cio->out;
    *(out->pos)++ = val & 0xFF;
    if (out->pos == out->end) {
        if (!(out->flush_buffer)(cio))
            err_exit(BUFFER_WRITE_ERR);
    }
}

void
write_word(compress_io *cio, UINT16 val) {
    write_byte(cio, (val >> 8) & 0xFF);
    write_byte(cio, val & 0xFF);
}

void
write_marker(compress_io *cio, JPEG_MARKER mark) {
    write_byte(cio, 0xFF);
    write_byte(cio, (int) mark);
}

void
write_bits(compress_io *cio, BITS bits) {
    BITS *temp = &(cio->temp_bits); // 先取出存在缓存区中的位
    int len = bits.len + temp->len - 16; // 判断写入位加上缓存区的位是否超过两个字节（缓存区大小）
    if (len >= 0) {
        UINT16 word = temp->val | bits.val >> len; // 需要写入的两字节
        UINT8 bytes = word >> 8;
        write_byte(cio, bytes);
        if (bytes == 0xFF) write_byte(cio, 0); // 每个字节分别写入并判断是否为0xFF
        bytes = word & 0xFF;
        write_byte(cio, bytes);
        if (bytes == 0xFF) write_byte(cio, 0);
        temp->len = len;
        temp->val = bits.val << (16 - len); // 缓存区保留剩余位
    } else {
        temp->len = 16 + len;
        temp->val |= bits.val << (-len); // 直接将写入位加入缓存区中
    }
}

void
write_align_bits(compress_io *cio) {
    BITS *temp = &(cio->temp_bits);
    int left_len = temp->len % 8; // 缓存区中多出来的位数
    BITS align_bits; // 需要补上的1
    align_bits.len = 8 - left_len;
    align_bits.val = ((UINT16) ~0x0) >> left_len;
    write_bits(cio, align_bits); // 将缓存区写入
    if (temp->len == 8) { // 若正好剩余8位，则仍会保留于缓存区中，需特殊处理
        UINT8 byte = temp->val >> 8;
        write_byte(cio, byte);
        if (byte == 0xFF) write_byte(cio, 0);
    }
}

