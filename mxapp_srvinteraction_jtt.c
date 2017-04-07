#include "mxapp_srv_jtt.h"
#include "mxapp_srvinteraction_jtt.h"

/********************
�������ݷ���.c
********************/
#define	MXAPP_ONENET_BUFF_MAX	(384)
#define AUTH_CODE_LEN 20 // ��Ȩ�볤�� Ҫ��֤���Ϊ0
#define CELL_NUMBER_LEN 6 // �ֻ��ų���
#define	FRAME_FLAG_SYMBOL	0x7E // ��ʶλ

#define	PHONE_NO_LEN		MX_APP_CALL_NUM_MAX_LEN

static kal_uint8 g_s8TmpBuf[MXAPP_ONENET_BUFF_MAX];
static kal_uint8 g_s8RxBuf[MXAPP_ONENET_BUFF_MAX];
static kal_uint8 g_s8TxBuf[MXAPP_ONENET_BUFF_MAX];

static kal_int8 g_s8Numfamily[PHONE_NO_LEN];

static kal_uint8 auth_code[AUTH_CODE_LEN]; // ��Ȩ��
static kal_uint8 auth_code_len; // ��Ȩ�볤�ȣ�Ĭ��0��
static kal_uint8 cell_number[CELL_NUMBER_LEN]; // TODO: ���迼����λ�ã��ն��ֻ��ţ�[0]�������λ

static kal_uint8 shutdown_is_active = 0;//ϵͳ���ڹػ�
static kal_uint16 serial_no_up; // ������Ϣ��ˮ��
static kal_uint16 serial_no_down = 0; // ������Ϣ��ˮ��

mx_srv_cb mx_srv_send_handle_callback = NULL;

static nvram_ef_mxapp_pos_mode_t mx_pos_mode;

/*
	0x00 - ��ͨ�ϴ�
	0x0C - �ػ�
	0x0D - ��������ʼֵ��
	0x10 - �����ϴ���λ����Ϣ��ѯ��
	0x11 - ����͸��
	����0x80 - ��������
*/
static kal_uint8 pos_info_type = 0x0D;	/*��ʼֵ��ʾ����*/

kal_uint8 mx_pos_info_type_set(kal_uint8 alarm)
{
	// �ػ� �� sos ״̬���ȼ��ߣ�������2��״̬��ʱ������״̬����
	if ((alarm != 0x00) && (alarm != 0x0C) && ((pos_info_type >= 0x80) || (pos_info_type == 0x0C))) return 0;/*sos important*//*shutdown power important*/

	pos_info_type = alarm;

	return 1;
}

kal_uint8 mx_pos_info_type_get(void)
{
	return pos_info_type;
}

// �����ն��������ͼ��
// ����: 0-�ɹ�
kal_int32 mx_srv_set_para_heartbeat(kal_uint8 *in, kal_uint32 in_len)
{
	kal_uint32 heartbeat = 0; // unit: s

	if (!in || in_len != 4) return -1;

	heartbeat |= in[0];
	heartbeat <<= 8;
	heartbeat |= in[1];
	heartbeat <<= 8;
	heartbeat |= in[2];
	heartbeat <<= 8;
	heartbeat |= in[3];

	// TODO: �����ն��������ͼ��

	return 0;
}

// ���ü����绰����
// ����: 0-�ɹ�
kal_int32 mx_srv_set_para_monitor_num(kal_uint8 *in, kal_uint32 in_len)
{
	kal_char *num = g_s8Numfamily;

	if (!in || in_len == 0) return -1;

	kal_mem_set(num, 0, sizeof(g_s8Numfamily));
	kal_mem_cpy(num, in, in_len);

#if defined(__WHMX_CALL_SUPPORT__)
	mxapp_call_save_number(MX_RELATION_MGR, num);
#elif defined(__WHMX_ADMIN_SUPPORT__)
	mxapp_admin_number_save(num);
#endif

	return 0;
}

// ���ö�λ�����ϴ���ʽ
// ����: 0-�ɹ�
// �ο�mx_srv_cmd_handle_down_pos_interval_min
kal_int32 mx_srv_set_para_loc_type(kal_uint8 *in, kal_uint32 in_len)
{
	kal_uint8 loc_type = -1;

	if (!in || in_len != 1) return -1;

	loc_type = in[0];
	if (loc_type == 0 || loc_type == 1) // 0-ʡ��ģʽ 1-��׼ģʽ
	{
		mx_pos_mode.dev_mode = loc_type;
		mx_srv_config_nv_write();
		mx_pos_mode_set(mx_pos_mode.dev_mode);
	}

	return 0;
}

// ���ö�λ�����ϴ�����
// ����: 0-�ɹ�
// �ο�mx_srv_cmd_handle_down_period
kal_int32 mx_srv_set_para_loc_prop(kal_uint8 *in, kal_uint32 in_len)
{
	kal_uint32 loc_prop = -1; // unit: s
	kal_uint16 period_min = 0; // unit: min

	if (!in || in_len != 4) return -1;

	loc_prop |= in[0];
	loc_prop <<= 8;
	loc_prop |= in[1];
	loc_prop <<= 8;
	loc_prop |= in[2];
	loc_prop <<= 8;
	loc_prop |= in[3];

	period_min = loc_prop / 60;

	if (mx_pos_mode.dev_mode == 1) // 1-��׼ģʽ
	{
		mx_pos_mode.custom_period = period_min;
		mx_srv_config_nv_write();
		mx_location_period_change(period_min);
	}

	return 0;
}

