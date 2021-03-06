#include "pch.h"
#include "SocketClient.h"
#include "Misc.h"
#include "Packet.h"
#include "Login.h"


CSocketClient::CSocketClient(CSocketClient* pMainSocketClient /* = nullptr*/) : m_pTcpPackClient(this) {
	
	m_bIsRunning = false;

	m_bIsMainSocketClient = (pMainSocketClient == nullptr) ? true : false;

	m_pMainSocketClient = m_bIsMainSocketClient ? this : pMainSocketClient;

	if (!m_bIsMainSocketClient) {
		m_pszAddress = m_pMainSocketClient->m_pszAddress;
		m_wPort = m_pMainSocketClient->m_wPort;
	}
	else {
		m_pszAddress = L"127.0.0.1";
		m_wPort = 5555;
	}

	m_pModuleManage = nullptr;
	
	m_dwClientStatus = NOT_ONLINE;

	m_hChildSocketClientExitEvent = nullptr;

	m_pModule = nullptr;

	m_dwConnectId = 0;

	//m_dwConnectId = m_pTcpPackClient->GetConnectionID();	// 这项要start之后才能获取

	m_pLastSocketClient = nullptr;
	m_pNextSocketClient = nullptr;


	// 设置数据包最大长度（有效数据包最大长度不能超过0x3FFFFF字节(4MB-1B)，默认：262144/0x40000 (256KB)
	m_pTcpPackClient->SetMaxPackSize(PACKET_MAX_LENGTH);
	// 设置心跳检测包发送间隔
	m_pTcpPackClient->SetKeepAliveTime(60 * 1000);
	// 设置心跳检测重试包发送间隔
	m_pTcpPackClient->SetKeepAliveInterval(20 * 1000);
}


CSocketClient::~CSocketClient() {
	if (!m_bIsMainSocketClient) {

		// 直到m_pTcpPackClient完全关闭才停止阻塞
		BOOL bJmp = false;
		while (!bJmp) {
			En_HP_ServiceState eStatus = m_pTcpPackClient->GetState();
			switch (eStatus) {

			case SS_STARTING:
			case SS_STARTED:
				m_pTcpPackClient->Stop();
				break;
				
			case SS_STOPPING:
				break;

			case SS_STOPPED:
				bJmp = true;
				break;
			};
		}

		if (m_pModuleManage != nullptr) {
			delete m_pModuleManage;
			m_pModuleManage = nullptr;
		}

		if (m_hChildSocketClientExitEvent != nullptr) {
			CloseHandle(m_hChildSocketClientExitEvent);
		}
	}

	m_pMainSocketClient = nullptr;
	m_pModuleManage = nullptr;
	m_pModule = nullptr;
	m_pLastSocketClient = nullptr;
	m_pNextSocketClient = nullptr;

	m_bIsRunning = false;
}


VOID CSocketClient::SetRemoteAddress(LPCTSTR pszAddress, WORD wPort) {
	m_pszAddress = pszAddress;
	m_wPort = wPort;
}


BOOL CSocketClient::StartSocketClient() {

	BOOL bRet;

	if (!(m_pTcpPackClient->IsConnected())) {
		// 默认是异步connect，bRet返回true不一定代表成功连接。坑死我了，现改成同步connect
		bRet = m_pTcpPackClient->Start(m_pszAddress, m_wPort, 0);
		if (!bRet) {
			return false;
		}
	}

	m_bIsRunning = true;

	if (!m_bIsMainSocketClient) {
		// 第二个参数为true时表示手动重置事件
		m_hChildSocketClientExitEvent = CreateEvent(NULL, true, false, NULL);	
	}

	// 组件管理对象
	if (m_bIsMainSocketClient) {
		m_pModuleManage = new CModuleManage(this);
	}

	// 生成随机密钥
	BYTE pbKey[16];
	BYTE pbIv[16];
	RandomBytes(pbKey, 16);
	RandomBytes(pbIv, 16);
	m_Crypto = CCrypto(AES_128_CFB, pbKey, pbIv);

	BYTE pbCipherKeyPacket[CRYPTO_KEY_PACKET_LENGTH];
	// 第一个字节表示是主socket的密钥还是子socket的密钥
	pbCipherKeyPacket[0] = (m_bIsMainSocketClient) ? CRYPTO_KEY_PACKET_TOKEN_FOR_MAIN_SOCKET : CRYPTO_KEY_PACKET_TOKEN_FOR_CHILD_SOCKET;
	memcpy(pbCipherKeyPacket + 1, m_Crypto.m_pbRsaEncrypted, 256);

	// 向主控端发送密钥
	bRet = m_pTcpPackClient->Send(pbCipherKeyPacket, CRYPTO_KEY_PACKET_LENGTH);
	if (bRet) {
		DebugPrint("成功向服务器发送通信密钥:\n");
		PrintData(pbCipherKeyPacket, CRYPTO_KEY_PACKET_LENGTH);
	}

	// 不可以在构造函数里GetConnectionID，一直会是0.估计start之后才有的CONNID吧
	m_dwConnectId = m_pTcpPackClient->GetConnectionID();			
	return bRet;
}


