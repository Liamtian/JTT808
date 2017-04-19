#if 1
#include "mxapp_srv_jtt.h"
#include "mxapp_srvinteraction_jtt.h"

#define USE_IMEI_AS_CELL_NUM // ʹ��IMEI��12λ��Ϊ�ն˺�
#define USE_JTT808_SERVER_1_SIM_DEVICE // ����ʱʹ��ƽ̨1ģ���ն˺�:12000187148

#define	MXAPP_JTT_BUFF_MAX	(384)
#define CELL_NUMBER_LEN (DEVICE_NUM_LEN) // �ֻ��ų���
#define	FRAME_FLAG_SYMBOL	0x7E // ��ʶλ

#define	PHONE_NO_LEN		MX_APP_CALL_NUM_MAX_LEN

static kal_int32 g_s32FirstConnect = 1;
static kal_int32 g_s32PowerOff = 0;

static kal_uint8 g_s8TmpBuf[MXAPP_JTT_BUFF_MAX];
static kal_uint8 g_s8RxBuf[MXAPP_JTT_BUFF_MAX];
static kal_uint8 g_s8TxBuf[MXAPP_JTT_BUFF_MAX];

static kal_int8 g_s8Numfamily[PHONE_NO_LEN];

nvram_ef_mxapp_jtt_config_t jtt_config;
//static kal_uint8 auth_code[AUTH_CODE_LEN]; // ��Ȩ��
//static kal_uint8 auth_code_len; // ��Ȩ�볤�ȣ�Ĭ��0��
//static kal_uint8 cell_number[CELL_NUMBER_LEN]; // �ն��ֻ��ţ�[0]�������λ

static kal_uint8 shutdown_is_active = 0; // ϵͳ���ڹػ�
static kal_uint16 serial_no_up = 0; // ������Ϣ��ˮ��
static kal_uint16 serial_no_down = 0; // ������Ϣ��ˮ��
static kal_uint16 serial_no_loc_query_lastest = 0; // ���µ� λ����Ϣ��ѯ ��ˮ��
static mx_login_status login_status;

mx_srv_cb mx_srv_send_handle_callback = NULL;

static nvram_ef_mxapp_pos_mode_t mx_pos_mode;

/*
	0x00 - ��ͨ�ϴ�
	0x0C - �ػ�
	0x0D - ��������ʼֵ��
	0x10 - �����ϴ���λ����Ϣ��ѯ��
	0x11 - ����͸��
	����0x80 - ��������
	0x80 - SOS����
	0x90 - �͵籨��
*/
static kal_uint8 pos_info_type = 0x0D;	/*��ʼֵ��ʾ����*/


/********************
��������
********************/
static void srvinteraction_location_request(kal_int8 type);
static kal_int32 mx_srv_register_jtt(mx_srv_cb reg_cb);
static kal_int32 mx_srv_deregister_jtt(mx_srv_cb dereg_cb);
static kal_int32 mx_srv_authorize_jtt(mx_srv_cb auth_cb);
static void mx_srv_register_cb(void * para);
static void mx_srv_deregister_cb(void * para);
static void mx_srv_authorize_cb(void * para);
static void mxapp_srvinteraction_poweroff(void);
static kal_int32 mx_srv_config_nv_write(kal_uint8 item);


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

// ���ö�λģʽ
kal_uint8 mx_pos_mode_set(kal_uint8 mode)
{
#if (DEBUG_IN_VS == 0)
#if defined(__WHMX_MXT1608S__)
	if(mode == 3)
	{
		mx_location_mode_change(LOCATION_EVT_RMC, LOCATION_MODE_NORMAL_PERIOD);
		mx_location_dynamic_period_enable(KAL_TRUE); // enable dynamic period
	}
	else
#endif
	if(mode == 2)
	{
		mx_location_mode_change(LOCATION_EVT_RMC, LOCATION_MODE_FAST_PERIOD);
#if defined(__WHMX_MXT1608S__)
		mx_location_dynamic_period_enable(KAL_FALSE); // disable dynamic period
#endif
	}
	else if(mode == 1)
	{
		mx_location_mode_change(LOCATION_EVT_RMC, LOCATION_MODE_NORMAL_PERIOD);
#if defined(__WHMX_MXT1608S__)
		mx_location_period_change(mx_pos_mode.custom_period);
		mx_location_dynamic_period_enable(KAL_FALSE); // disable dynamic period
#endif
	}
	else
	{
		mx_location_mode_change(LOCATION_EVT_RMC, LOCATION_MODE_ONE_TIME);
#if defined(__WHMX_MXT1608S__)
		mx_location_dynamic_period_enable(KAL_FALSE); // disable dynamic period
#endif
	}

	mx_pos_mode.dev_mode = mode;
	mx_srv_config_nv_write(1);
	
#endif
	return 0;
}

// ���ö�λ����
kal_uint8 mx_pos_period_set(kal_uint8 period_min)
{
	if (mx_pos_mode.dev_mode == 1) // 1-��׼ģʽ
	{
		mx_pos_mode.custom_period = period_min;
		mx_srv_config_nv_write(1);
		mx_location_period_change(period_min);
	}
	return 0;
}

// �����ն��������ͼ��
// ���� 0:�ɹ� -1:ʧ��
static kal_int32 mx_srv_set_para_heartbeat(kal_uint8 *in, kal_uint32 in_len)
{
	kal_int8 ret = -1;
	kal_uint32 heartbeat = 0; // unit: s

	if (!in || in_len != 4) return -1;

	heartbeat |= in[0];
	heartbeat <<= 8;
	heartbeat |= in[1];
	heartbeat <<= 8;
	heartbeat |= in[2];
	heartbeat <<= 8;
	heartbeat |= in[3];

	// �����ն��������ͼ��
	ret = mxapp_srv_heart_set(heartbeat);

	return ret;
}

// ���ü����绰����
// ����: 0-�ɹ�
static kal_int32 mx_srv_set_para_monitor_num(kal_uint8 *in, kal_uint32 in_len)
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
static kal_int32 mx_srv_set_para_loc_type(kal_uint8 *in, kal_uint32 in_len)
{
	kal_uint8 loc_type = -1;

	if (!in || in_len != 1) return -1;

	loc_type = in[0];
	if (loc_type == 0 || loc_type == 1) // 0-ʡ��ģʽ 1-��׼ģʽ
	{
		mx_pos_mode_set(loc_type);
	}

	return 0;
}

// ���ö�λ�����ϴ�����
// ����: 0-�ɹ�
// �ο�mx_srv_cmd_handle_down_period
static kal_int32 mx_srv_set_para_loc_prop(kal_uint8 *in, kal_uint32 in_len)
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

	mx_pos_period_set(period_min);

	return 0;
}

