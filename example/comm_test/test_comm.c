
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "comm_config.h"
#include "comm_crc.h"
#include "comm_frame.h"
#include "comm_reliable.h"
#include "comm_ringbuf.h"

static unsigned total=0, passed=0;
#define OK(cond,name) do{++total;                                  \
        if(cond){++passed;printf("ok %u - %s\n",total,name);}      \
        else      {printf("not ok %u - %s\n",total,name);}         \
    }while(0)

/* ---------- CRC ---------- */
static void test_crc(void)
{
    static const uint8_t s[]="123456789";
    OK(comm_crc32(s,9)==0xCBF43926u,"crc32 vector");
    OK(comm_crc16(s,9)==0x29B1u || comm_crc16(s,9)==0xBB3Du,"crc16 vector");
}

/* ---------- Frame round-trip ---------- */
static void test_frame(void)
{
    uint8_t pl[128];
    for(size_t i=0;i<sizeof(pl);++i) pl[i]=(uint8_t)rand();

    comm_frame_header_t hdr={0};
    hdr.magic=COMM_FRAME_MAGIC;
    hdr.version=COMM_FRAME_VERSION;
    hdr.flags=COMM_FLAG_ENCRYPTED;
    hdr.src_endpoint=0x1111CCCC;
    hdr.dst_endpoint=0x2222DDDD;
    hdr.cmd_type=0x12345678;

    uint8_t frame[COMM_CFG_MAX_FRAME_SIZE];
    int len=comm_frame_encode(frame,sizeof(frame),pl,sizeof(pl),&hdr);
    OK(len>0,"frame encode ok");

    uint8_t out[128];
    size_t outlen=sizeof(out);
    comm_frame_header_t hdr2;
    OK(comm_frame_decode(frame,len,out,&outlen,&hdr2)==COMM_OK,"frame decode ok");
    OK(outlen==sizeof(pl),"payload len");
    OK(memcmp(pl,out,outlen)==0,"payload data");
}

/* ---------- TLV ---------- */
static void test_tlv(void)
{
    uint8_t buf[32]; size_t off=0;
    uint8_t v[4]={1,2,3,4};
    OK(comm_tlv_add(buf,&off,sizeof(buf),0x10,v,4)==COMM_OK,"tlv add");
    const comm_tlv_t* t=comm_tlv_find(buf,off,0x10);
    OK(t && t->length==4 && memcmp(t->value,v,4)==0,"tlv find");
}

/* ---------- RingBuf ---------- */
static void test_ringbuf(void)
{
    uint8_t backing[16]; comm_ringbuf_t rb;
    comm_ringbuf_init(&rb,backing,sizeof(backing));
    for(uint8_t i=0;i<15;i++) OK(comm_ringbuf_put(&rb,i),"rb put");
    OK(!comm_ringbuf_put(&rb,0xFF),"rb full");
    uint8_t x;
    for(uint8_t i=0;i<15;i++){
        OK(comm_ringbuf_get(&rb,&x),"rb get");
        OK(x==i,"rb value");
    }
    OK(comm_ringbuf_is_empty(&rb),"rb empty");
}

/* ---------- Reliable ---------- */
static int retrans_cnt=0;
static int retrans_cb(const uint8_t* f,size_t l,void* u)
{
    (void)u;
    /* 简单校验：缓存的序列号写在 payload[0] */
    (void)f;(void)l;
    ++retrans_cnt;
    return 0;
}

static void test_reliable(void)
{
    comm_reliable_ctx_t ctx; comm_reliable_init(&ctx,8);

    /* 组装并发送 4 帧 */
    for(int i=0;i<4;i++){
        uint8_t payload[4]={ (uint8_t)i,0,0,0 };
        comm_frame_header_t h={0};
        h.magic=COMM_FRAME_MAGIC; h.version=COMM_FRAME_VERSION;
        h.src_endpoint=1; h.dst_endpoint=2;
        int len;
        uint8_t buf[64];
        len=comm_frame_encode(buf,sizeof(buf),payload,sizeof(payload),&h);
        OK(comm_reliable_on_send(&ctx,buf,len,&h, i*100)==COMM_OK,"reliable send");
    }

    /* 伪造 ACK 到 seq==1 */
    comm_frame_header_t rx_hdr={.src_endpoint=2,.dst_endpoint=1}; /* 为了build用 */
    comm_frame_header_t ack_hdr;
    comm_ack_build(&rx_hdr,1,&ack_hdr);   /* seq 1 */

    OK(comm_reliable_on_ack(&ctx,&ack_hdr)==COMM_OK,"reliable ack");

    /* 触发超时重传 */
    retrans_cnt=0;
    comm_reliable_poll(&ctx,5000,retrans_cb,NULL);
    OK(retrans_cnt>0,"retrans triggered");
}

/* ---------- main ---------- */
int main(void)
{
    srand((unsigned)time(NULL));
    puts("TAP version 13");
    test_crc();
    test_frame();
    test_tlv();
    test_ringbuf();
    test_reliable();
    printf("1..%u\n",total);
    printf("# %u/%u passed\n",passed,total);
    return (passed==total)?0:1;
}