static mx_set_para_handle_struct set_para_func[] =
{
	{ MXAPP_SRV_JTT_SET_PARA_HEARTBEAT, mx_srv_set_para_heartbeat }, // �ն��������ͼ��
	{ MXAPP_SRV_JTT_SET_PARA_MONITOR_NUM, mx_srv_set_para_monitor_num }, // �����绰����
	{ MXAPP_SRV_JTT_SET_PARA_LOC_TYPE, mx_srv_set_para_loc_type }, // ��λ�����ϴ���ʽ
	{ MXAPP_SRV_JTT_SET_PARA_LOC_PROP, mx_srv_set_para_loc_prop }, // ��λ�����ϴ�����

	{ MXAPP_SRV_JTT_SET_PARA_INVALID, NULL }
};

// �ն�ͨ��Ӧ��
// in: ��������Ϣ�����ݣ�Ӧ����ˮ�š�Ӧ��ID�������
static kal_int32 mx_srv_cmd_handle_up_ack(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	kal_int32 len_l = -1;
	kal_uint8 *dat_l = out;

	if (dat_l == NULL) return len_l;

	if (in&&in_len) // �������������
	{
		kal_mem_cpy(dat_l, in, in_len);
		len_l = in_len;
	} 
	else
	{
		return len_l;
	}

	mxapp_trace("%s:len_l(%d=0x%x)", __FUNCTION__, len_l, len_l);

	return len_l;
}

// �ն�����
static kal_int32 mx_srv_cmd_handle_up_heartbeat(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	kal_int32 len_l = -1;
	kal_uint8 *dat_l = out;
	kal_uint32 i = 0, len = 0;

	if (dat_l == NULL) return len_l;

	if (in&&in_len)
	{
		kal_mem_cpy(dat_l, in, in_len);
		len_l = in_len;
	}
	else
	{
		/*��Ϣ��Ϊ��*/
		len_l = 0;
		dat_l[len_l] = '\0';
	}

	mxapp_trace("%s:len_l(%d=0x%x)", __FUNCTION__, len_l, len_l);
	for (i = 0; i < len_l;)
	{
		mxapp_trace("%s\r\n", &dat_l[i]);
		i += 127;
	}

	return len_l;
}

// �ն�ע��
// ʾ����7E 0100002B0000000000000000 002A006F31323334354D585431363038000000000000000000000000004D58543136303801415A31323334 40 7E
static kal_int32 mx_srv_cmd_handle_up_register(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	kal_uint16 province = 42;
	kal_uint16 city = 111;
	kal_uint8 manufac[] = "12345";
	kal_uint8 dev_type[] = "MXT1608";
	kal_uint8 dev_id[] = "MXT1608";
	kal_uint8 license_color = 1;
	kal_char license[] = "AZ1234";

	kal_int32 len_l = -1;
	kal_uint8 *dat_l = out;
	kal_uint32 i = 0, len = 0;

	if (dat_l == NULL) return len_l;

	if (in&&in_len)
	{
		kal_mem_cpy(dat_l, in, in_len);
		len_l = in_len;
	}
	else
	{
		len_l = 0;

		/*province*/
		dat_l[len_l++] = (province >> 8) & 0xFF;
		dat_l[len_l++] = province & 0xFF;

		/*city*/
		dat_l[len_l++] = (city >> 8) & 0xFF;
		dat_l[len_l++] = city & 0xFF;

		/*manufac*/
		snprintf(&dat_l[len_l], 5, "%s\0", manufac);
		len_l += 5;

		/*dev_type*/
		len = strlen(dev_type);
		snprintf(&dat_l[len_l], 20, "%s\0", dev_type);
		for (i = len; i < 20; i++)
			dat_l[len_l + i] = 0; // ������0
		len_l += 20;

		/*dev_id*/
		len = strlen(dev_id);
		snprintf(&dat_l[len_l], 7, "%s\0", dev_id);
		for (i = len; i < 7; i++)
			dat_l[len_l + i] = 0; // ������0
		len_l += 7;

		/*license_color*/
		dat_l[len_l++] = license_color;

		/*license*/
		len = strlen(license);
		snprintf(&dat_l[len_l], len, "%s\0", license);
		len_l += len;

		dat_l[len_l] = '\0';
	}

	mxapp_trace("%s:len_l(%d=0x%x)", __FUNCTION__, len_l, len_l);
	for (i = 0; i < len_l;)
	{
		mxapp_trace("%s\r\n", &dat_l[i]);
		i += 127;
	}

	return len_l;
}

// �ն�ע��
static kal_int32 mx_srv_cmd_handle_up_deregister(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	// TODO: �ն�ע��
}

// �ն˼�Ȩ
static kal_int32 mx_srv_cmd_handle_up_authorize(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	kal_int32 len_l = -1;
	kal_uint8 *dat_l = out;
	kal_uint32 i = 0, len = 0;

	if (dat_l == NULL) return len_l;

	if (in&&in_len)
	{
		kal_mem_cpy(dat_l, in, in_len);
		len_l = in_len;
	}
	else
	{
		/*��Ȩ��*/
		memcpy(dat_l, auth_code, strlen(auth_code));
		len_l = strlen(auth_code);

		dat_l[len_l] = '\0';
	}

	mxapp_trace("%s:len_l(%d=0x%x)", __FUNCTION__, len_l, len_l);
	for (i = 0; i < len_l;)
	{
		mxapp_trace("%s\r\n", &dat_l[i]);
		i += 127;
	}

	return len_l;
}

// ��ѯ�ն˲���Ӧ��
static kal_int32 mx_srv_cmd_handle_up_query_para_ack(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	// TODO: ��ѯ�ն˲���Ӧ��
}

// ��ѯ�ն�����Ӧ��
static kal_int32 mx_srv_cmd_handle_up_query_prop_ack(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	// TODO: ��ѯ�ն�����Ӧ��
}