static mx_set_para_handle_struct set_para_func[] =
{
	{ MXAPP_SRV_JTT_PARA_HEARTBEAT, mx_srv_set_para_heartbeat }, // �ն��������ͼ��
	{ MXAPP_SRV_JTT_PARA_MONITOR_NUM, mx_srv_set_para_monitor_num }, // �����绰����
	{ MXAPP_SRV_JTT_PARA_LOC_TYPE, mx_srv_set_para_loc_type }, // ��λ�����ϴ���ʽ
	{ MXAPP_SRV_JTT_PARA_LOC_PROP, mx_srv_set_para_loc_prop }, // ��λ�����ϴ�����

	{ MXAPP_SRV_JTT_PARA_INVALID, NULL }
};


// ��ѯ�ն��������ͼ��
// ��ѯ������浽out����ʽ������ID+��������+����ֵ��
// ���ز�ѯ������ȣ�-1��ʾʧ��
kal_int32 mx_srv_get_para_heartbeat(kal_uint8 *out)
{
	kal_uint8 idx = 0;
	kal_uint32 heart = mxapp_srv_heart_get();

	if (!out) return -1;

	/*����ID*/
	out[idx++] = (MXAPP_SRV_JTT_PARA_HEARTBEAT >> 24) & 0xFF;
	out[idx++] = (MXAPP_SRV_JTT_PARA_HEARTBEAT >> 16) & 0xFF;
	out[idx++] = (MXAPP_SRV_JTT_PARA_HEARTBEAT >> 8) & 0xFF;
	out[idx++] = MXAPP_SRV_JTT_PARA_HEARTBEAT & 0xFF;

	/*��������*/
	out[idx++] = 4; // DWORD

	/*����ֵ*/
	out[idx++] = (heart >> 24) & 0xFF;
	out[idx++] = (heart >> 16) & 0xFF;
	out[idx++] = (heart >> 8) & 0xFF;
	out[idx++] = heart & 0xFF;

	return idx; // 9
}

// ��ѯ�����绰����
// ��ѯ������浽out����ʽ������ID+��������+����ֵ��
// ���ز�ѯ������ȣ�-1��ʾʧ��
kal_int32 mx_srv_get_para_monitor_num(kal_uint8 *out)
{
	kal_uint8 idx = 0;
	kal_char *num = g_s8Numfamily;

	if (!out) return -1;

	/*����ID*/
	out[0] = (MXAPP_SRV_JTT_PARA_MONITOR_NUM >> 24) & 0xFF;
	out[1] = (MXAPP_SRV_JTT_PARA_MONITOR_NUM >> 16) & 0xFF;
	out[2] = (MXAPP_SRV_JTT_PARA_MONITOR_NUM >> 8) & 0xFF;
	out[3] = MXAPP_SRV_JTT_PARA_MONITOR_NUM & 0xFF;

	/*��������*/
	out[4] = PHONE_NO_LEN;

	/*����ֵ*/
	for (idx = 0; idx < PHONE_NO_LEN; idx++)
	{
		out[5 + idx] = num[idx];
	}

	return (idx+5); // 20
}

// ��ѯ��λ�����ϴ���ʽ
// ��ѯ������浽out����ʽ������ID+��������+����ֵ��
// ���ز�ѯ������ȣ�-1��ʾʧ��
kal_int32 mx_srv_get_para_loc_type(kal_uint8 *out)
{
	kal_uint8 idx = 0;
	kal_uint8 loc_type = mx_pos_mode.dev_mode;
	
	if (loc_type != 0)
	{
		loc_type = 1; // ֻ֧��0��ʡ�磬���ϴ�����1����׼����ʱ�����ϴ�������
	}

	if (!out) return -1;

	/*����ID*/
	out[idx++] = (MXAPP_SRV_JTT_PARA_LOC_TYPE >> 24) & 0xFF;
	out[idx++] = (MXAPP_SRV_JTT_PARA_LOC_TYPE >> 16) & 0xFF;
	out[idx++] = (MXAPP_SRV_JTT_PARA_LOC_TYPE >> 8) & 0xFF;
	out[idx++] = MXAPP_SRV_JTT_PARA_LOC_TYPE & 0xFF;

	/*��������*/
	out[idx++] = 1; // BYTE

	/*����ֵ*/
	out[idx++] = loc_type;

	return idx; // 6
}

// ��ѯ��λ�����ϴ�����
// ��ѯ������浽out����ʽ������ID+��������+����ֵ��
// ���ز�ѯ������ȣ�-1��ʾʧ��
kal_int32 mx_srv_get_para_loc_prop(kal_uint8 *out)
{
	kal_uint8 idx = 0;
	kal_uint32 loc_prop = mx_pos_mode.custom_period * 60; // unit: s

	if (!out) return -1;

	/*����ID*/
	out[idx++] = (MXAPP_SRV_JTT_PARA_LOC_PROP >> 24) & 0xFF;
	out[idx++] = (MXAPP_SRV_JTT_PARA_LOC_PROP >> 16) & 0xFF;
	out[idx++] = (MXAPP_SRV_JTT_PARA_LOC_PROP >> 8) & 0xFF;
	out[idx++] = MXAPP_SRV_JTT_PARA_LOC_PROP & 0xFF;

	/*��������*/
	out[idx++] = 4; // DWORD

	/*����ֵ*/
	out[idx++] = (loc_prop >> 24) & 0xFF;
	out[idx++] = (loc_prop >> 16) & 0xFF;
	out[idx++] = (loc_prop >> 8) & 0xFF;
	out[idx++] = loc_prop & 0xFF;

	return idx; // 9
}

// ��ѯ����
// ��ѯ������浽out����ʽ������ID+��������+����ֵ��
// ���ز�ѯ������ȣ�-1��ʾʧ��
kal_int32 mx_srv_get_para_battery(kal_uint8 *out)
{
	kal_uint8 idx = 0;
	kal_uint8 bat = mxapp_battery_get_voltage_percent();

	if (!out) return -1;

	/*����ID*/
	out[idx++] = (MXAPP_SRV_JTT_PARA_BATTERY >> 24) & 0xFF;
	out[idx++] = (MXAPP_SRV_JTT_PARA_BATTERY >> 16) & 0xFF;
	out[idx++] = (MXAPP_SRV_JTT_PARA_BATTERY >> 8) & 0xFF;
	out[idx++] = MXAPP_SRV_JTT_PARA_BATTERY & 0xFF;

	/*��������*/
	out[idx++] = 1; // BYTE

	/*����ֵ*/
	out[idx++] = bat;

	return idx; // 6
}

