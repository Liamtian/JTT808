#ifndef __JTT808_H__
#define __JTT808_H__

#include "others.h"

/********************
�������ݷ���.h
********************/
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


//�����ն˲���: ����ID
typedef enum _SRV_SET_PARA_ID_
{
	MXAPP_SRV_JTT_SET_PARA_HEARTBEAT = 0x0001,
	MXAPP_SRV_JTT_SET_PARA_MONITOR_NUM = 0x0048,
	MXAPP_SRV_JTT_SET_PARA_LOC_TYPE = 0x0094,
	MXAPP_SRV_JTT_SET_PARA_LOC_PROP = 0x0095,

	MXAPP_SRV_JTT_SET_PARA_INVALID = 0x0000
}SRV_SET_PARA_ID;

//�����ն˲���: ����ID-������
typedef struct
{
	SRV_SET_PARA_ID para_id;
	kal_int32(*handle)(kal_uint8 *in, kal_uint32 in_len);
}mx_set_para_handle_struct;

typedef void(*mx_srv_cb)(void *);


kal_int32 mx_srv_receive_handle_jtt(kal_uint8 src, kal_uint8 *in, kal_int32 in_len);
kal_int32 mx_srv_register_jtt(void);
kal_int32 mx_srv_heartbeat_jtt(void);

#endif