#pragma once
#include "pch.h"
#include "Crypto.h"
#include "ModuleManage.h"
#include "Packet.h"


class CModuleManage;
class CModule;
class CPacket;


class CSocketClient : public CTcpClientListener {

public:
	LPCTSTR					m_pszAddress;
	WORD					m_wPort;

	BOOL					m_bIsMainSocketClient;
	CSocketClient*			m_pMainSocketClient;

	CModuleManage*			m_pModuleManage;				// 组件管理对象，只有主socket可以创建

	CTcpPackClientPtr		m_pTcpPackClient;

	BOOL					m_bIsRunning;

	CCrypto					m_Crypto;

	CLIENT_STATUS			m_dwClientStatus;

	HANDLE					m_hChildSocketClientExitEvent;

	CModule*				m_pModule;						// 仅针对子socket。一个子socket最多对应一个组件。由CModule的构造函数传进来该值。

	CONNID					m_dwConnectId;

	CSocketClient*			m_pLastSocketClient;
	CSocketClient*			m_pNextSocketClient;

public:

	CSocketClient(CSocketClient* pMainSocketClient = nullptr);
	~CSocketClient();

	VOID SetRemoteAddress(LPCTSTR pszAddress, WORD wPort);
	BOOL StartSocketClient();

	BOOL SendPacket(COMMAND_ID dwCommandId, PBYTE pbPacketBody, DWORD dwPacketBodyLength);

	void WaitForExitEvent();			// 直到收到退出事件时，子socket才退出。
	void DisconnectChildSocketClient();	// 设置退出事件


	// 重写回调函数
	virtual EnHandleResult OnPrepareConnect(ITcpClient* pSender, CONNID dwConnID, SOCKET socket);
	virtual EnHandleResult OnConnect(ITcpClient* pSender, CONNID dwConnID);
	virtual EnHandleResult OnHandShake(ITcpClient* pSender, CONNID dwConnID);
	virtual EnHandleResult OnSend(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHandleResult OnReceive(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength);
	virtual EnHandleResult OnClose(ITcpClient* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode);

	static VOID OnCloseThreadFunc(CSocketClient* pThis);

public:
	VOID ReceiveFunc(CPacket* pPacket);	
};