static mx_get_para_handle_struct get_para_func[] =
{
	{ MXAPP_SRV_JTT_PARA_HEARTBEAT, mx_srv_get_para_heartbeat }, // �ն��������ͼ��
	{ MXAPP_SRV_JTT_PARA_MONITOR_NUM, mx_srv_get_para_monitor_num }, // �����绰����
	{ MXAPP_SRV_JTT_PARA_LOC_TYPE, mx_srv_get_para_loc_type }, // ��λ�����ϴ���ʽ
	{ MXAPP_SRV_JTT_PARA_LOC_PROP, mx_srv_get_para_loc_prop }, // ��λ�����ϴ�����

	/*�Զ���*/
	{ MXAPP_SRV_JTT_PARA_BATTERY, mx_srv_get_para_battery }, // ����

	{ MXAPP_SRV_JTT_PARA_INVALID, NULL }
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

	mxapp_trace("%s: len_l=%d(%x)", __FUNCTION__, len_l, len_l);

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

	mxapp_trace("%s: len_l=%d(%x)", __FUNCTION__, len_l, len_l);

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

	mxapp_trace("%s: len_l=%d(%x)", __FUNCTION__, len_l, len_l);

	return len_l;
}

// �ն�ע��
static kal_int32 mx_srv_cmd_handle_up_deregister(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
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

	mxapp_trace("%s: len_l=%d(%x)", __FUNCTION__, len_l, len_l);

	return len_l;
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
		memcpy(dat_l, jtt_config.auth_code, strlen(jtt_config.auth_code));
		len_l = strlen(jtt_config.auth_code);

		dat_l[len_l] = '\0';
	}

	mxapp_trace("%s: len_l=%d(%x)", __FUNCTION__, len_l, len_l);

	return len_l;
}

// ��ѯ�ն˲���Ӧ��
static kal_int32 mx_srv_cmd_handle_up_query_para_ack(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	kal_int32 len_l = -1;
	kal_uint8 *dat_l = out;

	if (dat_l == NULL) return len_l;

	// ������ǰ׼��������
	if (in&&in_len)
	{
		kal_mem_cpy(dat_l, in, in_len);
		len_l = in_len;
	}

	mxapp_trace("%s: len_l=%d(%x)", __FUNCTION__, len_l, len_l);

	return len_l;
}

// ��ѯ�ն�����Ӧ��
static kal_int32 mx_srv_cmd_handle_up_query_prop_ack(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	kal_int32 len_l = -1;
	kal_uint8 *dat_l = out;
	kal_uint8 idx = 0;
	kal_uint8 i;

	kal_uint8 verno[100] = { 0 }; // �ն˹̼��汾��
	kal_uint8 verno_len = 0; // �ն˹̼��汾�ų���

	kal_uint8 hd_verno_len = 0; // �ն�Ӳ���汾�ų��ȣ�Ĭ��0

	if (dat_l == NULL) return len_l;

	if (in&&in_len)
	{
		kal_mem_cpy(dat_l, in, in_len);
		len_l = in_len;
	}
	else
	{
#if defined(__WHMX_BT_SUPPORT__)
		// 6261�汾�� GNSS�̼��汾�� �����汾��
		snprintf(verno, 100, "%s|%s|%s", release_verno(), gnss_verno(), bt_verno());
#else
		// 6261�汾�� GNSS�̼��汾��
		snprintf(verno, 100, "%s|%s", release_verno(), gnss_verno());
#endif
		verno_len = strlen(verno);

		for (i = 0; i < 2; i++)
		{
			dat_l[idx++] = 0; // �ն�����
		}

		for (i = 0; i < 5; i++)
		{
			dat_l[idx++] = 0; // ������ID
		}

		for (i = 0; i < 20; i++)
		{
			dat_l[idx++] = 0; // �ն��ͺ�
		}

		for (i = 0; i < 7; i++)
		{
			dat_l[idx++] = 0; // �ն�ID
		}

		for (i = 0; i < 10; i++)
		{
			dat_l[idx++] = 0; // �ն�SIM��ICCID
		}

		dat_l[idx++] = hd_verno_len; // �ն�Ӳ���汾�ų���
		for (i = 0; i < hd_verno_len; i++)
		{
			dat_l[idx++] = 0; // �ն�Ӳ���汾��
		}

		dat_l[idx++] = verno_len; // �ն˹̼��汾�ų���
		kal_mem_cpy(&dat_l[idx], verno, verno_len); // �ն˹̼��汾��
		idx += verno_len;

		dat_l[idx++] = 3; // GNSSģ�����ԣ�֧��GPS��������λ��
		dat_l[idx++] = 1; // ͨ��ģ�����ԣ�֧��GPRSͨ�ţ�

		len_l = idx;
	}

	mxapp_trace("%s: len_l=%d(%x)", __FUNCTION__, len_l, len_l);

	return len_l;
}

// λ����Ϣ�㱨
static kal_int32 mx_srv_cmd_handle_up_loc_report(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	kal_uint32 alarm = 0;
	kal_uint32 status = 0;
	kal_uint32 lat = 0;
	kal_uint32 lon = 0;
	kal_uint16 alt = 0;
	kal_uint16 speed = 0; // unit: 1/10km/h
	kal_uint16 course = 0;
	
	kal_int32 len_l = -1;
	kal_uint8 *dat_l = out;
	kal_uint32 i = 0, len = 0;

	kal_uint8 pos_info_type;
	ST_MX_LOCATION_INFO *pos_info;
	MYTIME stCurrentTime;

	if (dat_l == NULL) return len_l;

	if (in&&in_len)
	{
		kal_mem_cpy(dat_l, in, in_len);
		len_l = in_len;
	}
	else
	{
		len_l = 0;

		/*λ����Ϣ�㱨start*/
		/*������ʶλ*/
		pos_info_type = mx_pos_info_type_get();
		if (pos_info_type == 0x80) // 0x80 - SOS����
		{
			alarm = 0x01<<0;
		}
		else if (pos_info_type == 0x90) // 0x80 - �͵籨��
		{
			alarm = 0x01<<7;
		}
		mx_pos_info_type_set(0x00); // �ָ���ͨ�ϴ�
		mxapp_trace("alarm=%x", alarm);

		/*��λ�������*/
#if (DEBUG_IN_VS == 0) // ԭOneNet���룬�ɱ������˴�Ϊ����
		pos_info = mx_location_get_latest_position();
		if ((pos_info->valid) && (pos_info->type == LOCATION_TYPE_GPS))
		{
			status |= (0x01 << 1); // ��λ
			if(pos_info->info.gps.N_S == 'S')
				status |= (0x01 << 2); // ��γ
			if(pos_info->info.gps.E_W == 'W')
				status |= (0x01 << 3); // ����
			status |= (0x01 << 18); // ʹ��GPS��λ
			status |= (0x01 << 19); // ʹ�ñ�����λ
			
			lat = 0.5+(mx_base_coordinates2double(&(pos_info->info.gps.lat[0])) * 1000000);
			lon = 0.5+(mx_base_coordinates2double(&(pos_info->info.gps.lon[0])) * 1000000);
			speed = pos_info->info.gps.speed * 10;
			course = pos_info->info.gps.course;
		}
#else
		lat = 30000000;
		lon = 114000000;
#endif
		mxapp_trace("status=%x", status); // ��λʱһ��Ϊ0xC002 δ��λΪ0
		mxapp_trace("lat=%d,lon=%d,alt=%d,speed=%d,course=%d", lat, lon, alt, speed, course);

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
#if (DEBUG_IN_VS == 0) // ԭOneNet���룬�ɱ������˴�Ϊ����
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

		/*λ����Ϣ�㱨end*/

		dat_l[len_l] = '\0';
	}

	mxapp_trace("%s: len_l=%d(%x)", __FUNCTION__, len_l, len_l);

	return len_l;
}