// λ����Ϣ�㱨
static kal_int32 mx_srv_cmd_handle_up_loc_report(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	kal_uint32 alarm = 0;
	kal_uint32 status = 0;
	kal_uint32 lat = 0;
	kal_uint32 lon = 0;
	kal_uint16 alt = 50;
	kal_uint16 speed = 10;
	kal_uint16 course = 180;
	
	kal_int32 len_l = -1;
	kal_uint8 *dat_l = out;
	kal_uint32 i = 0, len = 0;

	if (dat_l == NULL) return len_l;

	if (in&&in_len)
	{
		kal_mem_cpy(dat_l, in, in_len);
		len_l = in_len;
	}
	else
	{
		len_l = 0;

		/*alarm*/
		dat_l[len_l++] = (alarm >> 24) & 0xFF;
		dat_l[len_l++] = (alarm >> 16) & 0xFF;
		dat_l[len_l++] = (alarm >> 8) & 0xFF;
		dat_l[len_l++] = alarm & 0xFF;

		/*status*/
		dat_l[len_l++] = (status >> 24) & 0xFF;
		dat_l[len_l++] = (status >> 16) & 0xFF;
		dat_l[len_l++] = (status >> 8) & 0xFF;
		dat_l[len_l++] = status & 0xFF;

		/*lat & lon*/
		ST_MX_LOCATION_INFO *pos_info;
#if 0
		pos_info = mx_location_get_latest_position();
		if ((pos_info->valid) && (pos_info->type == LOCATION_TYPE_GPS))
		{
			lat = mx_base_coordinates2double(&(pos_info->info.gps.lat[0])) * 1000000;
			lon = mx_base_coordinates2double(&(pos_info->info.gps.lon[0])) * 1000000;
		}
#else
		lat = 30000000;
		lon = 114000000;
#endif

		/*lat*/
		dat_l[len_l++] = (lat >> 24) & 0xFF;
		dat_l[len_l++] = (lat >> 16) & 0xFF;
		dat_l[len_l++] = (lat >> 8) & 0xFF;
		dat_l[len_l++] = lat & 0xFF;

		/*lon*/
		dat_l[len_l++] = (lon >> 24) & 0xFF;
		dat_l[len_l++] = (lon >> 16) & 0xFF;
		dat_l[len_l++] = (lon >> 8) & 0xFF;
		dat_l[len_l++] = lon & 0xFF;

		/*alt*/
		dat_l[len_l++] = (alt >> 8) & 0xFF;
		dat_l[len_l++] = alt & 0xFF;

		/*speed*/
		dat_l[len_l++] = (speed >> 8) & 0xFF;
		dat_l[len_l++] = speed & 0xFF;

		/*course*/
		dat_l[len_l++] = (course >> 8) & 0xFF;
		dat_l[len_l++] = course & 0xFF;

		/*time - BCD code*/
		MYTIME stCurrentTime;
#if 0
		DTGetRTCTime(&stCurrentTime);
#else
		stCurrentTime.nYear = 2017;
		stCurrentTime.nMonth = 4;
		stCurrentTime.nDay = 1;
		stCurrentTime.nHour = 17;
		stCurrentTime.nMin = 15;
		stCurrentTime.nSec = 33;
#endif
		stCurrentTime.nYear -= 2000;
		dat_l[len_l++] = ((stCurrentTime.nYear / 10) << 4) + (stCurrentTime.nYear % 10);
		dat_l[len_l++] = ((stCurrentTime.nMonth / 10) << 4) + (stCurrentTime.nMonth % 10);
		dat_l[len_l++] = ((stCurrentTime.nDay / 10) << 4) + (stCurrentTime.nDay % 10);
		dat_l[len_l++] = ((stCurrentTime.nHour / 10) << 4) + (stCurrentTime.nHour % 10);
		dat_l[len_l++] = ((stCurrentTime.nMin / 10) << 4) + (stCurrentTime.nMin % 10);
		dat_l[len_l++] = ((stCurrentTime.nSec / 10) << 4) + (stCurrentTime.nSec % 10);
		//len_l += 6;

		dat_l[len_l] = '\0';
	}

	mxapp_trace("%s:len_l(%d=0x%x)", __FUNCTION__, len_l, len_l);
	for (i = 0; i < len_l;)
	{
		mxapp_trace("%s\r\n", &dat_l[i]);
		i += 127;
	}

	return len_l;
}

// λ����Ϣ��ѯӦ��
static kal_int32 mx_srv_cmd_handle_up_loc_query_ack(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	// TODO: λ����Ϣ��ѯӦ��
}

// �¼�����
static kal_int32 mx_srv_cmd_handle_up_event_report(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	// TODO: �¼�����
}


// ƽ̨ͨ��Ӧ��
// ����ֵ: 0-�ɹ�/ȷ�� 1-ʧ�� 2-��Ϣ���� 3-��֧�� 4-��������ȷ��
static kal_int32 mx_srv_cmd_handle_down_ack(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	kal_uint8 *dat_l = in;
	kal_uint8 tmp;

	kal_uint16 ack_flow = 0; // Ӧ����ˮ��
	kal_uint16 ack_id = 0; // Ӧ��ID
	kal_int32 ret = -1; // ���

	if ((dat_l == NULL) || (in_len != 5)) return -1; // ƽ̨ͨ��Ӧ�� ��Ϣ�峤��in_lenӦΪ5
	mxapp_trace("%s:cmd(%x) in_len(%d)", __FUNCTION__, cmd, in_len);

	/*Ӧ����ˮ��*/
	tmp = *dat_l++;
	ack_flow += tmp << 8;
	tmp = *dat_l++;
	ack_flow += tmp;

	/*Ӧ��ID*/
	tmp = *dat_l++;
	ack_id += tmp << 8;
	tmp = *dat_l++;
	ack_id += tmp;

	/*���: 0-�ɹ�/ȷ�� 1-ʧ�� 2-��Ϣ���� 3-��֧�� 4-��������ȷ��*/
	tmp = *dat_l++;
	ret = tmp;
	
	return ret;
}

