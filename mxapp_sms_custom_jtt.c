#include "mxapp_sms_custom_jtt.h"
#include "mxapp_srvinteraction_jtt.h"
#include "mxapp_srv_jtt.h"

#define	MXAPP_JTT_BUFF_MAX	(384)

static kal_uint8 g_s8TmpBuf[MXAPP_JTT_BUFF_MAX];
static kal_uint8 g_s8TxBuf[MXAPP_JTT_BUFF_MAX];


// �����ն��������ͼ��
// ���� 0:�ɹ� -1:ʧ��
static kal_int32 mx_srv_set_para_heartbeat(kal_uint8 *in, kal_uint32 in_len)
{
	kal_int8 ret = -1;
	kal_uint8 idx = 0;
	kal_uint32 heartbeat = 0; // unit: s

	if (!in || in_len == 0) return -1;

	while (idx < in_len)
	{
		heartbeat = heartbeat * 10 + (in[idx++] - '0');
	}

	// �����ն��������ͼ��
	ret = mxapp_srv_heart_set(heartbeat);

	return ret;
}

// ���÷�����IP
// ���� 0:�ɹ� -1:ʧ��
static kal_int32 mx_srv_set_para_srv_ip(kal_uint8 *in, kal_uint32 in_len)
{
	kal_uint8 srv_ip[16] = { 0 };

	if (!in || in_len == 0) return -1;

	kal_mem_cpy(srv_ip, in, in_len);
	mxapp_srv_address_set(srv_ip, 0);

	return 0;
}

// ���÷�����TCP�˿�
// ���� 0:�ɹ� -1:ʧ��
static kal_int32 mx_srv_set_para_srv_tcp_port(kal_uint8 *in, kal_uint32 in_len)
{
	kal_uint8 idx = 0;
	kal_uint32 srv_port = 0;

	if (!in || in_len == 0) return -1;

	while (idx < in_len)
	{
		srv_port = srv_port * 10 + (in[idx++] - '0');
	}

	mxapp_srv_address_set(NULL, srv_port);

	return 0;
}

// ���ö�λ�����ϴ�����
// ����: 0-�ɹ�
// �ο�mx_srv_cmd_handle_down_period
static kal_int32 mx_srv_set_para_loc_prop(kal_uint8 *in, kal_uint32 in_len)
{
	kal_int8 ret = -1;
	kal_uint8 idx = 0;
	kal_uint32 loc_prop = 0; // unit: s

	if (!in || in_len == 0) return -1;

	while (idx < in_len)
	{
		loc_prop = loc_prop * 10 + (in[idx++] - '0');
	}

	ret = mx_pos_period_set(loc_prop);

	return ret;
}

static mx_set_para_handle_struct set_para_func[] =
{
	{ MXAPP_SRV_JTT_PARA_HEARTBEAT, mx_srv_set_para_heartbeat }, // �ն��������ͼ��
	{ MXAPP_SRV_JTT_PARA_SRV_IP, mx_srv_set_para_srv_ip }, // ������IP
	{ MXAPP_SRV_JTT_PARA_SRV_TCP_PORT, mx_srv_set_para_srv_tcp_port }, // ������TCP�˿�
	{ MXAPP_SRV_JTT_PARA_LOC_PROP, mx_srv_set_para_loc_prop }, // ��λ�����ϴ�����

	{ MXAPP_SRV_JTT_PARA_INVALID, NULL }
};

// �����ն˲���
// ��ʽ������ID,����ֵ,����ID,����ֵ...
// ����: 0-�ɹ�
static kal_int32 mx_srv_cmd_handle_down_set_para(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	kal_uint8 *dat_l = in;
	kal_uint8 *p = NULL;
	kal_uint32 tmp;

	kal_uint32 para_id = 0; // ����ID
	kal_uint8 para_len = 0; // ��������
	kal_uint8 *para = NULL; // ����ֵ

	mx_set_para_handle_struct *func_l = NULL;
	kal_uint32 idx = 0;
	kal_int32 ret = -1;

	while ((dat_l - in) < in_len)
	{
		/*����ID*/
		para_id = 0;
		tmp = *dat_l++ - '0';
		para_id |= (tmp << 12);
		tmp = *dat_l++ - '0';
		para_id |= (tmp << 8);
		tmp = *dat_l++ - '0';
		para_id |= (tmp << 4);
		tmp = *dat_l++ - '0';
		para_id |= tmp;
	
		if (*dat_l++ != ',') return -1;
	
		/*����ֵ*/
		para = dat_l++;

		/*����ֵ����*/
		p = strstr(dat_l, ","); // next ','
		if (!p)
		{
			para_len = in_len - (para - in);
		}
		else
		{
			para_len = p - para;
			
		}

		dat_l += para_len;
	
		/*���Ҳ���ID��Ӧ���ú���*/
		idx = 0;
		while (set_para_func[idx].para_id != MXAPP_SRV_JTT_PARA_INVALID)
		{
			if (set_para_func[idx].para_id == para_id)
			{
				func_l = &set_para_func[idx];
				break;
			}
			idx++;
		}
		if (!func_l)
		{
			mxapp_trace("--->>mx_srv_cmd_handle_down_set_para(para_id[%04x] undef!!!!)\n", para_id);
		}
		else
		{
			mxapp_trace("--->>mx_srv_cmd_handle_down_set_para([%d]=%04x,%04x)\n", idx, func_l->para_id, para_id);
		}
	
		/*ִ�в���ID��Ӧ���ú���*/
		if ((func_l) && (func_l->handle))
		{
			ret = func_l->handle(para, para_len);
		}

		if (ret != 0) // ĳһ��ʧ���������˳�
		{
			return ret;
		}
	}

	return ret; // �������þ��ɹ�
}