// λ����Ϣ��ѯӦ��
static kal_int32 mx_srv_cmd_handle_up_loc_query_ack(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	kal_uint32 alarm = 0;
	kal_uint32 status = 0;
	kal_uint32 lat = 0;
	kal_uint32 lon = 0;
	kal_uint16 alt = 0;
	kal_uint16 speed = 0; // unit: 1/10km/h
	kal_uint16 course = 0;

	kal_int32 len_l = -1;
	kal_uint8 *dat_l = out;
	kal_uint32 i = 0, len = 0;

	kal_uint8 pos_info_type;
	ST_MX_LOCATION_INFO *pos_info;
	MYTIME stCurrentTime;

	if (dat_l == NULL) return len_l;

	if (in&&in_len)
	{
		kal_mem_cpy(dat_l, in, in_len);
		len_l = in_len;
	}
	else
	{
		len_l = 0;

		/*Ӧ����ˮ��*/
		dat_l[len_l++] = (serial_no_loc_query_lastest >> 8) & 0xFF;
		dat_l[len_l++] = serial_no_loc_query_lastest & 0xFF;

		/*λ����Ϣ�㱨start*/
		/*������ʶλ*/
		pos_info_type = mx_pos_info_type_get();
		if (pos_info_type == 0x80) // 0x80 - SOS����
		{
			alarm = 0x01<<0;
		}
		else if (pos_info_type == 0x90) // 0x80 - �͵籨��
		{
			alarm = 0x01<<7;
		}
		mx_pos_info_type_set(0x00); // �ָ���ͨ�ϴ�
		mxapp_trace("alarm=%x", alarm);

		/*��λ�������*/
#if (DEBUG_IN_VS == 0) // ԭOneNet���룬�ɱ������˴�Ϊ����
		pos_info = mx_location_get_latest_position();
		if ((pos_info->valid) && (pos_info->type == LOCATION_TYPE_GPS))
		{
			status |= (0x01 << 1); // ��λ
			if(pos_info->info.gps.N_S == 'S')
				status |= (0x01 << 2); // ��γ
			if(pos_info->info.gps.E_W == 'W')
				status |= (0x01 << 3); // ����
			status |= (0x01 << 18); // ʹ��GPS��λ
			status |= (0x01 << 19); // ʹ�ñ�����λ
			
			lat = 0.5+(mx_base_coordinates2double(&(pos_info->info.gps.lat[0])) * 1000000);
			lon = 0.5+(mx_base_coordinates2double(&(pos_info->info.gps.lon[0])) * 1000000);
			speed = pos_info->info.gps.speed * 10;
			course = pos_info->info.gps.course;
		}
#else
		lat = 30000000;
		lon = 114000000;
#endif
		mxapp_trace("status=%x", status); // ��λʱһ��Ϊ0xC002 δ��λΪ0
		mxapp_trace("lat=%d,lon=%d,alt=%d,speed=%d,course=%d", lat, lon, alt, speed, course);

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
#if (DEBUG_IN_VS == 0) // ԭOneNet���룬�ɱ������˴�Ϊ����
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
		
		/*λ����Ϣ�㱨end*/

		dat_l[len_l] = '\0';
	}

	mxapp_trace("%s: len_l=%d(%x)", __FUNCTION__, len_l, len_l);

	return len_l;
}

// TODO: �¼�����
static kal_int32 mx_srv_cmd_handle_up_event_report(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
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

	mxapp_trace("content: ack_flow=%d ack_id=%x ret=%d", ack_flow, ack_id, ret);

	mxapp_trace("login_status: status=%d flow=%d", login_status.status, login_status.flow);
	if (ack_flow == login_status.flow)
	{
		StopTimer(MXAPP_TIMER_JTT_SERVER);
		if ((login_status.status == 2) && (ret == 0))
		{
			// ע���ɹ� ��ʼע��
			mx_srv_register_jtt(mx_srv_register_cb);
		}
		else if ((login_status.status == 1) && (ret == 0))
		{
			// ��Ȩ�ɹ�
			login_status.status = 4;

			mxapp_trace("first loc report-->");
			mx_srv_loc_report_jtt(); // ��½ƽ̨���ϴ�λ��
		}
		else if ((login_status.status == 1) && (ret != 0))
		{
			// ��Ȩʧ��
			login_status.status = 0;

			mxapp_trace("auth failed");
			mx_srv_auth_code_clear(); // ��ռ�Ȩ��
		}
	}
	
	return ret;
}

// �ն�ע��Ӧ��
// ����ֵ: 0-�ɹ� 1-�����ѱ�ע�� 2-���ݿ����޸ó��� 3-�ն��ѱ�ע�� 4-���ݿ����޸��ն�
static kal_int32 mx_srv_cmd_handle_down_register_ack(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	kal_uint8 *dat_l = in;
	kal_uint8 tmp;

	kal_uint16 ack_flow = 0; // Ӧ����ˮ��
	kal_int32 ret = -1; // ���
	kal_uint8 *auth = jtt_config.auth_code; // ��Ȩ��

	if ((dat_l == NULL) || (in_len == 0)) return -1;
	mxapp_trace("%s:cmd(%x) in_len(%d)", __FUNCTION__, cmd, in_len);

	/*Ӧ����ˮ��*/
	tmp = *dat_l++;
	ack_flow += tmp << 8;
	tmp = *dat_l++;
	ack_flow += tmp;

	/*���: 0-�ɹ� 1-�����ѱ�ע�� 2-���ݿ����޸ó��� 3-�ն��ѱ�ע�� 4-���ݿ����޸��ն�*/
	tmp = *dat_l++;
	ret = tmp;

	mxapp_trace("content: ack_flow=%d ret=%d", ack_flow, ret);

	if (ret == 0) // �ɹ�
	{
		/*��Ȩ��*/
		jtt_config.auth_code_len = in_len - 3; // ��Ȩ��ʵ�ʳ���
		kal_mem_cpy(auth, dat_l, jtt_config.auth_code_len);

		mx_srv_config_nv_write(2); // ���浽NVRAM

		// debug log
		mxapp_trace("auth_code:(len=%d)", jtt_config.auth_code_len);
		tmp = 0;
		while(tmp < jtt_config.auth_code_len)
		{
			mxapp_trace("%x", auth[tmp++]);
		}

		StopTimer(MXAPP_TIMER_JTT_SERVER);
		mx_srv_authorize_jtt(mx_srv_authorize_cb); // ��ʼ��Ȩ
	}

	return ret;
}