// �ն�ע��Ӧ��
// ����ֵ: 0-�ɹ� 1-�����ѱ�ע�� 2-���ݿ����޸ó��� 3-�ն��ѱ�ע�� 4-���ݿ����޸��ն�
static kal_int32 mx_srv_cmd_handle_down_register_ack(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	kal_uint8 *dat_l = in;
	kal_uint8 tmp;

	kal_uint16 flow = 0; // Ӧ����ˮ��
	kal_int32 ret = -1; // ���
	kal_uint8 *auth = auth_code; // ��Ȩ��

	if ((dat_l == NULL) || (in_len == 0)) return -1;
	mxapp_trace("%s:cmd(%x) in_len(%d)", __FUNCTION__, cmd, in_len);

	/*Ӧ����ˮ��*/
	tmp = *dat_l++;
	flow += tmp << 8;
	tmp = *dat_l++;
	flow += tmp;

	/*���: 0-�ɹ� 1-�����ѱ�ע�� 2-���ݿ����޸ó��� 3-�ն��ѱ�ע�� 4-���ݿ����޸��ն�*/
	tmp = *dat_l++;
	ret = tmp;

	if (ret == 0) // �ɹ�
	{
		/*��Ȩ��*/
		auth_code_len = in_len - 3; // ��Ȩ��ʵ�ʳ���
		kal_mem_cpy(auth, dat_l, auth_code_len);
	}

	return ret;
}

// �����ն˲���
// ����: 0-�ɹ�
static kal_int32 mx_srv_cmd_handle_down_set_para(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	kal_uint8 *dat_l = in;
	kal_uint32 tmp;
	kal_uint8 *buf_tx = g_s8TxBuf;

	kal_uint8 para_num = 0; // ��������
	kal_uint8 para_idx = 0;

	kal_uint32 para_id = 0; // ����ID
	kal_uint8 para_len = 0; // ��������
	kal_uint8 *para = NULL; // ����ֵ

	mx_set_para_handle_struct *func_l = NULL;
	kal_uint32 idx = 0;
	kal_int32 ret = -1;

	para_num = *dat_l++;
	while (para_idx < para_num)
	{
		/*����ID*/
		para_id = 0;
		tmp = *dat_l++;
		para_id |= (tmp << 24);
		tmp = *dat_l++;
		para_id |= (tmp << 16);
		tmp = *dat_l++;
		para_id |= (tmp << 8);
		tmp = *dat_l++;
		para_id |= tmp;

		/*��������*/
		tmp = *dat_l++;
		para_len = tmp;

		/*����ֵ*/
		para = dat_l;
		dat_l += para_len;

		/*���Ҳ���ID��Ӧ���ú���*/
		idx = 0;
		while (set_para_func[idx].para_id != MXAPP_SRV_JTT_SET_PARA_INVALID)
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

		para_idx++; 
	}

	return ret; // �������þ��ɹ�
}

// ��ѯ�ն˲���
static kal_int32 mx_srv_cmd_handle_down_query_para(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	// TODO: ��ѯ�ն˲���
}

// ��ѯָ���ն˲���
static kal_int32 mx_srv_cmd_handle_down_query_spec_para(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	// TODO: ��ѯָ���ն˲���
}

// ��ѯ�ն�����
static kal_int32 mx_srv_cmd_handle_down_query_prop(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	// TODO: ��ѯ�ն�����
}

// TODO: λ����Ϣ��ѯ
static kal_int32 mx_srv_cmd_handle_down_loc_query(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	/*��Ϣ��Ϊ��*/
	kal_int32 ret = 0;

	mx_pos_info_type_set(0x10); // λ����Ϣ��ѯ
	srvinteraction_location_request(0);

	return ret;
}

// �¼�����
static kal_int32 mx_srv_cmd_handle_down_event_set(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	// TODO: �¼�����
}

static mx_cmd_handle_struct cmd_func[] =
{
	{ MXAPP_SRV_JTT_CMD_UP_ACK, mx_srv_cmd_handle_up_ack }, // �ն�ͨ��Ӧ��
	{ MXAPP_SRV_JTT_CMD_UP_HEARTBEAT, mx_srv_cmd_handle_up_heartbeat }, // �ն�����
	{ MXAPP_SRV_JTT_CMD_UP_REGISTER, mx_srv_cmd_handle_up_register }, // �ն�ע��
	{ MXAPP_SRV_JTT_CMD_UP_DEREGISTER, mx_srv_cmd_handle_up_deregister }, // �ն�ע��
	{ MXAPP_SRV_JTT_CMD_UP_AUTHORIZE, mx_srv_cmd_handle_up_authorize }, // �ն˼�Ȩ
	{ MXAPP_SRV_JTT_CMD_UP_QUERY_PARA_ACK, mx_srv_cmd_handle_up_query_para_ack }, // ��ѯ�ն˲���Ӧ��
	{ MXAPP_SRV_JTT_CMD_UP_QUERY_PROP_ACK, mx_srv_cmd_handle_up_query_prop_ack }, // ��ѯ�ն�����Ӧ��
	{ MXAPP_SRV_JTT_CMD_UP_LOC_REPORT, mx_srv_cmd_handle_up_loc_report }, // λ����Ϣ�㱨
	{ MXAPP_SRV_JTT_CMD_UP_LOC_QUERY_ACK, mx_srv_cmd_handle_up_loc_query_ack }, // λ����Ϣ��ѯӦ��
	{ MXAPP_SRV_JTT_CMD_UP_EVENT_REPORT, mx_srv_cmd_handle_up_event_report }, // �¼�����

	{ MXAPP_SRV_JTT_CMD_DOWN_ACK, mx_srv_cmd_handle_down_ack }, // ƽ̨ͨ��Ӧ��
	{ MXAPP_SRV_JTT_CMD_DOWN_REGISTER_ACK, mx_srv_cmd_handle_down_register_ack }, // �ն�ע��Ӧ��
	{ MXAPP_SRV_JTT_CMD_DOWN_SET_PARA, mx_srv_cmd_handle_down_set_para }, // �����ն˲���
	{ MXAPP_SRV_JTT_CMD_DOWN_QUERY_PARA, mx_srv_cmd_handle_down_query_para }, // ��ѯ�ն˲���
	{ MXAPP_SRV_JTT_CMD_DOWN_QUERY_SPEC_PARA, mx_srv_cmd_handle_down_query_spec_para }, // ��ѯָ���ն˲���
	{ MXAPP_SRV_JTT_CMD_DOWN_QUERY_PROP, mx_srv_cmd_handle_down_query_prop }, // ��ѯ�ն�����
	{ MXAPP_SRV_JTT_CMD_DOWN_LOC_QUERY, mx_srv_cmd_handle_down_loc_query }, // λ����Ϣ��ѯ
	{ MXAPP_SRV_JTT_CMD_DOWN_EVENT_SET, mx_srv_cmd_handle_down_event_set }, // �¼�����
#ifdef __WHMX_CALL_SUPPORT__
	{ MXAPP_SRV_JTT_CMD_DOWN_CALL_MONITOR, mx_srv_cmd_handle_down_CALL_MONITOR }, // �绰�ز�
	{ MXAPP_SRV_JTT_CMD_DOWN_PHONEBOOK, mx_srv_cmd_handle_down_PHONEBOOK }, // ���õ绰��
#endif
#ifdef __WHMX_JTT_FENCE_SUPPORT__
#endif
	{ MXAPP_SRV_JTT_CMD_INVALID, NULL }
};





