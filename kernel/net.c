#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

#define PortNumber 65536
#define QueueSize 16

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;

struct FIFO{
  struct FIFO *next;
  char *packet;
};

struct port_queue{
  struct spinlock portlock;
  struct FIFO *FIFOqueue;
  uint64 binded;
  uint64 size;
};

static struct port_queue PortQ[PortNumber];

void
netinit(void)
{
  initlock(&netlock, "netlock");
  for (int i = 0; i < PortNumber; i++) {
    initlock(&PortQ[i].portlock, "portQ");
    PortQ[i].binded = 0;
    PortQ[i].FIFOqueue = 0;
    PortQ[i].size = 0;
  }
}


//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//
uint64
sys_bind(void)
{
  //
  // Your code here.
  //
  // 获取端口号
  int port;
  argint(0, &port);

  if(port < 0 || port >= PortNumber)
    return -1;
  // 为端口维护先进先出队列
  // 这个端口号如何被全局访问？
  // 如果不做成 二维数组？那该怎么安排？
  // 如果做成 二维数组，那怎么安排端口数量？
  
  // 初始化上述对应结构体
  struct port_queue *portQ = &PortQ[port];

  acquire(&portQ->portlock);
  if(portQ->binded == 1){
    release(&portQ->portlock);
    return -1;
  }
    
  portQ->binded = 1;
  portQ->size = 0;

  release(&portQ->portlock);
  return 0;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //
  int port;
  argint(0, &port);

  if(port < 0 || port >= PortNumber)
    return -1;

  struct port_queue *portQ = &PortQ[port];
  acquire(&portQ->portlock);

  // 释放 FIFO 队列
  portQ->binded = 0;
  struct FIFO *cur = portQ->FIFOqueue;
  while(cur){
    struct FIFO *next = cur->next;
    kfree((void*)cur);
    cur = next;
  }
  portQ->FIFOqueue = 0;
  release(&portQ->portlock);
  return 0;
  
  // 取消内存的部分之后再说
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//
uint64
sys_recv(void)
{
  //
  // Your code here.
  //
  // dport: 目的端口； sport：源端口
  // bufaddr：缓存地址
  // src：recv 将包的 32 bit IP 地址写进去的地址
  struct proc *p = myproc();
  int deport;
  uint64 srcaddr, sportaddr;      
  uint64 src;
  short sport;
  uint64 bufaddr;
  int maxlen;

  argint(0, &deport);
  argaddr(1, &srcaddr);
  argaddr(2, &sportaddr);
  argaddr(3, &bufaddr);
  argint(4, &maxlen);

  if(copyin(p->pagetable, (char*)&sport, sportaddr, sizeof(sport)) < 0)
    return -1;

  if(copyin(p->pagetable, (char*)&src, srcaddr, sizeof(src)) < 0)
    return -1;


  // recv()  应该以到达顺序查看到达的包
  // recv() 复制 包的 32bit 源 IP 地址到 src 中，
  // 并复制包的 16 bit UDP 源端口号到 sport，
  // 复制最多 maxlen 个包的 UDP payload 字节数到 buf 中，
  // 并将该包从 队列中移出

  if(maxlen > PGSIZE)
    return -1;


  // 先以顺序查看到达的包
  struct port_queue *deportQ = &PortQ[deport];
  acquire(&deportQ->portlock);
  if(deportQ->binded != 1){
    // 该端口号队列未初始化
    printf("the port queue isn't initialized");
    release(&deportQ->portlock);
    return -1;
  }

  if(deportQ->size == 0){
    // 如果没有包则 sleep 在当前进程的 chan 上
    sleep(deportQ, &deportQ->portlock);
  }
  // 怎么恢复比较合理？
  // 当 size >= 1 时恢复

  if(deportQ->size >= 1)
    deportQ->size -= 1;

  // 将该包从队列中取出，并减少size
  struct FIFO *FIFOQ = deportQ->FIFOqueue;
  struct FIFO *next = FIFOQ->next;
  deportQ->FIFOqueue = next;
  
  release(&deportQ->portlock);

  // 现在直接解析 FIFOQ 即可
  
  // 首先以 eth 的形式跳过 ethernet header
  // 因为不需要存储？

  struct eth *eth = (struct eth*)FIFOQ->packet;

  struct ip *ip = (struct ip*)(eth + 1);
  // recv() 复制 包的 32bit 源 IP 地址到 src 中
  // 是否要转格式之后再说
  src = ntohl(ip->ip_src);

  // 并复制包的 16 bit UDP 源端口号到 sport，
  struct udp *udp = (struct udp*)(ip + 1);
  // 是否要转格式之后再说
  sport = ntohs(udp->sport);

  int len = udp->ulen - sizeof(*udp) > maxlen ? maxlen : udp->ulen - sizeof(*udp);
  // 复制最多 maxlen 个 UDP payload 的字节数
  char *payload = (char *)(udp + 1);
  if(copyout(p->pagetable, bufaddr , payload, len) < 0){
    printf("copy the maxlen byte from packet payload to bufaddr failed");
    return -1;
  }

  // 正确释放包内存
  kfree(FIFOQ->packet);
  kfree(FIFOQ);

  // 返回复制字节数的大小: 即 UDP length - UDP header
  return len;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//
uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  e1000_transmit(buf, total);

  return 0;
}

void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  //
  // Your code here.
  //

  // ip_rx() 需要判断到达的包是否为 UDP ，以及是否目的端口被传递到 bind() 中
  // 如果两者均满足，则将包 保存到 recv() 能找到的地方
  struct eth *eth = (struct eth*)buf;
  struct ip *ip = (struct ip*)(eth + 1);


  // 判断到达的包是否为 UDP
  if(ip->ip_p != IPPROTO_UDP){
    kfree(buf);
    return ;
  }

  struct udp *udp = (struct udp*)(ip + 1);
  // 判断目的端口是否被传递到 bind() 中

  uint64 dport = ntohs(udp->dport);
  struct port_queue *dportQ= &PortQ[dport];
  acquire(&dportQ->portlock);
  if(dportQ->binded != 1){
    release(&dportQ->portlock);
    printf("port FIFO not binded");
    return;
  }

  // 循环判断 FIFO 中的包数
  int size = 0;
  struct FIFO *cur = dportQ->FIFOqueue;
  while(cur){
    size++;
    cur = cur->next;
  }
  dportQ->size = size;

  // 如果 已经有 16 个包等待 recv() ，则一个即将到来的包将被丢弃
  if(dportQ->size == QueueSize){
    printf("already have 16 in FIFO");
    release(&dportQ->portlock);
    kfree(buf);
    return;
  }

  struct FIFO *new = (struct FIFO*)kalloc();
  new->packet = buf;
  new->next = 0;

  // 将包保存到 recv 能找到的地方
  if(dportQ->FIFOqueue == 0){
    dportQ->FIFOqueue = new;
  }
  else{
    cur = dportQ->FIFOqueue;
    while(cur->next){
      cur = cur->next;
    }
    cur->next = new;
  }
  release(&dportQ->portlock);

  // wakeup(dportQ);
  
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}