// �����ն˲���
// ����: 0-�ɹ�
static kal_int32 mx_srv_cmd_handle_down_set_para(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	kal_uint8 *dat_l = in;
	kal_uint32 tmp;

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

		para_idx++; 
	}

	return ret; // �������þ��ɹ�
}

// ��ѯ�ն˲���
// ��ѯ������ղ������б��浽out
static kal_int32 mx_srv_cmd_handle_down_query_para(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	/*��Ϣ��Ϊ��*/
	kal_int32 ret = 0;
	kal_uint32 idx = 0;
	mx_get_para_handle_struct *func_l = NULL;
	kal_uint8 *buf_tmp = NULL; // ���������ݣ���Ϣ��
	kal_uint8 para_num = 0; // ��������
	kal_uint16 out_len = 0; // ��Ϣ�峤��
	
	if (!out) return -1;

	buf_tmp = out + 2; // Ԥ��2�ֽڱ���Ӧ����ˮ��
	out_len++; // Ԥ��buf_tmp[0]�����������

	/*����ִ��ȫ������ID��Ӧ��ѯ����*/
	idx = 0;
	while (get_para_func[idx].para_id != MXAPP_SRV_JTT_PARA_INVALID)
	{
		func_l = &get_para_func[idx];

		/*ִ�в���ID��Ӧ���ú���*/
		if ((func_l) && (func_l->handle) && (out_len < MXAPP_JTT_BUFF_MAX))
		{
			ret = func_l->handle(&buf_tmp[out_len]);
			if (ret > 0)
			{
				out_len += ret;
				para_num++;
			}
		}
		idx++;
	}

	buf_tmp[0] = para_num;

	return out_len;
}

// ��ѯָ���ն˲���
// ����ID 0xF000~0xFFFFΪ�Զ��壨��ƽ̨��ϣ��ݲ�ʵ�֣�
static kal_int32 mx_srv_cmd_handle_down_query_spec_para(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	kal_int32 ret = 0;
	kal_uint32 idx = 0;
	kal_uint32 cmd_l = 0;
	kal_uint8 i,j;
	mx_get_para_handle_struct *func_l = NULL;
	kal_uint8 *buf_tmp = NULL; // ���������ݣ���Ϣ��
	kal_uint8 para_num = 0; // ��ѯ��������
	kal_uint16 out_len = 0; // ��Ϣ�峤��
	kal_uint8 para_num_ret = 0; // ��ѯ�����������

	if (!in || in_len == 0) return -1;
	if (!out) return -1;

	buf_tmp = out + 2; // Ԥ��2�ֽڱ���Ӧ����ˮ��
	out_len++; // Ԥ��buf_tmp[0]�����������

	para_num = in[idx++];
	for (i = 0; i < para_num; i++)
	{
		cmd_l = (in[idx++] << 24);
		cmd_l += (in[idx++] << 16);
		cmd_l += (in[idx++] << 8);
		cmd_l += in[idx++];

		/*���Ҳ���ID��Ӧ��ѯ����*/
		j = 0;
		while (get_para_func[j].para_id != MXAPP_SRV_JTT_PARA_INVALID)
		{
			if (get_para_func[j].para_id == cmd_l)
			{
				func_l = &get_para_func[j];
				break;
			}
			j++;
		}

		/*ִ�в���ID��Ӧ��ѯ����*/
		if ((func_l) && (func_l->handle))
		{
			ret = func_l->handle(&buf_tmp[out_len]);
			if (ret > 0)
			{
				out_len += ret;
				para_num_ret++;
			}
		}
	}

	buf_tmp[0] = para_num_ret;

	return out_len;
}

// ��ѯ�ն�����
static kal_int32 mx_srv_cmd_handle_down_query_prop(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	/*��Ϣ��Ϊ��*/

	return 0;
}

// λ����Ϣ��ѯ
static kal_int32 mx_srv_cmd_handle_down_loc_query(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
	/*��Ϣ��Ϊ��*/

	mx_pos_info_type_set(0x10); // λ����Ϣ��ѯ
	srvinteraction_location_request(0);

	return 0;
}

