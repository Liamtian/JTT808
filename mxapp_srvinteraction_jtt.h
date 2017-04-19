
#if 1
#ifndef __MXAPP_SRVINTERACTION_JTT_H__
#define __MXAPP_SRVINTERACTION_JTT_H__

#include "others.h"

typedef enum _SRV_CMD_TYPE_
{
	MXAPP_SRV_JTT_CMD_UP_ACK = 0x0001, // �ն�ͨ��Ӧ��
	MXAPP_SRV_JTT_CMD_UP_HEARTBEAT = 0x0002, // �ն�����
	MXAPP_SRV_JTT_CMD_UP_REGISTER = 0x0100, // �ն�ע��
	MXAPP_SRV_JTT_CMD_UP_DEREGISTER = 0x0003, // �ն�ע��
	MXAPP_SRV_JTT_CMD_UP_AUTHORIZE = 0x0102, // �ն˼�Ȩ
	MXAPP_SRV_JTT_CMD_UP_QUERY_PARA_ACK = 0x0104, // ��ѯ�ն˲���Ӧ��
	MXAPP_SRV_JTT_CMD_UP_QUERY_PROP_ACK = 0x0107, // ��ѯ�ն�����Ӧ��
	MXAPP_SRV_JTT_CMD_UP_LOC_REPORT = 0x0200, // λ����Ϣ�㱨
	MXAPP_SRV_JTT_CMD_UP_LOC_QUERY_ACK = 0x0201, // λ����Ϣ��ѯӦ��
	MXAPP_SRV_JTT_CMD_UP_EVENT_REPORT = 0x0301, // �¼�����

	MXAPP_SRV_JTT_CMD_DOWN_ACK = 0x8001, // ƽ̨ͨ��Ӧ��
	MXAPP_SRV_JTT_CMD_DOWN_REGISTER_ACK = 0x8100, // �ն�ע��Ӧ��
	MXAPP_SRV_JTT_CMD_DOWN_SET_PARA = 0x8103, // �����ն˲���
	MXAPP_SRV_JTT_CMD_DOWN_QUERY_PARA = 0x8104, // ��ѯ�ն˲���
	MXAPP_SRV_JTT_CMD_DOWN_QUERY_SPEC_PARA = 0x8106, // ��ѯָ���ն˲���
	MXAPP_SRV_JTT_CMD_DOWN_QUERY_PROP = 0x8107, // ��ѯ�ն�����
	MXAPP_SRV_JTT_CMD_DOWN_LOC_QUERY = 0x8201, // λ����Ϣ��ѯ
	MXAPP_SRV_JTT_CMD_DOWN_EVENT_SET = 0x8301, // �¼�����
#ifdef __WHMX_CALL_SUPPORT__
	MXAPP_SRV_JTT_CMD_DOWN_CALL_MONITOR = 0x8400, // �绰�ز�
	MXAPP_SRV_JTT_CMD_DOWN_PHONEBOOK = 0x8401, // ���õ绰��
#endif
#ifdef __WHMX_JTT_FENCE_SUPPORT__
#endif

	MXAPP_SRV_JTT_CMD_INVALID = 0xFFFF
}SRV_ZXBD_CMD_TYPE;

typedef struct
{
	SRV_ZXBD_CMD_TYPE cmd;
	kal_int32(*handle)(SRV_ZXBD_CMD_TYPE cmd, kal_uint8 *in, kal_uint32 in_len, kal_uint8 *out);
}mx_cmd_handle_struct;


//�ն˲���: ����ID
typedef enum _SRV_PARA_ID_
{
	MXAPP_SRV_JTT_PARA_HEARTBEAT = 0x0001,
	MXAPP_SRV_JTT_PARA_SRV_IP = 0x0013,
	MXAPP_SRV_JTT_PARA_SRV_TCP_PORT = 0x0018,
	MXAPP_SRV_JTT_PARA_MONITOR_NUM = 0x0048,
	MXAPP_SRV_JTT_PARA_LOC_TYPE = 0x0094,
	MXAPP_SRV_JTT_PARA_LOC_PROP = 0x0095,

	/*�Զ���*/
	MXAPP_SRV_JTT_PARA_BATTERY = 0xF001,

	MXAPP_SRV_JTT_PARA_INVALID = 0x0000
}SRV_PARA_ID;

//�����ն˲���: ����ID-������
typedef struct
{
	SRV_PARA_ID para_id;
	kal_int32(*handle)(kal_uint8 *in, kal_uint32 in_len);
}mx_set_para_handle_struct;

//��ѯ�ն˲���: ����ID-������
typedef struct
{
	SRV_PARA_ID para_id;
	kal_int32(*handle)(kal_uint8 *out/*, kal_uint32 out_len*/);
}mx_get_para_handle_struct;

// �ն���������
typedef struct
{
	kal_uint8 status; // ��ʼ:0 ��Ȩ��:1 ע����:2 ע����:3 ��Ȩ�ɹ�:4
	kal_uint16 flow;
}mx_login_status;

typedef void(*mx_srv_cb)(void *);


/*�½ӿ�*/
kal_int32 mx_srv_receive_handle_jtt(kal_uint8 src, kal_uint8 *in, kal_int32 in_len);
kal_int32 mx_srv_heartbeat_jtt(void);
kal_int32 mx_srv_loc_report_jtt(void);
kal_int32 mx_srv_ack_jtt(kal_uint8 *para, kal_uint32 para_len);
kal_int32 mx_srv_config_nv_read(void);
void mx_srv_auth_code_clear(void);


/*ԭ�ӿ�*/
void mxapp_srvinteraction_connect(kal_int32 s32Level);
void srvinteraction_bootup_location_request(void);
void mxapp_srvinteraction_first_location(void);

kal_int32 mxapp_srvinteraction_uploader_pos_mode(void);
kal_int32 mxapp_srvinteraction_uploader_config(void);
kal_int32 mxapp_srvinteraction_uploader_batt_info(void);
kal_int32 mxapp_srvinteraction_send_battery_warning(void); 

kal_uint8 mx_pos_mode_set(kal_uint8 mode);
kal_uint8 mx_pos_period_set(kal_uint8 period_min);

#if defined(__WHMX_SERVER_JTT808__)
void mxapp_srvinteraction_sos(void);
#elif defined(__WHMX_SOS_2ROUND__)
void mxapp_srvinteraction_sos(kal_uint8 type);
#else
void mxapp_srvinteraction_sos(void);
#endif

void mxapp_srvinteraction_locate_and_poweroff(void);

kal_bool mxapp_srvinteraction_if_connected(void); // 2016-6-22
kal_bool mxapp_srvinteraction_is_connected(void);

#if defined(__WHMX_MXT1608S__)
void mxapp_srvinteraction_upload_balance_response(kal_uint8 *para,kal_uint32 para_len);
kal_uint8 mxapp_srvinteraction_location_again_get(void);
void mxapp_srvinteraction_location_again_set(kal_uint8 ret);
#endif

#if defined(WHMX_ST_LIS2DS12)
void mxapp_srvinteraction_upload_step(void);
#endif


#endif
#endif