// ת�崦�����ش���󳤶�
static kal_int32 mx_srv_data_escape(kal_uint8 *dat, kal_uint32 in_len)
{
	kal_uint32 idx, buf_idx;
	kal_uint8 buf[MXAPP_ONENET_BUFF_MAX];
	kal_uint8 temp;

	buf_idx = 0;
	for (idx = 0; idx < in_len; idx++)
	{
		temp = dat[idx];
		if (temp == 0x7e)
		{
			buf[buf_idx++] = 0x7d;
			buf[buf_idx++] = 0x02;
		}
		else if (temp == 0x7d)
		{
			buf[buf_idx++] = 0x7d;
			buf[buf_idx++] = 0x01;
		}
		else
		{
			buf[buf_idx++] = temp;
		}
	}

	kal_mem_cpy(dat, buf, buf_idx);
	return buf_idx;
}

// ������Ϣ ���ݴ���
// ��ʽ: ��ʶλ+��Ϣͷ+��Ϣ��+У����+��ʶλ
// ����: ��Ϣ��װ����>�������У���롪��>ת��
static kal_int32 mx_srv_data_package(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	kal_int32 ret = 0;
	kal_uint32 idx = 0;
	kal_uint8 *buf_l = out;
	kal_uint16 cmd_l = cmd;
	kal_uint8 cs = 0; // У����
	kal_uint16 buf_len = 0; // ��Ϣͷ+��Ϣ��+У���볤��

	if ((in == NULL) || (out == NULL)) return -1;

	/*��ʶλ*/
	buf_l[0] = FRAME_FLAG_SYMBOL;

	/*��Ϣͷ-START*/
	/*��ϢID*/
	buf_l[1] = (cmd_l >> 8);
	buf_l[2] = cmd_l & 0xFF;

	/*��Ϣ�����ԣ����ȣ�*/
	buf_l[3] = (in_len >> 8);
	buf_l[4] = in_len & 0xFF;

	/*�ն��ֻ���*/
	for (idx = 0; idx < 6; idx++)
	{
		buf_l[5 + idx] = cell_number[idx]; // ��λ��ǰ
	}
	
	/*��Ϣ��ˮ�ţ���0��ʼѭ���ۼӣ�*/
	buf_l[11] = (serial_no_up >> 8);
	buf_l[12] = serial_no_up & 0xFF;
	serial_no_up++;

	/*��Ϣ����װ��-��*/
	/*��Ϣͷ-END*/

	/*��Ϣ��*/
	if (in_len != 0)
	{
		kal_mem_cpy(&buf_l[13], in, in_len);
	}

	/*У����*/
	cs = buf_l[1];
	for (idx = 2; idx < (13 + in_len); idx++)
	{
		cs ^= buf_l[idx];
	}
	buf_l[13 + in_len] = cs;

	/*ת��*/
	buf_len = 13 + in_len;
	buf_len = mx_srv_data_escape(&buf_l[1], buf_len);

	/*��ʶλ*/
	buf_l[buf_len + 1] = FRAME_FLAG_SYMBOL;

	ret = buf_len + 2; // ��Ϣ�ܳ���

	mxapp_trace("--->>mx_srv_data_package(cmd=%04x,msg_len=%d,len=%d,cs=0x%x,next_flow_no=%d)\n", cmd_l, buf_len, ret, cs, serial_no_up);
	return ret;
}