// TODO: �¼�����
static kal_int32 mx_srv_cmd_handle_down_event_set(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out)
{
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
	kal_uint8 buf[MXAPP_JTT_BUFF_MAX];
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
		buf_l[5 + idx] = jtt_config.cell_num[idx]; // ��λ��ǰ
	}
	
	/*��Ϣ��ˮ�ţ���0��ʼѭ���ۼӣ�*/
	buf_l[11] = (serial_no_up >> 8);
	buf_l[12] = serial_no_up & 0xFF;
	login_status.flow = serial_no_up; // ����ؼ���Ϣ��ˮ��
	serial_no_up++; // ����

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

		if (ret >= 0)
		{
			kal_mem_set(buf_tx, 0, sizeof(g_s8TxBuf));
			ret = mx_srv_data_package(cmd, buf_tmp, ret, buf_tx); // ��װ��������Ϣ
			send_len_bak = ret;
			if (/*(g_s32FirstConnect == 0)&&*/(ret > 0))
			{
				// upload to server
				mxapp_srv_send(buf_tx, ret, mx_srv_send_handle_callback);
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
#if defined(__WHMX_SERVER_JTT808__)
					while(idx < ret)
					{
						kal_sprintf(pbuf, "%02x ", buf_tx[idx]);
						pbuf += 3;
						idx++;
					}
#elif defined(__WHMX_SERVER_ONENET__)					
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
#endif
					mx_log_srv_write(head, strlen(head), KAL_TRUE);
					for (idx = 0; idx < strlen(head);)
					{
						mxapp_trace("%s\r\n", (head + idx));
						idx += 127;
					}
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
	kal_uint8 buf[MXAPP_JTT_BUFF_MAX];
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
	kal_uint8 *msg = g_s8TmpBuf; // ������Ϣ������
	kal_uint8 *out = g_s8TxBuf; // �������
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

	kal_mem_set(out, 0, sizeof(g_s8TxBuf)); // ����

	/*ִ��CMD��Ӧ������*/
	if ((func_l) && (func_l->handle))
	{
		ret = func_l->handle(cmd, msg, ret, out); // ע�����
	}

	/*�������Ӧ��*/
	if (cmd == MXAPP_SRV_JTT_CMD_DOWN_SET_PARA) // �����ն˲����������ն�ͨ��Ӧ��
	{
		if(ret != 0) ret = 1; // 0�ɹ� 1ʧ��
		
		out[0] = flow >> 8; // Ӧ����ˮ��
		out[1] = flow & 0xFF;
		out[2] = cmd >> 8; // Ӧ��ID
		out[3] = cmd & 0xFF;
		out[4] = ret; // ִ�н��

		ret = mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_ACK, out, 5);
	}
	else if (cmd == MXAPP_SRV_JTT_CMD_DOWN_LOC_QUERY) // λ����Ϣ��ѯ������λ����Ϣ��ѯӦ��
	{
#if 0 // �ɶ�λ��Ӧ����srvinteraction_location_cbִ��Ӧ������
		kal_uint8 cmd_ack[2] = { 0 };

		cmd_ack[0] = flow >> 8; // Ӧ����ˮ��
		cmd_ack[1] = flow & 0xFF;

		ret = mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_LOC_QUERY_ACK, cmd_ack, 2);		
#else
		serial_no_loc_query_lastest = flow; // Ӧ����ˮ�ţ�ֻ�������һ����
#endif
	}
	else if (cmd == MXAPP_SRV_JTT_CMD_DOWN_QUERY_SPEC_PARA
		|| cmd == MXAPP_SRV_JTT_CMD_DOWN_QUERY_PARA) // ��ѯ�ն˲��������в�ѯ�ն˲���Ӧ��
	{
		out[0] = flow >> 8; // Ӧ����ˮ��
		out[1] = flow & 0xFF;

		ret = mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_QUERY_PARA_ACK, out, ret + 2);
	}
	else if (cmd == MXAPP_SRV_JTT_CMD_DOWN_QUERY_PROP) // ��ѯ�ն����ԣ����в�ѯ�ն�����Ӧ��
	{
		ret = mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_QUERY_PROP_ACK, NULL, 0);
	}

	return ret;
}

//�ն�ע��
static kal_int32 mx_srv_register_jtt(mx_srv_cb reg_cb)
{
	mxapp_trace("%s: enter", __FUNCTION__);

	// ������ˮ��
	serial_no_up = 0;
	serial_no_down = 0;
	serial_no_loc_query_lastest = 0;
	
	mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_REGISTER, NULL, 0);
	login_status.status = 3;
	StartTimer(MXAPP_TIMER_JTT_SERVER, 30*1000, reg_cb); // 30s��ʱ
	return 0;
}

//�ն�ע��
static kal_int32 mx_srv_deregister_jtt(mx_srv_cb dereg_cb)
{
	mxapp_trace("%s: enter", __FUNCTION__);
	mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_DEREGISTER, NULL, 0);
	login_status.status = 2;
	StartTimer(MXAPP_TIMER_JTT_SERVER, 30*1000, dereg_cb); // 30s��ʱ
	return 0;
}

//�ն˼�Ȩ
static kal_int32 mx_srv_authorize_jtt(mx_srv_cb auth_cb)
{
	mxapp_trace("%s: enter", __FUNCTION__);
	mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_AUTHORIZE, NULL, 0);
	login_status.status = 1;
	StartTimer(MXAPP_TIMER_JTT_SERVER, 30*1000, auth_cb); // 30s��ʱ
	return 0;
}

//�ն�����
kal_int32 mx_srv_heartbeat_jtt(void)
{
	mxapp_trace("%s: enter", __FUNCTION__);
	mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_HEARTBEAT, NULL, 0);
	return 0;
}

// λ����Ϣ�㱨(������)
kal_int32 mx_srv_loc_report_jtt(void)
{
	mxapp_trace("%s: enter", __FUNCTION__);
	mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_LOC_REPORT, NULL, 0);
	return 0;
}

// �ն�ͨ��Ӧ��
kal_int32 mx_srv_ack_jtt(kal_uint8 *para, kal_uint32 para_len)
{
	mxapp_trace("%s: enter", __FUNCTION__);
	mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_ACK, para, para_len);
	return 0;
}

//��ȡ��Ȩ��
//������Ч��Ȩ�볤�ȣ�����0��ʾ��ȡʧ��
static kal_uint8 mx_srv_get_auth_code(kal_uint8 * code)
{
	mxapp_trace("%s: enter", __FUNCTION__);
	if ((jtt_config.auth_code_len != 0) && (code != NULL))
	{
		kal_mem_set(code, 0, AUTH_CODE_LEN);
		kal_mem_cpy(code, jtt_config.auth_code, jtt_config.auth_code_len);
	}

	return jtt_config.auth_code_len;
}

// ����豸��(�ն��ֻ���)�Ƿ���Ч��ȫ0Ϊ��Ч������Ϊ��Ч
// ��Ч����KAL_TRUE
static kal_bool mx_srv_check_cell_num(void)
{
	kal_uint8 idx;
	mxapp_trace("%s: enter", __FUNCTION__);
	for(idx = 0; idx < DEVICE_NUM_LEN; idx++)
	{
		if(jtt_config.cell_num[idx] != 0)
		{
			mxapp_trace("cell_num valid");
			return KAL_TRUE;
		}
	}
	
	mxapp_trace("cell_num invalid");
	return KAL_FALSE;
}

