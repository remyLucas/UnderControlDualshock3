#include <linux/init.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>

#define L2CAP_INFO_RSP_SIZE 8
#define HID_PROFILE_HDR_SIZE 1
#define HID_REPORT_OUTPUT 0x52

static void (*def_recv_inspector)(struct l2cap_conn*, struct sk_buff**);
extern void (*l2cap_recv_inspector)(struct l2cap_conn*, struct sk_buff**);
static void (*def_send_inspector)(struct l2cap_conn*, struct sk_buff**);
extern void (*l2cap_send_inspector)(struct l2cap_conn*, struct sk_buff**);

/* Add 2 info response packet at the end of skb
 * The controller never send info response packet so we need
 * to inject them to continue the connection (otherwise the socket
 * stay in CONNECT2 state)
 */
static struct sk_buff* inject_info_rsp(struct sk_buff *skb,
			unsigned char *next_cmd, unsigned char *hdr)
{
	struct sk_buff *new_skb;
	struct l2cap_info_rsp *rsp;
	struct l2cap_hdr *lh;
	struct l2cap_cmd_hdr *cmd;
	unsigned int offset, hdr_offset;
	u16 len;

	offset = next_cmd - skb->data;
	hdr_offset = hdr - skb->data; 
	new_skb = skb_copy_expand(skb,0,2*(L2CAP_INFO_RSP_SIZE + L2CAP_CMD_HDR_SIZE),0);

	lh = (void*)new_skb->data + hdr_offset;

	cmd = (void*)new_skb->data + offset;
	rsp = (void*)new_skb->data + offset + L2CAP_CMD_HDR_SIZE;
	rsp->type   = cpu_to_le16(L2CAP_IT_FEAT_MASK);
	rsp->result = cpu_to_le16(L2CAP_IR_SUCCESS);
	*((u32*)rsp->data) = 0;
	cmd->code  = L2CAP_INFO_RSP;
	cmd->ident = 1;
	cmd->len   = cpu_to_le16(L2CAP_INFO_RSP_SIZE);

	cmd = (void*)((unsigned char*)cmd + L2CAP_INFO_RSP_SIZE + L2CAP_CMD_HDR_SIZE);
	rsp = (void*)((unsigned char*)rsp+L2CAP_INFO_RSP_SIZE + L2CAP_CMD_HDR_SIZE);
	rsp->type   = cpu_to_le16(L2CAP_IT_FEAT_MASK);
	rsp->result = cpu_to_le16(L2CAP_IR_SUCCESS);
	*((u32*)rsp->data) = 0;
	cmd->code  = L2CAP_INFO_RSP;
	cmd->ident = 2;
	cmd->len   = cpu_to_le16(L2CAP_INFO_RSP_SIZE);

	len = le16_to_cpu(lh->len);
	len += 2*(L2CAP_INFO_RSP_SIZE + L2CAP_CMD_HDR_SIZE);
	new_skb->len += 2*(L2CAP_INFO_RSP_SIZE + L2CAP_CMD_HDR_SIZE);
	lh->len = cpu_to_le16(len);

	kfree_skb(skb);

	return new_skb;
}

static u16 translate_cid(u16 cid)
{
	/* The cotroller often assume that channel ids for openned
	 * connections are 0x2 and 0x3 (reserved channels) while
	 * the host openned channels 0x40 and 0x41 respectively
	 */
	cid = le16_to_cpu(cid);
	if(cid == 0x2 || cid == 0x3)
		return cpu_to_le16(cid + 0x3e);
	else
		return cpu_to_le16(cid);
}

void inspect_recv(struct l2cap_conn *conn, struct sk_buff **pskb)
{
	struct l2cap_cmd_hdr *cmd;
	struct l2cap_hdr *lh;
	u16 cid, len;
	struct l2cap_conf_req *req;
	struct l2cap_conf_rsp *rsp;
	struct sk_buff *skb = *pskb;
	u8 *data = (void*)skb->data;

	lh = (void*)data;
	cid = __le16_to_cpu(lh->cid);
	len = __le16_to_cpu(lh->len);
	data += L2CAP_HDR_SIZE;

	switch(cid)
	{
		case L2CAP_CID_SIGNALING:
			cmd = (void*)data;
			data += L2CAP_CMD_HDR_SIZE;

			switch(cmd->code)
			{
				case L2CAP_CONF_REQ:
					req = (void*)data;
					req->dcid = translate_cid(req->dcid);
					data += __le16_to_cpu(cmd->len);
					*pskb = inject_info_rsp(skb,data,(void*)lh);
					break;

				case L2CAP_CONF_RSP:
					rsp = (void*)data;
					/* the first configuration response
					 * packet is malformed, command
					 * identifier must be set to 3
					 */
					if(cmd->ident == 0)
						cmd->ident = 3;
					rsp->scid = translate_cid(rsp->scid);
					break;
				default:
					break;
			}
		break;
		default:
			lh->cid = translate_cid(lh->cid);
		break;
	}
}

void inspect_send(struct l2cap_conn *conn, struct sk_buff **pskb)
{
	struct l2cap_hdr *lh;
	u16 cid, len;
	struct sk_buff *skb = *pskb;
	u8 *data = (void*)skb->data;

	lh = (void*)data;
	cid = __le16_to_cpu(lh->cid);
	len = __le16_to_cpu(lh->len);
	data += L2CAP_HDR_SIZE;

	switch(cid)
	{
		/* Assuming the Controller always use 0x00ee channel
		 * for HID-Control
		 */
		case 0x00ee:
			/* All output report packets make the controller
			 * disconnect, truncate them keep the connection
			 * alive. As a result, led control and force
			 * feedback does not work.
			 */
			if(*data == HID_REPORT_OUTPUT)
			{
				lh->len = cpu_to_le16(0);
				skb->len -= len;
			}
		break;
		default:
		break;
	}
}

static int my_init(void)
{
	if(l2cap_recv_inspector != NULL)
	{
		/* capture L2CAP packets before they are handled
		 * by the host
		 */
		def_recv_inspector = l2cap_recv_inspector;
		l2cap_recv_inspector = inspect_recv;
	}
	if(l2cap_send_inspector != NULL)
	{
		/* capture L2CAP packets before they are sent to
		 * the controller
		 */
		def_send_inspector = l2cap_send_inspector;
		l2cap_send_inspector = inspect_send;
	}

	return  0;
}
   
static void my_exit(void)
{
	l2cap_recv_inspector = def_recv_inspector;
	l2cap_send_inspector = def_send_inspector;
	return;
}
   
module_init(my_init);
module_exit(my_exit);
MODULE_LICENSE("GPL");