// �ն˷�����Ϣ��ƽ̨
static kal_int32 mx_srv_send_handle(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *para, kal_uint32 para_len)
{
	static kal_uint32 send_len_bak = 0;
	mx_cmd_handle_struct *func_l = NULL;
	kal_uint8 *buf_tmp = g_s8TmpBuf;
	kal_uint8 *buf_tx = g_s8TxBuf;
	kal_uint32 idx = 0;
	kal_int32 ret = 0;

	if (shutdown_is_active) return -1;

	// get cmd-function pointer
	while (cmd_func[idx].cmd != MXAPP_SRV_JTT_CMD_INVALID)
	{
		if (cmd_func[idx].cmd == cmd)
		{
			func_l = &cmd_func[idx];
			break;
		}
		idx++;
	}

	// no corresponding cmd-function
	if (!func_l)
	{
		mxapp_trace("--->>mx_srv_send_handle(cmd[%04x] undef!!!!)\n", cmd);

		return -1;
	}

	mxapp_trace("--->>mx_srv_send_handle([%d]=%04x,%04x)\n", idx, func_l->cmd, cmd);

	//	if(KAL_TRUE == mx_srv_call_connected()) return -1;

	if (func_l->handle)
	{
		kal_mem_set(buf_tmp, 0, sizeof(g_s8TmpBuf));

		// execute cmd-function handler
		ret = func_l->handle(cmd, para, para_len, buf_tmp);

		if (ret > 0)
		{
			kal_mem_set(buf_tx, 0, sizeof(g_s8TxBuf));
			ret = mx_srv_data_package(cmd, buf_tmp, ret, buf_tx); // formatting
			send_len_bak = ret;
			if (/*(g_s32FirstConnect == 0)&&*/(ret > 0))
			{
#if !defined(__WHMX_SERVER_NO_SMS__)
#if defined(__WHMX_CALL_SUPPORT__)
				if (mxapp_call_is_admin_set())
#elif defined(__WHMX_ADMIN_SUPPORT__)
				if (mxapp_admin_is_set())
#endif
#endif	
				{ // upload to server
					mxapp_srv_send(buf_tx, ret, mx_srv_send_handle_callback);
				}
				mx_srv_send_handle_callback = NULL;
			}
#if defined(__WHMX_LOG_SRV_SUPPORT__)
			{
				kal_char *head = NULL;
				kal_char *pbuf = NULL;
				kal_uint32 idx = 0;

				head = mx_log_srv_write_buf();
				if (head)
				{
					pbuf = head;
					kal_sprintf(pbuf, "mx_srv_send_handle[%d][%04x]:", ret, cmd);
					pbuf += strlen(pbuf);
					kal_sprintf(pbuf, "%c%c%c%c ", buf_tx[0], buf_tx[1], buf_tx[2], buf_tx[3]);
					pbuf += 5;
					kal_sprintf(pbuf, "%d ", (buf_tx[4] << 8) | buf_tx[5]);
					pbuf += strlen(pbuf);
					for (idx = 6; idx < 18; idx++)
					{
						kal_sprintf(pbuf, "%02x", buf_tx[idx]);
						pbuf += 2;
					}
					*pbuf = ' ';
					pbuf += 1;
					kal_sprintf(pbuf, "<%04x> ", (buf_tx[18] << 8) | buf_tx[19]);
					pbuf += 7;

					kal_sprintf(pbuf, "<");
					pbuf += 1;
					if ((cmd == MXAPP_SRV_ZXBD_CMD_UP_POSITION))
					{
						kal_sprintf(pbuf, "%02d-%02d-%02d ", buf_tx[20], buf_tx[21], buf_tx[22]);
						pbuf += 9;
						kal_sprintf(pbuf, "%02d%02d%02d ", buf_tx[23], buf_tx[24], buf_tx[25]);
						pbuf += strlen(pbuf);
						kal_sprintf(pbuf, "%d.%02d%02d%02d ", buf_tx[26], buf_tx[27], buf_tx[28], buf_tx[29]);
						pbuf += strlen(pbuf);
						kal_sprintf(pbuf, "%d.%02d%02d%02d ", buf_tx[30], buf_tx[31], buf_tx[32], buf_tx[33]);
						pbuf += strlen(pbuf);
						kal_sprintf(pbuf, "%d ", buf_tx[34]);
						pbuf += strlen(pbuf);
						kal_sprintf(pbuf, "%d ", buf_tx[35]);
						pbuf += strlen(pbuf);
						kal_sprintf(pbuf, "%d ", (buf_tx[36] << 8) | (buf_tx[37]));/*�߶�*/
						pbuf += strlen(pbuf);
						kal_sprintf(pbuf, "0x%02x ", buf_tx[38]);/*��λ״̬*/
						pbuf += 5;
						kal_sprintf(pbuf, "0x%02x ", buf_tx[39]);/*����״̬*/
						pbuf += 5;
						kal_sprintf(pbuf, "%d ", buf_tx[40]);/*����*/
						pbuf += strlen(pbuf);
						kal_sprintf(pbuf, "%c ", buf_tx[41]);/*���ȱ�־*/
						pbuf += 2;
						kal_sprintf(pbuf, "%c ", buf_tx[42]);/*γ�ȱ�־*/
						pbuf += 2;
						kal_sprintf(pbuf, "0x%02x ", buf_tx[43]);/*REV*/
						pbuf += 5;
						kal_sprintf(pbuf, "%d:", (buf_tx[44] << 8) | (buf_tx[45]));/*����*/
						pbuf += strlen(pbuf);
						kal_sprintf(pbuf, "%02d:", (buf_tx[46]));/*��Ӫ��*/
						pbuf += 3;
						kal_sprintf(pbuf, "%d:", (buf_tx[47] << 8) | (buf_tx[48]));/*С�����*/
						pbuf += strlen(pbuf);
						kal_sprintf(pbuf, "%d ", (buf_tx[49] << 8) | (buf_tx[50]));/*��վ����*/
						pbuf += strlen(pbuf);
						kal_sprintf(pbuf, "0x%02x ", buf_tx[51]);/*REV*/
						pbuf += 5;
						kal_sprintf(pbuf, "-%d ", (buf_tx[52]));/*�ź���*/
						pbuf += strlen(pbuf);
						kal_sprintf(pbuf, "%02d-%02d-%02d ", buf_tx[53], buf_tx[54], buf_tx[55]);/*��վʱ��1*/
						pbuf += 9;
						kal_sprintf(pbuf, "%02d:%02d:%02d ", buf_tx[56], buf_tx[57], buf_tx[58]);/*��վʱ��2*/
						pbuf += 9;
						kal_sprintf(pbuf, "0x%02x 0x%02x ", buf_tx[72], buf_tx[73]);/*����*/
						pbuf += 10;
						for (idx = 74; idx < (send_len_bak - 3); idx++)/*WiFi����*/
						{
							if (buf_tx[idx] == 0) break;

							if (buf_tx[idx] != 0xFF)
							{
								*pbuf++ = buf_tx[idx];
							}
							else
							{
								*pbuf++ = ' ';
							}
						}
					}
					else
					{
						for (idx = 20; idx < (send_len_bak - 4); idx++)
						{
							kal_sprintf(pbuf, "%02x ", buf_tx[idx]);
							pbuf += 3;
						}
						kal_sprintf(pbuf, "%02x", buf_tx[idx]);
						pbuf += 2;
					}
					kal_sprintf(pbuf, "> ");
					pbuf += 2;

					kal_sprintf(pbuf, "%02x %02x ", buf_tx[send_len_bak - 3], buf_tx[send_len_bak - 2]);
					pbuf += 6;
					kal_sprintf(pbuf, "0x%02x\0", buf_tx[send_len_bak - 1]);
					pbuf += 4;

					mx_log_srv_write(head, strlen(head), KAL_TRUE);
					mxapp_trace("%s", head);
				}
			}
#endif
		}
	}

	return ret;
}