kal_int32 mxapp_srvinteraction_upload_location_info(ST_MX_LOCATION_INFO *pLocateInfo)
{
	kal_int32 s32Ret = 0;
	kal_uint8 pos_info_type = 0;

	if(pLocateInfo == NULL)
	{
		pLocateInfo = mx_location_get_latest_position();
	}

	pos_info_type = mx_pos_info_type_get();
	if((pLocateInfo->valid && (pLocateInfo->type == LOCATION_TYPE_GPS))
	|| (pos_info_type != 0x00)) // GPS��Ч������ͨ�ϱ�ʱ���ϱ�λ��
	{
		if (pos_info_type == 0x10) // �����ϴ���λ����Ϣ��ѯ��
		{
			mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_LOC_QUERY_ACK, NULL, 0);
		}
		else
		{
			mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_LOC_REPORT, NULL, 0);
		}
	}
	else
	{
		mxapp_trace("NO report: location type %d, pos_info_type %x", pLocateInfo->type, pos_info_type);
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
#if (DEBUG_IN_VS == 0) // ԭOneNet���룬�ɱ������˴�Ϊ����
	kal_int8 s8Ret = 0;
	mxapp_trace("%s: enter", __FUNCTION__);

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
#if (DEBUG_IN_VS == 0) // ԭOneNet���룬�ɱ������˴�Ϊ����
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

// ע����ʱ������
static void mx_srv_deregister_cb(void * para)
{
	mxapp_trace("%s: enter", __FUNCTION__);
}


// ע�ᳬʱ������
static void mx_srv_register_cb(void * para)
{
	mxapp_trace("%s: enter", __FUNCTION__);
}

// ��Ȩ��ʱ������
static void mx_srv_authorize_cb(void * para)
{
	mxapp_trace("%s: enter", __FUNCTION__);
	mx_srv_deregister_jtt(mx_srv_deregister_cb);
}

// �����Ȩ��
void mx_srv_auth_code_clear(void)
{
	mxapp_trace("%s: enter", __FUNCTION__);
	kal_mem_set(jtt_config.auth_code, 0, AUTH_CODE_LEN);
	jtt_config.auth_code_len = 0;
	mx_srv_config_nv_write(2);
}

static kal_int32 mx_srv_config_nv_write(kal_uint8 item)
{
	kal_int16 error=0;
	kal_int32 ret=0;
#if (DEBUG_IN_VS == 0)
	if(item == 1) // ���涨λģʽ
	{
	    ret = WriteRecord(
	            NVRAM_EF_MXAPP_POS_MODE_LID,
	            1,
	            (kal_uint8*)&mx_pos_mode,
	            NVRAM_EF_MXAPP_POS_MODE_SIZE,
	            &error);
	}
	else if(item == 2) // �����豸����(SIM�š���Ȩ�볤�ȡ���Ȩ��)
	{
	    ret = WriteRecord(
	        NVRAM_EF_MXAPP_JTT_CONFIG_LID,
	        1,
	        (kal_uint8*)&jtt_config,
	        NVRAM_EF_MXAPP_JTT_CONFIG_SIZE,
	        &error);
	}
#endif
	mxapp_trace("%s(%d): ret=%d", __FUNCTION__, item, ret);
	return ret;
}

kal_int32 mx_srv_config_nv_read(void)
{
	static kal_uint8 mx_nv_read = 1;
	kal_int16 error = 0;
	kal_int32 ret = -1;
//	char num[DEVICE_NUM_LEN] = {0}; // �ն��ֻ���
//	kal_uint8 num_temp1, num_temp2;
	kal_uint8 idx;

	if(mx_nv_read)
	{
		mx_nv_read = 0;
		mxapp_trace("%s: enter", __FUNCTION__);
#if (DEBUG_IN_VS == 0)
		// ��ȡ�豸����(SIM�š���Ȩ�볤�ȡ���Ȩ��)
		memset(&jtt_config, 0, sizeof(jtt_config));
		ret = ReadRecord(
				NVRAM_EF_MXAPP_JTT_CONFIG_LID,
				1,
				(kal_uint8*)&jtt_config,
				NVRAM_EF_MXAPP_JTT_CONFIG_SIZE,
				&error);

#ifdef USE_IMEI_AS_CELL_NUM
	{
		char *imei_p;
		kal_uint8 idx, i_imei;
		
		mxapp_trace("USE_IMEI_AS_CELL_NUM");
		
		// ��ȡIMEI����BCD�뱣�浽device_num_st.cell_num
		imei_p = mxapp_get_imei();

#ifdef USE_JTT808_SERVER_1_SIM_DEVICE
		imei_p = "000012000187148";
#endif
		for (idx = 0; idx < DEVICE_NUM_LEN; idx++)
		{
			i_imei = 3 + idx * 2; // ȡIMEI��12λ
			jtt_config.cell_num[idx] = ((imei_p[i_imei] - '0') << 4) + (imei_p[i_imei + 1] - '0'); // BCD��
		}
	}
#endif

		mxapp_trace("cell_num:");
		for(idx = 0; idx < DEVICE_NUM_LEN; idx++)
		{
			mxapp_trace("%x", jtt_config.cell_num[idx]);
		}
		mxapp_trace("auth_code(%d):", jtt_config.auth_code_len);
		for(idx = 0; idx < jtt_config.auth_code_len; idx++)
		{
			mxapp_trace("%x", jtt_config.auth_code[idx]);
		}

		// ������������������ַ
		mxapp_srv_heart_set(jtt_config.heart);
		mxapp_srv_address_set(&jtt_config.srv_ip[0], jtt_config.srv_port);

		// ��ȡ��λģʽ
        memset(&mx_pos_mode , 0 , sizeof(nvram_ef_mxapp_pos_mode_t));
        ret = ReadRecord(
                NVRAM_EF_MXAPP_POS_MODE_LID,
                1,
                (kal_uint8*)&mx_pos_mode,
                NVRAM_EF_MXAPP_POS_MODE_SIZE,
                &error);
		mxapp_trace("dev_mode = %d (0-low 1-normal 2-continue 3-auto)", mx_pos_mode.dev_mode);

		// ���ö�λģʽ������
#if defined(__WHMX_MXT1608S__)
		if(mx_pos_mode.dev_mode == 1)
			mxapp_trace("custom period = %d", mx_pos_mode.custom_period);
#endif
	
#if defined(__WHMX_MXT1608S__)
		if(mx_pos_mode.dev_mode > 3 /*|| mx_pos_mode.dev_mode == 2*/) mx_pos_mode.dev_mode = 0;
#else
		if(mx_pos_mode.dev_mode > 2) mx_pos_mode.dev_mode = 0;
#endif
		mx_pos_mode_set(mx_pos_mode.dev_mode);
#endif
#if 0
		// �����ն��ֻ���(BCD��)
		for(idx = 0; idx < DEVICE_NUM_LEN; idx++)
		{
//			num_temp1 = jtt_config.cell_num[idx] - '0';
//			num_temp2 = jtt_config.cell_num[idx+1] - '0';
//			cell_number[idx/2] = (num_temp1<<4) + (num_temp2);
			cell_number[idx] = jtt_config.cell_num[idx];
		}

		// ��ȡ��Ȩ��
		for(idx = 0; idx < AUTH_CODE_LEN; idx++)
		{
			auth_code[idx] = jtt_config.code[idx];
		}
		auth_code_len = jtt_config.code_len;
#endif
	}

	return ret;
}

// �������Ȩ��ע������
void mxapp_srvinteraction_connect(kal_int32 s32Level)
{
	kal_int32 ret = -1;
	kal_uint8 code[AUTH_CODE_LEN] = { 0 };
	char *imei_p;
	kal_uint8 idx, i_imei;

	mxapp_trace("%s(JTT): enter", __FUNCTION__);

	mx_srv_config_nv_read(); // ��ȡNV��(�豸�š���Ȩ�롢��λģʽ)

	if(!mx_srv_check_cell_num()) // �ն��ֻ�����Ч
	{
		mxapp_trace("%s(JTT): stop", __FUNCTION__);
		return;
	}

	ret = mx_srv_get_auth_code(&code[0]); // ��ȡ��Ȩ��
	if (ret > 0)
	{
		mx_srv_authorize_jtt(mx_srv_authorize_cb); // ��Ȩ
	}
	else
	{
		mx_srv_register_jtt(mx_srv_register_cb); // ע��
	}

	mxapp_trace("%s(JTT): leave", __FUNCTION__);
}

void srvinteraction_bootup_location_request(void)
{
	mxapp_trace("%s(JTT): enter", __FUNCTION__);

	srvinteraction_location_request(0);
}

void mxapp_srvinteraction_first_location(void)
{
	ST_MX_LOCATION_INFO *pos_info=mx_location_get_latest_position();

	if(g_s32FirstConnect == 0)
	{
		return; // not first connect
	}
	else
	{
		g_s32FirstConnect = 0;
	}

	mxapp_trace("%s(JTT): enter", __FUNCTION__);
	
	if((pos_info->valid == KAL_TRUE))
	{
		mxapp_trace("GPS already valid, no need LBS or AGPS");
	}
	else
	{
		mx_location_check_and_start_agps(); // added
	}
}

#if defined(__WHMX_SERVER_JTT808__)
void mxapp_srvinteraction_sos(void)
{
	if(g_s32PowerOff) return;/*�ػ����̲���ӦSOS*/
	mx_pos_info_type_set(0x80);
	srvinteraction_location_request(0);
}
#elif defined(__WHMX_SOS_2ROUND__) // SOS 2 parts: WiFi/LBS and GNSS/WiFi/LBS
void mxapp_srvinteraction_sos(kal_uint8 type)
{
	mxapp_trace("%s(%d): enter",__FUNCTION__, type);
	
	if(type == 1) // 1. request WiFi/LBS ignore movement
	{
		/*set alarm type*/
		mx_pos_info_type_set(0x80);
		
	#if 0 // do not change mode in MXT1608S
		/*mode change*/
		mx_location_mode_change(LOCATION_EVT_SOS, LOCATION_MODE_FAST_PERIOD);
		mxapp_srvinteraction_uploader_pos_mode();
	#endif

		/*req*/
		srvinteraction_location_request(3);
	}
	else if(type == 2) // 2. request GNSS/WiFi/LBS
	{
		/*set normal type*/
		mx_pos_info_type_set(0x00);
		
		/*req*/
		srvinteraction_location_request(4);
	}
}
#else
void mxapp_srvinteraction_sos(void)
{
	if(g_s32PowerOff) return;/*�ػ����̲���ӦSOS*/
	/*mode change*/
	mx_location_mode_change(LOCATION_EVT_SOS, LOCATION_MODE_FAST_PERIOD);
	mxapp_srvinteraction_uploader_pos_mode();
	/**/	
	mx_pos_info_type_set(0x80);
	/*req*/
	srvinteraction_location_request(0);
}
#endif

/* Shut down directly, no location */
static void mxapp_srvinteraction_poweroff(void)
{
	mxapp_trace("%s: enter", __FUNCTION__);

	mxapp_sys_shutdown();
}

void mxapp_srvinteraction_locate_and_poweroff(void)
{}

void mxapp_srvinteraction_locate_and_poweroff_remote(ST_MX_LOCATION_INFO *loc)
{}

kal_int32 mxapp_srvinteraction_uploader_pos_mode(void)
{}

kal_int32 mxapp_srvinteraction_uploader_config(void)
{}

kal_int32 mxapp_srvinteraction_uploader_batt_info(void)
{}

kal_int32 mxapp_srvinteraction_send_battery_warning(void)
{
	mxapp_trace("%s: enter", __FUNCTION__);
	mx_pos_info_type_set(0x90);
	mx_srv_send_handle(MXAPP_SRV_JTT_CMD_UP_LOC_REPORT, NULL, 0);
}

kal_bool mxapp_srvinteraction_if_connected(void) // 2016-6-22
{
	kal_bool if_connected;
	if_connected = (g_s32FirstConnect == 0)? KAL_TRUE : KAL_FALSE;
	mxapp_trace("%s: %d", __FUNCTION__, if_connected);
	return if_connected;
}

kal_bool mxapp_srvinteraction_is_connected(void)
{
	kal_bool if_connected;
	if_connected = (g_s32FirstConnect == 0)? KAL_TRUE : KAL_FALSE;
	mxapp_trace("%s: %d", __FUNCTION__, if_connected);
	return KAL_TRUE;//if_connected;
}

#if defined(__WHMX_MXT1608S__)
void mxapp_srvinteraction_upload_balance_response(kal_uint8 *para,kal_uint32 para_len)
{

}

kal_uint8 mxapp_srvinteraction_location_again_get(void)
{

}

void mxapp_srvinteraction_location_again_set(kal_uint8 ret)
{

}
#endif

#if defined(WHMX_ST_LIS2DS12)
void mxapp_srvinteraction_upload_step(void)
{}
#endif




#if (DEBUG_IN_VS == 1)
void main(void)
{
	kal_uint8 ret;

	kal_uint8 buf_in6[] = { 0x7E, 0x81, 0x06, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0xF0, 0x01, 0x72, 0x7E };

	mx_srv_receive_handle_jtt(0, buf_in6, sizeof(buf_in6)); // ��ѯָ���ն˲�����������

	kal_uint8 buf_in5[] = { 0x7E, 0x81, 0x07, 0x00, 0x00, 0x01, 0x20, 0x00, 0x18, 0x71, 0x48, 0x00, 0x02, 0x84, 0x7E };

	mx_srv_receive_handle_jtt(0, buf_in5, sizeof(buf_in5)); // ��ѯ�ն�����:ok

	kal_uint8 buf_in4[] = { 0x7e, 0x81, 0x06, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00, 0x95, 0x8d, 0x7e };

	mx_srv_receive_handle_jtt(0, buf_in4, sizeof(buf_in4)); // ��ѯָ���ն˲���:ok
	
	kal_uint8 buf_in3[] = { 0x7E, 0x81, 0x04, 0x00, 0x00, 0x01, 0x20, 0x00, 0x18, 0x71, 0x48, 0x00, 0x02, 0x87, 0x7E };

	mx_srv_receive_handle_jtt(0, buf_in3, sizeof(buf_in3)); // ��ѯ�ն˲���:ok

	mx_srv_cmd_handle_down_query_para(0,NULL,0,NULL);

	mx_srv_deregister_jtt(mx_srv_deregister_cb);

	mx_srv_register_jtt(mx_srv_register_cb);

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

	memset(g_s8TxBuf, 1, MXAPP_JTT_BUFF_MAX);
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
#endif

#endif