static mx_cmd_handle_struct cmd_func[] =
{
	{ MXAPP_SRV_JTT_CMD_DOWN_SET_PARA, mx_srv_cmd_handle_down_set_para }, // �����ն˲���

	{ MXAPP_SRV_JTT_CMD_INVALID, NULL }
};

// ������Ϣ ���ݴ���
// ��ʽ: ��ϢID,��Ϣ��ˮ��,����ID,��Ϣ��
// ������Ϣ�峤��
static kal_int32 mx_srv_data_parse(kal_uint8 src, kal_uint8 *in, kal_uint32 in_len, kal_uint32 *cmd, kal_uint16 *flow, kal_uint8 *msg)
{
	kal_uint8 *buf = in;
	kal_uint32 idx = 0;
	kal_uint8 tmp = 0;
	kal_uint8 *p = NULL;
	kal_uint16 msg_len = -1; // ��Ϣ�峤��

	if (src != 1) return -1; // Ĭ��Ϊ7BIT��ʽ src = (SRV_SMS_DCS_7BIT == dcs)?1:2
	if (!cmd || !flow || !msg) return -1; // ������

	/*��ϢID ʮ������*/
	*cmd = 0;
	tmp = buf[idx++] - '0';
	*cmd += tmp << 12;
	tmp = buf[idx++] - '0';
	*cmd += tmp << 8;
	tmp = buf[idx++] - '0';
	*cmd += tmp << 4;
	tmp = buf[idx++] - '0';
	*cmd += tmp;

	p = strstr(buf, ","); // first ','
	if (!p)	return -1;
	
	/*��Ϣ��ˮ�� ʮ����*/
	buf = p + 1;
	idx = 0;
	*flow = 0;
	while (buf[idx] != ',')
	{
		*flow = (*flow * 10) + (buf[idx++] - '0');
	}

	p = strstr(buf, ","); // next ','
	if (!p)	return -1;

	/*��Ϣ�� �ַ���*/
	msg_len = in_len - (p + 1 - in);
	kal_mem_cpy(msg, p + 1, msg_len);

	return msg_len;
}

kal_int32 mx_sms_custom_jtt_handle(kal_uint8 src, kal_uint8 *in, kal_int32 in_len)
{
	kal_int32 ret = -1;
	kal_uint32 cmd = 0; // ��ϢID
	kal_uint16 flow = 0; // ��Ϣ��ˮ��
	kal_uint8 *msg = g_s8TmpBuf; // ������Ϣ������
	kal_uint8 *out = g_s8TxBuf; // �������
	mx_cmd_handle_struct *func_l = NULL;
	kal_uint32 idx = 0;

	if ((in == NULL) || (in_len < 1)) return -1;

	kal_mem_set(msg, 0, sizeof(g_s8TmpBuf));

	/*������������ �õ�CMD ��ˮ�� ��Ϣ��*/
	ret = mx_srv_data_parse(src, in, in_len, &cmd, &flow, msg);

	/*����CMD��Ӧ������*/
	while (cmd_func[idx].cmd != MXAPP_SRV_JTT_CMD_INVALID)
	{
		if (cmd_func[idx].cmd == cmd)
		{
			func_l = &cmd_func[idx];
			break;
		}
		idx++;
	}

	kal_mem_set(out, 0, sizeof(g_s8TxBuf)); // ����

	/*ִ��CMD��Ӧ������*/
	if ((func_l) && (func_l->handle))
	{
		ret = func_l->handle(cmd, msg, ret, out); // ע�����
	}

	/*�������Ӧ��*/
	if (cmd == MXAPP_SRV_JTT_CMD_DOWN_SET_PARA) // �����ն�ͨ��Ӧ��
	{
		if(ret != 0) ret = 1; // 0�ɹ� 1ʧ��
		
		out[0] = flow >> 8; // Ӧ����ˮ��
		out[1] = flow & 0xFF;
		out[2] = cmd >> 8; // Ӧ��ID
		out[3] = cmd & 0xFF;
		out[4] = ret; // ִ�н��

		mx_srv_ack_jtt(out, 5);
	}

	return 0;
}

#if 0//(DEBUG_IN_VS == 1)
void main(void)
{
	kal_uint8 ret;

	kal_uint8 buf_in11[] = "8103,,0013,122.114.160.11,0018,1";
	mx_sms_custom_jtt_handle(1, buf_in11, strlen(buf_in11));

	kal_uint8 buf_in10[] = "8103,,0013,122.114.160.11,0018,7611";
	mx_sms_custom_jtt_handle(1, buf_in10, strlen(buf_in10));

	kal_uint8 buf_in6[] = "8103,123,0001,180";
	mx_sms_custom_jtt_handle(1, buf_in6, strlen(buf_in6));

	kal_uint8 buf_in7[] = "8103,456,0013,122.114.160.11";
	mx_sms_custom_jtt_handle(1, buf_in7, strlen(buf_in7));

	kal_uint8 buf_in8[] = "8103,78,0018,7611";
	mx_sms_custom_jtt_handle(1, buf_in8, strlen(buf_in8));

	kal_uint8 buf_in9[] = "8103,9,0095,300";
	mx_sms_custom_jtt_handle(1, buf_in9, strlen(buf_in9));

	kal_uint8 buf_in5[] = "8103,,0001,180";
	mx_sms_custom_jtt_handle(1, buf_in5, strlen(buf_in5));


}
#endif