// ��ת�崦�����ش���󳤶�
static kal_int32 mx_srv_data_de_escape(kal_uint8 *dat, kal_uint32 in_len)
{
	kal_uint32 idx, buf_idx;
	kal_uint8 buf[MXAPP_ONENET_BUFF_MAX];
	kal_uint8 temp1, temp2;

	buf_idx = 0;
	for (idx = 0; idx < in_len;)
	{
		temp1 = dat[idx];
		temp2 = 0;
		if (idx < in_len - 1)
		{
			temp2 = dat[idx + 1];
		}
		
		if (temp1 == 0x7d && temp2 == 0x02)
		{
			buf[buf_idx++] = 0x7e;
			idx += 2;
		}
		else if (temp1 == 0x7d && temp2 == 0x01)
		{
			buf[buf_idx++] = 0x7d;
			idx += 2;
		}
		else
		{
			buf[buf_idx++] = temp1;
			idx += 1;
		}
	}

	kal_mem_cpy(dat, buf, buf_idx);
	return buf_idx;
}


// ������Ϣ ���ݴ���
// ��ʽ: ��ʶλ+��Ϣͷ+��Ϣ��+У����+��ʶλ
// ����: ��ת�塪��>��֤У���롪��>������Ϣ
// ������Ϣ�峤��
static kal_int32 mx_srv_data_parse(kal_uint8 src, kal_uint8 *in, kal_uint32 in_len, kal_uint32 *cmd, kal_uint16 *flow, kal_uint8 *msg)
{
	kal_uint8 *buf = in;
	kal_int32 buf_len = -1; // ��ת��󳤶�
	kal_uint8 cs = 0; // У����
	kal_uint32 idx = 0;
	kal_uint16 msg_len = -1; // ��Ϣ�峤��

	if (src != 0) return -1; // srcΪ���ò�����Ĭ��Ϊ0
	if (!cmd || !flow || !msg) return -1; // ������

	/*����ʶλ*/
	if ((buf[0] != 0x7e) || (buf[in_len-1] != 0x7e)) return -1;

	/*��ת��*/
	buf_len = mx_srv_data_de_escape(&buf[1], in_len - 2); // ��ȥ��ʶλ�ĳ���
	buf_len += 2; // ��ת����ܳ���

	/*��֤У����*/
	cs = buf[1];
	for (idx = 2; idx < buf_len - 2; idx++)
	{
		cs ^= buf[idx];
	}
	if (buf[buf_len - 2] != cs) return -1;

	/*��ϢID*/
	*cmd = (buf[1] << 8) | buf[2];

	/*��Ϣ�峤��*/
	msg_len = (buf[3] << 8) | buf[4];

	/*��Ϣ��ˮ��*/
	*flow = (buf[11] << 8) | buf[12];

	if ((msg_len & 0x20) == 0) // ���ְ�
	{
		kal_mem_cpy(msg, &buf[13], msg_len); // ��Ϣ������
	}
	else // �ְ�
	{
	}

	return msg_len;
}



// �ն˽���ƽ̨��Ϣ
kal_int32 mx_srv_receive_handle_jtt(kal_uint8 src, kal_uint8 *in, kal_int32 in_len)
{	
	kal_int32 ret = 0;
	kal_uint32 cmd = 0; // ��ϢID
	kal_uint16 flow = 0; // ��Ϣ��ˮ��
	kal_uint8 *msg = g_s8TmpBuf; // ��Ϣ������
	mx_cmd_handle_struct *func_l = NULL;
	kal_uint32 idx = 0;

	if ((in == NULL) || (in_len < 1)) return -1;

	kal_mem_set(msg, 0, sizeof(g_s8TmpBuf));
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
	if (!func_l)
	{
		mxapp_trace("--->>mx_srv_receive_handle(cmd[%04x] undef!!!!)\n", cmd);
	}
	else
	{
		mxapp_trace("--->>mx_srv_receive_handle([%d]=%04x,%04x)\n", idx, func_l->cmd, cmd);
	}

	/*ִ��CMD��Ӧ������*/
	if ((func_l) && (func_l->handle))
	{
		ret = func_l->handle(cmd, msg, ret, NULL);
	}

	/*TODO: �������Ӧ��*/
	if (cmd == MXAPP_SRV_JTT_CMD_DOWN_SET_PARA) // �ն�ͨ��Ӧ��
	{
		kal_uint8 cmd_ack[5] = { 0 };

		cmd_ack[0] = flow >> 8;
		cmd_ack[1] = flow & 0xFF;
		cmd_ack[2] = cmd >> 8;
		cmd_ack[3] = cmd & 0xFF;
		cmd_ack[4] = ret; // ִ�н��

		ret = mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_ACK, cmd_ack, 5);
	}
	else if (cmd == MXAPP_SRV_JTT_CMD_DOWN_LOC_QUERY)
	{

	}

	return ret;
}