BOOL CSocketClient::SendPacket(COMMAND_ID dwCommandId, PBYTE pbPacketBody, DWORD dwPacketBodyLength) {
	if (!m_pTcpPackClient->IsConnected()) {
		return false;
	}

	CPacket Packet = CPacket(this);
	Packet.PacketCombine(dwCommandId, pbPacketBody, dwPacketBodyLength);
	BOOL bRet;

	if (m_bIsRunning == 1) {
		bRet = m_pTcpPackClient->Send(Packet.m_pbPacketCiphertext, Packet.m_dwPacketLength);
	}
	else {
		bRet = false;
	}
	
	return bRet;
}


// 写的时候只是用于阻塞子socket的退出。
void CSocketClient::WaitForExitEvent() {
	WaitForSingleObject(m_hChildSocketClientExitEvent, INFINITE);
}


void CSocketClient::DisconnectChildSocketClient() {
	SetEvent(m_hChildSocketClientExitEvent);
}




// 回调函数

EnHandleResult CSocketClient::OnPrepareConnect(ITcpClient* pSender, CONNID dwConnID, SOCKET socket) {
	DebugPrint("[Client %d] OnPrepareConnect: \n", dwConnID);

	return HR_OK;
}


EnHandleResult CSocketClient::OnConnect(ITcpClient* pSender, CONNID dwConnID) {
	DebugPrint("[Client %d] OnConnect: \n", dwConnID);

	return HR_OK;
}


EnHandleResult CSocketClient::OnHandShake(ITcpClient* pSender, CONNID dwConnID) {
	DebugPrint("[Client %d] OnHandShake: \n", dwConnID);

	return HR_OK;
}


EnHandleResult CSocketClient::OnSend(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength) {
	DebugPrint("[Client %d] OnSend: %d Bytes \n", dwConnID, iLength);
	//PrintData((PBYTE)pData, iLength);

	return HR_OK;
}


EnHandleResult CSocketClient::OnReceive(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength) {
	DebugPrint("[Client %d] OnReceive: %d Bytes \n", dwConnID, iLength);

	PrintData((PBYTE)pData, iLength);

	CPacket* pPacket = new CPacket(this);
	pPacket->PacketParse((PBYTE)pData, iLength);
	
	ReceiveFunc(pPacket);				// C2712: 无法再要求对象展开的函数中使用__try
										// SEH不能在有类对象构造之类的函数内，所以单独拎出来放到一个函数内。

	return HR_OK;
}


VOID CSocketClient::ReceiveFunc(CPacket* pPacket) {
	__try {
		// 处理主socket以及和子socket共有部分，主要是登录相关的包
		switch (pPacket->m_PacketHead.wCommandId) {

		// Server接收到Client的发出的密钥后，给Client响应一个CRYPTO_KEY包。
		case CRYPTO_KEY:

			if (m_bIsMainSocketClient) {				// 如果是Client的主socket发来的，那么Client发出上线包
				m_dwClientStatus = WAIT_FOR_LOGIN;

				BYTE pbLoginPacketBody[LOGIN_PACKET_BODY_LENGTH];
				GetLoginInfo(pbLoginPacketBody);
				SendPacket(LOGIN, pbLoginPacketBody, LOGIN_PACKET_BODY_LENGTH);
			}
			else {
				m_dwClientStatus = LOGINED;				// 子socket不需要发上线包，直接就算登录
			}
			__leave;

		case LOGIN:
			m_dwClientStatus = LOGINED;
			__leave;

		// 这里一定不要写default, 不然后面的代码就不继续走下去了。

		} // switch (pPacket->m_PacketHead.wCommandId)


		// 如果还不是LOGINED状态，就不可以处理下面的这些包
		// 主socket: 收到上线包后进入LOGINED状态。子socket: 收到密钥后进入LOGINED状态。
		if (m_dwClientStatus != LOGINED) {
			__leave;
		}


		// 处理主socket特有的包
		if (m_bIsMainSocketClient) {

			// 处理组件的CONNECT包。CONNECT包都来自主socket哦
			BOOL bHaveProcess = m_pModuleManage->OnReceiveConnectPacket(pPacket);

			// 处理主socket中 不是 组件的CONNECT包 的包
			if (!bHaveProcess) {
				switch (pPacket->m_PacketHead.wCommandId) {
				case ECHO:
					DebugPrint("接收到ECHO测试包，明文内容如下：\n");
					PrintData(pPacket->m_pbPacketBody, pPacket->m_dwPacketBodyLength);

					// 再把这个明文发回给主控端（即服务端），以完成ECHO测试
					SendPacket(ECHO, pPacket->m_pbPacketBody, pPacket->m_dwPacketBodyLength);
					__leave;
				}
			}
		}

		// 子socket
		else {
			assert(m_pModule != NULL);
			m_pModule->OnRecvivePacket(pPacket);				// 剩下的封包交给相关的组件处理（CModule是基类，派生出不同的组件类）
		}
	}

	__finally {
		if (pPacket) {
			delete pPacket;
			pPacket = NULL;
		}
	}
}



EnHandleResult CSocketClient::OnClose(ITcpClient* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode) {
	DebugPrint("[Client %d] OnClose: \n", dwConnID);

	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)OnCloseThreadFunc, this, 0, NULL);
	// 也是莫名奇妙的崩在return，另起一个线程就好了，估计还是老问题：HPSocket作者强调的回调函数内不要做高IO的事情。

	return HR_OK;
}


VOID CSocketClient::OnCloseThreadFunc(CSocketClient* pThis) {
	if (!pThis->m_bIsMainSocketClient) {
		pThis->DisconnectChildSocketClient();
	}
}