kal_int32 mx_srv_register_jtt(void)
{
	mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_REGISTER, NULL, 0);
}

kal_int32 mx_srv_heartbeat_jtt(void)
{
	mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_HEARTBEAT, NULL, 0);
}




kal_int32 mxapp_srvinteraction_upload_location_info(ST_MX_LOCATION_INFO *pLocateInfo)
{
	kal_int32 s32Ret = 0;

	if(pLocateInfo == NULL)
	{
		pLocateInfo = mx_location_get_latest_position();
	}

	if(pLocateInfo->valid)
	{
		mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_LOC_REPORT, NULL, 0);
	}

#if MX_LBS_UPLOAD_FILTER
	if(pLocateInfo->valid)
	{
		if(pLocateInfo->type == LOCATION_TYPE_LBS)
		{
			mx_location_lbs_check_repeat(1, &(pLocateInfo->info.lbs)); // set
		}
		else
		{
			mx_location_lbs_check_repeat(1, NULL); // clear
		}
	}
#endif

	return s32Ret;
}

static kal_int8 srvinteraction_location_cb(ST_MX_LOCATION_INFO *pParams)
{
#if 0 // ԭOneNet���룬�ɱ������˴�Ϊ����
	kal_int8 s8Ret = 0;

	mxapp_srvinteraction_upload_location_info(pParams);

	if (g_s32PowerOff==1)
	{
		g_s32PowerOff = 2;/*!=0,,��ֹ���·���͵�ػ���λ����*/

		mxapp_trace("srvinteraction poweroff");

#if defined(__WHMX_MXT1608S__)
		// buzzer 1s
		mxapp_buzzer_ctrl(MX_BUZZER_ONCE);
		// start led with buzzer
		mxapp_led_ctrl(MX_LED_POWER_OFF, MX_PLAY_ONCE);
#endif

		StopTimer(MXAPP_TIMER_NET_UPLOAD);
		StartTimer(MXAPP_TIMER_NET_UPLOAD, (10 * 1000), mxapp_srvinteraction_poweroff);
	}

	return s8Ret;
#else
	mxapp_srvinteraction_upload_location_info(pParams);
#endif
}


static void srvinteraction_location_request(kal_int8 type)
{
#if 0 // ԭOneNet���룬�ɱ������˴�Ϊ����
	kal_prompt_trace(MOD_MMI, "srvinteraction_location_request (type=%d) --------------> \r\n", type);

	switch (type)
	{
	case 0:	// GNSS + LBS
		mx_location_request(srvinteraction_location_cb);
		break;
	case 1: // LBS only
		//		mx_location_request_LBS_only(srvinteraction_location_cb);
		break;
	case 2:
		//		mx_location_request(srvinteraction_bootup_location_cb);
		break;
	case 3: // sos-1
		mx_location_request_NO_GNSS(srvinteraction_location_cb);
		break;
	case 4: // sos-2
		mx_location_request_ignore_movement(srvinteraction_location_cb);
		break;
	}
#else
	srvinteraction_location_cb(NULL);
#endif
}


void main(void)
{
	kal_uint8 ret;

	mx_srv_register_jtt();

	kal_uint8 buf_in2[] = { 0x7E, 0x81, 0x03, 0x00, 0x1A, 0x01, 0x20, 0x00, 0x18, 0x71, 0x48, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x01, 0x04, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x48, 0x0B, 0x31, 0x33, 0x38, 0x30, 0x38, 0x36, 0x32, 0x38, 0x38, 0x36, 0x33, 0xD2, 0x7E };
	ret = mx_srv_receive_handle_jtt(0, buf_in2, sizeof(buf_in2));

	kal_uint8 buf_in[] = { 0x7E, 0x80, 0x01, 0x00, 0x05, 0x02, 0x00, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x25, 0x01, 0x00, 0x01, 0xB6, 0x7E }; // 0x8001 ƽ̨ͨ��Ӧ��
	ret = mx_srv_receive_handle_jtt(0, buf_in, sizeof(buf_in));

	kal_uint8 buf_in1[] = { 0x7E, 0x81, 0x00, 0x00, 0x09, 0x02, 0x00, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x25, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x15, 0xAD, 0x7E }; // 0x8100 �ն�ע��Ӧ��
	ret = mx_srv_receive_handle_jtt(0, buf_in1, sizeof(buf_in1));



	kal_uint8 tmp0[] = { 0x30,0x7e,0x08,0x7d,0x55 };
	ret = mx_srv_data_escape(tmp0, sizeof(tmp0));
	ret = mx_srv_data_de_escape(tmp0, ret);

	mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_REGISTER, NULL, 0);

	memset(g_s8TxBuf, 1, MXAPP_ONENET_BUFF_MAX);
	mx_srv_cmd_handle_up_register(0, NULL, 0, g_s8TxBuf);

	kal_uint8 tmp[] = { 0x00, 0x25, 0x00, 0x02, 0x31, 0x32, 0x30, 0x30, 0x30, 0x31, 0x38, 0x37, 0x31, 0x34, 0x38 };
	memcpy(g_s8RxBuf, tmp, sizeof(tmp));
	mx_srv_cmd_handle_down_register_ack(0, g_s8RxBuf, sizeof(tmp), NULL);

	mx_srv_cmd_handle_up_authorize(0, NULL, 0, g_s8TxBuf);

	mx_srv_cmd_handle_up_loc_report(0, NULL, 0, g_s8TxBuf);

	kal_uint8 tmp1[] = { 0x00, 0x02, 0x02, 0x00, 0x04 };
	memcpy(g_s8RxBuf, tmp1, sizeof(tmp));
	mx_srv_cmd_handle_down_ack(0, g_s8RxBuf, sizeof(tmp), NULL);
}